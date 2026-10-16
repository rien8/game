#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/ecs/diagnostic.hpp"

#include "gamev2/genetics/breeding.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/statistics.hpp"
#include "gamev2/genetics/survival.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gamev2::ecs::adapt {

using namespace gamev2::genetics;

namespace {

/// 一个待提交的子代。物种随子代一起记下：SeasonOutput 只给基因型与表型，
/// 不回传子代出自哪个父池。
struct StagedOffspring
{
    SpeciesId species;
    Genome genome;
    Phenotype phenotype;
    environment::EnvironmentSnapshot environment;
};

/// 单个池单季的子代数上限。
///
/// 公式本身没有上界：trait_max 配错一个量级就能让 N × Fecundity 算出天文
/// 数字，而每个子代还要走一遍减数分裂、突变与表型求值。截断让「配置写错」
/// 退化成种群偏大这种可恢复的症状，而不是一季之内把主线程烧穿。
constexpr std::uint32_t kMaxProgenyPerPool = 4096;

/// 本季该给这个物种出多少子代。
///
/// 设 N 为本物种个体数、s 为平均存活率、f 为平均繁殖力，取
///     progeny = round(N × f)
/// 则下季规模为
///     N' = s · N + round(N · f) ≈ N · (s + f)
///
/// 也就是说种群既不增也不减的条件是 s + f = 1：繁殖力恰好补上存活率的缺口。
/// 这正是「稳定种群」的可写形式——比值只依赖两者之和，配置期可以直接拿它
/// 调平衡。反过来说，若每个池每季只出一个子代（老行为），N' = s · N + 1，
/// 子代数与物种数无关而与种群规模无关，s < 1 时必然单调衰减到 1，而 1 个个体
/// 的池完不成有性生殖（PoolTooSmall），种群就此永久灭绝。
///
/// 这里取的是本季个体数而非存活数：存活数要等 settle 跑完第 1 步才知道，
/// 而 settle 是被调用一次的，预先算一遍等于把它的抽签提前消费一次。
std::uint32_t progeny_for(const std::vector<Phenotype>& pool) noexcept
{
    // 「N × 平均繁殖力」就是繁殖力之和：逐个累加省掉一次除法，也让空池与
    // 全员绝育落到同一个 0 上，不必为「分母为零」单开一条分支。
    float fecundity = 0.0f;
    for (const auto& p : pool)
    {
        // 负的繁殖力在表型区间配错时可能出现；它没有生态含义，且会让总数
        // 下溢成天文数字（无符号取负）。
        fecundity += std::max(p.traits[trait_index(TraitKind::Fecundity)],
                              0.0f);
    }
    // 判据必须落在取整之前：std::lround 的结果要塞进 long，而 Windows 上
    // long 只有 32 位；NaN 的两个比较都为假，会从夹取里原样穿过去。
    if (!std::isfinite(fecundity)
        || fecundity >= static_cast<float>(kMaxProgenyPerPool))
    {
        return std::isfinite(fecundity) ? kMaxProgenyPerPool : 0U;
    }
    return static_cast<std::uint32_t>(std::lround(fecundity));
}

}  // namespace

// View 的回调只给组件引用（View 的 Row 是 std::tuple<Ts&...>），不给实体
// 句柄，而 outlived 判死之后要交给 destroy、统计要按桶收基因组，两者都要
// 句柄。因此这两个系统走 for_each_entity 而不是 View：需要句柄的系统本来
// 就得随机访问，View 的价值（按 chunk 连续遍历）在这里换不来东西。

void AgingSystem::execute(World& world, CommandBuffer& cmd)
{
    std::vector<EntityId> expired;
    world.for_each_entity([&](EntityId e)
    {
        auto* v = world.get_if<Vitality>(e);
        auto* p = world.get_if<CreaturePhenotype>(e);
        if (v == nullptr || p == nullptr)
        {
            return;
        }
        ++v->age_seasons;
        if (outlived(p->phenotype, v->age_seasons))
        {
            expired.push_back(e);
        }
    });

    for (const auto& e : expired)
    {
        cmd.destroy(e);
    }
}

void BreedingSystem::execute(World& world, CommandBuffer& cmd)
{
    std::vector<Genome> genomes;
    std::vector<Phenotype> phenotypes;
    std::vector<environment::EnvironmentSnapshot> environments;
    std::vector<float> threats;
    std::vector<std::uint32_t> species_ids;
    /// 与上面五个 vector 同序，用来把 settle 的 survived 下标对回实体
    std::vector<EntityId> handles;

    world.for_each_entity([&](EntityId e)
    {
        auto* g = world.get_if<Genome>(e);
        auto* p = world.get_if<CreaturePhenotype>(e);
        auto* env = world.get_if<Environment>(e);
        auto* tag = world.get_if<SpeciesTag>(e);
        if (g == nullptr || p == nullptr || env == nullptr || tag == nullptr)
        {
            return;
        }
        // SpeciesTag 默认构造出来的 value 是 0，而 species_id 从 1 起；超出
        // species_count() 同样不成立。这类实体不属于任何物种，统计 system
        // 有 slot < buckets.size() 守卫因而静默跳过——若这里不跳过，它会在
        // survived 保持 0 的情况下被当成「本季死亡」销毁。两个 system 对同一个
        // id 必须有同一个答案，否则「统计里没有的个体」会被系统悄悄吃掉。
        if (tag->species.value == 0
            || tag->species.value > species_.species_count())
        {
            world.report(Diagnostic{
                DiagnosticLevel::error, "GENETICS_UNKNOWN_SPECIES",
                "SpeciesTag 引用未注册的物种 id "
                    + std::to_string(tag->species.value) + "，该实体本季被跳过"});
            return;
        }
        genomes.push_back(*g);
        phenotypes.push_back(p->phenotype);
        environments.push_back(env->snapshot);
        threats.push_back(0.0f);
        species_ids.push_back(tag->species.value);
        handles.push_back(e);
    });

    // 一个物种结算一次。settle 的 SeasonOutput 不回传子代出自哪个父池，
    // 多个池同处一次调用时子代认不回自己的物种，一次一种则归属由调用点
    // 天然确定。空间邻域分桶属于生态 spec。
    RandomStreams streams{root_seed_, season_index_};
    std::vector<std::uint8_t> survived(genomes.size(), 0);
    std::vector<StagedOffspring> born;

    for (std::uint32_t s = 1; s <= species_.species_count(); ++s)
    {
        std::vector<std::size_t> members;
        std::vector<Genome> pool_genomes;
        std::vector<Phenotype> pool_phenotypes;
        std::vector<environment::EnvironmentSnapshot> pool_environments;
        std::vector<float> pool_threats;
        for (std::size_t i = 0; i < genomes.size(); ++i)
        {
            if (species_ids[i] != s)
            {
                continue;
            }
            members.push_back(i);
            pool_genomes.push_back(genomes[i]);
            pool_phenotypes.push_back(phenotypes[i]);
            pool_environments.push_back(environments[i]);
            pool_threats.push_back(threats[i]);
        }
        if (members.empty())
        {
            continue;
        }

        const std::vector<std::uint32_t> pool_species(members.size(), s);
        std::vector<Parent> parents;
        parents.reserve(members.size());
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            // index 填的是本池内的下标，settle 靠它回查 survived 与季节
            // 适应力。fitness 是先验权重，settle 重建父池时整列替换成季节
            // 适应力，这里给什么都不影响结果。
            parents.push_back(Parent{i, &pool_genomes[i], 1.0f});
        }
        const std::vector<ParentPool> sites{ParentPool{
            parents, SpeciesId{s}, std::nullopt, progeny_for(pool_phenotypes)}};

        // 统计交给 PopulationStatisticsSystem：settle 里那份每季 O(S·N) 重算
        // 一遍等位基因频率，生产路径上没人读。
        const auto out = settle(
            SeasonInput{pool_genomes, pool_phenotypes, pool_environments,
                        pool_threats, pool_species, sites, false},
            table_, species_, config_, streams);

        for (std::size_t i = 0; i < members.size(); ++i)
        {
            survived[members[i]] = out.survived[i];
            if (out.survived[i] == 0)
            {
                continue;
            }
            // 只写存活者：没活过本季的个体同帧就被销毁，往它身上写生存率
            // 是白费一次跨 archetype 的写。
            auto* v = world.get_if<Vitality>(handles[members[i]]);
            if (v != nullptr)
            {
                v->survival_rate = out.survival_rates[i];
                v->fitness = out.season_fitness[i];
            }
        }
        for (std::size_t i = 0; i < out.offspring.size(); ++i)
        {
            born.push_back(StagedOffspring{SpeciesId{s}, out.offspring[i],
                                          out.offspring_phenotypes[i],
                                          out.offspring_environments[i]});
        }
    }

    // 换代：未存活的老个体销毁，新个体经队列创建。两者都在帧末 flush 时
    // 发生，行为系统不会观察到半新半旧的种群。
    //
    // 销毁命令按 EntityId 升序提交。for_each_entity 走的是槽位下标序，而
    // EntityId 的高 32 位是版本号，两者不是同一个序；固定成升序之后结算
    // 顺序与命令序列解耦。
    std::vector<std::pair<EntityId, bool>> verdict;
    verdict.reserve(handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        verdict.push_back({handles[i], survived[i] != 0});
    }
    std::ranges::sort(verdict, {}, [](const auto& v) { return v.first.value; });
    for (const auto& [e, alive] : verdict)
    {
        if (!alive)
        {
            cmd.destroy(e);
        }
    }

    // 先定长再入队：create 把 newborn_ 的元素地址交给了队列，
    // 循环中任何一次扩容都会让已入队的引用悬空。
    newborn_.assign(born.size(), EntityId{});
    for (std::size_t i = 0; i < born.size(); ++i)
    {
        cmd.create(newborn_[i], SpeciesTag{born[i].species},
                   Environment{born[i].environment}, born[i].genome,
                   CreaturePhenotype{born[i].phenotype}, Vitality{});
    }
}

void PopulationStatisticsSystem::execute(World& world, CommandBuffer& cmd)
{
    (void)cmd;

    // 按 species_ids 的取值分桶，桶的插入顺序即 species_ 的注册顺序，
    // 因此 stats() 的下标稳定对应 SpeciesId::value - 1。
    std::vector<std::vector<Genome>> buckets(species_.species_count());
    world.for_each_entity([&](EntityId e)
    {
        auto* g = world.get_if<Genome>(e);
        auto* tag = world.get_if<SpeciesTag>(e);
        if (g == nullptr || tag == nullptr)
        {
            return;
        }
        const std::size_t slot = tag->species.value - 1;
        if (slot < buckets.size())
        {
            buckets[slot].push_back(*g);
        }
    });

    stats_.clear();
    for (std::size_t i = 0; i < buckets.size(); ++i)
    {
        const SpeciesId sid{static_cast<std::uint32_t>(i + 1)};
        stats_.push_back(summarize(buckets[i], species_.mask(sid)));
    }
}

}  // namespace gamev2::ecs::adapt

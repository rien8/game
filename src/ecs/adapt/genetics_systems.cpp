#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/diagnostic.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/breeding.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/statistics.hpp"
#include "gamev2/genetics/survival.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gamev2::ecs::adapt {

using namespace gamev2::genetics;

namespace {

/// 一个待提交的子代。物种随子代一起记下：SeasonOutput 只给基因型与表型，
/// 不回传子代出自哪个父池。cell 同理——settle 按 sites 顺序追加子代，
/// 「哪个子代属于哪个格」只能靠这个顺序对回去。
struct StagedOffspring
{
    SpeciesId species;
    Genome genome;
    Phenotype phenotype;
    environment::EnvironmentSnapshot environment;
    environment::CellIndex cell;
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

/// 格距离是否在半径内。切比雪夫距离——正方形邻域，与「半径 R 格」的
/// 自然读法一致。
[[nodiscard]] bool in_radius(const environment::CellIndex from,
                             const environment::CellIndex to,
                             const float radius_cells) noexcept
{
    const auto side = static_cast<std::uint16_t>(environment::kGridSide);
    const auto dx = std::fabs(static_cast<float>(from.value % side)
                              - static_cast<float>(to.value % side));
    const auto dy = std::fabs(static_cast<float>(from.value / side)
                              - static_cast<float>(to.value / side));
    return std::max(dx, dy) <= radius_cells;
}

/// 子代落在它所属格里的坐标。抖动只由出生序号决定，同一种子两次运行
/// 必须落在同一处，否则确定性测试挂在个体位置而不是算法上。
[[nodiscard]] Position place_in(const environment::CellIndex cell,
                                const std::size_t birth_index) noexcept
{
    const auto side = static_cast<std::uint16_t>(environment::kGridSide);
    const auto slot =
        static_cast<std::uint32_t>((birth_index * 2654435761ULL) % 1000U);
    const float jitter = static_cast<float>(slot) / 1000.0f * 0.9f;
    return Position{static_cast<float>(cell.value % side) + jitter,
                    static_cast<float>(cell.value / side) + jitter};
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
    std::vector<Position> positions;
    /// 与 positions 同序。缺 Position 的个体在任何半径内都不进父池——把缺失
    /// 压成 Position{} 会让它落进 (0, 0) 那一格，于是凭空成为那一格站点的
    /// 亲代，而环境层从来不为它产出过站点。
    std::vector<std::uint8_t> has_position;
    std::vector<float> feedings;
    /// 与上面六个 vector 同序，用来把 settle 的 survived 下标对回实体
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
        const auto* pos = world.get_if<Position>(e);
        positions.push_back(pos == nullptr ? Position{}
                                            : Position{pos->x, pos->y});
        has_position.push_back(pos != nullptr);
        // 缺 Feeding 组件按理论摄食上限处理，与「未接入环境层」是同一条
        // 降级路径，两者必须给出同一个答案。
        const auto* fed = world.get_if<Feeding>(e);
        feedings.push_back(fed == nullptr ? 1.0f : fed->satisfaction);
        species_ids.push_back(tag->species.value);
        handles.push_back(e);
    });

    // 一个物种结算一次。settle 的 SeasonOutput 不回传子代出自哪个父池，
    // 多个池同处一次调用时子代认不回自己的物种，一次一种则归属由调用点
    // 天然确定。空间邻域分桶属于生态 spec。
    //
    // 近交系数按物种分开存，下标即 SpeciesId::value − 1。补齐到当前物种数，
    // 免得下标越界；尚未结算过的物种保持 0（等于不压制）。
    inbreeding_.resize(species_.species_count(), 0.0f);
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
        std::vector<float> pool_feedings;
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
            pool_feedings.push_back(feedings[i]);
        }
        if (members.empty())
        {
            continue;
        }

        const std::vector<std::uint32_t> pool_species(members.size(), s);
        std::vector<ParentPool> sites;
        // 与 sites 同序：sites[k] 的子代落在 target_cells[k]。
        std::vector<environment::CellIndex> target_cells;
        // ParentPool::parents 是 span，只借用不拥有，因此装桶的 vector 必须活得
        // 比 settle 长。桶若留在分支的块体内，sites[k].parents 在 settle 读到
        // 它时早已悬垂——原代码把 parents 提到 settle 之前正是为此，接环境层
        // 时不能把这个前提丢掉。两条分支各用自己的容器：空间路径一桶一个，
        // 降级路径仍是一个大池。
        std::vector<std::vector<Parent>> buckets;
        std::vector<Parent> parents;
        // 两条分支填 Parent 时用的是同一套约定：index 填本池内的下标，
        // settle 靠它回查 survived 与季节适应力；fitness 是先验权重，
        // settle 重建父池时整列替换成季节适应力，这里给什么都不影响结果。
        // 育种禁令：表型落在区间的个体不进亲代池。**只看 CreaturePhenotype**
        // —— 玩家的指令说的是「这种长相的不许生」，不是「这个基因型不许生」；
        // 同一表型可由多个基因型产生，按基因型过滤会误伤。
        const auto barred = [this, s, &pool_phenotypes](const std::size_t i)
        {
            const auto& traits = pool_phenotypes[i].traits;
            return std::ranges::any_of(
                culls_,
                [s, &traits](const CullRule& rule)
                {
                    if (rule.species.value != s)
                    {
                        return false;
                    }
                    const auto v = traits[trait_index(rule.trait)];
                    return v >= rule.lo && v <= rule.hi;
                });
        };
        if (targets_ != nullptr)
        {
            // 空间分桶在 adapt 层完成：Parent::index 必须是本物种数组内的
            // 下标，而那个数组由本函数切出，环境层无从知道它的形状。
            // 缺 Position 的个体进不了任何桶——环境层同样只为有 Position
            // 的实体产出站点，两个 system 对「谁参与繁殖」答案一致。
            //
            // reserve 而非依赖扩容：下面交给 sites 的 span 指向 buckets 的
            // 内层缓冲，预留一次之后那批缓冲的位置就再也不会变。
            buckets.reserve(targets_->size());
            for (const auto& target : *targets_)
            {
                if (target.species.value != s)
                {
                    continue;
                }
                std::vector<Parent> bucket;
                for (std::size_t i = 0; i < members.size(); ++i)
                {
                    if (has_position[members[i]] == 0 || barred(i))
                    {
                        continue;
                    }
                    const auto& p = positions[members[i]];
                    if (in_radius(environment::CellIndex::of(p.x, p.y),
                                  target.cell, mating_radius_cells_))
                    {
                        bucket.push_back(Parent{i, &pool_genomes[i], 1.0f});
                    }
                }
                if (bucket.size() < 2)
                {
                    // 池内不足两个个体时 make_offspring 必然失败，
                    // 与其让它空转一轮，不如不产出子代——省下的格子
                    // 才能让子代认对位置。
                    continue;
                }
                buckets.push_back(std::move(bucket));
                sites.push_back(ParentPool{buckets.back(), SpeciesId{s},
                                           target.environment,
                                           target.progeny});
                target_cells.push_back(target.cell);
            }
        }
        else
        {
            parents.reserve(members.size());
            std::vector<Phenotype> kept;
            kept.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                if (barred(i))
                {
                    continue;
                }
                parents.push_back(Parent{i, &pool_genomes[i], 1.0f});
                // 出生预算也只按留下的那些算，否则被禁的个体仍以繁殖力的
                // 份额撑着一个自己填不满的池子。
                kept.push_back(pool_phenotypes[i]);
            }
            sites.push_back(ParentPool{parents, SpeciesId{s}, std::nullopt,
                                       progeny_for(kept)});
        }

        // 子代认回自己的格靠顺序：settle 按 sites 顺序逐个追加子代，而
        // birth_cells 是按 sites[k].progeny 摊平的同一个序列。
        //
        // **已知限制**：摊平按下标对位，因此只在「每个站点要么足额、要么整站
        // 缺席」时成立，而后者由上面的 bucket.size() < 2 保证。settle 还会在
        // 建池后把本季死亡的亲代剔掉，于是某个站点可以**中途少产**：站点 A 产
        // 0、站点 B 产 2 时，B 的两个孩子都落进 A 的格。触发条件是同格亲代整批
        // 团灭（make_offspring 因池里活着的不足两个而失败），而饥荒正是把
        // survival_rate 压下来、把这一支变成常态分支的机制。
        //
        // 修它需要 settle 按站点回报实际产量（遗传层的契约变更），已作为独立
        // 任务推迟；在此之前，下面的 std::min 只兜得住尾部 shortfall。
        //
        // 另有一处不对称：settle 把 `progeny == 0` 按 1 处理，这里摊 0 份。
        // 这一半不可达——build_sites 只在 progeny > 0 时才发站点。
        std::vector<environment::CellIndex> birth_cells;
        if (targets_ != nullptr)
        {
            for (std::size_t k = 0; k < sites.size(); ++k)
            {
                for (std::uint32_t n = 0; n < sites[k].progeny; ++n)
                {
                    birth_cells.push_back(target_cells[k]);
                }
            }
        }

        // 统计交给 PopulationStatisticsSystem：settle 里那份每季 O(S·N) 重算
        // 一遍等位基因频率，生产路径上没人读。
        //
        // 未接入环境层时给空 span，而不是一串 1.0f。settle 的判据是
        // 「feedings.size() == genomes.size()」，一串 1.0f 同样命中，于是它会
        // 去读个体身上真的有的 Feeding——而那只有环境层跑过之后才存在。
        // 后果不是数值漂移：energy_survival 里
        // intake = SenseRange × Fecundity × feeding 直接改写 survival_rate，
        // 存活抽签、活下来的父池、子代基因组逐层跟着变。空 span 让 settle 走回
        // 它自己的 1.0f 降级分支，调用形状与本轮之前逐字相同。
        const std::span<const float> feedings_in =
            targets_ != nullptr ? std::span<const float>{pool_feedings}
                                : std::span<const float>{};
        // 本物种自己的近交系数。缺项按 0：那是「本季还没算过这个物种」，
        // 漏压一次好过读越界。
        const float inbreeding = s <= inbreeding_.size() ? inbreeding_[s - 1]
                                                         : 0.0f;
        const auto out = settle(
            SeasonInput{pool_genomes, pool_phenotypes, pool_environments,
                        pool_threats, pool_species, sites, false, feedings_in,
                        inbreeding},
            table_, species_, config_, streams);
        // **必须按下标写回，不能整份覆盖。** 逐物种结算的结果只属于那个物种，
        // 而一季可能有多个物种各结算一次；整份覆盖会让排在后面的物种抹掉前面
        // 的读数，下一季所有物种于是都吃最后一个物种的近交系数。
        inbreeding_[s - 1] = out.offspring_inbreeding;

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
            // 兜底的 min 只挡住**尾部** shortfall：settle 因「本季死绝、活下
            // 来的亲代不足两个」而少于预算数时，多出的子代认回最后一个站点，
            // 好过越界读。**中部** shortfall（站点 A 产 0、站点 B 产 2）会让
            // B 的子代落进 A 的格，那个形状这里挡不住——见上面 birth_cells
            // 的已知限制。
            const auto cell =
                birth_cells.empty()
                    ? environment::CellIndex{}
                    : birth_cells[std::min(i, birth_cells.size() - 1)];
            born.push_back(StagedOffspring{SpeciesId{s}, out.offspring[i],
                                           out.offspring_phenotypes[i],
                                           out.offspring_environments[i],
                                           cell});
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
        // 接入环境层才挂 Position：组件集在创建那一刻定死，事后 add 只会把刚
        // 出生的个体迁往新 archetype，而它本季还没进过任何一格，谁也不知道该
        // 迁到哪去。
        //
        // 未接入时不给。gene/表型/环境与随机流消费都不受它影响（门禁测试逐位
        // 相同仍然成立），但无条件下挂时 birth_cells 是空的、cell 恒为 (0, 0)，
        // 于是历史上每一个未接入期出生的个体都被钉在那一格——实体组件集与本轮
        // 之前不同，任何未来读 Position 的 system 都会看到那堆个体。
        if (targets_ != nullptr)
        {
            const auto place = place_in(born[i].cell, i);
            cmd.create(newborn_[i], SpeciesTag{born[i].species},
                       Environment{born[i].environment}, born[i].genome,
                       CreaturePhenotype{born[i].phenotype}, Vitality{},
                       Position{place.x, place.y});
        }
        else
        {
            cmd.create(newborn_[i], SpeciesTag{born[i].species},
                       Environment{born[i].environment}, born[i].genome,
                       CreaturePhenotype{born[i].phenotype}, Vitality{});
        }
    }
}

void BreedingSystem::add_cull(CullRule rule)
{
    culls_.push_back(rule);
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

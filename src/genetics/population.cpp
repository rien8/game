// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/population.hpp"

#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/survival.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::genetics {

namespace {

/// 子代的环境快照取自待繁殖位置的父池。生态 spec 交付前没有采样结果时，
/// 退回全默认快照：温度偏移 0、光照与资源充足度 1。
[[nodiscard]] environment::EnvironmentSnapshot site_environment(
    const ParentPool& pool)
{
    return pool.environment.value_or(environment::EnvironmentSnapshot{});
}

}  // namespace

SeasonOutput settle(const SeasonInput& input, const LocusTable& table,
                    const SpeciesRegistry& species,
                    const GeneticsConfig& config, RandomStreams& streams)
{
    SeasonOutput out{};
    out.survived.assign(input.genomes.size(), 0);
    out.survival_rates.assign(input.genomes.size(), 0.0f);
    out.season_fitness.assign(input.genomes.size(), 0.0f);

    // 空种群提前返回，stats 也就随之留空而不是填满 census = 0 的物种摘要：
    // 下面「统计覆盖全部已注册物种」只约束非空种群，这里没有任何个体可统计，
    // 给出零摘要只会把「本季普查过」和「本季一个都没有」混为一谈。
    if (input.genomes.empty())
    {
        return out;
    }

    // 第 1 步：存活筛选 + 适应力。
    // 归一化基准必须在整季遍历前先取全体最大值：边遍历边算会让先算到的
    // 个体用自己的局部最大值把适应力顶到 1，先算的和后算的不可比。
    float max_fecundity = 0.0f;
    for (const auto& p : input.phenotypes)
    {
        max_fecundity = std::max(
            max_fecundity, p.traits[trait_index(TraitKind::Fecundity)]);
    }

    auto& survival_rng = streams.stream(RandomPurpose::Survival);
    for (std::size_t i = 0; i < input.genomes.size(); ++i)
    {
        // 寿命是硬判定：过寿的个体不参与本季存活筛选，适应力也保持 0。
        if (outlived(input.phenotypes[i], 0))
        {
            continue;
        }
        const SurvivalInput s{
            input.phenotypes[i], input.environments[i],
            // 空 span 是「未接入环境层」的降级路径，逐个回落理论摄食上限。
            input.feedings.size() == input.genomes.size() ? input.feedings[i]
                                                           : 1.0f,
            input.threats[i]};
        const float rate = survival_rate(s, config);
        if (uniform_unit(survival_rng) < rate)
        {
            out.survived[i] = 1;
        }
        // 赋值必须在存活判定之外：抽签失败只影响 survived，生存率与适应力
        // 都是体质的函数，与本季运气无关。
        out.survival_rates[i] = rate;
        out.season_fitness[i] = fitness(rate, input.phenotypes[i],
                                        max_fecundity);
    }

    // 第 2、3、4 步：建亲代池 → 重组与突变 → 表型。
    // 调用方传入的池只回答「谁在半径 R 内」，谁活到了本季、谁该多产子代由
    // settle 判定。直接用传入的池会让本季已死亡的个体继续繁殖——设计文档
    // §7.1 把建池放在 settle 内的第 2 步，正是为此。
    //
    // progeny 由调用方给出：它回答的是「这个位置（或这个物种）本季该出多少
    // 子代」，而这正是 settle 之外才掌握的信息（空间 spec 的生态位密度、
    // 物种级的繁殖力）。settle 只负责把本季死亡的亲代剔掉，再按这个数
    // 重复交配。
    std::vector<Parent> living;
    for (const auto& site : input.sites)
    {
        living.clear();
        for (const auto& parent : site.parents)
        {
            // index 越界说明调用方没按 SeasonInput::genomes 的下标填父池，
            // 丢弃该条目好过读越界——症状是整季无声地产不出后代，而不是崩。
            if (parent.index >= out.survived.size()
                || out.survived[parent.index] == 0)
            {
                continue;
            }
            living.push_back(
                Parent{parent.index, parent.genome,
                       out.season_fitness[parent.index]});
        }
        const ParentPool live{living, site.species, site.environment};
        const environment::EnvironmentSnapshot env = site_environment(site);
        // 0 按 1 处理：留 0 的调用方得到的是「每个 site 一个子代」的旧行为，
        // 而 0 本身在生态上说不通（既没灭绝也没繁殖）。
        const std::uint32_t count = site.progeny == 0 ? 1 : site.progeny;
        for (std::uint32_t n = 0; n < count; ++n)
        {
            const auto child = make_offspring(live, table, config, streams);
            if (!child.has_value())
            {
                // 池空或只剩一个亲代时，这个判定与已经产了几个子代无关：
                // 再试也是同一条分支，重试只会白烧随机数。
                break;
            }
            out.offspring.push_back(*child);
            out.offspring_environments.push_back(env);
            out.offspring_phenotypes.push_back(
                phenotype_of(*child, table, species.mask(site.species), env,
                             config));
        }
    }

    // 第 5 步：按物种分组统计。开关关着时 out.stats 留空，调用方需要统计
    // 就去跑 PopulationStatisticsSystem。
    // 覆盖全部已注册物种而不只是 sites 里出现过的：本季归零的物种仍要
    // 产出一条 census = 0 的摘要，否则种群归零时玩家面板会突然少一物种。
    // 只在本函数已越过空种群早退后成立。
    if (!input.want_statistics)
    {
        return out;
    }
    out.stats.resize(species.species_count());
    for (std::size_t s = 0; s < out.stats.size(); ++s)
    {
        // SpeciesRegistry 的 id 从 1 起，masks_ 下标比 id 小一。
        const auto id = SpeciesId{static_cast<std::uint32_t>(s + 1)};
        std::vector<Genome> members;
        for (std::size_t i = 0; i < input.genomes.size(); ++i)
        {
            if (input.species_ids[i] == id.value)
            {
                members.push_back(input.genomes[i]);
            }
        }
        out.stats[s] = summarize(members, species.mask(id));
    }

    return out;
}

}  // namespace gamev2::genetics

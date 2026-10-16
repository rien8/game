/// @file population.hpp
/// @brief 一季种群结算：SeasonInput 收齐本季全部输入，settle 按固定五步
///        单线程走完存活筛选 → 逐位置建池交配 → 遗传 → 表型 → 统计，
///        一次性返回 SeasonOutput。
///
/// 「一次性返回」是这里的关键约定：子代先在 staging 里积累，行为系统因此永远
/// 读不到半新半旧的种群，整季换代在调用方看来是一次原子提交。函数本身不持有
/// 跨季状态，随机性由调用方传入的 RandomStreams 提供。
#pragma once

#include "gamev2/genetics/breeding.hpp"
#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/species.hpp"
#include "gamev2/genetics/statistics.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::genetics {

/// 一季结算的全部输入。五个 span 数组在 genomes 下标上互相对齐，
/// sites 则独立于种群，它描述的是「哪里要繁殖」。
struct SeasonInput
{
    std::span<const Genome> genomes;
    std::span<const Phenotype> phenotypes;
    std::span<const EnvironmentSnapshot> environments;
    std::span<const float> threats;
    /// 与 genomes 同序的物种标签，供第 5 步按物种分组统计
    std::span<const std::uint32_t> species_ids;
    /// 待繁殖位置及其已分桶的亲代池。空列表意味着本季无繁殖。
    std::span<const ParentPool> sites;
    /// 是否在第 5 步附一份逐物种统计。
    ///
    /// 统计的正式归属是 ECS 侧的 PopulationStatisticsSystem；settle 里
    /// 这份是给不打算单独跑一个 system 的调用方（确定性 / 漂变测试、将来的
    /// 离线分析）用的可选副本。它每季按物种重扫一遍全体基因组，成本
    /// O(S·N)，生产路径上原样再算一遍没有收益，因此默认关闭。
    bool want_statistics = false;
};

/// 子代先在 staging 里积累，整季结算完成后一次性返回。
/// 行为系统因此永远读不到半新半旧的种群。
struct SeasonOutput
{
    std::vector<Genome> offspring;
    std::vector<Phenotype> offspring_phenotypes;
    std::vector<EnvironmentSnapshot> offspring_environments;
    /// 与 genomes 同序，
    /// 表示本季活到下一季。
    std::vector<std::uint8_t> survived;
    /// 与 genomes 同序，本季各体的生存率（各项生理约束存活率之积）。
    /// 与 survived 不同：它恒在 (0, 1]，抽签输赢是另一次抽样。outlived
    /// 判死的个体保持 0。
    std::vector<float> survival_rates;
    /// 与 genomes 同序，本季的相对适应力。它是 fitness(survival_rate, …)
    /// 的输出，与抽签结果无关：抽输了的个体照样带权重，只是不进亲代池。
    /// outlived 判死的个体保持 0。
    std::vector<float> season_fitness;
    /// 与 SpeciesRegistry 的注册顺序同序，覆盖全部已注册物种。
    /// 仅在 SeasonInput::want_statistics 为真时填充。
    std::vector<SpeciesStatistics> stats;
};

/// 五个步骤顺序固定、单线程：存活筛选 → 逐位置建亲代池并交配 → 遗传 →
/// 表型 → 统计。整季只返回一个 SeasonOutput，调用方在此之后才提交换代。
/// 第 5 步受 SeasonInput::want_statistics 控制，默认不跑。
///
/// 存活判定传给 outlived 的 age 恒为 0：SeasonInput 不携带年龄字段，逐季的
/// 年龄递增由 ECS 侧的 AgingSystem 负责。因此寿命这道硬判定在本函数里只
/// 筛掉 `Lifespan <= 0` 的个体；全零的 trait_min / trait_max 会把每个性状
/// 夹成 0，于是全员第一季判死、整季零子代且不报错。
///
/// \pre `sites` 每个 `ParentPool::parents` 条目的 `index` 都落在
/// `input.genomes` 内。settle 靠它回查本季存活标记与适应力，越界的条目会被
/// 丢弃而不是报错。
[[nodiscard]] SeasonOutput settle(const SeasonInput& input,
                                 const LocusTable& table,
                                 const SpeciesRegistry& species,
                                 const GeneticsConfig& config,
                                 RandomStreams& streams);

}  // namespace gamev2::genetics

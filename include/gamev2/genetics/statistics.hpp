#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/species.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::genetics {

struct AlleleStats
{
    std::array<float, kMaxAlleles> frequency{};
    float h_observed = 0.0f;
    float h_expected = 0.0f;
};

/// 漂变、瓶颈、近交衰退共用这一条统计路径——三者不是三个算法，
/// 而是同一采样机制在三个尺度上的表现。
struct SpeciesStatistics
{
    /// 只包含激活位点，逐位点明细仅对玩家选中的物种计算
    std::vector<AlleleStats> loci;
    float heterozygosity = 0.0f;
    float f_coefficient = 0.0f;
    float effective_population = 0.0f;
    std::uint32_t census = 0;
};

/// 近交系数用杂合度估计，不追踪家谱——追踪家谱要在每代给每个个体
/// 存一份祖先集合，而当代等位基因频率已经含有同等信息。
///
/// \pre `genomes` 中每个 `AlleleCall::allele` 都落在 `[0, kMaxAlleles)`。
/// 本函数不改 LocusTable，无从区分「该位点只声明了 2 个等位基因、第 3 个
/// 槽位闲置」与「这个值本身就是垃圾」，因此对越界取值只能夹取而不能报错：
/// 契约不成立时结果是可解释但失真的普查，不是越界写。
[[nodiscard]] SpeciesStatistics summarize(std::span<const Genome> genomes,
                                          const SpeciesMask& mask);

}  // namespace gamev2::genetics

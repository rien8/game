/// @file statistics.hpp
/// @brief 群体统计：summarize 扫一遍种群的基因组，产出各位点的等位基因频率、
///        杂合度、近交系数与有效种群大小。
///
/// 漂变、瓶颈、近交衰退不是三个算法，而是同一采样机制在三个尺度上的表现，
/// 因此共用这一条统计路径。近交系数用杂合度反推而不追踪家谱：家谱要在每代
/// 给每个个体存一份祖先集合，而当代等位基因频率已含有同等信息。
#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/species.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::genetics {

/// 单个位点的等位基因频率与杂合度实测 / 理论值。h 的两支相减即近交系数
/// 的来源。
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

/// 两组之间的 Nei 遗传距离（等位基因频率版本）：`Σ_locus |f_a − f_b| / 2`。
///
/// \pre 无需成立：任意两组都能比——包括空的，恒得 0.0。
/// \post 返回值 ∈ [0, min(a.loci.size(), b.loci.size()) / 2]。
///   **注意这是 [0, 150] 而不是 [0, 0.5]**（300 位点全激活时）。把它当归一化
///   量读，就是设计文档 §2.3 里 θ 定错两个数量级的直接原因。
///
/// 只取公共前缀：两边掩码不同则 loci 长度可能不同。
[[nodiscard]] double divergence(const SpeciesStatistics& a,
                                const SpeciesStatistics& b) noexcept;

}  // namespace gamev2::genetics

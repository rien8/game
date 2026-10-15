#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::genetics {

inline constexpr std::size_t kLocusCount = 300;
inline constexpr std::size_t kMaxAlleles = 4;

using AlleleId = std::uint8_t;  ///< 位点内索引，不是全局编号

/// jitter 是等位基因取值上的有界微扰，使 Trait 位点的突变成为平滑步进
/// 而非在离散等位基因间跳变；Structural 位点忽略它。
struct AlleleCall
{
    AlleleId allele = 0;
    std::int8_t jitter = 0;

    /// 逐位比对靠它：Haplotype / Genome 的比较都由它递归得到，
    /// 确定性测试不必再手写按位点遍历。
    friend bool operator==(const AlleleCall&, const AlleleCall&) = default;
};

using Haplotype = std::array<AlleleCall, kLocusCount>;

/// 单倍型拆成两个数组而非交错存储，是为了让重组（按单倍型整体交叉）
/// 与统计（按单倍型遍历）都能顺序访问。
struct Genome
{
    Haplotype a{};
    Haplotype b{};

    friend bool operator==(const Genome&, const Genome&) = default;
};

}  // namespace gamev2::genetics

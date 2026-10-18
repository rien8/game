// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gamev2::genetics {

namespace {

/// H_exp 低于此值的位点，其 H_obs / H_exp 比值几乎全是估计噪声。
/// 跳过它们，否则近交系数会被这些近乎单态的位点主导。
constexpr float kMinExpectedHeterozygosity = 1.0f / 1024.0f;

/// 越界 AlleleId 的夹取目标。见 summarize 里那段说明。
constexpr AlleleId kLastAllele = static_cast<AlleleId>(kMaxAlleles - 1);

}  // namespace

SpeciesStatistics summarize(std::span<const Genome> genomes,
                            const SpeciesMask& mask)
{
    SpeciesStatistics out{};
    out.census = static_cast<std::uint32_t>(genomes.size());
    if (genomes.empty())
    {
        return out;
    }

    std::vector<std::size_t> active;
    active.reserve(mask.active.count());
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        if (mask.active.test(i))
        {
            active.push_back(i);
        }
    }
    out.loci.resize(active.size());

    float heterozygosity_sum = 0.0f;
    float f_weighted_sum = 0.0f;
    float f_weight_sum = 0.0f;
    float h_loss_sum = 0.0f;

    for (std::size_t slot = 0; slot < active.size(); ++slot)
    {
        const std::size_t locus = active[slot];
        AlleleStats& stats = out.loci[slot];

        std::array<std::uint32_t, kMaxAlleles> counts{};
        std::uint32_t heterozygous = 0;

        for (const auto& genome : genomes)
        {
            // Genome 是公开的 ECS 组件，Any World::add<Genome>() 都能塞进
            // uint8_t 宽度的越界 AlleleId，而 counts 只有 kMaxAlleles 个元素：
            // std::array 不在 _ITERATOR_DEBUG_LEVEL 的管辖内，debug STL 的
            // 边界检查抓不到，这里必须自己挡住。
            //
            // 夹取而非跳过：跳过一个等位基因会让 sum(counts) < 2n，而下游的
            // p_k = counts[k] / (2n) 之和便不再为 1，杂合度算出来没有任何
            // 可解释的语义；夹取则保持 2n 不变，越界个体被并进最后一个等位
            // 基因——普查照常完成，频率仍是一份合法的频率分布。
            const AlleleId x = std::min(genome.a[locus].allele, kLastAllele);
            const AlleleId y = std::min(genome.b[locus].allele, kLastAllele);
            ++counts[x];
            ++counts[y];
            if (x != y)
            {
                ++heterozygous;
            }
        }

        const float n = static_cast<float>(genomes.size());
        float h_expected = 0.0f;
        for (std::size_t k = 0; k < kMaxAlleles; ++k)
        {
            const float p = static_cast<float>(counts[k]) / (2.0f * n);
            stats.frequency[k] = p;
            h_expected += p * (1.0f - p);
        }

        stats.h_observed = static_cast<float>(heterozygous) / n;
        stats.h_expected = h_expected;
        heterozygosity_sum += stats.h_observed;

        if (h_expected >= kMinExpectedHeterozygosity)
        {
            const float f = 1.0f - stats.h_observed / h_expected;
            // 按 H_exp 加权：多样性低的位点上 F 的估计噪声大，不加权会让
            // 近交系数被它们主导。
            f_weighted_sum += f * h_expected;
            f_weight_sum += h_expected;
        }
        h_loss_sum += 1.0f - h_expected;
    }

    if (!active.empty())
    {
        out.heterozygosity =
            heterozygosity_sum / static_cast<float>(active.size());
    }
    out.f_coefficient = (f_weight_sum > 0.0f) ? f_weighted_sum / f_weight_sum
                                              : 0.0f;
    // 缺额是逐位点的量，必须先除到位点数再取倒数，与上面 heterozygosity
    // 同一个口径。裸求和会让 Ne 随激活位点数线性缩水：掩码大小不同的两个
    // 物种即便逐位点数据完全一致，Ne 也会差出与位点数成正比的倍数。
    //
    // 不走教科书那条 1 / (2F̄)：F̂ = 1 - H_obs/H_exp 在有限样本下系统性偏负
    // （2n 次多项抽样使 E[H_obs] ≈ 2n/(2n-1) × H_exp），偏差按 1/n 收缩，
    // 取倒数后随机交配的正常种群会报出负的 Ne，且绝对值随普查规模一起发散
    // ——n = 64 时已到 -122。F̄ 只作诊断读数保留在 f_coefficient 里。
    out.effective_population =
        (h_loss_sum > 0.0f) ? static_cast<float>(active.size())
                                 / (2.0f * h_loss_sum)
                             : 0.0f;
    return out;
}

double divergence(const SpeciesStatistics& a,
                  const SpeciesStatistics& b) noexcept
{
    const auto n = std::min(a.loci.size(), b.loci.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        sum += std::fabs(static_cast<double>(a.loci[i].frequency[0])
                         - static_cast<double>(b.loci[i].frequency[0]));
    }
    return sum / 2.0;
}

}  // namespace gamev2::genetics

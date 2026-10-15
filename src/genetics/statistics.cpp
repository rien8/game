// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/statistics.hpp"

#include <algorithm>
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
    out.effective_population =
        (h_loss_sum > 0.0f) ? 1.0f / (2.0f * h_loss_sum) : 0.0f;
    return out;
}

}  // namespace gamev2::genetics

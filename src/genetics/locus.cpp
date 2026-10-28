// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/locus.hpp"

#include <cstdint>

namespace gamev2::genetics {

namespace {

bool rank_is_unique(const LocusDef& locus)
{
    for (std::size_t i = 0; i < locus.allele_count; ++i)
    {
        for (std::size_t j = i + 1; j < locus.allele_count; ++j)
        {
            if (locus.alleles[i].dom_rank == locus.alleles[j].dom_rank)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

std::expected<void, LocusConfigError>
validate(const LocusTable& table, const GeneticsConfig& config)
{
    std::size_t overdominant = 0;

    for (const auto& locus : table.loci)
    {
        if (locus.allele_count < 2 || locus.allele_count > kMaxAlleles)
        {
            return std::unexpected(LocusConfigError::AlleleCountOutOfRange);
        }
        if (locus.kind == LocusKind::Structural && !rank_is_unique(locus))
        {
            return std::unexpected(LocusConfigError::DuplicateRank);
        }
        if (locus.kind == LocusKind::Trait
            && locus.target >= static_cast<std::uint8_t>(kTraitCount))
        {
            return std::unexpected(LocusConfigError::TraitOutOfRange);
        }
        // Structural 的 target 越界在配置期拦下：运行期没有兜底分支，漏过这里
        // 就等于静默丢弃一次结构写入。
        if (locus.kind == LocusKind::Structural
            && locus.target
                   >= static_cast<std::uint8_t>(StructuralSlot::kSlotCount))
        {
            return std::unexpected(LocusConfigError::SlotOutOfRange);
        }
        if (locus.dominance < 0.0f)
        {
            return std::unexpected(LocusConfigError::NegativeDominance);
        }
        if (locus.switch_rate < 0.0f || locus.switch_rate > 1.0f)
        {
            return std::unexpected(LocusConfigError::BadSwitchRate);
        }
        if (locus.dominance > 1.0f)
        {
            ++overdominant;
        }
    }

    if (overdominant > config.max_overdominant_loci)
    {
        return std::unexpected(LocusConfigError::TooManyOverdominant);
    }

    // 表校验完再校验配置本身。区间退化在这里拦下是因为表达层只做 clamp：
    // 运行期没有「区间不合理」的分支，漏过就静默把性状钉死在常量上。
    for (std::size_t t = 0; t < kTraitCount; ++t)
    {
        if (config.trait_min[t] >= config.trait_max[t])
        {
            return std::unexpected(LocusConfigError::BadTraitRange);
        }
    }

    // breeding 用 uniform_index(2 × jitter_step + 1) 抽步长，再把结果
    // clamp 到 [−jitter_step, jitter_step]。负步长让计数转成天文数、且
    // clamp 的下界大于上界——两者都是 UB，只能在配置期拦。
    if (config.jitter_step < 0)
    {
        return std::unexpected(LocusConfigError::NegativeJitterStep);
    }

    return {};
}

}  // namespace gamev2::genetics

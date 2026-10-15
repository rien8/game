// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/expression.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>

namespace gamev2::genetics {

namespace {

float allele_value(const AlleleDef& allele, std::int8_t jitter,
                   const GeneticsConfig& config) noexcept
{
    return allele.additive + static_cast<float>(jitter) * config.jitter_unit;
}

void write_slot(Phenotype& p, const LocusDef& locus,
                std::uint8_t state) noexcept
{
    // 槽位由配置期的 target 定死，运行期只按它分发；多个位点指向同一槽位时
    // 按位点 id 升序依次覆盖，后者胜。
    //
    // 兜底不能是 std::unreachable：它只在 target 恰好等于 kSlotCount 时命中，
    // target >= 5 一个 case 都不匹配，从 switch 末尾掉出去同样是未定义行为，
    // 而 unreachable 只是把同一个错误换成更难查的形态。default 覆盖任何越界，
    // 断言把「配置写错」停在发生点上。
    switch (static_cast<StructuralSlot>(locus.target))
    {
    case StructuralSlot::BodySegments:
        p.body_segments = state;
        break;
    case StructuralSlot::LimbPairs:
        p.limb_pairs = state;
        break;
    case StructuralSlot::Eyes:
        p.eyes = state;
        break;
    case StructuralSlot::Flags:
        p.flags = state;
        break;
    case StructuralSlot::kSlotCount:
    default:
        assert(false && "Structural 位点 target 越界，validate 应已拦下");
        break;
    }
}

}  // namespace

float trait_value(const LocusDef& locus, AlleleId x, std::int8_t jx,
                  AlleleId y, std::int8_t jy,
                  const GeneticsConfig& config) noexcept
{
    const AlleleDef& ax = locus.alleles[x];
    const AlleleDef& ay = locus.alleles[y];

    const float vx = allele_value(ax, jx, config);
    const float vy = allele_value(ay, jy, config);

    const float m = (vx + vy) * 0.5f;
    const float a = (vx - vy) * 0.5f;

    if (ax.dom_rank == ay.dom_rank)
    {
        return m;
    }
    // h_eff 的符号由 dom_rank 的高低给出：等级高的一侧被推向另一侧，
    // 使杂合子随 h 增大而越过显性纯合子。
    const float h_eff = (ax.dom_rank > ay.dom_rank) ? locus.dominance
                                                     : -locus.dominance;
    return m + h_eff * a;
}

std::uint8_t structural_state(const LocusDef& locus, AlleleId x,
                              AlleleId y) noexcept
{
    const AlleleDef& ax = locus.alleles[x];
    const AlleleDef& ay = locus.alleles[y];

    if (ax.dom_rank == ay.dom_rank)
    {
        return std::max(ax.state, ay.state);
    }
    return (ax.dom_rank > ay.dom_rank) ? ax.state : ay.state;
}

Phenotype express(const Genome& genome, const LocusTable& table,
                  const SpeciesMask& mask, const GeneticsConfig& config)
{
    Phenotype p{};
    p.traits = config.trait_base;

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        if (!mask.active.test(i))
        {
            continue;
        }
        const LocusDef& locus = table.loci[i];

        if (locus.kind == LocusKind::Trait)
        {
            const float v = trait_value(locus, genome.a[i].allele,
                                        genome.a[i].jitter,
                                        genome.b[i].allele,
                                        genome.b[i].jitter, config);
            p.traits[trait_index(static_cast<TraitKind>(locus.target))] +=
                locus.scale * v;
        }
        else
        {
            write_slot(p, locus,
                       structural_state(locus, genome.a[i].allele,
                                        genome.b[i].allele));
        }
    }

    for (std::size_t t = 0; t < kTraitCount; ++t)
    {
        p.traits[t] = std::clamp(p.traits[t], config.trait_min[t],
                                 config.trait_max[t]);
    }
    return p;
}

void apply_environment(Phenotype& phenotype,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config)
{
    // 温度偏离适宜区抬高代谢：温度应激是真实的代谢负担，它经由
    // Metabolism 传导到能量收支约束，因此不必另设一项约束。
    phenotype.traits[trait_index(TraitKind::Metabolism)] +=
        std::fabs(environment.temperature)
        * config.temperature_metabolic_load;

    // 资源不足压低繁殖力。
    phenotype.traits[trait_index(TraitKind::Fecundity)] +=
        (environment.fertility - 1.0f) * config.fertility_fecundity_gain;

    // 光照不足削弱视觉。环境不触碰结构字段：形态有反应规范，
    // 但那会让结构位点的表达规则与遗传方差分解一起膨胀。
    phenotype.traits[trait_index(TraitKind::SenseRange)] +=
        (environment.light - 1.0f) * config.light_sense_gain;

    for (std::size_t t = 0; t < kTraitCount; ++t)
    {
        phenotype.traits[t] = std::clamp(phenotype.traits[t],
                                         config.trait_min[t],
                                         config.trait_max[t]);
    }
}

Phenotype phenotype_of(const Genome& genome, const LocusTable& table,
                       const SpeciesMask& mask,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config)
{
    Phenotype p = express(genome, table, mask, config);
    apply_environment(p, environment, config);
    return p;
}

}  // namespace gamev2::genetics

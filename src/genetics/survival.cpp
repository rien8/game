// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/survival.hpp"

#include <algorithm>
#include <cmath>

namespace gamev2::genetics {

namespace {

float trait_of(const Phenotype& p, TraitKind t) noexcept
{
    return p.traits[trait_index(t)];
}

float energy_survival(const SurvivalInput& in) noexcept
{
    const float intake = trait_of(in.phenotype, TraitKind::SenseRange)
                         * trait_of(in.phenotype, TraitKind::Fecundity)
                         * in.feeding;
    const float burn = trait_of(in.phenotype, TraitKind::Metabolism)
                       * trait_of(in.phenotype, TraitKind::BodySize)
                       * (1.0f + trait_of(in.phenotype, TraitKind::Speed));
    if (burn <= 0.0f)
    {
        return 1.0f;
    }
    return std::clamp(intake / burn, 0.0f, 1.0f);
}

float thermal_survival(const SurvivalInput& in,
                       const GeneticsConfig& config) noexcept
{
    const float width = config.thermal_base_width
                        + config.thermal_resilience_gain
                              * trait_of(in.phenotype,
                                         TraitKind::Resilience);
    if (width <= 0.0f)
    {
        return 0.0f;
    }
    const float distance = std::fabs(in.environment.temperature);
    return std::clamp(1.0f - distance / width, 0.0f, 1.0f);
}

float escape_survival(const SurvivalInput& in,
                      const GeneticsConfig& config) noexcept
{
    // 捕食压力随体型上升：大个体即便绝对速度不低，单位体重的机动性也更差，
    // 更容易被拦截。它同时构成与能量收支的天然权衡——大体型消耗高，
    // 又得靠更高的速度把逃脱余量补回来。
    const float demand = in.threat_level
                         * trait_of(in.phenotype, TraitKind::BodySize);
    const float headroom = trait_of(in.phenotype, TraitKind::Speed) - demand;
    return std::clamp(headroom * config.escape_responsiveness, 0.0f, 1.0f);
}

}  // namespace

float survival_rate(const SurvivalInput& input, const GeneticsConfig& config)
{
    return energy_survival(input) * thermal_survival(input, config)
           * escape_survival(input, config);
}

float fitness(float survival, const Phenotype& phenotype,
              float max_fecundity) noexcept
{
    if (max_fecundity <= 0.0f)
    {
        // 退化路径刻意不加 clamp：survival 契约上已是概率，再截断一遍没有
        // 收益，却会掩盖调用方传错 max_fecundity 的事实。
        return survival;
    }
    return std::clamp(survival * trait_of(phenotype, TraitKind::Fecundity)
                          / max_fecundity,
                      0.0f, 1.0f);
}

bool outlived(const Phenotype& phenotype, std::uint32_t age) noexcept
{
    return static_cast<float>(age)
           >= trait_of(phenotype, TraitKind::Lifespan);
}

}  // namespace gamev2::genetics

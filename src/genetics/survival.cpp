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

/// 近交衰退：存活率随种群近交系数线性衰减，load 为 0 时恒为 1（默认关闭）。
///
/// 它与其余三项的差别值得记一笔：那三项都是**逐个体**的体质约束——喂多少、
/// 多热、多危险，都由这个个体自己的基因与环境决定；而近交系数是整群一个数
/// （见 SurvivalInput::inbreeding 的说明），同一季里每个个体拿到的都一样。
/// 因此这一项表达的是「群体的近交程度整体压低存活」，不是「近亲交配的子代
/// 更弱」——后者要谱系才写得出来。
///
/// 线性而非其他形式，是为了让 load 的量纲清楚：它就是 F 每上升 1 时存活率
/// 下降的比例。load ≥ 1 时 F 达到 1 会把存活率压到 0。
float inbreeding_survival(const SurvivalInput& in,
                          const GeneticsConfig& config) noexcept
{
    if (config.inbreeding_load <= 0.0f)
    {
        return 1.0f;
    }
    // 输入先夹到 [0, 1]。Σw² 天然落在 [1/N, 1] 上，但调用方给别的来源
    // （含 genetics::summarize 那一路的 f_coefficient，它在有限样本下系统性
    // 偏负，n = 1 时恰好是 −1）时可能越界。不夹的话负值会被当成「比零更
    // 健康」：最后一个幸存者拿到 `1 − load × (−1)`，被 clamp 成 1，也就是
    // 打不死——而那正是近交衰退最该管住的终局。
    // 负值的正确读法是「测不出近交」，因此不施加惩罚。
    const float f = std::clamp(in.inbreeding, 0.0f, 1.0f);
    return std::clamp(1.0f - config.inbreeding_load * f, 0.0f, 1.0f);
}

}  // namespace

float survival_rate(const SurvivalInput& input, const GeneticsConfig& config)
{
    return energy_survival(input) * thermal_survival(input, config)
           * escape_survival(input, config)
           * inbreeding_survival(input, config);
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

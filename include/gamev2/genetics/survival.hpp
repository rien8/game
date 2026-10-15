#pragma once

#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <cstdint>

namespace gamev2::genetics {

struct SurvivalInput
{
    const Phenotype& phenotype;
    const EnvironmentSnapshot& environment;
    /// 实际摄入的能量，取值 0..1 的资源满足度。行为 spec 接入前传理论值 1。
    float feeding = 1.0f;
    /// 捕食威胁等级，0..1。无天敌时传 0。
    float threat_level = 0.0f;
};

/// 各项生理约束存活率之积。
///
/// 这里刻意不是适应度函数：适应力全部来自基因与环境的交互，没有开发者
/// 私下加的权重。每项约束都可以单独展示、单独调平衡、单独向玩家解释。
///
/// 寿命不在这个乘积里——它是硬判定，见 outlived。
[[nodiscard]] float survival_rate(const SurvivalInput& input,
                                  const GeneticsConfig& config);

/// 相对繁殖贡献，用于亲代抽样权重。max_fecundity 是当前季种群的最大
/// Fecundity，除以它使本季最优者恰为 1。
///
/// max_fecundity <= 0 时没有可用的归一化基准，直接原样返回 survival。
[[nodiscard]] float fitness(float survival, const Phenotype& phenotype,
                            float max_fecundity) noexcept;

/// 寿命是硬判定而非概率约束，因此不参与 survival_rate 的乘积。
[[nodiscard]] bool outlived(const Phenotype& phenotype,
                            std::uint32_t age) noexcept;

}  // namespace gamev2::genetics

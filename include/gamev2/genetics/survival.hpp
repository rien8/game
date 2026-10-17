/// @file survival.hpp
/// @brief 存活与适应力的纯函数：survival_rate 给出各项生理约束存活率之积，
///        fitness 换算成亲代抽样的相对权重，outlived 单独处理寿命这道硬判定。
///
/// 刻意不做成带开发者权重的单一适应度函数：适应力全部来自基因与环境的交互，
/// 每项约束都要能单独展示、单独调平衡、单独向玩家解释。寿命是确定死亡而非
/// 概率约束，因此不参与乘积。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <cstdint>

namespace gamev2::genetics {

/// 单个个体的存活判定输入。phenotype 与 environment 之外的 feeding 与
/// threat_level 来自行为层，尚未接入时传理论值。
struct SurvivalInput
{
    const Phenotype& phenotype;
    const environment::EnvironmentSnapshot& environment;
    /// 实际摄入的能量，取值 0..1 的资源满足度。行为 spec 接入前传理论值 1。
    float feeding = 1.0f;
    /// 捕食威胁等级，0..1。无天敌时传 0。
    float threat_level = 0.0f;
    /// 种群当前携带的近交系数，0..1。
    ///
    /// **它不是 genetics::summarize 的 f_coefficient。** 那一路读数走
    /// `1 − H_obs/H_exp`，在有生态结构的种群上量的是 Hardy–Weinberg 偏离，
    /// 而这张地图上 4096 个格子气候不同、选择方向不同，全群合并根本不在
    /// HWE 上：实测全群读数约 +0.07，逐格读数约 −0.21，两者都不是近交。
    /// 本项由 genetics::settle 从亲代池直接算出（见
    /// SeasonOutput::offspring_inbreeding），只看池子有多小、适应力有多集中。
    ///
    /// 它仍然是**整群一个数**，不是逐个体的——理想种群模型下同池的每个子代
    /// 取到同一个近交系数。
    ///
    /// `config.inbreeding_load` 为 0（默认）时这一项恒为 1.0，等于不接入。
    float inbreeding = 0.0f;
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

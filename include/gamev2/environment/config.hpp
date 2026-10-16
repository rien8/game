/// @file config.hpp
/// @brief 环境模块的调平衡旋钮，随配置加载一次。
///
/// 不做成编译期常量，与 GeneticsConfig 同一个理由：这些恰恰是需要反复调整的
/// 参数，固化进二进制会让每次调参都变成一次重编。
#pragma once

#include <cstdint>

namespace gamev2::environment {

struct EnvironmentConfig
{
    /// \pre steps_per_season >= 1：run_season 用它做归一化除数，0 会让
    ///      feeding 变成 NaN。本模块没有配置期校验机制，因此这里只写明，
    ///      不在运行时拦。
    std::uint32_t steps_per_season = 8;
    /// fertility = 1 时一格支持几个体，即**出生硬上限**：任何一季的出生数加上
    /// 在场数都不得超过 Σcapacity = 4096 × capacity_base × mean(base_fertility)，
    /// 而 mean(base_fertility) = (0.25 + 1) / 2 = 0.625 —— 不是 1。
    ///
    /// 它**不是**种群平衡点。满占用下每步需求是 demand_rate × d × n = 0.35c，
    /// 而再生上限只有 regen_rate × c = 0.25c（d = 1），满占用本来就不可持续；
    /// 实测 N = 9984 时 feeding 已是 mean 0.796 / min 0.321 且仍在下降。
    /// 未饱和分支的均衡解约为 n ≈ c / (1.75 d) ≈ 0.57c，远在上限之下。
    ///
    /// 真要把稳定种群顶到 1 万，要调的是 regen_rate / demand_rate 而不是
    /// capacity_base；确切均衡点还需要遗传层的存活率才解得出来，本内核给不出。
    float capacity_base = 3.9f;
    /// 每步向 capacity 回归的比例，(0, 1]。
    float regen_rate = 0.25f;
    /// 单体单步的绝对摄取上限，与 share 同量纲（都是「个体」）：want 取它乘
    /// 表型需求，人均可得不足 want 时才按 share 截断。它是绝对量而非存量比例，
    /// 否则会被 got = min(want, share) 约掉，feeding 对存量退化为常数。
    float demand_rate = 0.35f;
    /// 每个体本季最多产几个子代。
    /// \pre offspring_scale >= 0：build_sites 把它折成
    ///      `static_cast<std::uint32_t>(lround(here * offspring_scale))`，
    ///      负值转无符号是 UB（不是饱和转换）。本模块没有配置期校验机制，
    ///      因此这里只写明，不在运行时拦。
    float offspring_scale = 2.0f;
    /// 局部交配半径，单位为格。0 表示只有本格内个体互相交配。
    float mating_radius_cells = 1.0f;
    float temperature_amplitude = 6.0f;
    float temperature_period = 8.0f;  ///< 季节数 / 周期
    float light_amplitude = 0.15f;
};

}  // namespace gamev2::environment

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
    /// fertility = 1 时一格支持几个体。种群平衡点在
    /// Σcapacity = 4096 × capacity_base × mean(base_fertility)，而
    /// mean(base_fertility) = (0.25 + 1) / 2 = 0.625 —— 不是 1。
    /// 默认 3.9 → ≈ 9984，落在「1 万个体」的规模定位上。是配平的主旋钮。
    float capacity_base = 3.9f;
    /// 每步向 capacity 回归的比例，(0, 1]。
    float regen_rate = 0.25f;
    /// 单体单步最多吃掉本地存量的比例。
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

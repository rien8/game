/// @file environment.hpp
/// @brief 环境快照：在个体建立时一次性采样的三项环境量，作为表型层 g(E) 的
///        唯一输入。
///
/// 快照在个体生命周期内锁定而非每帧重采，个体因此具有确定的表型——既可测，
/// 也可存档可复现。三个字段各自带默认值，环境未提供时调用方落到默认值即
/// 得到「无环境影响」的基准情形。
#pragma once

namespace gamev2::genetics {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
/// 三项环境量的取值范围约定：temperature 是相对该物种适宜温度的偏移，
/// fertility 与 light 是 0..1 的充足度，默认值即「无环境影响」。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

}  // namespace gamev2::genetics

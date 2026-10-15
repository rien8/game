#pragma once

namespace gamev2::genetics {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

}  // namespace gamev2::genetics

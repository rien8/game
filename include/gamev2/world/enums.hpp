/// @file enums.hpp
/// @brief 世界生成用到的全部枚举与刻度常量。
#pragma once

#include <cstdint>
#include <cstddef>

namespace gamev2::world {

/// 高程档。DF 先判高程带再判气候，因此群系判定第一步就是它。
enum class ElevClass : std::uint8_t
{
    ocean,
    lowland,
    hills,
    mountain,
};

/// 群系。取值集合严格对应 DF 群系文档列出的那些，不做增补。
enum class Biome : std::uint8_t
{
    ocean,
    lake,
    mountain,
    marshland,
    swamp,
    taiga,
    temperate_coniferous,
    temperate_broadleaf,
    tropical_coniferous,
    tropical_dry_broadleaf,
    tropical_moist_broadleaf,
    grassland,
    meadowland,
    savanna,
    shrubland,
    pine_barrens,
    sand_desert,
    rocky_wasteland,
    badlands,
    tundra,
    glacier,
};

inline constexpr std::size_t kBiomeCount = 21;

/// 岩性。sedimentary_* 是表层沉积，igneous_* 是深层与侵入体。
enum class Stone : std::uint8_t
{
    sandstone,
    shale,
    limestone,
    granite,
    basalt,
    slate,
    gravel,
    marble,
    clay,
    ice,
    igneous,
    igneous_intrusive,
};

/// 矿脉种类。none 表示该格没有矿脉。
enum class Deposit : std::uint8_t
{
    none,
    copper,
    iron,
    gold,
    silver,
    tin,
    lead,
    zinc,
    coal,
    diamond,
    sapphire,
    emerald,
    adamantine,
};

/// downstream 的哨兵：该格是海洋，或落在洼地底部。
inline constexpr std::uint32_t kNoDownstream = 0xFFFFFFFFu;

/// temperature byte 线性映射到的摄氏度区间，取 -40°C..+50°C。
inline constexpr int kMinCelsius = -40;
inline constexpr int kCelsiusSpan = 90;

[[nodiscard]] constexpr int celsius_of(std::uint8_t temperature) noexcept
{
    return kMinCelsius + temperature * kCelsiusSpan / 255;
}

[[nodiscard]] constexpr std::uint8_t temperature_of(int celsius) noexcept
{
    return static_cast<std::uint8_t>((celsius - kMinCelsius) * 255
                                     / kCelsiusSpan);
}

/// DF 原文的冻结阈值：低于它则基础群系转为苔原或冰川。
inline constexpr int kFreezingCelsius = -5;

/// 热带覆写阈值。
inline constexpr int kTropicalCelsius = 30;

/// DF 原文：排水量低于该值的群系冻结为苔原，高于则为冰川。
inline constexpr std::uint8_t kGlacierDrainage = 75;

}  // namespace gamev2::world

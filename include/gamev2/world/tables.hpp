/// @file tables.hpp
/// @brief 群系查表与矿脉定义表。
///
/// 放头文件并用 inline constexpr，是为了让测试能引用同一份数据核对，
/// 而不是复制一份会与实现漂移的副本。
#pragma once

#include "gamev2/world/enums.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gamev2::world {

/// 降水分档，0 最干到 7 最湿。
[[nodiscard]] constexpr std::size_t rain_band(std::uint8_t rain) noexcept
{
    return std::min<std::size_t>(7, rain / 32);
}

/// 排水分档，0 排水最差到 7 排水最好。
[[nodiscard]] constexpr std::size_t drainage_band(std::uint8_t d) noexcept
{
    return std::min<std::size_t>(7, d / 13);
}

/// DF 的判定顺序：先看高程带，再谈气候。上四分之一为山岳，DF 只有山岳
/// 一个高程群系，因此不再细分峰顶。
[[nodiscard]] constexpr ElevClass elev_class_of(std::uint8_t elevation,
                                                std::uint8_t sea_level) noexcept
{
    if (elevation < sea_level)
    {
        return ElevClass::ocean;
    }
    const int span = 100 - static_cast<int>(sea_level);
    const int above = static_cast<int>(elevation) - sea_level;
    if (above * 4 >= span * 3)
    {
        return ElevClass::mountain;
    }
    if (above * 2 >= span)
    {
        return ElevClass::hills;
    }
    return ElevClass::lowland;
}

/// 基础群系表。行是降水分档，列是排水分档，取值全部来自 DF 群系文档。
/// 湿地在低排水列、森林在高排水列，是这张表最容易核对的两条。
inline constexpr std::array<std::array<Biome, 8>, 8> kBaseBiome{{
    {  // rain 0，最干
        Biome::rocky_wasteland, Biome::sand_desert, Biome::sand_desert,
        Biome::sand_desert, Biome::sand_desert, Biome::badlands,
        Biome::badlands, Biome::badlands },
    {  // rain 1
        Biome::rocky_wasteland, Biome::sand_desert, Biome::sand_desert,
        Biome::sand_desert, Biome::badlands, Biome::badlands,
        Biome::badlands, Biome::shrubland },
    {  // rain 2
        Biome::shrubland, Biome::savanna, Biome::savanna,
        Biome::savanna, Biome::savanna, Biome::savanna,
        Biome::shrubland, Biome::shrubland },
    {  // rain 3
        Biome::shrubland, Biome::grassland, Biome::grassland,
        Biome::grassland, Biome::grassland, Biome::shrubland,
        Biome::shrubland, Biome::shrubland },
    {  // rain 4
        Biome::marshland, Biome::grassland, Biome::grassland,
        Biome::grassland, Biome::meadowland, Biome::meadowland,
        Biome::pine_barrens, Biome::pine_barrens },
    {  // rain 5
        Biome::swamp, Biome::swamp, Biome::temperate_broadleaf,
        Biome::grassland, Biome::meadowland, Biome::pine_barrens,
        Biome::pine_barrens, Biome::pine_barrens },
    {  // rain 6
        Biome::swamp, Biome::swamp, Biome::swamp, Biome::temperate_broadleaf,
        Biome::temperate_broadleaf, Biome::temperate_coniferous,
        Biome::temperate_coniferous, Biome::temperate_coniferous },
    {  // rain 7，最湿
        Biome::swamp, Biome::swamp, Biome::swamp, Biome::swamp,
        Biome::temperate_broadleaf, Biome::temperate_coniferous,
        Biome::temperate_coniferous, Biome::temperate_broadleaf },
}};

/// DF 的三步判定：高程先定海洋与山岳，基础群系由排水量与降水量决定，
/// 温度最后覆写成苔原、冰川或热带变体。
[[nodiscard]] constexpr Biome select_biome(ElevClass cls,
                                           std::uint8_t drainage,
                                           std::uint8_t rain,
                                           std::uint8_t temp) noexcept
{
    if (cls == ElevClass::ocean)
    {
        return Biome::ocean;
    }
    if (cls == ElevClass::mountain)
    {
        return Biome::mountain;
    }

    if (celsius_of(temp) <= kFreezingCelsius)
    {
        return drainage < kGlacierDrainage ? Biome::tundra : Biome::glacier;
    }

    const Biome base = kBaseBiome[rain_band(rain)][drainage_band(drainage)];

    if (celsius_of(temp) >= kTropicalCelsius)
    {
        switch (base)
        {
        case Biome::temperate_coniferous:
        case Biome::pine_barrens:
            return Biome::tropical_coniferous;
        case Biome::temperate_broadleaf:
            return Biome::tropical_moist_broadleaf;
        case Biome::savanna:
        case Biome::shrubland:
            return Biome::tropical_dry_broadleaf;
        default:
            break;
        }
    }

    return base;
}

/// 矿脉定义表。表本身与群系表放在一起，是为了让测试能引用同一份数据核对，
/// 而不是复制一份会与实现漂移的副本。
struct DepositSpec
{
    Deposit kind;
    /// 每张地图的撒点尝试次数，不是概率。次数写死才能让不同矿种的
    /// 相对密度可预期。
    std::uint16_t vein_count;
    /// 只到这个 z 层为止，越深的矿越稀有。
    std::uint8_t max_z;
    /// 是否允许大矿脉。DF 对部分矿种禁止大脉，本表照此收窄。
    bool allow_large;
    std::span<const Stone> hosts;
};

inline constexpr std::array kIgneousHosts{
    Stone::granite, Stone::basalt, Stone::slate,
    Stone::igneous, Stone::igneous_intrusive};

inline constexpr std::array kSedimentaryHosts{
    Stone::limestone, Stone::sandstone, Stone::shale, Stone::marble};

inline constexpr std::array kCoalHosts{
    Stone::shale, Stone::sandstone, Stone::gravel};

inline constexpr std::array kGemHosts{
    Stone::marble, Stone::granite, Stone::igneous};

inline constexpr std::array<DepositSpec, 12> kDepositSpecs{{
    {Deposit::copper, 90, 5, true, kIgneousHosts},
    {Deposit::iron, 140, 5, true, kSedimentaryHosts},
    {Deposit::gold, 30, 5, false, kIgneousHosts},
    {Deposit::silver, 45, 5, false, kIgneousHosts},
    {Deposit::tin, 50, 4, false, kSedimentaryHosts},
    {Deposit::lead, 60, 4, false, kSedimentaryHosts},
    {Deposit::zinc, 55, 4, false, kSedimentaryHosts},
    {Deposit::coal, 180, 5, true, kCoalHosts},
    {Deposit::diamond, 18, 5, false, kGemHosts},
    {Deposit::sapphire, 14, 5, false, kGemHosts},
    {Deposit::emerald, 14, 5, false, kGemHosts},
    {Deposit::adamantine, 6, 5, true, kGemHosts},
}};

}  // namespace gamev2::world

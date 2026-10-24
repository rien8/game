/// @file enums.hpp
/// @brief 世界生成用到的全部枚举与刻度常量。
#pragma once

#include <array>
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

inline constexpr std::size_t kBiomeCount = 20;

/// 岩性。表层是沉积（sandstone / shale / limestone / gravel / clay / marble），
/// 深层与侵入体是火成（granite / basalt / slate / igneous / igneous_intrusive）。
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

/// 随机流编号。设计文档 4.1：编号全局唯一，同一个编号只能被一条随机流占用，
/// 否则两个阶段从同一条流上取数，派生 seed 相同。
///
/// 名字是**流名**不是阶段号：region / offset / intrusion 三条流同属阶段 7，
/// cavern 是阶段 8 却用 3 号流。
///
/// 枚举的取值不保证互异（`a = 3, b = 3` 合法），所以唯一性由紧随其后的
/// static_assert 钉住：那份清单就是流号登记表，新增一条流必须同改。
enum class Stream : std::uint32_t
{
    elevation = 0,
    rain = 1,
    /// 温度阶段不取随机数，编号先占住。
    temperature = 2,
    cavern = 3,
    /// 水文与群系目前不取随机数，编号先占住。
    hydrology = 4,
    biome = 5,
    region = 6,
    offset = 7,
    intrusion = 8,
    deposit = 9,
    /// 灵潮状态阶段不取随机数，编号先占住。
    tide = 10,
    ley = 11,
    vein = 12,
    domain = 13,
    scar = 14,
    /// 洞天阶段不取随机数，编号先占住。
    heaven = 15,
    /// 地貌判定阶段不取随机数，编号先占住。
    land = 16,
    /// 时序层的越采年抽样。
    ledger = 17,
    /// 派系演化层的越采发现抽样。
    faction = 18,
    /// 时序层的渡劫年份与成败抽样。
    ///
    /// 与 faction 同源竞争 18 号：派系层先发布，本层让号改占 19。改号会
    /// 换掉渡劫的抽样结果，所以这条一旦定下就不要再动。
    tribulation = 19,
};

/// 全部已占用的流号。清单与枚举必须同步维护。
inline constexpr std::array<std::uint32_t, 20> kStreamIds{
    static_cast<std::uint32_t>(Stream::elevation),
    static_cast<std::uint32_t>(Stream::rain),
    static_cast<std::uint32_t>(Stream::temperature),
    static_cast<std::uint32_t>(Stream::cavern),
    static_cast<std::uint32_t>(Stream::hydrology),
    static_cast<std::uint32_t>(Stream::biome),
    static_cast<std::uint32_t>(Stream::region),
    static_cast<std::uint32_t>(Stream::offset),
    static_cast<std::uint32_t>(Stream::intrusion),
    static_cast<std::uint32_t>(Stream::deposit),
    static_cast<std::uint32_t>(Stream::tide),
    static_cast<std::uint32_t>(Stream::ley),
    static_cast<std::uint32_t>(Stream::vein),
    static_cast<std::uint32_t>(Stream::domain),
    static_cast<std::uint32_t>(Stream::scar),
    static_cast<std::uint32_t>(Stream::heaven),
    static_cast<std::uint32_t>(Stream::land),
    static_cast<std::uint32_t>(Stream::ledger),
    static_cast<std::uint32_t>(Stream::faction),
    static_cast<std::uint32_t>(Stream::tribulation),
};

/// 流号两两不同。重复即意味着两个阶段共用一条随机流——那不是确定性缺陷，
/// 但会让两个阶段的相关性悄悄渗进世界，规格却写着它们互不串扰。
consteval bool stream_ids_are_distinct() noexcept
{
    for (std::size_t i = 0; i < kStreamIds.size(); ++i)
    {
        for (std::size_t j = i + 1; j < kStreamIds.size(); ++j)
        {
            if (kStreamIds[i] == kStreamIds[j])
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(stream_ids_are_distinct());

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

/// 洞穴碎屑阈值：体素数低于该值的空腔连通域视为碎屑，填实。
inline constexpr std::uint32_t kMinCavernVolume = 64;

}  // namespace gamev2::world

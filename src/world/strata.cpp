#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gamev2::world {
namespace {

/// 土壤基值由群系给出，降水只做一档修正。调参项见设计文档 11.2。
std::uint8_t soil_depth_of(Biome biome, std::uint8_t rain)
{
    std::uint8_t base = 1;
    switch (biome)
    {
    case Biome::swamp:
    case Biome::marshland:
    case Biome::temperate_broadleaf:
    case Biome::tropical_moist_broadleaf:
        base = 3;
        break;
    case Biome::grassland:
    case Biome::meadowland:
    case Biome::temperate_coniferous:
    case Biome::tropical_coniferous:
    case Biome::tropical_dry_broadleaf:
        base = 2;
        break;
    case Biome::ocean:
    case Biome::lake:
    case Biome::sand_desert:
    case Biome::rocky_wasteland:
    case Biome::badlands:
    case Biome::glacier:
    case Biome::mountain:
        base = 0;
        break;
    default:
        break;
    }

    // 裸地无土，冰盖无土（设计文档 11.2）：降水再多也不会在岩床或冰体上
    // 积起土层，所以修正项对 base == 0 一律不适用。
    if (base == 0)
    {
        return 0;
    }

    const int adjusted = static_cast<int>(base) + (rain > 200 ? 1 : 0)
                          - (rain < 60 ? 1 : 0);
    return static_cast<std::uint8_t>(std::clamp(adjusted, 0, 3));
}

/// 基岩区域的特征尺度：24 格一个区域，大陆上才看得出岩性分块。
constexpr double kRegionFreq = 24.0;
/// 层界起伏的特征尺度。
constexpr double kOffsetFreq = 12.0;
constexpr int kIntrusionCount = 40;

using RegionRock = std::array<Stone, 6>;

/// 大陆由少数几种岩性区域拼接而成，这是 DF 层序的骨架。
constexpr RegionRock kRegionRock{
    Stone::limestone, Stone::sandstone, Stone::shale,
    Stone::granite,   Stone::basalt,    Stone::slate,
};

/// 表层一定是沉积物：任何有土壤的地方下面都是碎屑层。
constexpr RegionRock kSedimentRock{
    Stone::gravel, Stone::sandstone, Stone::clay,
    Stone::sandstone, Stone::gravel, Stone::clay,
};

enum class Strata : std::uint8_t
{
    sediment,
    regional,
    volcanic,
};

/// 表层沉积占 z=0，往下依次是区域岩性与深层火成。
Strata strata_of(std::uint32_t z, std::uint32_t z_levels)
{
    if (z == 0)
    {
        return Strata::sediment;
    }
    return (static_cast<double>(z) / static_cast<double>(z_levels) < 0.5)
               ? Strata::regional
               : Strata::volcanic;
}

Stone stone_at(std::size_t region, Strata s)
{
    switch (s)
    {
    case Strata::sediment:
        return kSedimentRock[region];
    case Strata::regional:
        return kRegionRock[region];
    case Strata::volcanic:
        return Stone::igneous;
    }
    return Stone::granite;
}

void stamp_ellipsoid(World& world, std::uint32_t cx, std::uint32_t cy,
                     std::uint32_t cz, int radius, Stone kind)
{
    const WorldParams& p = world.params();
    const double inv = 1.0 / (static_cast<double>(radius) * radius);
    for (int dz = -radius; dz <= radius; ++dz)
    {
        const auto z = static_cast<int>(cz) + dz;
        // 侵入体不穿出地表，z=0 留给沉积层。
        if (z < 1 || z >= static_cast<int>(p.z_levels))
        {
            continue;
        }
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                if ((dx * dx + dy * dy + dz * dz) * inv > 1.0)
                {
                    continue;
                }
                const auto x = static_cast<int>(cx) + dx;
                const auto y = static_cast<int>(cy) + dy;
                if (x < 0 || y < 0 || x >= static_cast<int>(p.width)
                    || y >= static_cast<int>(p.height))
                {
                    continue;
                }
                const std::uint32_t i = world.elevation.index_of(
                    static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(y));
                world.stone[static_cast<std::size_t>(z)].at(i) = kind;
            }
        }
    }
}

void stamp_intrusions(World& world)
{
    const WorldParams& p = world.params();
    if (p.z_levels < 2)
    {
        return;
    }
    Pcg32 rng(stage_seed(p.seed, static_cast<std::uint32_t>(Stream::intrusion)));
    for (int k = 0; k < kIntrusionCount; ++k)
    {
        const auto cz = 1 + rng.below(p.z_levels - 1);
        const auto radius = static_cast<int>(3 + rng.below(9));
        const Stone kind = rng.below(2) == 0 ? Stone::igneous
                                             : Stone::igneous_intrusive;
        stamp_ellipsoid(world, rng.below(p.width), rng.below(p.height), cz,
                        radius, kind);
    }
}

/// 无雨时的含水层顶面深度。雨越多水越高，顶面就越浅。
constexpr int kAquiferBaseDepth = 70;
constexpr int kAquiferRainScale = 70;

void assign_aquifer_top(World& world)
{
    const auto depth = static_cast<int>(world.depth());
    for (std::uint32_t i = 0; i < world.aquifer_top.size(); ++i)
    {
        const Biome b = world.biome.at(i);
        // 冰川下是冰，沙漠无地下水，山岳与水体的岩体也不蓄水。
        const bool dry = b == Biome::ocean || b == Biome::lake
                         || b == Biome::sand_desert
                         || b == Biome::rocky_wasteland
                         || b == Biome::badlands || b == Biome::glacier
                         || b == Biome::mountain;
        if (dry)
        {
            world.aquifer_top.at(i) = 0;
            continue;
        }
        const int base =
            kAquiferBaseDepth
            - static_cast<int>(world.rainfall.at(i)) * kAquiferRainScale / 255;
        // 下界 1 保证含水层永远到不了地表。上界取 max(1, depth - 1)：
        // depth == 1 时 depth - 1 == 0，夹取的 lo > hi 是未定义行为。
        world.aquifer_top.at(i) = static_cast<std::uint8_t>(
            std::clamp(base, 1, std::max(1, depth - 1)));
    }
}

}  // namespace

void make_soils(World& world)
{
    for (std::uint32_t i = 0; i < world.soil_depth.size(); ++i)
    {
        world.soil_depth.at(i) =
            soil_depth_of(world.biome.at(i), world.rainfall.at(i));
    }
}

void make_strata(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t region_seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::region));
    const std::uint64_t offset_seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::offset));
    const auto levels = static_cast<std::int64_t>(p.z_levels);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const bool frozen = world.biome.at(i) == Biome::glacier;

            const auto raw_region = static_cast<std::size_t>(
                value_noise(region_seed, x / kRegionFreq, y / kRegionFreq)
                * 6.0);
            const std::size_t region = std::min(raw_region, std::size_t{5});

            // 层界随低频噪声起伏，否则层与层之间是死板的水平面。
            const auto shift = static_cast<std::int64_t>(std::lround(
                (value_noise(offset_seed, x / kOffsetFreq, y / kOffsetFreq)
                 - 0.5)
                * 2.0));

            for (std::uint32_t z = 0; z < p.z_levels; ++z)
            {
                // 表层恒为沉积物：shift 用来让层界起伏，不得把地表顶出
                // 沉积层，因此 z=0 的 moved 固定为 0。
                const std::int64_t shifted = z == 0
                                                ? 0
                                                : static_cast<std::int64_t>(z)
                                                      + shift;
                const auto moved = static_cast<std::uint32_t>(
                    std::clamp(shifted, std::int64_t{0}, levels - 1));
                world.stone[z].at(i) =
                    frozen ? Stone::ice
                           : stone_at(region,
                                      strata_of(moved, p.z_levels));
            }
        }
    }

    stamp_intrusions(world);
    assign_aquifer_top(world);
}

}  // namespace gamev2::world

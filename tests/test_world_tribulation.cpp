#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::TribulationSite;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::make_tribulation_sites;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 18 的前置状态。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    gamev2::world::make_historical_scars(w);
    gamev2::world::make_land_types(w);
    make_tribulation_sites(w);
    return w;
}

/// 灵潮期末：福地最多，渡劫场候选最足。
constexpr std::uint32_t kTideEndYear = 9300;
/// 谷底期：当代。没有福地，渡劫场必然为空。
constexpr std::uint32_t kNadirYear = 8914;

/// 两格的欧氏距离。口径与实现一致：阶段 17 的洞天、阶段 15 的疤痕都用它。
double cell_distance(const World& world, std::uint32_t a, std::uint32_t b)
{
    const auto w = world.params().width;
    const double dx = static_cast<double>(a % w) - static_cast<double>(b % w);
    const double dy = static_cast<double>(a / w) - static_cast<double>(b / w);
    return std::sqrt(dx * dx + dy * dy);
}

TEST(tribulation_sites_only_sit_on_blessed_ground)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK(w.land_type.at(site.cell) == LandType::blessed);
    }
}

TEST(tribulation_sites_avoid_old_scars)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK(w.scar_kind.at(site.cell) == Scar::none);
    }
}

TEST(the_jiyun_reaches_one_hundred_li)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK_EQ(site.radius, gamev2::world::kTribulationRadius);
    }
}

TEST(the_grid_and_the_list_agree)
{
    const World w = build_world(3, kTideEndYear);
    std::size_t marked = 0;
    for (std::uint32_t i = 0; i < w.tribulation_site.size(); ++i)
    {
        if (w.tribulation_site.at(i))
        {
            ++marked;
        }
    }
    CHECK_EQ(marked, w.tribulation_sites.size());
}

TEST(tribulation_sites_keep_their_distance)
{
    const World w = build_world(3, kTideEndYear);
    for (std::size_t i = 0; i < w.tribulation_sites.size(); ++i)
    {
        for (std::size_t j = i + 1; j < w.tribulation_sites.size(); ++j)
        {
            const double d = cell_distance(w, w.tribulation_sites[i].cell,
                                           w.tribulation_sites[j].cell);
            CHECK(d >= static_cast<double>(
                           gamev2::world::kTribulationSpacing));
        }
    }
}

TEST(the_tide_end_offers_tribulation_sites)
{
    // 灵潮期末福地成片，渡劫场应当真的排得出来——否则阶段 18 是个空壳。
    const World w = build_world(3, kTideEndYear);
    CHECK(!w.tribulation_sites.empty());
}

TEST(the_nadir_has_no_tribulation_site)
{
    // 谷底期没有福地，设定 2.6 说福地随灵潮出现而扩张。
    const World w = build_world(3, kNadirYear);
    CHECK(w.tribulation_sites.empty());
    CHECK_EQ(w.tribulation_site.size(), w.params().width * w.params().height);
}

TEST(the_sites_are_deterministic)
{
    const World a = build_world(7, kTideEndYear);
    const World b = build_world(7, kTideEndYear);
    CHECK_EQ(a.tribulation_sites.size(), b.tribulation_sites.size());
    for (std::size_t i = 0; i < a.tribulation_sites.size(); ++i)
    {
        CHECK_EQ(a.tribulation_sites[i].cell, b.tribulation_sites[i].cell);
        CHECK_EQ(a.tribulation_sites[i].radius, b.tribulation_sites[i].radius);
        CHECK_EQ(a.tribulation_sites[i].vein, b.tribulation_sites[i].vein);
    }
}

TEST(the_grid_has_the_world_dimensions_from_the_start)
{
    // 每张地表网格都在 World 的构造函数里分配，尺寸与图幅一致。
    //
    // 这条断言盯住「新阶段加了网格却忘了加进构造函数」这类漏：漏掉的
    // 网格是空网格，size() 为 0，而阶段 18 自己会重新分配它，漏掉的地方
    // 于是被阶段掩盖过去——只有在本阶段跑之前看，尺寸才是错的。
    const World w(params(1, kTideEndYear));
    CHECK_EQ(w.tribulation_site.width(), w.params().width);
    CHECK_EQ(w.tribulation_site.height(), w.params().height);
    CHECK_EQ(w.tribulation_site.size(),
             w.params().width * w.params().height);
}

}  // namespace

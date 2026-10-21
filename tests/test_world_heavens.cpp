#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::CaveHeaven;
using gamev2::world::LandType;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::make_cave_heavens;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 17 的前置状态。
///
/// 世界级用例默认取**灵潮期末**（灵历 9300，潮位 99）。当代是谷底期，
/// 那里没有福地，洞天数恒为 0，用例会整条空跑。设定 2.6 说福地与洞天
/// 是随灵潮出现并扩张的，所以洞天必须在灵潮期取样。
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
    make_cave_heavens(w);
    return w;
}

/// 灵潮期末：谷底期没有福地，洞天随之不存在。
constexpr std::uint32_t kTideEndYear = 9300;
/// 谷底期：当代。灵气半死，没有福地。
constexpr std::uint32_t kNadirYear = 8914;

TEST(caves_only_sit_on_blessed_ground)
{
    const World w = build_world(3, kTideEndYear);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(w.land_type.at(cave.cell) == LandType::blessed);
    }
}

TEST(every_cave_costs_stones_every_year)
{
    const World w = build_world(3, kTideEndYear);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(cave.capacity > 0u);
        CHECK(cave.upkeep > 0u);
        // upkeep 就是设定红线 9 的兑现：开一座洞天要多花灵石。
        CHECK_EQ(cave.upkeep, cave.capacity * 2u);
    }
}

TEST(the_cave_grid_matches_the_cave_list)
{
    const World w = build_world(3, kTideEndYear);
    std::uint32_t marked = 0;
    for (std::uint32_t i = 0; i < w.cave_heaven.size(); ++i)
    {
        if (w.cave_heaven.at(i))
        {
            ++marked;
        }
    }
    CHECK_EQ(marked, w.cave_heavens.size());
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(w.cave_heaven.at(cave.cell));
    }
}

TEST(caves_keep_their_distance)
{
    const World w = build_world(3, kTideEndYear);
    const std::uint32_t width = w.params().width;
    bool spaced = true;
    for (std::size_t a = 0; a < w.cave_heavens.size(); ++a)
    {
        for (std::size_t b = a + 1; b < w.cave_heavens.size(); ++b)
        {
            const std::int64_t dx =
                static_cast<std::int64_t>(w.cave_heavens[a].cell % width)
                - static_cast<std::int64_t>(w.cave_heavens[b].cell % width);
            const std::int64_t dy =
                static_cast<std::int64_t>(w.cave_heavens[a].cell / width)
                - static_cast<std::int64_t>(w.cave_heavens[b].cell / width);
            spaced = spaced && (dx * dx + dy * dy >= 64);
        }
    }
    CHECK(spaced);
}

TEST(the_nadir_year_has_no_cave_heavens)
{
    // 谷底期（灵历 8914，潮位 33）没有福地，因此没有洞天。
    //
    // 这不是缺陷，是模型的表达：潮位是纯乘子，谷底期的字节是灵潮末的
    // 三分之一；而字节上限 255 决定了「谷底期有福地」与「灵潮末福地
    // 占比 <= 6%」不可兼得。设定 2.6 说灵潮期「洞天自生、福地扩张」，
    // 谷底期「灵脉半死」，所以当代没有洞天正是设定要的样子。
    //
    // 写这一条是为了把「0 座」钉成规格，而不是让它成为没人注意的空跑。
    const World w = build_world(3, kNadirYear);
    CHECK_EQ(w.spirit_tide.level, 33u);
    CHECK_EQ(w.cave_heavens.size(), 0u);
}

TEST(cave_count_follows_the_blessed_area)
{
    // 合成：1600 格福地，每格浓度 200。
    // 洞天数 = 1600 / 400 = 4；容量 = 200 * 8 = 1600；年耗 = 1600 * 2。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    std::uint32_t blessed = 0;
    for (std::uint32_t y = 10; y < 50; ++y)
    {
        for (std::uint32_t x = 10; x < 50; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.land_type.at(i) = LandType::blessed;
            w.domain_concentration.at(i) = 200;
            ++blessed;
        }
    }
    make_cave_heavens(w);

    CHECK_EQ(blessed, 1600u);
    CHECK_EQ(w.cave_heavens.size(), 4u);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK_EQ(cave.capacity, 1600u);
        CHECK_EQ(cave.upkeep, 3200u);
    }
}

/// 洞天的间距约束必须真的生效：高浓度福地挤在一起时也要散开。
///
/// 真实世界里浓度最高的福地格天然分散在几十只灵眼周围，彼此相距很远，
/// 于是「两座洞天间距 >= 8」这条规格在真实世界上几乎从不生效——把实现里
/// 的间距检查换成 `if (false)`，全部用例照样全绿。合成用例把三个高浓度格
/// 挤在 5~9 格之内，间距约束才显出形状。
///
/// 布局：64x64，福地格是 x ∈ [0,40)、y ∈ [0,20) 共 800 格 ⇒ 洞天数
/// 800 / 400 = 2。三个高浓度格 A(10,10)=250、B(15,10)=240、C(19,10)=230，
/// 其余 797 格浓度 200。
///
/// 按浓度降序贪心（设计文档 §3.7）：A 先落；B 距 A 5 格 < 8 ⇒ 跳过；
/// C 距 A 9 格 >= 8 ⇒ 落。于是洞天恰好是 A 与 C 两座。
TEST(caves_respect_the_spacing_when_the_best_ground_is_adjacent)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    std::uint32_t blessed = 0;
    for (std::uint32_t y = 0; y < 20; ++y)
    {
        for (std::uint32_t x = 0; x < 40; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.land_type.at(i) = LandType::blessed;
            w.domain_concentration.at(i) = 200;
            ++blessed;
        }
    }
    const std::uint32_t cell_a = w.elevation.index_of(10, 10);
    const std::uint32_t cell_b = w.elevation.index_of(15, 10);
    const std::uint32_t cell_c = w.elevation.index_of(19, 10);
    w.domain_concentration.at(cell_a) = 250;
    w.domain_concentration.at(cell_b) = 240;
    w.domain_concentration.at(cell_c) = 230;

    make_cave_heavens(w);

    CHECK_EQ(blessed, 800u);
    CHECK_EQ(w.cave_heavens.size(), 2u);
    // 落点就是 A 与 C：B 被间距约束挡掉了。
    CHECK(w.cave_heaven.at(cell_a));
    CHECK(w.cave_heaven.at(cell_c));
    CHECK(!w.cave_heaven.at(cell_b));
    // 间距是规格的一部分：任意两座洞天的中心距 >= 8 格。
    const std::uint32_t width = w.params().width;
    for (std::size_t i = 0; i < w.cave_heavens.size(); ++i)
    {
        for (std::size_t j = i + 1; j < w.cave_heavens.size(); ++j)
        {
            const std::int64_t dx =
                static_cast<std::int64_t>(w.cave_heavens[i].cell % width)
                - static_cast<std::int64_t>(w.cave_heavens[j].cell % width);
            const std::int64_t dy =
                static_cast<std::int64_t>(w.cave_heavens[i].cell / width)
                - static_cast<std::int64_t>(w.cave_heavens[j].cell / width);
            CHECK(dx * dx + dy * dy >= 64);
        }
    }
    // 洞天开在它落下去的那一格的浓度上。
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        const std::uint32_t raw =
            static_cast<std::uint32_t>(w.domain_concentration.at(cave.cell));
        CHECK_EQ(cave.capacity, raw * 8u);
        CHECK_EQ(cave.upkeep, cave.capacity * 2u);
    }
}

TEST(cave_heavens_are_deterministic)
{
    const World a = build_world(3, kTideEndYear);
    const World b = build_world(3, kTideEndYear);
    bool same = a.cave_heavens.size() == b.cave_heavens.size();
    for (std::size_t i = 0; i < a.cave_heavens.size() && i < b.cave_heavens.size();
         ++i)
    {
        same = same && a.cave_heavens[i].cell == b.cave_heavens[i].cell;
        same = same && a.cave_heavens[i].capacity == b.cave_heavens[i].capacity;
        same = same && a.cave_heavens[i].upkeep == b.cave_heavens[i].upkeep;
    }
    for (std::uint32_t i = 0; i < a.cave_heaven.size(); ++i)
    {
        same = same && a.cave_heaven.at(i) == b.cave_heaven.at(i);
    }
    CHECK(same);
}

}  // namespace

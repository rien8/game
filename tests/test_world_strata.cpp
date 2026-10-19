#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>

namespace {

using gamev2::world::Biome;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;

/// 跑到阶段 6 为止的流水线：有些测试要自己摆布降水再跑阶段 7。
World build_before_strata(std::uint32_t seed)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.seed = seed;
    World w(p);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_soils(w);
    return w;
}

/// 跑到指定阶段为止的流水线，测试只关心自己之后的字段。
World build(std::uint32_t seed)
{
    World w = build_before_strata(seed);
    gamev2::world::make_strata(w);
    return w;
}

bool is_sediment(Stone s)
{
    return s == Stone::sandstone || s == Stone::shale
           || s == Stone::limestone || s == Stone::gravel
           || s == Stone::clay;
}

/// 无含水层的那组群系：冰川下是冰，沙漠无地下水，山岳与水体的岩体
/// 也不蓄水。与 assign_aquifer_top 里的判据一一对应。
bool is_dry_biome(Biome b)
{
    return b == Biome::ocean || b == Biome::lake || b == Biome::sand_desert
           || b == Biome::rocky_wasteland || b == Biome::badlands
           || b == Biome::glacier || b == Biome::mountain;
}

/// 沉积层专属的岩性标记。kSedimentRock 只有 gravel / sandstone / clay，
/// 而 kRegionRock 也有 limestone / sandstone / shale —— sandstone 两边
/// 都有，认不出「这一格还在沉积层还是已经进了区域岩层」，只有 gravel
/// 与 clay 无歧义。
bool is_sediment_marker(Stone s)
{
    return s == Stone::gravel || s == Stone::clay;
}

/// 区域岩层专属的岩性标记：kRegionRock 里除 sandstone 外的五种。
bool is_regional_marker(Stone s)
{
    return s == Stone::limestone || s == Stone::shale
           || s == Stone::granite || s == Stone::basalt
           || s == Stone::slate;
}

/// soil_depth_of 里基值为 3 的那组群系，wet_forests_carry_the_deepest_soil
/// 断言的就是它们。
bool is_wet_forest(Biome b)
{
    return b == Biome::swamp || b == Biome::marshland
           || b == Biome::temperate_broadleaf
           || b == Biome::tropical_moist_broadleaf;
}

/// 土壤基值为 0 的那组群系：岩床、冰体、水面，无土可积。
bool is_bare_ground(Biome b)
{
    return b == Biome::ocean || b == Biome::lake || b == Biome::sand_desert
           || b == Biome::rocky_wasteland || b == Biome::badlands
           || b == Biome::glacier || b == Biome::mountain;
}

TEST(soil_depth_stays_in_range)
{
    const World w = build(21);
    bool in_range = true;
    bool any_written = false;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        in_range = in_range && w.soil_depth.at(i) <= 3;
        any_written = any_written || w.soil_depth.at(i) > 0;
    }
    CHECK(in_range);
    // 上界单独自足不够：这个世界上 rain > 200 的 64 格全是裸地（实测），
    // 裸地被早返回挡在 0，所以没有任何一格会因为降水加项越过 3 ——
    // 摘掉 std::clamp 也造不出越界值，只查 <= 3 的断言在
    // 「make_soils 从未被调用」时同样会通过（全 0 平凡满足上界）。
    // 「至少有一格非 0」把它钉在真实写入上，与另外两个测试一起堵住这条空档。
    // 注意这不代表降水项在本世界完全无效：base 为 1 的群系里有 468 格
    // 落在 rain < 60，被减项压到 0；只是那 468 格不会越上界。
    CHECK(any_written);
}

TEST(soil_is_absent_on_bare_ground)
{
    const World w = build(21);
    bool bare = true;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        const Biome b = w.biome.at(i);
        if (is_bare_ground(b))
        {
            bare = bare && w.soil_depth.at(i) == 0;
        }
    }
    CHECK(bare);
}

TEST(wet_forests_carry_the_deepest_soil)
{
    const World w = build(21);
    std::uint32_t deepest = 0;
    bool wet_forests_are_deep = true;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        deepest = std::max(deepest,
                           static_cast<std::uint32_t>(w.soil_depth.at(i)));
        if (is_wet_forest(w.biome.at(i)))
        {
            wet_forests_are_deep =
                wet_forests_are_deep && w.soil_depth.at(i) >= 2;
        }
    }
    CHECK(deepest >= 2);
    // 上面的全图最大值只是「最深不超过这个数」的下界，它不读 biome：
    // 把整张表压成同一个基值的实现里，基值 1 会被 deepest >= 2 挡住，
    // 只有基值 2 或 3 能混过去。群系条件这一条才是本
    // 测试名字所承诺的那句话 —— 湿林必须真的拿到 2 以上。它同时钉住群系
    // 查表：若 soil_depth_of 读错了网格（例如拿 elevation 当 biome），
    // 湿林格就拿不到基值 3，这条随即失败。两条都留着：一条防「整体太浅」，
    // 一条防「湿林不特殊」。
    CHECK(wet_forests_are_deep);
}

TEST(rainfall_dryness_strips_soil_from_vegetated_ground)
{
    const World w = build(21);
    bool some_vegetated_cell_is_bare = false;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        if (is_bare_ground(w.biome.at(i)))
        {
            continue;
        }
        some_vegetated_cell_is_bare =
            some_vegetated_cell_is_bare || w.soil_depth.at(i) == 0;
    }
    // 本测试钉住的是降水的**减项**（rain < 60 扣一档）：基值为 1 的群系
    // （tundra、savanna、shrubland、pine_barrens）落在干旱区时被压到 0。
    // 本世界实测 468 格如此，且全部集中在 tundra（tundra 共 587 格）——
    // 这四类基值都是 1，是唯一会因扣项跌到 0 的一组，所以断言非裸地
    // 存在 0 深度格就是在钉这条分支：忽略降水的实现会让它们停在 1。
    //
    // 加项（rain > 200 加一档）在这里钉不住：本世界没有任何基值非 0 的格
    // 降水超过 200（全部 64 格 rain > 200 都落在裸地，被早返回挡在 0），
    // 所以没有一格会因加项高于自身基值，无从观察。
    CHECK(some_vegetated_cell_is_bare);
}

TEST(surface_layer_is_sedimentary_except_under_glaciers)
{
    const World w = build(21);
    bool ok = true;
    std::uint32_t glaciers = 0;
    for (std::uint32_t i = 0; i < w.biome.size(); ++i)
    {
        const Stone s = w.stone[0].at(i);
        if (w.biome.at(i) == Biome::glacier)
        {
            ++glaciers;
            ok = ok && s == Stone::ice;
        }
        else
        {
            ok = ok && is_sediment(s);
        }
    }
    CHECK(ok);
    CHECK(glaciers > 0);
}

TEST(deep_layers_hold_volcanic_rock)
{
    const World w = build(21);
    const auto deepest = w.params().z_levels - 1;
    bool ok = true;
    std::uint32_t checked = 0;
    for (std::uint32_t i = 0; i < w.biome.size(); ++i)
    {
        if (w.biome.at(i) == Biome::glacier)
        {
            // 冰川整列是冰，不是火成：设计允许的例外，与地表那一条同构。
            continue;
        }
        const Stone s = w.stone[deepest].at(i);
        // 逐格断言，而不是「至少有一格是火成」——侵入体的 40 个椭球
        // 在最深层也留下了火成格，「volcanic > 0」在整层变成区域岩性
        // 时照样成立，钉不住任何东西。
        ok = ok && (s == Stone::igneous || s == Stone::igneous_intrusive);
        ++checked;
    }
    // 最深层的 moved 落在 [z_levels-2, z_levels-1]；本世界 z_levels = 6，
    // 下界 4/6 = 0.67 仍 >= 0.5，所以整层按构造就是火成层。
    CHECK(ok);
    CHECK(checked > 0);
}

TEST(aquifer_top_is_zero_on_dry_biomes)
{
    const World w = build(21);
    bool ok = true;
    std::uint32_t wet = 0;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        if (is_dry_biome(w.biome.at(i)))
        {
            ok = ok && w.aquifer_top.at(i) == 0;
        }
        else
        {
            wet += w.aquifer_top.at(i) > 0 ? 1u : 0u;
        }
    }
    CHECK(ok);
    CHECK(wet > 0);
}

/// 含水层顶面夹在世界内部：不到地表，也不在世界底之下。
TEST(aquifer_top_stays_below_the_surface_and_above_the_world_floor)
{
    const World w = build(21);
    bool ok = true;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        const auto top = static_cast<int>(w.aquifer_top.at(i));
        // 第一条：顶面不跑到世界底之下。0 表示该格没有含水层。
        ok = ok && (top == 0 || top < static_cast<int>(w.depth()));
        // 第二条：顶面绝不触到地表。真正保证这一条的是
        // assign_aquifer_top 里的 clamp —— 它的下界 1 把有含水层的
        // 格子按在 [1, depth-1] 里。clamp 的下界一旦被放松到 0，
        // 这一条立刻红，第一条（`top < depth()`）不会。
        if (!is_dry_biome(w.biome.at(i)))
        {
            ok = ok && top >= 1;
        }
    }
    CHECK(ok);
}

/// 同一件事的极端版本：降水拉满时顶面也不能贴到地表。
///
/// 本世界真实的降水最高只到 195，base 最小是 17，clamp 的下界 1 永远
/// 碰不到 —— 所以上面那条断言在真实数据上是被 vacuous 地满足的。
/// 这里把湿格的降水按到 255（base 恰好 0），把下界逼到边界上。
TEST(aquifer_top_stays_below_the_surface_at_maximum_rainfall)
{
    World w = build_before_strata(21);
    for (std::uint32_t i = 0; i < w.rainfall.size(); ++i)
    {
        if (!is_dry_biome(w.biome.at(i)))
        {
            w.rainfall.at(i) = 255;
        }
    }
    gamev2::world::make_strata(w);

    bool ok = true;
    std::uint32_t wet = 0;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        if (is_dry_biome(w.biome.at(i)))
        {
            continue;
        }
        ++wet;
        ok = ok && w.aquifer_top.at(i) >= 1;
    }
    CHECK(ok);
    CHECK(wet > 0);
}

/// 深度为 1 的世界：夹取的上下界会重合。
///
/// depth == 1 时 depth - 1 == 0，std::clamp(base, 1, 0) 的 lo > hi，
/// 前置条件被违反（未定义行为）。实测那个未定义路径返回 0，把这个世界上
/// 的含水层全部抹掉。上界取 max(1, depth - 1) 之后，夹取落在合法区间里。
TEST(aquifer_top_survives_a_world_one_cell_deep)
{
    WorldParams p;
    p.width = 16;
    p.height = 16;
    p.z_levels = 1;
    p.z_cell_depth = 1;
    p.seed = 21;
    World w(p);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_soils(w);
    gamev2::world::make_strata(w);

    CHECK(w.depth() == 1);
    bool ok = true;
    std::uint32_t wet = 0;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        if (is_dry_biome(w.biome.at(i)))
        {
            ok = ok && w.aquifer_top.at(i) == 0;
            continue;
        }
        ++wet;
        ok = ok && w.aquifer_top.at(i) >= 1;
    }
    CHECK(ok);
    CHECK(wet > 0);
}

/// 层界必须起伏，否则层与层之间是死板的水平面。
///
/// 这条钉的是**沉积层 / 区域岩层**那个界面：shift == -1 的格子在 z = 1
/// 上仍然停在沉积层，shift == 0 或 +1 的已经进了区域岩层。所以 z = 1
/// 上两种标记同时存在，就说明界面在移动。
///
/// 判据必须用无歧义的岩性：沉积层专属的是 gravel / clay，区域岩层专属
/// 的是 limestone / shale / granite / basalt / slate。sandstone 两张表
/// 都有（kSedimentRock 与 kRegionRock），拿它当标记认不出这一格到底在
/// 哪一层。
TEST(layer_boundary_undulates)
{
    const World w = build(21);
    std::uint32_t land = 0;
    std::uint32_t still_sediment = 0;
    std::uint32_t already_regional = 0;
    for (std::uint32_t i = 0; i < w.biome.size(); ++i)
    {
        const Biome b = w.biome.at(i);
        if (b == Biome::ocean || b == Biome::lake)
        {
            continue;
        }
        ++land;
        const Stone s = w.stone[1].at(i);
        still_sediment += is_sediment_marker(s) ? 1u : 0u;
        already_regional += is_regional_marker(s) ? 1u : 0u;
    }
    // 一个沉积标记都没有：shift 恒为 0，界面被抹平成死板水平面。
    // 满格都是沉积标记：界面被埋到最下面，一样是层界没在动。
    CHECK(still_sediment > 0);
    CHECK(still_sediment < land);
    // 区域岩层标记一个都没有：界面被压到最上面，层序整个塌成一档。
    CHECK(already_regional > 0);
}

}  // namespace

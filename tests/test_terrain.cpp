#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/terrain.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::environment::TerrainField;

namespace {

[[nodiscard]] std::size_t count_water(const TerrainField& terrain) noexcept
{
    std::size_t water = 0;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        if (!terrain.passable(CellIndex{static_cast<std::uint16_t>(i)}))
        {
            ++water;
        }
    }
    return water;
}

/// 4 邻域连通分量的格数，从大到小排序。
///
/// 必须真的遍历全图取各分量而不是从任意一格泛洪：只从第一块陆地泛洪会让
/// 「最大陆块没占满陆地」退化成「某块陆地没占满陆地」，一张「一整块大陆加
/// 一颗孤岛」的图也能让断言变绿。
///
/// 4 邻域而不是 8：地形是障碍模型，个体也只沿 4 邻域滑行，按 8 邻域会把
/// 一道对角水缝当成通路。
[[nodiscard]] std::vector<std::size_t> landmasses(const TerrainField& terrain)
{
    std::vector<bool> seen(kCellCount, false);
    std::vector<std::size_t> sizes;

    for (std::size_t seed = 0; seed < kCellCount; ++seed)
    {
        if (seen[seed]
            || !terrain.passable(CellIndex{static_cast<std::uint16_t>(seed)}))
        {
            continue;
        }
        std::size_t size = 0;
        std::vector<std::size_t> stack{seed};
        while (!stack.empty())
        {
            const auto i = stack.back();
            stack.pop_back();
            if (seen[i]
                || !terrain.passable(CellIndex{static_cast<std::uint16_t>(i)}))
            {
                continue;
            }
            seen[i] = true;
            ++size;

            const auto x = i % kGridSide;
            const auto y = i / kGridSide;
            if (x > 0)
            {
                stack.push_back(i - 1);
            }
            if (x + 1 < kGridSide)
            {
                stack.push_back(i + 1);
            }
            if (y > 0)
            {
                stack.push_back(i - kGridSide);
            }
            if (y + 1 < kGridSide)
            {
                stack.push_back(i + kGridSide);
            }
        }
        sizes.push_back(size);
    }
    std::ranges::sort(sizes, std::ranges::greater{});
    return sizes;
}

}  // namespace

TEST(terrain_is_deterministic_per_seed)
{
    // 地形参与逐位确定性契约（决策 11）：它被 MovementSystem 每季消费，
    // 不同 seed 之间若有任何一个格不同，equal_seeds_produce_identical_
    // seasons 之外的同种子复现就全失效了。
    const EnvironmentConfig config{};
    const auto a = TerrainField::generate(7, config);
    const auto b = TerrainField::generate(7, config);

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
        CHECK_EQ(a.elevation(cell), b.elevation(cell));
        CHECK_EQ(a.passable(cell), b.passable(cell));
    }
}

TEST(different_seeds_give_different_maps)
{
    const EnvironmentConfig config{};
    const auto a = TerrainField::generate(7, config);
    const auto b = TerrainField::generate(8, config);

    std::size_t differing = 0;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        if (a.elevation(CellIndex{static_cast<std::uint16_t>(i)})
            != b.elevation(CellIndex{static_cast<std::uint16_t>(i)}))
        {
            ++differing;
        }
    }
    CHECK(differing > kCellCount / 2);
}

TEST(water_fraction_holds_for_every_seed)
{
    // **可变异性守卫**：海平面若写成固定常数，这条会随种子时红时绿——
    // 它守的正是「取分位数」这件事，而不是「70 这个数恰好对」。
    const EnvironmentConfig config{};
    for (const std::uint64_t seed :
         {1ull, 7ull, 31ull, 20261016ull, 999983ull, 424242ull})
    {
        const auto terrain = TerrainField::generate(seed, config);
        const auto ratio =
            static_cast<double>(count_water(terrain))
            / static_cast<double>(kCellCount);
        CHECK(ratio > 0.68);
        CHECK(ratio < 0.72);
    }
}

TEST(no_water_fraction_means_every_cell_is_land)
{
    // 「全陆地世界」必须是 water_fraction = 0 这个合法配置，而不是一条
    // 特例分支。既有 environment 测试大量依赖全陆地（见 Task 2）。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    const auto terrain = TerrainField::generate(31, config);

    CHECK_EQ(count_water(terrain), std::size_t{0});
}

TEST(full_water_fraction_leaves_exactly_one_land_cell)
{
    // water_fraction = 1 的 cut 正好是 kCellCount - 1，是这个下标不越界的
    // 最后一端。越界即 UB，而 \pre 只写在头文件里、没有运行时拦。
    EnvironmentConfig config{};
    config.water_fraction = 1.0f;
    const auto terrain = TerrainField::generate(31, config);

    CHECK_EQ(count_water(terrain), kCellCount - 1);
}

TEST(the_map_has_a_landmass_big_enough_to_be_a_barrier)
{
    // 阶段 2 的招牌产出挂在屏障上，而屏障要求一块**够大的**次大陆：湖心
    // 小岛个体绕过去就行，构不成隔离。200 格是这条的门槛——远高于
    // min_viable = 20，又落在 base 0.10 / 水 70% 下六个种子的实测区间
    // 226~474 之内，因此它断言的是「每张图都成立」，不是「挑了个好种子」。
    //
    // **这条就是当初那个弱断言的替代品。** 旧版只断 `largest < land`，
    // 而那在「一整块大陆 + 一颗 15 格子湖心岛」的图上照样成立。
    const EnvironmentConfig config{};
    for (const std::uint64_t seed : {1ull, 31ull, 20261016ull, 999983ull})
    {
        const auto terrain = TerrainField::generate(seed, config);
        const auto sizes = landmasses(terrain);

        // 两条合成一条是必须的：CHECK 只累加失败数、不中止，拆开写的话
        // 「退回单一大陆」这个最该报警的情形反而会走到 sizes[1] 越界读，
        // 在 ASan 里表现为 sanitizer 中止而不是一条干净的 FAIL。
        CHECK(sizes.size() >= 2 && sizes[1] > 200);
    }
}

TEST(flooding_a_cell_makes_it_impassable_and_draining_gives_it_back)
{
    EnvironmentConfig config{};
    auto terrain = TerrainField::generate(31, config);

    // 挑一个当前是陆地的格：flood 只改 passable，elevation 与海平面都不动，
    // 因此测试与「这块地原本多高」无关。
    std::size_t land = kCellCount;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
        if (terrain.passable(cell))
        {
            land = i;
            break;
        }
    }
    CHECK(land < kCellCount);
    const auto cell = CellIndex{static_cast<std::uint16_t>(land)};

    const auto before = terrain.elevation(cell);
    terrain.flood(cell);
    CHECK(!terrain.passable(cell));
    // **高程不动**：玩家的干预改的是「现在能不能站」，不是「这里本来多高」。
    // 渲染层画水墨晕染读的是 elevation，若 flood 顺手改了它，玩家一淹就
    // 把地势也抹平了。
    CHECK_EQ(terrain.elevation(cell), before);

    terrain.drain(cell);
    CHECK(terrain.passable(cell));
    CHECK_EQ(terrain.elevation(cell), before);
}

TEST(drained_water_stays_passable_below_the_original_sea_level)
{
    // drain 可以把格变成比海平面还低的「水」——那是玩家的画，不是自然地形。
    // 因此「passable」与「elevation > sea_level」必须彻底解耦，否则 drain
    // 过的格下一季会被 generate 的逻辑重新淹掉。
    EnvironmentConfig config{};
    auto terrain = TerrainField::generate(31, config);
    const auto sea = terrain.sea_level();

    // 挑一个本就在水位之下的水格。找不到就让这条测试失败而不是悄悄跳过：
    // 70% 的水保证至少有一格，而断言要断的正是「这一格 drain 之后仍然是陆」。
    std::size_t deep = kCellCount;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
        if (!terrain.passable(cell) && terrain.elevation(cell) < sea)
        {
            deep = i;
            break;
        }
    }
    CHECK(deep < kCellCount);
    const auto cell = CellIndex{static_cast<std::uint16_t>(deep)};

    terrain.drain(cell);
    // 两条判据解耦的直接证据：可通行，但高程仍在海平面之下。
    CHECK(terrain.passable(cell));
    CHECK(terrain.elevation(cell) < sea);
    // 海平面是生成时的产物，玩家的干预改不动它。
    CHECK_EQ(terrain.sea_level(), sea);
}

TEST(rebinding_the_resource_field_gives_painted_land_its_capacity_back)
{
    // 玩家填水造陆时 capacity 必须从 0 回到 capacity_base × base_fertility，
    // 否则新造的陆地是零承载力的死地 —— 铺得进去、生不出来。
    EnvironmentConfig config{};
    const gamev2::environment::ClimateField climate{20261016, config};
    auto terrain = TerrainField::generate(20261016, config);
    gamev2::environment::ResourceField field{climate, config, terrain};

    std::size_t water = kCellCount;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
        if (!terrain.passable(cell))
        {
            water = i;
            break;
        }
    }
    CHECK(water < kCellCount);
    const auto cell = CellIndex{static_cast<std::uint16_t>(water)};
    CHECK_EQ(field.capacity(cell), 0.0f);

    terrain.drain(cell);
    field.rebind_terrain(terrain);

    CHECK_EQ(field.capacity(cell),
             config.capacity_base * climate.base(cell).base_fertility);
}

TEST(water_cells_carry_no_capacity)
{
    // Σcapacity 是 sim 那句「末季 X = Σcapacity 的 Y%」的分母，也是
    // population_never_exceeds_the_capacity_left_below_it 的那条界。
    // 水格永远站不了人，却仍按 base_fertility 贡献一份承载力的话，两个
    // 读数都会虚高。
    const EnvironmentConfig config{};
    const auto terrain = TerrainField::generate(20261016, config);
    const auto climate = gamev2::environment::ClimateField{20261016, config};
    const auto field =
        gamev2::environment::ResourceField{climate, config, terrain};

    std::size_t water = 0;
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
        if (terrain.passable(cell))
        {
            CHECK(field.capacity(cell) > 0.0f);
        }
        else
        {
            ++water;
            CHECK_EQ(field.capacity(cell), 0.0f);
            CHECK_EQ(field.stock(cell), 0.0f);
        }
    }
    CHECK(water > 0);
}

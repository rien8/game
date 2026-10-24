#include "gamev2/world/world.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::Biome;
using gamev2::world::Grid;
using gamev2::world::World;
using gamev2::world::WorldParams;

TEST(grid_indexes_row_major_and_supports_linear_access)
{
    Grid<std::uint32_t> g(4, 3, 7);
    CHECK_EQ(g.index_of(0, 0), 0u);
    CHECK_EQ(g.index_of(3, 0), 3u);
    CHECK_EQ(g.index_of(0, 1), 4u);
    CHECK_EQ(g.index_of(2, 2), 10u);
    CHECK_EQ(g.size(), 12u);
    CHECK_EQ(g(2, 2), 7u);

    g(2, 2) = 42;
    CHECK_EQ(g.at(g.index_of(2, 2)), 42u);
}

TEST(world_sizes_grids_and_layers)
{
    WorldParams p;
    p.width = 16;
    p.height = 12;
    p.z_levels = 3;
    p.z_cell_depth = 8;

    World w(p);
    CHECK_EQ(w.elevation.width(), 16u);
    CHECK_EQ(w.elevation.height(), 12u);
    CHECK_EQ(w.stone.size(), std::size_t{3});
    CHECK_EQ(w.deposit[0].size(), 192u);
    CHECK_EQ(w.depth(), 24u);
    CHECK_EQ(w.cell_depth(2, 5), 21u);
    CHECK_EQ(w.cell_depth(0, 0), 0u);
}

TEST(world_sea_level_starts_at_zero)
{
    World w(WorldParams{});
    CHECK_EQ(w.sea_level(), 0u);
    // 未生成高程前没有海洋可言。
    CHECK(!w.is_ocean(0, 0));
}

TEST(params_reject_degenerate_sizes)
{
    CHECK(gamev2::world::WorldParams{}.is_valid());
    CHECK(!gamev2::world::WorldParams{.width = 0}.is_valid());
    CHECK(!gamev2::world::WorldParams{.height = 0}.is_valid());
    CHECK(!gamev2::world::WorldParams{.z_levels = 0}.is_valid());
    CHECK(!gamev2::world::WorldParams{.z_cell_depth = 0}.is_valid());
    CHECK(!gamev2::world::WorldParams{.ocean_percent = 101}.is_valid());
}

TEST(celsius_scale_round_trips)
{
    CHECK_EQ(gamev2::world::celsius_of(0), -40);
    CHECK_EQ(gamev2::world::celsius_of(255), 50);
    CHECK_EQ(gamev2::world::temperature_of(-40), 0u);
    CHECK_EQ(gamev2::world::temperature_of(50), 255u);
    CHECK(gamev2::world::celsius_of(
              gamev2::world::temperature_of(-5)) <= -5);
}

TEST(biome_enum_matches_declared_count)
{
    CHECK_EQ(static_cast<std::size_t>(Biome::glacier) + 1,
             gamev2::world::kBiomeCount);
}

TEST(grid_of_bool_supports_assignment_through_both_accessors)
{
    Grid<bool> g(3, 3, false);
    CHECK(!g.at(g.index_of(1, 1)));

    g(1, 1) = true;
    CHECK(g(1, 1));
    CHECK(g.at(g.index_of(1, 1)));

    g.at(g.index_of(1, 1)) = false;
    CHECK(!g.at(g.index_of(1, 1)));

    Grid<bool> filled(2, 2, true);
    CHECK(filled(0, 0));
    CHECK_EQ(filled.size(), 4u);
}

}  // namespace

#include "gamev2/world/tables.hpp"
#include "test_framework.hpp"

#include <cstddef>

namespace {

using gamev2::world::Biome;
using gamev2::world::celsius_of;
using gamev2::world::drainage_band;
using gamev2::world::elev_class_of;
using gamev2::world::kBiomeCount;
using gamev2::world::kBaseBiome;
using gamev2::world::kFreezingCelsius;
using gamev2::world::kGlacierDrainage;
using gamev2::world::kTropicalCelsius;
using gamev2::world::rain_band;
using gamev2::world::select_biome;
using gamev2::world::temperature_of;

TEST(elevation_class_thresholds_split_the_land)
{
    CHECK(elev_class_of(10, 40) == gamev2::world::ElevClass::ocean);
    CHECK(elev_class_of(40, 40) == gamev2::world::ElevClass::lowland);
    CHECK(elev_class_of(70, 40) == gamev2::world::ElevClass::hills);
    CHECK(elev_class_of(100, 40) == gamev2::world::ElevClass::mountain);
}

TEST(bands_stay_inside_the_table)
{
    CHECK_EQ(rain_band(0), std::size_t{0});
    CHECK_EQ(rain_band(32), std::size_t{1});
    CHECK_EQ(rain_band(255), std::size_t{7});
    CHECK_EQ(drainage_band(0), std::size_t{0});
    CHECK_EQ(drainage_band(13), std::size_t{1});
    // 去掉 std::min 后这里会是 15，测试随即失败。
    CHECK_EQ(drainage_band(200), std::size_t{7});
}

TEST(base_table_has_no_out_of_range_values)
{
    bool ok = true;
    for (const auto& row : kBaseBiome)
    {
        for (const Biome b : row)
        {
            ok = ok && static_cast<std::size_t>(b) < kBiomeCount;
        }
    }
    CHECK(ok);
}

TEST(base_table_keeps_wetlands_and_forests_on_their_own_sides)
{
    // 表头自称的两条不变式，在湿润的降水行上才成立；干行右侧是荒漠与
    // 灌丛，不受此约束。逐格断言不变式而非抄表，抄错一格仍会被抓到。
    bool ok = true;
    for (std::size_t r = 4; r < 8; ++r)
    {
        const Biome poorly_drained = kBaseBiome[r][0];
        const Biome well_drained = kBaseBiome[r][7];
        ok = ok && (poorly_drained == Biome::swamp
                    || poorly_drained == Biome::marshland);
        ok = ok && (well_drained == Biome::temperate_broadleaf
                    || well_drained == Biome::temperate_coniferous
                    || well_drained == Biome::pine_barrens);
    }
    CHECK(ok);
}

TEST(base_table_rows_and_columns_are_not_interchangeable)
{
    // 行是降水、列是排水，两轴取值不同，交换下标顺序必然产出不同群系。
    // 必须走 select_biome 断言：直接查 kBaseBiome 只能查出表本身抄错，
    // 查不出 select_biome 里两个下标写反。选 [7][4] 与 [4][7] 这一对
    // 非对称格，表内大部分格是对称的，只有挑对格子才能区分两个顺序。
    const auto mild = temperature_of(20);
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 55, 250, mild)
          == Biome::temperate_broadleaf);
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 95, 140, mild)
          == Biome::pine_barrens);
}

TEST(elevation_decides_ocean_and_mountain_first)
{
    const auto hot = temperature_of(20);
    CHECK(select_biome(gamev2::world::ElevClass::ocean, 0, 0, hot)
          == Biome::ocean);
    CHECK(select_biome(gamev2::world::ElevClass::mountain, 100, 255, hot)
          == Biome::mountain);
    // 高程判定必须排在冻结覆写之前，否则海洋与山岳会在极地退化成苔原或
    // 冰川。这两行把分支顺序钉死。
    const auto cold = temperature_of(kFreezingCelsius);
    CHECK(select_biome(gamev2::world::ElevClass::ocean, 0, 0, cold)
          == Biome::ocean);
    CHECK(select_biome(gamev2::world::ElevClass::mountain, 100, 255, cold)
          == Biome::mountain);
}

TEST(freezing_overrides_by_drainage)
{
    const auto cold = temperature_of(kFreezingCelsius);
    CHECK(celsius_of(cold) <= kFreezingCelsius);
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 10, 128, cold)
          == Biome::tundra);
    CHECK(select_biome(gamev2::world::ElevClass::lowland,
                       kGlacierDrainage, 128, cold) == Biome::glacier);
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 50, 128,
                       temperature_of(20)) != Biome::tundra);
}

TEST(tropical_overrides_forests)
{
    // temperature_of 向零截断，30°C 对应的字节是 198，而 celsius_of(198) 只有
    // 29°C，取它做输入会跳过整个热带覆写。+1 让字节真正越过阈值。
    const auto hot = temperature_of(kTropicalCelsius + 1);
    // 基础表第 7 行第 7 列是 temperate_broadleaf，热带覆写后转潮湿变体。
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 95, 250, hot)
          == Biome::tropical_moist_broadleaf);
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 95, 250,
                       temperature_of(20))
          == Biome::temperate_broadleaf);
    // 第 7 行第 6 列是 temperate_coniferous，热带覆写后转热带针叶林。
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 90, 240, hot)
          == Biome::tropical_coniferous);
}

TEST(wet_and_dry_quadrants_differ)
{
    const auto mild = temperature_of(20);
    // 同一降水下，排水最差与排水最好的格必须给出不同群系，否则整套
    // drainage 判定就是摆设。
    CHECK(select_biome(gamev2::world::ElevClass::lowland, 0, 160, mild)
          != select_biome(gamev2::world::ElevClass::lowland, 100, 160, mild));
}

}  // namespace

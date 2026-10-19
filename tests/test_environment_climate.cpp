#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

using gamev2::environment::CellIndex;
using gamev2::environment::ClimateField;
using gamev2::environment::EnvironmentConfig;

namespace {

bool bitwise_same(const ClimateField& a, const ClimateField& b)
{
    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        const auto& ca = a.base(index);
        const auto& cb = b.base(index);
        // 逐位而非逐值：相同种子必须复现同一批比特，不是「差不多」。
        if (std::bit_cast<std::uint32_t>(ca.base_temperature)
            != std::bit_cast<std::uint32_t>(cb.base_temperature))
        {
            return false;
        }
        if (std::bit_cast<std::uint32_t>(ca.base_fertility)
            != std::bit_cast<std::uint32_t>(cb.base_fertility))
        {
            return false;
        }
        if (std::bit_cast<std::uint32_t>(ca.base_light)
            != std::bit_cast<std::uint32_t>(cb.base_light))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(climate_same_seed_is_bitwise_identical)
{
    const EnvironmentConfig config{};
    CHECK(bitwise_same(ClimateField{1234, config},
                       ClimateField{1234, config}));
}

TEST(climate_different_seed_differs)
{
    const EnvironmentConfig config{};
    CHECK(!bitwise_same(ClimateField{1234, config},
                        ClimateField{5678, config}));
}

TEST(climate_base_fertility_stays_within_its_declared_interval)
{
    const ClimateField field{99, EnvironmentConfig{}};
    // 跨格的实际取值区间，而不是逐格断言非零：uniform() 只在 unit 恰为 0 时
    // 才返回 lo，单格概率 2^-24，把下界改成 0.0f 几乎抓不到。
    float lowest = 1.0f;
    float highest = 0.0f;
    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        const float value = field.base(index).base_fertility;
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
    }
    // 零承载力的格永远不会有生物，地图上是一块谁也解释不了的死地。
    CHECK(lowest >= 0.25f);
    // 分布确实铺满了声明区间的下端，而不是挤在区间上部。
    CHECK(lowest < 0.30f);
    CHECK(highest <= 1.0f);
}

TEST(climate_season_changes_temperature_within_amplitude)
{
    const EnvironmentConfig config{};
    ClimateField field{7, config};
    field.advance(0);
    // 季节 0 是基准相位，sin 为 0，是全年最冷的一端，不是最暖。
    const auto cold = field.at(CellIndex::of(3.0f, 5.0f));

    field.advance(2);
    const auto other = field.at(CellIndex::of(3.0f, 5.0f));

    CHECK(cold.temperature != other.temperature);

    const float base = field.base(CellIndex::of(3.0f, 5.0f)).base_temperature;
    CHECK(std::fabs(other.temperature - base) <= config.temperature_amplitude
                                                + 1e-4f);
}

TEST(climate_advance_is_a_pure_function_of_the_season)
{
    // 季节只改派生量、不累加：任意一季都能直接算出，读到的是哪一季与
    // 之前推进过几季无关。
    // 本例只守「不累加」：advance() 若退化成空函数，两边 season_ 都是 0，
    // 断言照样成立。「advance 真的记录了季节」由上一个用例的
    // cold != other 覆盖。
    ClimateField a{42, EnvironmentConfig{}};
    a.advance(0);
    a.advance(1);
    a.advance(2);
    a.advance(3);

    ClimateField b{42, EnvironmentConfig{}};
    b.advance(3);

    CHECK_EQ(a.at(CellIndex::of(10.0f, 10.0f)).temperature,
             b.at(CellIndex::of(10.0f, 10.0f)).temperature);
}

TEST(cell_index_maps_world_coordinates_into_the_grid)
{
    const auto side = static_cast<std::uint16_t>(
        gamev2::environment::kGridSide);
    CHECK_EQ(CellIndex::of(0.0f, 0.0f).value, std::uint16_t{0});
    CHECK_EQ(CellIndex::of(1.5f, 2.5f).value,
             static_cast<std::uint16_t>(2 * side + 1));
    CHECK_EQ(CellIndex::of(63.9f, 63.9f).value,
             static_cast<std::uint16_t>(63 * side + 63));
}

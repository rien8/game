#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/sites.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using gamev2::environment::CellIndex;
using gamev2::environment::ClimateField;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::Forager;
using gamev2::environment::ResourceField;
using gamev2::environment::TerrainField;

namespace {

/// 既有断言全部在 capacity_base × base_fertility 这个关系上，而该关系在
/// water_fraction = 0（全陆地）下与引入地形之前逐位相同——因此这 7 条
/// 测试的断言一个字都不用改，改的只是「传哪个地形进去」。
EnvironmentConfig flat()
{
    auto c = EnvironmentConfig{};
    c.water_fraction = 0.0f;
    return c;
}

}  // namespace

TEST(resource_capacity_scales_with_base_fertility)
{
    const auto config = flat();
    const ClimateField climate{5, config};
    const TerrainField terrain = TerrainField::generate(5, config);
    const ResourceField field{climate, config, terrain};

    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        const float expected = config.capacity_base
                               * climate.base(index).base_fertility;
        CHECK_EQ(field.capacity(index), expected);
    }
}

TEST(resource_starts_full)
{
    const auto config = flat();
    const ClimateField climate{5, config};
    const TerrainField terrain = TerrainField::generate(5, config);
    const ResourceField field{climate, config, terrain};

    const auto index = CellIndex::of(20.0f, 20.0f);
    // 否则第一季所有个体都从饥饿开始，而那不是任何配置意图表达的初始条件。
    CHECK_EQ(field.stock(index), field.capacity(index));
}

TEST(resource_regrow_never_exceeds_capacity)
{
    const auto config = flat();
    const ClimateField climate{11, config};
    const TerrainField terrain = TerrainField::generate(11, config);
    ResourceField field{climate, config, terrain};
    const auto index = CellIndex::of(20.0f, 20.0f);

    field.draw(index, field.capacity(index) * 0.5f);

    for (int step = 0; step < 200; ++step)
    {
        field.regrow();
        CHECK(field.stock(index) <= field.capacity(index));
    }
    // 从半满跑 200 步必须已逼近容量，否则「逼近」只是巧合。
    CHECK(field.stock(index) > field.capacity(index) * 0.99f);
}

TEST(resource_draw_clamps_at_zero)
{
    const auto config = flat();
    const ClimateField climate{3, config};
    const TerrainField terrain = TerrainField::generate(3, config);
    ResourceField field{climate, config, terrain};
    const auto index = CellIndex::of(1.0f, 1.0f);

    field.draw(index, 1e9f);
    CHECK_EQ(field.stock(index), 0.0f);
}

TEST(resource_occupants_start_at_zero)
{
    const auto config = flat();
    const ClimateField climate{3, config};
    const TerrainField terrain = TerrainField::generate(3, config);
    const ResourceField field{climate, config, terrain};

    CHECK_EQ(field.occupants(CellIndex::of(0.5f, 0.5f)),
             std::uint16_t{0});
}

TEST(regen_rate_comes_from_config)
{
    auto config = flat();
    config.regen_rate = 0.5f;
    const ClimateField climate{5, config};
    const TerrainField terrain = TerrainField::generate(5, config);
    ResourceField field{climate, config, terrain};
    const auto index = CellIndex::of(20.0f, 20.0f);

    field.draw(index, field.capacity(index) * 0.5f);
    field.regrow();
    // 0.5c + 0.5 × (c − 0.5c) = 0.75c；成员默认值 0.25f 会给出 0.625c。
    // 全程是 c 的 2 的幂缩放与求和，float 下精确，严格相等成立。
    CHECK_EQ(field.stock(index), field.capacity(index) * 0.75f);
}

TEST(count_occupants_tallies_cells_by_position)
{
    auto config = flat();
    const ClimateField climate{3, config};
    const TerrainField terrain = TerrainField::generate(3, config);
    ResourceField field{climate, config, terrain};

    // 三个挤在 (1,1) 格、一个在 (3,1) 格。坐标刻意对角不对称：
    // 对称的夹具在 x/y 取反后落进同一批格，两组断言会同时保持绿。
    const std::vector<Forager> foragers{
        Forager{1.5f, 1.5f, 1.0f}, Forager{1.5f, 1.5f, 1.0f},
        Forager{1.5f, 1.5f, 1.0f}, Forager{1.5f, 3.5f, 1.0f}};

    field.count_occupants(foragers);
    CHECK_EQ(field.occupants(CellIndex::of(1.5f, 1.5f)),
             std::uint16_t{3});
    // 这一条专门杀 x/y 取反：取反后 {1.5, 3.5} 会落进 of(3.5, 1.5)，该格变成 1。
    CHECK_EQ(field.occupants(CellIndex::of(3.5f, 1.5f)),
             std::uint16_t{0});

    field.count_occupants({});
    CHECK_EQ(field.occupants(CellIndex::of(1.5f, 1.5f)),
             std::uint16_t{0});
}

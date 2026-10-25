### Task 1: 网格容器、世界容器与构建目标

**Files:**
- Create: `include/gamev2/world/grid.hpp`
- Create: `include/gamev2/world/enums.hpp`
- Create: `include/gamev2/world/params.hpp`
- Create: `include/gamev2/world/world.hpp`
- Create: `src/world/world.cpp`
- Create: `tests/test_world_grid.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无（首个任务）
- Produces:
  - `template<typename T> class gamev2::world::Grid`，构造 `Grid(uint32_t w,
    uint32_t h, T fill = T{})`，成员 `T& operator()(uint32_t x, uint32_t y)`、
    `const T& operator()(...) const`、`T& at(uint32_t linear)`、
    `uint32_t index_of(uint32_t x, uint32_t y) const`、
    `uint32_t width() const`、`uint32_t height() const`、`uint32_t size() const`
  - `enum class ElevClass { ocean, lowland, hills, mountain }`
  - `enum class Biome { ... 21 项，见下方代码 }`
  - `enum class Stone { ... 13 项 }`
  - `enum class Deposit { none, ... 12 项 }`
  - `constexpr uint32_t kNoDownstream`
  - `constexpr int celsius_of(uint8_t)`、`constexpr uint8_t temperature_of(int)`
  - `inline constexpr int kFreezingCelsius`、`kTropicalCelsius`
  - `inline constexpr std::size_t kBiomeCount`
  - `struct WorldParams`，成员 `width/height/z_levels/z_cell_depth/seed/
    ocean_percent`，方法 `constexpr bool is_valid() const`
  - `enum class WorldError { invalid_params }`
  - `class World`，构造 `explicit World(WorldParams)`，成员见下方代码

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_grid.cpp`：

```cpp
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

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 编译失败，报 `gamev2/world/world.hpp` 不存在。

- [ ] **Step 3: 写 `grid.hpp`**

创建 `include/gamev2/world/grid.hpp`：

```cpp
/// @file grid.hpp
/// @brief 稠密二维网格容器。世界的全部地表量与每层地下量都存放在这里。
#pragma once

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 行主序稠密二维网格。默认构造得到空网格，任何访问都是未定义行为，
/// 边界检查留给调用方：网格尺寸在生成期固定，热路径上再查一遍是纯开销。
template<typename T>
class Grid
{
public:
    Grid() = default;

    Grid(std::uint32_t width, std::uint32_t height, T fill = T{})
        : width_(width),
          height_(height),
          cells_(static_cast<std::size_t>(width) * height, fill)
    {
    }

    [[nodiscard]] T& operator()(std::uint32_t x, std::uint32_t y) noexcept
    {
        return cells_[index_of(x, y)];
    }

    [[nodiscard]] const T& operator()(std::uint32_t x,
                                      std::uint32_t y) const noexcept
    {
        return cells_[index_of(x, y)];
    }

    /// 线性寻址。downstream 这类存格子索引的字段靠它跳转，省掉一次重复
    /// 的 x/y 到索引换算。
    [[nodiscard]] T& at(std::uint32_t linear) noexcept
    {
        return cells_[linear];
    }

    [[nodiscard]] std::uint32_t index_of(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return y * width_ + x;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return static_cast<std::uint32_t>(cells_.size());
    }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<T> cells_;
};

namespace detail {

/// std::vector<bool> 是位压缩特化，其 operator[] 返回代理而非 bool&，
/// 存进 Grid 之后无法满足 T& 的返回契约。用一个可平凡移动的包装体把存储
/// 换回普通 std::vector：语义不变，仍然一格一字节。
struct BoolCell
{
    bool value = false;
};

}  // namespace detail

/// bool 特化。接口与主模板逐字一致，只有底层存储不同。
///
/// 之所以单独特化而不是让 cavern / aquifer 改用 std::uint8_t 网格：那样
/// 会把 `!= 0` 推进到二十多处调用点与断言里，总复杂度反而更高。
template<>
class Grid<bool>
{
public:
    Grid() = default;

    Grid(std::uint32_t width, std::uint32_t height, bool fill = false)
        : width_(width),
          height_(height),
          cells_(static_cast<std::size_t>(width) * height,
                 detail::BoolCell{fill})
    {
    }

    [[nodiscard]] bool& operator()(std::uint32_t x,
                                   std::uint32_t y) noexcept
    {
        return cells_[index_of(x, y)].value;
    }

    [[nodiscard]] const bool& operator()(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return cells_[index_of(x, y)].value;
    }

    [[nodiscard]] bool& at(std::uint32_t linear) noexcept
    {
        return cells_[linear].value;
    }

    [[nodiscard]] std::uint32_t index_of(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return y * width_ + x;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return static_cast<std::uint32_t>(cells_.size());
    }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<detail::BoolCell> cells_;
};

}  // namespace gamev2::world
```

在 `tests/test_world_grid.cpp` 的匿名命名空间内追加一个用例，覆盖 bool 特化的
可写访问——主模板与特化的接口必须逐字一致，这条只有真写一次才验得住：

```cpp
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
```

`std::vector<bool>` 的位压缩特化会让 `operator[]` 返回代理而非 `bool&`，
主模板因此无法满足 `T&` 的返回契约。本任务不触发（`cavern` / `aquifer`
此时只构造不索引），但任务 11 与 12 一写 `world.cavern[z](x, y) = true`
就会编译失败。

- [ ] **Step 4: 写 `enums.hpp`**

创建 `include/gamev2/world/enums.hpp`：

```cpp
/// @file enums.hpp
/// @brief 世界生成用到的全部枚举与刻度常量。
#pragma once

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
    taiga,
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

inline constexpr std::size_t kBiomeCount = 21;

/// 岩性。sedimentary_* 是表层沉积，igneous_* 是深层与侵入体。
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

}  // namespace gamev2::world
```

- [ ] **Step 5: 写 `params.hpp`**

创建 `include/gamev2/world/params.hpp`：

```cpp
/// @file params.hpp
/// @brief 世界生成的尺寸与常量，以及唯一的可预期失败原因。
#pragma once

#include <cstdint>

namespace gamev2::world {

/// 一次生成所需的全部输入。
struct WorldParams
{
    std::uint32_t width = 256;
    std::uint32_t height = 256;
    /// z 层数：0 为地表，1..z_levels-1 为地下。
    std::uint32_t z_levels = 6;
    /// 每 z 层厚度，以格计。
    std::uint32_t z_cell_depth = 16;
    std::uint64_t seed = 0;
    /// 目标海洋面积占比，海平面由高程分位数反解，因此海陆比不随种子漂移。
    std::uint8_t ocean_percent = 60;

    /// 只拦下会让分配或下标计算失去意义的参数，其余字段一律放行。
    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return width > 0 && height > 0 && z_levels > 0
               && z_cell_depth > 0 && ocean_percent <= 100;
    }
};

/// 参数非法。分配失败走 bad_alloc，不归到这里。
enum class WorldError
{
    invalid_params,
};

}  // namespace gamev2::world
```

- [ ] **Step 6: 写 `world.hpp`**

创建 `include/gamev2/world/world.hpp`：

```cpp
/// @file world.hpp
/// @brief 世界容器。纯数据，不含行为：生成阶段以 World& 为唯一操作对象
///        就地改写它，读取方按坐标查询，World 从不反向引用实体。
#pragma once

#include "gamev2/world/enums.hpp"
#include "gamev2/world/grid.hpp"
#include "gamev2/world/params.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 全部生成阶段之间共享的唯一状态。
class World
{
public:
    explicit World(WorldParams params);

    [[nodiscard]] const WorldParams& params() const noexcept
    {
        return params_;
    }

    /// 总格深 = z_levels * z_cell_depth。
    [[nodiscard]] std::uint32_t depth() const noexcept;

    /// z 层号加层内格深，换成总格深坐标。
    [[nodiscard]] std::uint32_t cell_depth(std::uint32_t z,
                                           std::uint32_t d) const noexcept;

    /// 阶段 1 反解出的实际海平面。
    [[nodiscard]] std::uint8_t sea_level() const noexcept { return sea_level_; }

    [[nodiscard]] bool is_ocean(std::uint32_t x,
                                std::uint32_t y) const noexcept;

    /// 地表量，每 (x, y) 一个值。
    Grid<std::uint8_t>  elevation;
    Grid<std::uint8_t>  rainfall;
    Grid<std::uint8_t>  temperature;
    Grid<Biome>         biome;
    Grid<std::uint32_t> downstream;
    Grid<std::uint32_t> flow;
    Grid<std::uint8_t>  river_width;
    Grid<std::uint8_t>  lake_depth;
    Grid<std::uint8_t>  drainage;
    Grid<std::uint8_t>  soil_depth;
    Grid<std::uint8_t>  aquifer_top;

    /// 地下量，每 z 层一份同构网格。
    std::vector<Grid<Stone>>   stone;
    std::vector<Grid<Deposit>> deposit;
    std::vector<Grid<bool>>    cavern;
    std::vector<Grid<bool>>    aquifer;

private:
    friend void make_elevation(World&);

    WorldParams params_;
    std::uint8_t sea_level_ = 0;
};

}  // namespace gamev2::world
```

- [ ] **Step 7: 写 `world.cpp`**

创建 `src/world/world.cpp`：

```cpp
#include "gamev2/world/world.hpp"

namespace gamev2::world {
namespace {

/// 三维量就是每 z 层一份同构网格，用同一个工厂避免四段重复的循环。
template<typename T>
std::vector<Grid<T>> make_layers(const WorldParams& params, T fill)
{
    std::vector<Grid<T>> layers;
    layers.reserve(params.z_levels);
    for (std::uint32_t z = 0; z < params.z_levels; ++z)
    {
        layers.emplace_back(params.width, params.height, fill);
    }
    return layers;
}

}  // namespace

World::World(WorldParams params)
    : params_(params),
      elevation(params.width, params.height, 0),
      rainfall(params.width, params.height, 0),
      temperature(params.width, params.height, 128),
      biome(params.width, params.height, Biome::ocean),
      downstream(params.width, params.height, kNoDownstream),
      flow(params.width, params.height, 0),
      river_width(params.width, params.height, 0),
      lake_depth(params.width, params.height, 0),
      drainage(params.width, params.height, 100),
      soil_depth(params.width, params.height, 0),
      aquifer_top(params.width, params.height, 0),
      stone(make_layers(params, Stone::granite)),
      deposit(make_layers(params, Deposit::none)),
      cavern(make_layers(params, false)),
      aquifer(make_layers(params, false))
{
}

std::uint32_t World::depth() const noexcept
{
    return params_.z_levels * params_.z_cell_depth;
}

std::uint32_t World::cell_depth(std::uint32_t z,
                                std::uint32_t d) const noexcept
{
    return z * params_.z_cell_depth + d;
}

bool World::is_ocean(std::uint32_t x, std::uint32_t y) const noexcept
{
    return elevation(x, y) < sea_level_;
}

}  // namespace gamev2::world
```

- [ ] **Step 8: 在 CMakeLists 中登记目标**

修改 `CMakeLists.txt`，在 `add_executable(ecs_tests ...)` 之后追加：

```cmake
set(WORLDGEN_SOURCES
    src/world/world.cpp
)
add_library(worldgen STATIC ${WORLDGEN_SOURCES})
target_include_directories(worldgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(WORLDGEN_TEST_SOURCES
    tests/main.cpp
    tests/test_world_grid.cpp
)
add_executable(worldgen_tests ${WORLDGEN_TEST_SOURCES})
target_link_libraries(worldgen_tests PRIVATE worldgen)
```

不要把 `worldgen` 加进 ASan 子目录：那个子目录的源文件列表写死了 `ecs`，
扩它属于本轮范围外。

- [ ] **Step 9: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `7 test(s), 0 failed`

- [ ] **Step 10: 提交**

```bash
git add include/gamev2/world src/world tests/test_world_grid.cpp CMakeLists.txt
```

向用户报告本任务完成，**等待用户决定是否提交**（Global Constraints 禁止
agent 自行 commit）。

---


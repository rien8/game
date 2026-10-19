# 世界生成模块实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个 C++23 世界生成模块，给定 seed 与尺寸生成一个 Dwarf Fortress
式的世界——地表自然地理（高程 / 降水 / 温度 / 水文 / 群系）加地下结构
（土壤 / 岩层 / 洞穴 / 矿脉 / 含水层）。

**Architecture:** 一个纯数据容器 `World` 持有全部网格，十个生成阶段以 `World&`
为唯一操作对象就地改写它，编排顺序只写在 `generate_world()` 一处。三维数据
表示为 `std::vector<Grid<T>>`（每 z 层一份同构数组），二维量用 `Grid<T>`。
世界不进 ECS：它是数据而非实体，且地表会被高频改写。

**Tech Stack:** C++23、clang++ 23.1.1、MSVC STL 14.51、CMake 4.4、
Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-19-worldgen-design.md`

**DF 参考：** `agent-temp/df_Biome.txt`（群系规则原文）

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 编译器固定为 clang++。MinGW g++ 8.1.0 不支持 C++23，不得使用
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- 任何一行不得超过 90 个字符，超长行必须手动折行
- Windows 环境下通过 `pwsh` 调用终端
- **不得执行 `git commit`**，提交时机由用户决定。每个任务末尾的 commit 步骤
  改为「向用户报告并等待提交指令」
- 输出文件（PPM、统计）写入 `agent-temp/`，该目录已被 gitignore
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- 噪声一律走 `value_noise` / `fbm`，禁止在生成代码里另写哈希

构建与测试命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

只跑单个测试文件不需要额外手段：测试框架靠静态注册，删掉 CMakeLists 里的
其余条目即可。正常做法是全部构建。

## 文件总览

本计划创建下列文件。文件职责单一，遵循「一件事一个文件」。

| 文件 | 职责 |
|---|---|
| `include/gamev2/world/grid.hpp` | 二维稠密网格容器，纯模板 |
| `include/gamev2/world/enums.hpp` | 全部枚举与刻度常量 |
| `include/gamev2/world/params.hpp` | `WorldParams` 与 `WorldError` |
| `include/gamev2/world/world.hpp` | `World` 容器 |
| `include/gamev2/world/rng.hpp` | `Pcg32` 与无状态噪声 |
| `include/gamev2/world/tables.hpp` | 群系查表与矿脉定义表 |
| `include/gamev2/world/generate.hpp` | 十个阶段函数声明 + `generate_world` |
| `src/world/world.cpp` | `World` 构造与查询 |
| `src/world/rng.cpp` | `Pcg32` 与噪声实现 |
| `src/world/terrain.cpp` | 阶段 1-3：高程 / 降水 / 温度 |
| `src/world/hydrology.cpp` | 阶段 4：填平 / 下游 / 汇流 / 河 / 湖 / 排水量 |
| `src/world/biomes.cpp` | 阶段 5：群系 |
| `src/world/strata.cpp` | 阶段 6-7：土壤 / 岩层 |
| `src/world/caverns.cpp` | 阶段 8：洞穴 |
| `src/world/deposits.cpp` | 阶段 9-10：矿脉 / 含水层 |
| `src/world/generate.cpp` | `generate_world` 编排 |
| `src/demo/main.cpp` | 统计与 PPM 导出 |
| `tests/test_world_grid.cpp` | 网格与容器 |
| `tests/test_world_rng.cpp` | 随机源与噪声 |
| `tests/test_world_elevation.cpp` | 阶段 1 |
| `tests/test_world_rainfall.cpp` | 阶段 2 |
| `tests/test_world_temperature.cpp` | 阶段 3 |
| `tests/test_world_hydrology.cpp` | 阶段 4 |
| `tests/test_world_biomes.cpp` | 阶段 5 |
| `tests/test_world_strata.cpp` | 阶段 6-7 |
| `tests/test_world_caverns.cpp` | 阶段 8 |
| `tests/test_world_deposits.cpp` | 阶段 9-10 |
| `tests/test_world_determinism.cpp` | 全流程确定性 |

`CMakeLists.txt` 每个任务追加对应源文件。

---

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

}  // namespace gamev2::world
```

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

Expected: `6 test(s), 0 failed`

- [ ] **Step 10: 提交**

```bash
git add include/gamev2/world src/world tests/test_world_grid.cpp CMakeLists.txt
```

向用户报告本任务完成，**等待用户决定是否提交**（Global Constraints 禁止
agent 自行 commit）。

---

### Task 2: 确定性随机源与无状态噪声

**Files:**
- Create: `include/gamev2/world/rng.hpp`
- Create: `src/world/rng.cpp`
- Create: `tests/test_world_rng.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无（不依赖前一个任务）
- Produces:
  - `class gamev2::world::Pcg32`，构造 `explicit Pcg32(uint64_t seed)`，
    方法 `uint32_t next()`、`uint32_t below(uint32_t n)`、`double unit()`
  - `uint64_t gamev2::world::stage_seed(uint64_t seed, uint32_t stage)`
  - `double value_noise(uint64_t, double x, double y)`，返回 `[0, 1]`
  - `double value_noise3(uint64_t, double x, double y, double z)`，返回 `[0, 1]`
  - `double fbm(uint64_t, double x, double y, int octaves, double lacunarity,
    double gain)`，返回 `[-1, 1]`
  - `double fbm3(uint64_t, double x, double y, double z, int octaves,
    double lacunarity, double gain)`，返回 `[-1, 1]`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_rng.cpp`：

```cpp
#include "gamev2/world/rng.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::fbm;
using gamev2::world::fbm3;
using gamev2::world::Pcg32;
using gamev2::world::stage_seed;
using gamev2::world::value_noise;
using gamev2::world::value_noise3;

TEST(pcg32_is_reproducible_and_bounded)
{
    Pcg32 a(12345);
    Pcg32 b(12345);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK_EQ(a.next(), b.next());
    }

    Pcg32 c(12345);
    bool below_in_range = true;
    for (int i = 0; i < 1000; ++i)
    {
        below_in_range = below_in_range && c.below(7) < 7;
    }
    CHECK(below_in_range);
    CHECK_EQ(Pcg32(1).below(0), 0u);

    Pcg32 d(1);
    bool unit_in_range = true;
    for (int i = 0; i < 1000; ++i)
    {
        const double u = d.unit();
        unit_in_range = unit_in_range && u >= 0.0 && u < 1.0;
    }
    CHECK(unit_in_range);
}

TEST(pcg32_below_covers_every_bucket)
{
    std::array<bool, 5> seen{};
    Pcg32 rng(2024);
    for (int i = 0; i < 500; ++i)
    {
        seen[rng.below(5)] = true;
    }
    for (const bool hit : seen)
    {
        CHECK(hit);
    }
}

TEST(stage_seeds_are_reproducible_and_independent)
{
    CHECK_EQ(stage_seed(9, 0), stage_seed(9, 0));
    CHECK(stage_seed(9, 0) != stage_seed(9, 1));
    CHECK(stage_seed(9, 1) != stage_seed(8, 1));
}

TEST(value_noise_is_stateless_and_bounded)
{
    // 无状态是硬约束：求值顺序不影响结果，所以反复取值必须一致。
    CHECK_EQ(value_noise(7, 3.5, -2.25), value_noise(7, 3.5, -2.25));
    CHECK(value_noise(7, 3.5, -2.25) != value_noise(8, 3.5, -2.25));

    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = value_noise(7, i * 0.37, i * -0.11);
        in_range = in_range && v >= 0.0 && v <= 1.0;
    }
    CHECK(in_range);
}

TEST(value_noise3_is_bounded)
{
    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = value_noise3(7, i * 0.37, i * -0.11, i * 0.53);
        in_range = in_range && v >= 0.0 && v <= 1.0;
    }
    CHECK(in_range);
}

TEST(fbm_stays_within_unit_range)
{
    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = fbm(3, i * 0.21, i * 0.17, 4, 2.0, 0.5);
        const double w = fbm3(3, i * 0.21, i * 0.17, i * 0.13, 3, 2.0, 0.5);
        in_range = in_range && v >= -1.0 && v <= 1.0;
        in_range = in_range && w >= -1.0 && w <= 1.0;
    }
    CHECK(in_range);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `gamev2/world/rng.hpp` 不存在。

- [ ] **Step 3: 写 `rng.hpp`**

创建 `include/gamev2/world/rng.hpp`：

```cpp
/// @file rng.hpp
/// @brief 确定性随机源与无状态噪声。
///
/// 刻意不用 <random> 的分布：它们是实现相关的，跨标准库不稳定，会破坏
/// 「同 seed 同世界」这一前提。噪声同样无状态：按坐标与种子直接求值，
/// 不依赖遍历顺序，否则仅仅调整阶段内的循环次序就会换一个世界。
#pragma once

#include <cstdint>

namespace gamev2::world {

/// PCG-XSH-RR 32。取值一律经 below() 或 unit()，不直接用原始位模式。
class Pcg32
{
public:
    explicit Pcg32(std::uint64_t seed) noexcept;

    std::uint32_t next() noexcept;
    /// [0, n) 无偏。n 为 0 时返回 0，不做除零。
    [[nodiscard]] std::uint32_t below(std::uint32_t n) noexcept;
    /// [0, 1)。
    [[nodiscard]] double unit() noexcept;

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

/// 阶段之间的随机流互不串扰：各阶段用自己的派生 seed 自建 Pcg32。
[[nodiscard]] std::uint64_t stage_seed(std::uint64_t seed,
                                      std::uint32_t stage) noexcept;

/// 梯度噪声取格子点值，无状态。[0, 1]
[[nodiscard]] double value_noise(std::uint64_t seed,
                                 double x, double y) noexcept;
[[nodiscard]] double value_noise3(std::uint64_t seed, double x, double y,
                                  double z) noexcept;

/// 分形叠加，归一化到 [-1, 1]
[[nodiscard]] double fbm(std::uint64_t seed, double x, double y, int octaves,
                        double lacunarity, double gain) noexcept;
[[nodiscard]] double fbm3(std::uint64_t seed, double x, double y, double z,
                         int octaves, double lacunarity, double gain) noexcept;

}  // namespace gamev2::world
```

- [ ] **Step 4: 写 `rng.cpp`**

创建 `src/world/rng.cpp`：

```cpp
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace gamev2::world {
namespace {

constexpr double kInv53 = 1.0 / 9007199254740992.0;
constexpr double kInv24 = 1.0 / 16777216.0;

/// splitmix64：把一个 seed 摊成高质量的 64 位。
std::uint64_t splitmix64(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 坐标混入种子。乘法散列而非异或链：相邻格子的哈希必须看起来无关。
std::uint64_t lattice_hash(std::uint64_t seed, std::int64_t x,
                           std::int64_t y, std::int64_t z) noexcept
{
    std::uint64_t h = seed;
    h ^= static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<std::uint64_t>(z) * 0x165667B19E3779F9ull;
    return splitmix64(h);
}

double lattice(std::uint64_t seed, std::int64_t x, std::int64_t y,
               std::int64_t z) noexcept
{
    return static_cast<double>(lattice_hash(seed, x, y, z) >> 11) * kInv53;
}

/// quintic 插值：一阶与二阶导数在格点处都为 0，避免出现格状痕迹。
double smooth(double t) noexcept
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

std::int64_t floor_to(std::int64_t& base, double v) noexcept
{
    base = static_cast<std::int64_t>(std::floor(v));
    return base;
}

}  // namespace

Pcg32::Pcg32(std::uint64_t seed) noexcept
    : state_(0), inc_((splitmix64(seed) << 1) | 1)
{
    next();
    state_ += seed;
    next();
}

std::uint32_t Pcg32::next() noexcept
{
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    const auto xorshifted =
        static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return std::rotr(xorshifted, rot);
}

std::uint32_t Pcg32::below(std::uint32_t n) noexcept
{
    if (n == 0)
    {
        return 0;
    }
    // 拒绝采样消除取模偏差：2^32 不整除 n 时，低位若干个值出现概率更高。
    const std::uint32_t threshold = (~n + 1u) % n;
    for (;;)
    {
        const std::uint32_t r = next();
        if (r >= threshold)
        {
            return r % n;
        }
    }
}

double Pcg32::unit() noexcept
{
    return static_cast<double>(next() >> 8) * kInv24;
}

std::uint64_t stage_seed(std::uint64_t seed, std::uint32_t stage) noexcept
{
    return splitmix64(seed ^ (static_cast<std::uint64_t>(stage)
                              * 0x9E3779B97F4A7C15ull));
}

double value_noise(std::uint64_t seed, double x, double y) noexcept
{
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    floor_to(x0, x);
    floor_to(y0, y);
    const double fx = smooth(x - static_cast<double>(x0));
    const double fy = smooth(y - static_cast<double>(y0));

    const double v00 = lattice(seed, x0, y0, 0);
    const double v10 = lattice(seed, x0 + 1, y0, 0);
    const double v01 = lattice(seed, x0, y0 + 1, 0);
    const double v11 = lattice(seed, x0 + 1, y0 + 1, 0);

    const double a = v00 + (v10 - v00) * fx;
    const double b = v01 + (v11 - v01) * fx;
    return a + (b - a) * fy;
}

double value_noise3(std::uint64_t seed, double x, double y, double z) noexcept
{
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t z0 = 0;
    floor_to(x0, x);
    floor_to(y0, y);
    floor_to(z0, z);
    const double fx = smooth(x - static_cast<double>(x0));
    const double fy = smooth(y - static_cast<double>(y0));
    const double fz = smooth(z - static_cast<double>(z0));

    double acc = 0.0;
    for (std::int64_t dz = 0; dz < 2; ++dz)
    {
        const double wz = dz == 0 ? 1.0 - fz : fz;
        for (std::int64_t dy = 0; dy < 2; ++dy)
        {
            const double wy = dy == 0 ? 1.0 - fy : fy;
            for (std::int64_t dx = 0; dx < 2; ++dx)
            {
                const double wx = dx == 0 ? 1.0 - fx : fx;
                acc += wx * wy * wz
                       * lattice(seed, x0 + dx, y0 + dy, z0 + dz);
            }
        }
    }
    return acc;
}

double fbm(std::uint64_t seed, double x, double y, int octaves,
           double lacunarity, double gain) noexcept
{
    double sum = 0.0;
    double amp = 1.0;
    double norm = 0.0;
    double freq = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * (value_noise(seed, x * freq, y * freq) * 2.0 - 1.0);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

double fbm3(std::uint64_t seed, double x, double y, double z, int octaves,
            double lacunarity, double gain) noexcept
{
    double sum = 0.0;
    double amp = 1.0;
    double norm = 0.0;
    double freq = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp
               * (value_noise3(seed, x * freq, y * freq, z * freq) * 2.0 - 1.0);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 在 CMakeLists 中登记新文件**

在 `set(WORLDGEN_SOURCES ...)` 内追加 `src/world/rng.cpp`，在
`set(WORLDGEN_TEST_SOURCES ...)` 内追加 `tests/test_world_rng.cpp`。

- [ ] **Step 6: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `12 test(s), 0 failed`

- [ ] **Step 7: 提交**

```bash
git add include/gamev2/world/rng.hpp src/world/rng.cpp \
        tests/test_world_rng.cpp CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 3: 阶段 1 高程与海平面反解

**Files:**
- Create: `include/gamev2/world/generate.hpp`
- Create: `src/world/terrain.cpp`
- Create: `src/world/generate.cpp`
- Create: `tests/test_world_elevation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `World`、`WorldParams`、`fbm`、`stage_seed`（Task 1、2）
- Produces:
  - `void gamev2::world::make_elevation(World&)`，写 `elevation` 与 `sea_level_`
  - `std::expected<World, WorldError> gamev2::world::generate_world(
    const WorldParams&)`，本任务只调用 `make_elevation`，后续任务逐步追加

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_elevation.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::generate_world;
using gamev2::world::make_elevation;
using gamev2::world::World;
using gamev2::world::WorldError;
using gamev2::world::WorldParams;

WorldParams small_params()
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 4242;
    return p;
}

TEST(elevation_stays_in_range_and_hits_target_ocean_share)
{
    World w(small_params());
    make_elevation(w);

    bool in_range = true;
    std::uint32_t ocean = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            in_range = in_range && w.elevation(x, y) <= 100;
            ocean += w.is_ocean(x, y) ? 1u : 0u;
        }
    }
    CHECK(in_range);

    const double ratio = static_cast<double>(ocean) / w.elevation.size();
    CHECK(ratio > 0.55);
    CHECK(ratio < 0.65);
}

TEST(elevation_is_deterministic_and_seed_sensitive)
{
    const WorldParams p = small_params();

    World a(p);
    World b(p);
    make_elevation(a);
    make_elevation(b);

    WorldParams other = p;
    other.seed = 777;
    World c(other);
    make_elevation(c);

    bool ab_same = true;
    bool c_differs = false;
    for (std::uint32_t i = 0; i < a.elevation.size(); ++i)
    {
        ab_same = ab_same && a.elevation.at(i) == b.elevation.at(i);
        c_differs = c_differs || a.elevation.at(i) != c.elevation.at(i);
    }
    CHECK(ab_same);
    CHECK(c_differs);
    CHECK_EQ(a.sea_level(), b.sea_level());
}

TEST(generate_world_rejects_degenerate_params)
{
    WorldParams p;
    p.width = 0;

    auto rejected = generate_world(p);
    CHECK(!rejected.has_value());
    if (!rejected.has_value())
    {
        CHECK(rejected.error() == WorldError::invalid_params);
    }

    CHECK(generate_world(WorldParams{}).has_value());
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `gamev2/world/generate.hpp` 不存在。

- [ ] **Step 3: 写 `generate.hpp`**

创建 `include/gamev2/world/generate.hpp`：

```cpp
/// @file generate.hpp
/// @brief 世界生成的十个阶段与编排入口。
///
/// 阶段之间是严格的单向依赖，而编排顺序只写在 generate_world() 一处：
/// 调换顺序不会报错，只会静默产出错误世界。因此每个阶段的 doxygen 都写明
/// 它读哪些字段，新增阶段时必须同步更新 generate_world()。
#pragma once

#include "gamev2/world/world.hpp"

#include <cstdint>
#include <expected>

namespace gamev2::world {

/// 阶段 1。只读坐标与种子，写 elevation 与反解出的海平面。
void make_elevation(World& world);

/// 汇流量超过该阈值即为河道。调参项，见设计文档 11.2。
inline constexpr std::uint32_t kRiverThreshold = 200;

/// 生成一个世界。参数非法时返回 WorldError::invalid_params。
[[nodiscard]] std::expected<World, WorldError>
generate_world(const WorldParams& params);

}  // namespace gamev2::world
```

- [ ] **Step 4: 写 `terrain.cpp` 的高程部分**

创建 `src/world/terrain.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gamev2::world {
namespace {

/// 噪声特征尺度：8 格一个特征，世界尺度上才看得出大陆轮廓。
constexpr double kElevationFreq = 8.0;
constexpr std::uint32_t kElevationStage = 0;
constexpr double kElevationSpan = 100.0;

/// 由高程分位数反解海平面：取第 ocean_percent 分位以下的格子为海洋。
/// 反解而非固定常量，是为了让海陆比不随种子漂移。
std::uint8_t solve_sea_level(const Grid<std::uint8_t>& elevation,
                             std::uint8_t ocean_percent)
{
    std::array<std::uint32_t, 256> histogram{};
    for (std::uint32_t i = 0; i < elevation.size(); ++i)
    {
        ++histogram[elevation.at(i)];
    }

    const std::uint32_t cut =
        static_cast<std::uint32_t>(elevation.size()) * ocean_percent / 100;
    std::uint32_t seen = 0;
    for (std::uint32_t v = 0; v < histogram.size(); ++v)
    {
        seen += histogram[v];
        if (seen >= cut)
        {
            return static_cast<std::uint8_t>(v);
        }
    }
    return 255;
}

}  // namespace

void make_elevation(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t seed = stage_seed(p.seed, kElevationStage);
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    std::vector<double> raw(n);
    double lo = 1.0;
    double hi = -1.0;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const double v = fbm(seed, x / kElevationFreq, y / kElevationFreq,
                                 3, 2.0, 0.5);
            const std::uint32_t i = world.elevation.index_of(x, y);
            raw[i] = v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }

    const double span = hi - lo;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = span > 0.0 ? (raw[i] - lo) / span : 0.0;
        world.elevation.at(static_cast<std::uint32_t>(i)) =
            static_cast<std::uint8_t>(std::lround(t * kElevationSpan));
    }

    world.sea_level_ = solve_sea_level(world.elevation, p.ocean_percent);
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 写 `generate.cpp`**

创建 `src/world/generate.cpp`：

```cpp
#include "gamev2/world/generate.hpp"

namespace gamev2::world {

std::expected<World, WorldError> generate_world(const WorldParams& params)
{
    if (!params.is_valid())
    {
        return std::unexpected(WorldError::invalid_params);
    }

    World world(params);
    make_elevation(world);
    return world;
}

}  // namespace gamev2::world
```

- [ ] **Step 6: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/terrain.cpp` 与 `src/world/generate.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_elevation.cpp`。

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `15 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add include/gamev2/world/generate.hpp src/world \
        tests/test_world_elevation.cpp CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 4: 阶段 2 降水与雨影

**Files:**
- Create: `tests/test_world_rainfall.cpp`
- Modify: `src/world/terrain.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `elevation`、`is_ocean`、`sea_level`（Task 3 产出）
- Produces: `void gamev2::world::make_rainfall(World&)`，写 `rainfall`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_rainfall.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::make_elevation;
using gamev2::world::make_rainfall;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.seed = seed;
    return p;
}

TEST(rainfall_is_wettest_at_the_coast_and_dries_inland)
{
    World w(params(5));
    make_elevation(w);
    make_rainfall(w);

    double ocean_sum = 0.0;
    double land_sum = 0.0;
    std::uint32_t ocean_n = 0;
    std::uint32_t land_n = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const double v = w.rainfall(x, y);
            if (w.is_ocean(x, y))
            {
                ocean_sum += v;
                ++ocean_n;
            }
            else
            {
                land_sum += v;
                ++land_n;
            }
        }
    }
    CHECK(ocean_n > 100);
    CHECK(land_n > 100);
    CHECK(ocean_sum / ocean_n > land_sum / land_n);
}

TEST(rainfall_is_deterministic)
{
    World a(params(5));
    World b(params(5));
    make_elevation(a);
    make_elevation(b);
    make_rainfall(a);
    make_rainfall(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.rainfall.size(); ++i)
    {
        same = same && a.rainfall.at(i) == b.rainfall.at(i);
    }
    CHECK(same);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `make_rainfall` 未声明。

- [ ] **Step 3: 在 `generate.hpp` 声明该阶段**

在 `void make_elevation(World& world);` 之后追加：

```cpp
/// 阶段 2。读 elevation，写 rainfall。
void make_rainfall(World& world);
```

- [ ] **Step 4: 在 `terrain.cpp` 实现该阶段**

在匿名命名空间内追加常量：

```cpp
constexpr double kRainFreq = 6.0;
constexpr std::uint32_t kRainStage = 1;
/// 降水衰减到零所需的离海格数。
constexpr double kRainReach = 40.0;
/// 雨影累积量除以它得到遮挡比例，120 格高度对应九成遮挡。
constexpr double kShadowGain = 120.0;
```

并在 `make_elevation` 之后追加实现：

```cpp
void make_rainfall(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t seed = stage_seed(p.seed, kRainStage);
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    std::vector<std::uint32_t> distance(n, 0);
    std::vector<std::uint32_t> queue;
    queue.reserve(n);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const double v = fbm(seed, x / kRainFreq, y / kRainFreq,
                                 3, 2.0, 0.5);
            world.rainfall.at(i) =
                static_cast<std::uint8_t>(std::lround((v + 1.0) * 127.5));
            if (world.is_ocean(x, y))
            {
                queue.push_back(i);
            }
        }
    }

    // 多源 BFS 求离海曼哈顿距离。4 邻域而非 8 邻域：与衰减按曼哈顿
    // 距离线性配套，两者必须一致。
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dy[4] = {0, 0, 1, -1};
    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const std::uint32_t i = queue[head];
        const auto x = static_cast<int>(i % p.width);
        const auto y = static_cast<int>(i / p.width);
        for (int d = 0; d < 4; ++d)
        {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            const std::uint32_t j =
                world.elevation.index_of(static_cast<std::uint32_t>(nx),
                                        static_cast<std::uint32_t>(ny));
            if (distance[j] != 0 || world.is_ocean(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny)))
            {
                continue;
            }
            distance[j] = distance[i] + 1;
            queue.push_back(j);
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        const auto d = static_cast<double>(distance[i]);
        const double keep = std::max(0.0, 1.0 - d / kRainReach);
        world.rainfall.at(i) = static_cast<std::uint8_t>(
            std::lround(static_cast<double>(world.rainfall.at(i)) * keep));
    }

    // 雨影：沿固定主导风向逐行扫描，累积沿途抬升，越过高地后后续格
    // 降水被压低。风向固定是简化，DF 的风向随季节变化。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        double shadow = 0.0;
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            if (world.is_ocean(x, y))
            {
                shadow = 0.0;
                continue;
            }
            const double rise =
                static_cast<double>(world.elevation(x, y)) - world.sea_level();
            shadow = std::max(0.0, shadow - 1.0) + std::max(0.0, rise);
            const double shade = 1.0 - std::min(0.9, shadow / kShadowGain);
            const std::uint32_t i = world.elevation.index_of(x, y);
            world.rainfall.at(i) = static_cast<std::uint8_t>(std::lround(
                static_cast<double>(world.rainfall.at(i)) * shade));
        }
    }
}
```

- [ ] **Step 5: 在 `generate.cpp` 接入编排**

在 `make_elevation(world);` 之后追加一行：

```cpp
    make_rainfall(world);
```

- [ ] **Step 6: 在 CMakeLists 中登记测试文件**

`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_rainfall.cpp`。

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `17 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add src/world include/gamev2/world tests/test_world_rainfall.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 5: 阶段 3 温度

**Files:**
- Create: `tests/test_world_temperature.cpp`
- Modify: `src/world/terrain.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `elevation`、`rainfall`、`sea_level`、`is_ocean`（Task 3、4）
- Produces: `void gamev2::world::make_temperature(World&)`，写 `temperature`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_temperature.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>

namespace {

using gamev2::world::make_elevation;
using gamev2::world::make_rainfall;
using gamev2::world::make_temperature;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params()
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.seed = 11;
    return p;
}

TEST(temperature_falls_towards_the_poles)
{
    World w(params());
    make_elevation(w);
    make_rainfall(w);
    make_temperature(w);

    const std::uint32_t north = w.params().height - 1;
    double south_sum = 0.0;
    double north_sum = 0.0;
    for (std::uint32_t x = 0; x < w.params().width; ++x)
    {
        south_sum += w.temperature(x, 0);
        north_sum += w.temperature(x, north);
    }
    CHECK(north_sum < south_sum);
}

TEST(temperature_anticorrelates_with_height_above_sea)
{
    World w(params());
    make_elevation(w);
    make_rainfall(w);
    make_temperature(w);

    // 用协方差而不是分组均值：分组在样本偏斜时会不稳定，而协方差只看
    // 整体趋势，正是高度递减率该被验证的东西。
    const double n = static_cast<double>(w.elevation.size());
    double sx = 0.0;
    double sy = 0.0;
    double sxy = 0.0;
    for (std::uint32_t i = 0; i < w.elevation.size(); ++i)
    {
        const double above = std::max(
            0.0, static_cast<double>(w.elevation.at(i)) - w.sea_level());
        const double t = w.temperature.at(i);
        sx += above;
        sy += t;
        sxy += above * t;
    }
    CHECK(sxy / n - (sx / n) * (sy / n) < 0.0);
}

TEST(temperature_is_deterministic)
{
    World a(params());
    World b(params());
    make_elevation(a);
    make_elevation(b);
    make_rainfall(a);
    make_rainfall(b);
    make_temperature(a);
    make_temperature(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.temperature.size(); ++i)
    {
        same = same && a.temperature.at(i) == b.temperature.at(i);
    }
    CHECK(same);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `make_temperature` 未声明。

- [ ] **Step 3: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 3。读 elevation 与 rainfall，写 temperature。
void make_temperature(World& world);
```

- [ ] **Step 4: 在 `terrain.cpp` 实现该阶段**

在匿名命名空间内追加：

```cpp
constexpr std::uint32_t kTemperatureStage = 2;
/// 每升高一个高程单位降多少温度 byte。最大落差约 50 个单位，
/// 对应约 32°C，与真实垂直递减率的量级一致。
constexpr double kLapseRate = 0.9;
/// 干燥区额外的温度惩罚上限。
constexpr double kDryPenalty = 20.0;
/// 赤道基线温度 byte，对应约 +30°C。
constexpr double kTropicalBaseline = 198.0;
/// 极地基线温度 byte，对应约 -25°C。
constexpr double kPolarBaseline = 42.0;
```

并在 `make_rainfall` 之后追加实现：

```cpp
void make_temperature(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    std::vector<double> baseline(n, 0.0);
    std::vector<double> ocean_sum(p.height, 0.0);
    std::vector<std::uint32_t> ocean_count(p.height, 0);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        const double v =
            (static_cast<double>(y) + 0.5) / static_cast<double>(p.height);
        // v=0.5 为赤道，0 与 1 为两极。平方而非线性：赤道到中纬的温差
        // 大于中纬到极地。
        const double lat = std::abs(v - 0.5) * 2.0;
        const double t = kPolarBaseline
                         + (kTropicalBaseline - kPolarBaseline)
                                   * (1.0 - lat * lat);
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            baseline[i] = t;
            if (world.is_ocean(x, y))
            {
                ocean_sum[y] += t;
                ++ocean_count[y];
            }
        }
    }

    // 海洋向该纬度均值靠拢。没有这一步，内陆会被高度递减压得比同纬度
    // 海洋低一大截，而海洋会被纬度高估，呈现出明显失真的结果。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        if (ocean_count[y] == 0)
        {
            continue;
        }
        const double mean = ocean_sum[y] / ocean_count[y];
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            if (!world.is_ocean(x, y))
            {
                continue;
            }
            baseline[world.elevation.index_of(x, y)] = mean;
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        const double above = std::max(
            0.0, static_cast<double>(world.elevation.at(i)) - world.sea_level());
        const double dry =
            1.0 - static_cast<double>(world.rainfall.at(i)) / 255.0;
        const double t = baseline[i] - kLapseRate * above
                         - kDryPenalty * dry;
        world.temperature.at(i) = static_cast<std::uint8_t>(
            std::lround(std::clamp(t, 0.0, 255.0)));
    }
}
```

- [ ] **Step 5: 在 `generate.cpp` 接入编排**

追加一行 `make_temperature(world);`

- [ ] **Step 6: 在 CMakeLists 中登记测试文件**

`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_temperature.cpp`。

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `20 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add src/world include/gamev2/world tests/test_world_temperature.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 6: 阶段 4 上半 —— 洼地填平、下游指针、汇流量与河道

**Files:**
- Create: `include/gamev2/world/hydrology.hpp`
- Create: `src/world/hydrology.cpp`
- Create: `tests/test_world_hydrology.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `elevation`、`is_ocean`（Task 3）
- Produces:
  - `void gamev2::world::make_hydrology(World&)`，本任务写 `downstream`、`flow`、
    `river_width`；Task 7 追加 `lake_depth` 与 `drainage`
  - `struct gamev2::world::FilledDem { std::vector<uint8_t> level; }` —
    阶段 4 内部量的载体。刻意不入 `World`：只有阶段 4 自己消费它，
    现在存进去是无人使用的字段（见设计文档 11.6）

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_hydrology.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::kRiverThreshold;
using gamev2::world::kNoDownstream;
using gamev2::world::make_elevation;
using gamev2::world::make_hydrology;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.seed = seed;
    return p;
}

TEST(hydrology_downstream_never_cycles)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 下游择取用的是严格小于，填平高度沿指针严格递减，环在数学上不可能
    // 出现；本测试是对这条推理的兜底核对。
    bool terminates = true;
    for (std::uint32_t start = 0; start < w.downstream.size(); start += 13)
    {
        std::uint32_t cur = start;
        std::uint32_t steps = 0;
        while (cur != kNoDownstream && steps < w.downstream.size())
        {
            cur = w.downstream.at(cur);
            ++steps;
        }
        terminates = terminates && steps < w.downstream.size();
    }
    CHECK(terminates);
}

TEST(hydrology_stops_at_the_ocean)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool oceans_are_terminal = true;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            if (!w.is_ocean(x, y))
            {
                continue;
            }
            const std::uint32_t i = w.elevation.index_of(x, y);
            oceans_are_terminal =
                oceans_are_terminal && w.downstream.at(i) == kNoDownstream;
        }
    }
    CHECK(oceans_are_terminal);
}

TEST(hydrology_flow_grows_downstream)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool monotonic = true;
    std::uint32_t river_cells = 0;
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        const std::uint32_t d = w.downstream.at(i);
        if (d != kNoDownstream)
        {
            monotonic = monotonic && w.flow.at(d) >= w.flow.at(i);
        }
        river_cells += w.river_width.at(i) > 0 ? 1u : 0u;
    }
    CHECK(monotonic);
    CHECK(river_cells > 0);
}

TEST(hydrology_river_width_follows_flow)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool consistent = true;
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        const std::uint32_t f = w.flow.at(i);
        const std::uint8_t width = w.river_width.at(i);
        consistent = consistent && ((f >= kRiverThreshold) == (width > 0));
        if (width > 0)
        {
            consistent = consistent && width <= 5;
        }
    }
    CHECK(consistent);
}

TEST(hydrology_is_deterministic)
{
    World a(params(3));
    World b(params(3));
    make_elevation(a);
    make_elevation(b);
    make_hydrology(a);
    make_hydrology(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.flow.size(); ++i)
    {
        same = same && a.flow.at(i) == b.flow.at(i);
        same = same && a.downstream.at(i) == b.downstream.at(i);
        same = same && a.river_width.at(i) == b.river_width.at(i);
    }
    CHECK(same);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `make_hydrology` 未声明。

- [ ] **Step 3: 写 `hydrology.hpp`**

创建 `include/gamev2/world/hydrology.hpp`：

```cpp
/// @file hydrology.hpp
/// @brief 阶段 4 的内部量。刻意不放进 World：只有阶段 4 消费它。
#pragma once

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 洼地填平后的无洼 DEM。design 文档 11.6 记录了为何不把它存进 World。
struct FilledDem
{
    std::vector<std::uint8_t> level;
};

}  // namespace gamev2::world
```

- [ ] **Step 4: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 4。读 elevation，写 downstream、flow、river_width、lake_depth、
/// drainage。水文排在群系之前，是因为群系判定要用它产出的排水量。
void make_hydrology(World& world);
```

- [ ] **Step 5: 写 `hydrology.cpp` 的填平与下游**

创建 `src/world/hydrology.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/hydrology.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

constexpr int kDx4[4] = {1, -1, 0, 0};
constexpr int kDy4[4] = {0, 0, 1, -1};
constexpr int kDx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

bool in_bounds(const WorldParams& p, int x, int y) noexcept
{
    return x >= 0 && y >= 0 && x < static_cast<int>(p.width)
           && y < static_cast<int>(p.height);
}

/// priority-flood：反复取出当前最低的格子，用它的高度去填平邻格，
/// 直到全图被处理完。得到的 DEM 没有洼地，因此最陡下降总能走到边界。
void fill_depressions(const World& world, FilledDem& out)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    out.level.assign(n, 0);
    std::vector<bool> done(n, false);

    using Entry = std::pair<std::uint8_t, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

    // 边界格与海洋格是「水流出图外」的天然出口，作为填平的种子。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const bool edge = x == 0 || y == 0 || x == p.width - 1
                              || y == p.height - 1;
            if (edge || world.is_ocean(x, y))
            {
                heap.emplace(world.elevation.at(i), i);
            }
        }
    }

    while (!heap.empty())
    {
        const auto [level, i] = heap.top();
        heap.pop();
        if (done[i])
        {
            continue;
        }
        done[i] = true;
        out.level[i] = level;

        const auto x = static_cast<int>(i % p.width);
        const auto y = static_cast<int>(i / p.width);
        for (int d = 0; d < 4; ++d)
        {
            const int nx = x + kDx4[d];
            const int ny = y + kDy4[d];
            if (!in_bounds(p, nx, ny))
            {
                continue;
            }
            const std::uint32_t j = world.elevation.index_of(
                static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
            if (done[j])
            {
                continue;
            }
            heap.emplace(std::max(world.elevation.at(j), level), j);
        }
    }
}

/// 最陡下降。择取条件是严格小于，因此填平高度沿指针严格递减，环不可能
/// 出现；等高邻居取先遍历到的那个，遍历顺序固定故结果仍确定。
void build_downstream(World& world, const FilledDem& dem)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.is_ocean(x, y))
            {
                // 水在海里汇集，不该再往海底流。
                world.downstream.at(i) = kNoDownstream;
                continue;
            }

            std::uint8_t best = dem.level[i];
            std::uint32_t target = kNoDownstream;
            for (int d = 0; d < 8; ++d)
            {
                const int nx = static_cast<int>(x) + kDx8[d];
                const int ny = static_cast<int>(y) + kDy8[d];
                if (!in_bounds(p, nx, ny))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (dem.level[j] < best)
                {
                    best = dem.level[j];
                    target = j;
                }
            }
            world.downstream.at(i) = target;
        }
    }
}

/// 汇流量从高到低累加：下游的填平高度必定更低，所以按高程降序处理时
/// 下游一定已经算完，可以放心把流量交给它。计数排序，高程只有 256 桶。
void accumulate_flow(World& world, const FilledDem& dem)
{
    const auto n = static_cast<std::size_t>(world.downstream.size());

    std::array<std::uint32_t, 256> histogram{};
    for (const std::uint8_t v : dem.level)
    {
        ++histogram[v];
    }

    std::array<std::uint32_t, 256> cursor{};
    std::uint32_t run = 0;
    for (std::size_t v = 0; v < histogram.size(); ++v)
    {
        cursor[v] = run;
        run += histogram[v];
    }

    std::vector<std::uint32_t> order(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        order[cursor[dem.level[i]]++] = static_cast<std::uint32_t>(i);
    }

    std::vector<std::uint32_t> flow(n, 1);
    for (std::size_t k = n; k-- > 0;)
    {
        const std::uint32_t i = order[k];
        const std::uint32_t d = world.downstream.at(i);
        if (d != kNoDownstream)
        {
            flow[d] += flow[i];
        }
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        world.flow.at(static_cast<std::uint32_t>(i)) = flow[i];
    }
}

std::uint8_t river_width_of(std::uint32_t flow) noexcept
{
    if (flow < kRiverThreshold)
    {
        return 0;
    }
    if (flow < 2 * kRiverThreshold)
    {
        return 1;
    }
    if (flow < 6 * kRiverThreshold)
    {
        return 2;
    }
    if (flow < 20 * kRiverThreshold)
    {
        return 3;
    }
    if (flow < 60 * kRiverThreshold)
    {
        return 4;
    }
    return 5;
}

void mark_rivers(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.is_ocean(x, y))
            {
                world.river_width.at(i) = 0;
                continue;
            }
            world.river_width.at(i) = river_width_of(world.flow.at(i));
        }
    }
}

}  // namespace

void make_hydrology(World& world)
{
    FilledDem dem;
    fill_depressions(world, dem);
    build_downstream(world, dem);
    accumulate_flow(world, dem);
    mark_rivers(world);
}

}  // namespace gamev2::world
```

- [ ] **Step 6: 在 `generate.cpp` 接入编排**

追加一行 `make_hydrology(world);`

- [ ] **Step 7: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/hydrology.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_hydrology.cpp`。

- [ ] **Step 8: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `25 test(s), 0 failed`

- [ ] **Step 9: 提交**

```bash
git add include/gamev2/world src/world tests/test_world_hydrology.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 7: 阶段 4 下半 —— 湖泊与排水量

**Files:**
- Modify: `src/world/hydrology.cpp`
- Modify: `tests/test_world_hydrology.cpp`
- Modify: `include/gamev2/world/generate.hpp`

**Interfaces:**
- Consumes: `downstream`、`flow`、`elevation`、`river_width`（Task 6）
- Produces: `lake_depth` 与 `drainage`。`make_hydrology` 签名不变

- [ ] **Step 1: 写失败的测试**

在 `tests/test_world_hydrology.cpp` 的 `TEST(hydrology_is_deterministic)` 之前插入：

```cpp
TEST(hydrology_terminates_in_ocean_or_lake)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool ends_in_water = true;
    std::uint32_t lakes = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (w.lake_depth.at(i) > 0)
            {
                ++lakes;
            }
            if (w.downstream.at(i) != kNoDownstream)
            {
                continue;
            }
            ends_in_water = ends_in_water
                           && (w.is_ocean(x, y) || w.lake_depth.at(i) > 0);
        }
    }
    CHECK(ends_in_water);
    CHECK(lakes > 0);
}

TEST(lakes_have_the_worst_drainage_on_the_map)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool lakes_are_wettest = true;
    bool some_cell_is_dry = false;
    for (std::uint32_t i = 0; i < w.drainage.size(); ++i)
    {
        if (w.lake_depth.at(i) > 0)
        {
            lakes_are_wettest = lakes_are_wettest && w.drainage.at(i) == 0;
        }
        some_cell_is_dry = some_cell_is_dry || w.drainage.at(i) == 100;
    }
    CHECK(lakes_are_wettest);
    CHECK(some_cell_is_dry);
}

TEST(drainage_rises_away_from_rivers)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 河道自身的排水量不可能好过周边：它就是最湿的地方之一。
    bool rivers_are_wet = true;
    std::uint32_t rivers = 0;
    for (std::uint32_t i = 0; i < w.drainage.size(); ++i)
    {
        if (w.river_width.at(i) == 0)
        {
            continue;
        }
        ++rivers;
        rivers_are_wet = rivers_are_wet && w.drainage.at(i) <= 50;
    }
    CHECK(rivers > 0);
    CHECK(rivers_are_wet);
}
```

并在该文件的 `}  // namespace` 之前不需要改动。测试用到 `kNoDownstream`，
已在文件顶部导入。

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
./build/worldgen_tests.exe
```

Expected: 失败，`lakes > 0` 不成立（`lake_depth` 全为 0）。

- [ ] **Step 3: 在 `hydrology.cpp` 实现湖泊**

在匿名命名空间内追加：

```cpp
/// 汇流量低于该值的洼地只淤积，不成湖。
constexpr std::uint32_t kLakeMinFlow = kRiverThreshold;
```

并在 `mark_rivers` 之后追加：

```cpp
/// 洼地成湖，水位铺满整个洼底。只标最低那一格会让湖缩成一个点，
/// 也撑不起排水量扩散所需的源。
void carve_lakes(World& world, const FilledDem& dem)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<std::uint32_t> seeds;

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.is_ocean(x, y)
                || world.downstream.at(i) != kNoDownstream)
            {
                continue;
            }
            const int depth =
                static_cast<int>(dem.level[i]) - world.elevation.at(i);
            if (depth <= 0 || world.flow.at(i) < kLakeMinFlow)
            {
                continue;
            }
            seeds.push_back(i);
            world.lake_depth.at(i) =
                static_cast<std::uint8_t>(std::min(depth, 255));
        }
    }

    std::vector<bool> seen(n, false);
    std::vector<std::uint32_t> queue;
    for (const std::uint32_t seed : seeds)
    {
        // 逐湖重置：两个湖的洼底可以部分重叠，共用一个 seen 会让后一个
        // 的水面缺一块。
        std::ranges::fill(seen, false);
        const std::uint8_t level = dem.level[seed];
        queue.clear();
        queue.push_back(seed);
        seen[seed] = true;

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            const std::uint32_t i = queue[head];
            const auto x = static_cast<int>(i % p.width);
            const auto y = static_cast<int>(i / p.width);
            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + kDx4[d];
                const int ny = y + kDy4[d];
                if (!in_bounds(p, nx, ny))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (seen[j] || world.elevation.at(j) >= level)
                {
                    continue;
                }
                seen[j] = true;
                world.lake_depth.at(j) = static_cast<std::uint8_t>(
                    level - world.elevation.at(j));
                queue.push_back(j);
            }
        }
    }
}
```

在 `hydrology.cpp` 的 include 列表中追加 `<ranges>`。

- [ ] **Step 4: 在 `hydrology.cpp` 实现排水量**

在 `carve_lakes` 之后追加：

```cpp
/// 排水量是 DF 的群系判定输入之一，但它由泛洪平原计算得出、算法未公开。
/// 这里以水体为源做一次多源 BFS 扩散作为等价近似：离水越远越干。
void derive_drainage(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<std::uint8_t> wetness(n, 0);
    std::vector<std::uint32_t> queue;
    queue.reserve(n);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            std::uint8_t start = 0;
            if (world.lake_depth.at(i) > 0)
            {
                start = 100;
            }
            else if (world.river_width.at(i) > 0)
            {
                start = static_cast<std::uint8_t>(
                    50 * world.river_width.at(i) / 5);
            }
            if (start == 0)
            {
                continue;
            }
            wetness[i] = start;
            queue.push_back(i);
        }
    }

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const std::uint32_t i = queue[head];
        const auto x = static_cast<int>(i % p.width);
        const auto y = static_cast<int>(i / p.width);
        for (int d = 0; d < 4; ++d)
        {
            const int nx = x + kDx4[d];
            const int ny = y + kDy4[d];
            if (!in_bounds(p, nx, ny))
            {
                continue;
            }
            const std::uint32_t j = world.elevation.index_of(
                static_cast<std::uint32_t>(nx),
                static_cast<std::uint32_t>(ny));
            if (wetness[j] != 0)
            {
                continue;
            }
            // 每向外一格衰减一档，下限 0。
            const auto next = static_cast<std::uint8_t>(wetness[i] - 1);
            if (next == 0)
            {
                continue;
            }
            wetness[j] = next;
            queue.push_back(j);
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        world.drainage.at(i) = static_cast<std::uint8_t>(100 - wetness[i]);
    }
}
```

在 `make_hydrology` 中追加两行调用：

```cpp
    mark_rivers(world);
    carve_lakes(world, dem);
    derive_drainage(world);
```

- [ ] **Step 5: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `28 test(s), 0 failed`

- [ ] **Step 6: 提交**

```bash
git add src/world/hydrology.cpp tests/test_world_hydrology.cpp
```

向用户报告，**等待提交指令**。

---

### Task 8: 阶段 5 群系

**Files:**
- Create: `include/gamev2/world/tables.hpp`
- Create: `src/world/biomes.cpp`
- Create: `tests/test_world_biomes.cpp`
- Modify: `include/gamev2/world/enums.hpp`（删掉 `taiga`）
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `elevation`、`sea_level`、`drainage`、`rainfall`、`temperature`、
  `lake_depth`
- Produces:
  - `constexpr std::size_t rain_band(uint8_t)`、`drainage_band(uint8_t)`
  - `constexpr ElevClass elev_class_of(uint8_t elevation, uint8_t sea_level)`
  - `constexpr Biome select_biome(ElevClass, uint8_t drainage, uint8_t rain,
    uint8_t temp)`
  - `void gamev2::world::make_biomes(World&)`，写 `biome`

- [ ] **Step 1: 删除不可达的 `taiga` 枚举**

在 `include/gamev2/world/enums.hpp` 的 `Biome` 中删掉 `taiga,` 一行，并把
`kBiomeCount` 改为 `20`。

**理由**：基础群系表与温度覆写都不产生 `taiga`，留着一个没人能生成的枚举值
会让「枚举即可能取值」这个约定失效。

- [ ] **Step 2: 写失败的测试**

创建 `tests/test_world_biomes.cpp`：

```cpp
#include "gamev2/world/tables.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::Biome;
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
    CHECK_EQ(rain_band(255), std::size_t{7});
    CHECK_EQ(drainage_band(0), std::size_t{0});
    CHECK_EQ(drainage_band(100), std::size_t{7});
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

TEST(elevation_decides_ocean_and_mountain_first)
{
    const auto hot = temperature_of(20);
    CHECK(select_biome(gamev2::world::ElevClass::ocean, 0, 0, hot)
          == Biome::ocean);
    CHECK(select_biome(gamev2::world::ElevClass::mountain, 100, 255, hot)
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
    const auto hot = temperature_of(kTropicalCelsius);
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
```

在文件顶部补一行 using：

```cpp
using gamev2::world::celsius_of;
```

- [ ] **Step 3: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `gamev2/world/tables.hpp` 不存在。

- [ ] **Step 4: 写 `tables.hpp`**

创建 `include/gamev2/world/tables.hpp`：

```cpp
/// @file tables.hpp
/// @brief 群系查表与矿脉定义表。
///
/// 放头文件并用 inline constexpr，是为了让测试能引用同一份数据核对，
/// 而不是复制一份会与实现漂移的副本。
#pragma once

#include "gamev2/world/enums.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::world {

/// 降水分档，0 最干到 7 最湿。
[[nodiscard]] constexpr std::size_t rain_band(std::uint8_t rain) noexcept
{
    return std::min<std::size_t>(7, rain / 32);
}

/// 排水分档，0 排水最差到 7 排水最好。
[[nodiscard]] constexpr std::size_t drainage_band(std::uint8_t d) noexcept
{
    return std::min<std::size_t>(7, d / 13);
}

/// DF 的判定顺序：先看高程带，再谈气候。上四分之一为山岳，DF 只有山岳
/// 一个高程群系，因此不再细分峰顶。
[[nodiscard]] constexpr ElevClass elev_class_of(std::uint8_t elevation,
                                                std::uint8_t sea_level) noexcept
{
    if (elevation < sea_level)
    {
        return ElevClass::ocean;
    }
    const int span = 100 - static_cast<int>(sea_level);
    const int above = static_cast<int>(elevation) - sea_level;
    if (above * 4 >= span * 3)
    {
        return ElevClass::mountain;
    }
    if (above * 2 >= span)
    {
        return ElevClass::hills;
    }
    return ElevClass::lowland;
}

/// 基础群系表。行是降水分档，列是排水分档，取值全部来自 DF 群系文档。
/// 湿地在低排水列、森林在高排水列，是这张表最容易核对的两条。
inline constexpr std::array<std::array<Biome, 8>, 8> kBaseBiome{{
    {  // rain 0，最干
        Biome::rocky_wasteland, Biome::sand_desert, Biome::sand_desert,
        Biome::sand_desert, Biome::sand_desert, Biome::badlands,
        Biome::badlands, Biome::badlands },
    {  // rain 1
        Biome::rocky_wasteland, Biome::sand_desert, Biome::sand_desert,
        Biome::sand_desert, Biome::badlands, Biome::badlands,
        Biome::badlands, Biome::shrubland },
    {  // rain 2
        Biome::shrubland, Biome::savanna, Biome::savanna,
        Biome::savanna, Biome::savanna, Biome::savanna,
        Biome::shrubland, Biome::shrubland },
    {  // rain 3
        Biome::shrubland, Biome::grassland, Biome::grassland,
        Biome::grassland, Biome::grassland, Biome::shrubland,
        Biome::shrubland, Biome::shrubland },
    {  // rain 4
        Biome::marshland, Biome::grassland, Biome::grassland,
        Biome::grassland, Biome::meadowland, Biome::meadowland,
        Biome::pine_barrens, Biome::pine_barrens },
    {  // rain 5
        Biome::swamp, Biome::swamp, Biome::temperate_broadleaf,
        Biome::grassland, Biome::meadowland, Biome::pine_barrens,
        Biome::pine_barrens, Biome::pine_barrens },
    {  // rain 6
        Biome::swamp, Biome::swamp, Biome::swamp, Biome::temperate_broadleaf,
        Biome::temperate_broadleaf, Biome::temperate_coniferous,
        Biome::temperate_coniferous, Biome::temperate_coniferous },
    {  // rain 7，最湿
        Biome::swamp, Biome::swamp, Biome::swamp, Biome::swamp,
        Biome::temperate_broadleaf, Biome::temperate_coniferous,
        Biome::temperate_coniferous, Biome::temperate_broadleaf },
}};

/// DF 的三步判定：高程先定海洋与山岳，基础群系由排水量与降水量决定，
/// 温度最后覆写成苔原、冰川或热带变体。
[[nodiscard]] constexpr Biome select_biome(ElevClass cls,
                                           std::uint8_t drainage,
                                           std::uint8_t rain,
                                           std::uint8_t temp) noexcept
{
    if (cls == ElevClass::ocean)
    {
        return Biome::ocean;
    }
    if (cls == ElevClass::mountain)
    {
        return Biome::mountain;
    }

    if (celsius_of(temp) <= kFreezingCelsius)
    {
        return drainage < kGlacierDrainage ? Biome::tundra : Biome::glacier;
    }

    const Biome base = kBaseBiome[rain_band(rain)][drainage_band(drainage)];

    if (celsius_of(temp) >= kTropicalCelsius)
    {
        switch (base)
        {
        case Biome::temperate_coniferous:
        case Biome::pine_barrens:
            return Biome::tropical_coniferous;
        case Biome::temperate_broadleaf:
            return Biome::tropical_moist_broadleaf;
        case Biome::savanna:
        case Biome::shrubland:
            return Biome::tropical_dry_broadleaf;
        default:
            break;
        }
    }

    return base;
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 5。读 elevation、rainfall、temperature、drainage，写 biome。
void make_biomes(World& world);
```

- [ ] **Step 6: 写 `biomes.cpp`**

创建 `src/world/biomes.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"

namespace gamev2::world {

void make_biomes(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.lake_depth.at(i) > 0)
            {
                world.biome.at(i) = Biome::lake;
                continue;
            }
            world.biome.at(i) =
                select_biome(elev_class_of(world.elevation.at(i),
                                           world.sea_level()),
                             world.drainage.at(i), world.rainfall.at(i),
                             world.temperature.at(i));
        }
    }
}

}  // namespace gamev2::world
```

- [ ] **Step 7: 在 `generate.cpp` 接入编排**

追加一行 `make_biomes(world);`

- [ ] **Step 8: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/biomes.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_biomes.cpp`。

- [ ] **Step 9: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `35 test(s), 0 failed`

- [ ] **Step 10: 提交**

```bash
git add include/gamev2/world src/world tests/test_world_biomes.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 9: 阶段 6 土壤

**Files:**
- Create: `src/world/strata.cpp`
- Create: `tests/test_world_strata.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `biome`、`rainfall`（Task 5、8）
- Produces: `void gamev2::world::make_soils(World&)`，写 `soil_depth`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_strata.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::Biome;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;

/// 跑到指定阶段为止的流水线，测试只关心自己之后的字段。
World build(std::uint32_t seed)
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
    return w;
}

bool is_sediment(Stone s)
{
    return s == Stone::sandstone || s == Stone::shale
           || s == Stone::limestone || s == Stone::gravel
           || s == Stone::clay;
}

TEST(soil_depth_stays_in_range)
{
    const World w = build(21);
    bool in_range = true;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        in_range = in_range && w.soil_depth.at(i) <= 3;
    }
    CHECK(in_range);
}

TEST(soil_is_absent_on_bare_ground)
{
    const World w = build(21);
    bool bare = true;
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        const Biome b = w.biome.at(i);
        const bool expected_bare = b == Biome::ocean || b == Biome::lake
                                   || b == Biome::sand_desert
                                   || b == Biome::rocky_wasteland
                                   || b == Biome::badlands
                                   || b == Biome::glacier
                                   || b == Biome::mountain;
        if (expected_bare)
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
    for (std::uint32_t i = 0; i < w.soil_depth.size(); ++i)
    {
        deepest = std::max(deepest,
                           static_cast<std::uint32_t>(w.soil_depth.at(i)));
    }
    CHECK(deepest >= 2);
}

}  // namespace
```

在文件顶部补两行：

```cpp
#include <algorithm>
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `make_soils` 未声明。

- [ ] **Step 3: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 6。读 biome 与 rainfall，写 soil_depth。
void make_soils(World& world);
```

- [ ] **Step 4: 写 `strata.cpp` 的土壤部分**

创建 `src/world/strata.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

    const int adjusted = static_cast<int>(base) + (rain > 200 ? 1 : 0)
                          - (rain < 60 ? 1 : 0);
    return static_cast<std::uint8_t>(std::clamp(adjusted, 0, 3));
}

}  // namespace

void make_soils(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t i = 0; i < world.soil_depth.size(); ++i)
    {
        world.soil_depth.at(i) =
            soil_depth_of(world.biome.at(i), world.rainfall.at(i));
    }
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 在 `generate.cpp` 接入编排**

追加一行 `make_soils(world);`

- [ ] **Step 6: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/strata.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_strata.cpp`。

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `38 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add src/world include/gamev2/world tests/test_world_strata.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 10: 阶段 7 岩层与含水层顶面

**Files:**
- Modify: `src/world/strata.cpp`
- Modify: `tests/test_world_strata.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`

**Interfaces:**
- Consumes: `biome`、`rainfall`、`elevation`（Task 5、8、9）
- Produces: `void gamev2::world::make_strata(World&)`，写 `stone` 与 `aquifer_top`

- [ ] **Step 1: 写失败的测试**

在 `tests/test_world_strata.cpp` 的 `}  // namespace` 之前插入：

```cpp
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
    std::uint32_t volcanic = 0;
    for (std::uint32_t i = 0; i < w.biome.size(); ++i)
    {
        const Stone s = w.stone[deepest].at(i);
        volcanic += (s == Stone::igneous || s == Stone::igneous_intrusive)
                        ? 1u
                        : 0u;
    }
    CHECK(volcanic > 0);
}

TEST(aquifer_top_is_zero_on_dry_biomes)
{
    const World w = build(21);
    bool ok = true;
    std::uint32_t wet = 0;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        const Biome b = w.biome.at(i);
        const bool dry = b == Biome::ocean || b == Biome::lake
                         || b == Biome::sand_desert
                         || b == Biome::rocky_wasteland
                         || b == Biome::badlands || b == Biome::glacier
                         || b == Biome::mountain;
        if (dry)
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

TEST(aquifer_top_sits_below_the_shallowest_layer)
{
    const World w = build(21);
    bool ok = true;
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        const auto top = static_cast<int>(w.aquifer_top.at(i));
        ok = ok && (top == 0 || top < static_cast<int>(w.depth()));
    }
    CHECK(ok);
}
```

并把该文件里的 `build` 补上土壤阶段：

```cpp
    gamev2::world::make_biomes(w);
    gamev2::world::make_soils(w);
    return w;
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
./build/worldgen_tests.exe
```

Expected: 失败，`w.stone[0]` 仍是构造函数填的 `Stone::granite`。

- [ ] **Step 3: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 7。读 biome 与 rainfall，写 stone 与 aquifer_top。
/// 含水层的填充推迟到阶段 10，那时洞室已定型。
void make_strata(World& world);
```

- [ ] **Step 4: 在 `strata.cpp` 实现岩层**

以下代码块整体**替换** `strata.cpp` 现有的匿名命名空间闭合处
（`}  // namespace` 及其之前的空行），即把该块插到 `soil_depth_of` 之后、
原 `}  // namespace` 之前。块内自带 `}  // namespace`，不要再留一个旧的。

```cpp
constexpr std::uint32_t kRegionStage = 6;
constexpr std::uint32_t kOffsetStage = 7;
constexpr std::uint32_t kIntrusionStage = 8;
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
    Pcg32 rng(stage_seed(p.seed, kIntrusionStage));
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

}  // namespace
```

把 Step 4 插入的匿名命名空间闭合之后，追加 `make_strata` 实现：

```cpp
void make_strata(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t region_seed = stage_seed(p.seed, kRegionStage);
    const std::uint64_t offset_seed = stage_seed(p.seed, kOffsetStage);
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
                const auto moved = static_cast<std::uint32_t>(std::clamp(
                    static_cast<std::int64_t>(z) + shift, std::int64_t{0},
                    levels - 1));
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
```

- [ ] **Step 5: 在 `strata.cpp` 实现含水层顶面**

在 `make_strata` 之前插入：

```cpp
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
        world.aquifer_top.at(i) =
            static_cast<std::uint8_t>(std::clamp(base, 1, depth - 1));
    }
}
```

`make_strata` 内的 `assign_aquifer_top(world)` 调用保留。

- [ ] **Step 6: 在 `generate.cpp` 接入编排**

追加一行 `make_strata(world);`

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `42 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add src/world include/gamev2/world tests/test_world_strata.cpp
```

向用户报告，**等待提交指令**。

---

### Task 11: 阶段 8 洞穴

**Files:**
- Create: `src/world/caverns.cpp`
- Create: `tests/test_world_caverns.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无（只依赖 `WorldParams` 的尺寸与种子）
- Produces: `void gamev2::world::make_caverns(World&)`，写 `cavern`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_caverns.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::World;
using gamev2::world::WorldParams;

World build()
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = 6;
    p.seed = 33;
    World w(p);
    gamev2::world::make_caverns(w);
    return w;
}

TEST(caverns_exist_underground_and_never_at_the_surface)
{
    const World w = build();
    std::uint32_t cells = 0;
    bool surface_clean = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.cavern[z].size(); ++i)
        {
            if (!w.cavern[z].at(i))
            {
                continue;
            }
            ++cells;
            if (z == 0)
            {
                surface_clean = false;
            }
        }
    }
    CHECK(cells > 0);
    CHECK(surface_clean);
}

TEST(caverns_stay_away_from_the_sky)
{
    // 地下全部岩体封闭时，洞室无处可去，此时不该有洞室而不是把地表掏空。
    WorldParams p;
    p.width = 32;
    p.height = 32;
    p.z_levels = 1;
    p.seed = 5;
    World w(p);
    gamev2::world::make_caverns(w);

    bool empty = true;
    for (std::uint32_t i = 0; i < w.cavern[0].size(); ++i)
    {
        empty = empty && !w.cavern[0].at(i);
    }
    CHECK(empty);
}

TEST(caverns_are_deterministic)
{
    const World a = build();
    const World b = build();
    bool same = true;
    for (std::uint32_t z = 0; z < a.cavern.size(); ++z)
    {
        for (std::uint32_t i = 0; i < a.cavern[z].size(); ++i)
        {
            same = same && a.cavern[z].at(i) == b.cavern[z].at(i);
        }
    }
    CHECK(same);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `make_caverns` 未声明。

- [ ] **Step 3: 在 `generate.hpp` 声明该阶段**

追加：

```cpp
/// 阶段 8。读尺寸与种子，写 cavern。不挖穿 z=0。
void make_caverns(World& world);
```

- [ ] **Step 4: 写 `caverns.cpp`**

创建 `src/world/caverns.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

constexpr std::uint32_t kCavernStage = 8;
constexpr double kCavernFill = 0.45;
constexpr int kCavernIterations = 4;
/// 小于该体素数的空腔视为碎屑，DF 的洞穴系统里不保留这类噪点。
constexpr std::uint32_t kMinCavernVolume = 64;

constexpr int kDx6[6] = {1, -1, 0, 0, 0, 0};
constexpr int kDy6[6] = {0, 0, 1, -1, 0, 0};
constexpr int kDz6[6] = {0, 0, 0, 0, 1, -1};

struct Cell
{
    std::int64_t x;
    std::int64_t y;
    std::int64_t z;
};

bool solid_neighbours(const World& world, Cell c)
{
    const WorldParams& p = world.params();
    std::int64_t n = 0;
    for (int d = 0; d < 6; ++d)
    {
        const auto x = c.x + kDx6[d];
        const auto y = c.y + kDy6[d];
        const auto z = c.z + kDz6[d];
        if (z < 0 || z >= static_cast<std::int64_t>(p.z_levels))
        {
            continue;
        }
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(p.width)
            || y >= static_cast<std::int64_t>(p.height))
        {
            continue;
        }
        const std::uint32_t i = world.elevation.index_of(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        n += world.cavern[static_cast<std::size_t>(z)].at(i) ? 0 : 1;
    }
    return n;
}

/// 洪泛标记空腔连通域，剔除体素数不足的碎屑。
void drop_fragments(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<bool> seen(p.z_levels * n, false);
    std::vector<Cell> component;
    std::deque<Cell> queue;

    for (std::uint32_t z = 1; z < p.z_levels; ++z)
    {
        for (std::uint32_t y = 0; y < p.height; ++y)
        {
            for (std::uint32_t x = 0; x < p.width; ++x)
            {
                const std::uint32_t i = world.elevation.index_of(x, y);
                if (!world.cavern[z].at(i)
                    || seen[static_cast<std::size_t>(z) * n + i])
                {
                    continue;
                }
                component.clear();
                queue.clear();
                queue.push_back({x, y, z});
                seen[static_cast<std::size_t>(z) * n + i] = true;
                while (!queue.empty())
                {
                    const Cell c = queue.front();
                    queue.pop_front();
                    component.push_back(c);
                    for (int d = 0; d < 6; ++d)
                    {
                        const auto nx = c.x + kDx6[d];
                        const auto ny = c.y + kDy6[d];
                        const auto nz = c.z + kDz6[d];
                        if (nz < 1
                            || nz >= static_cast<std::int64_t>(p.z_levels)
                            || nx < 0 || ny < 0
                            || nx >= static_cast<std::int64_t>(p.width)
                            || ny >= static_cast<std::int64_t>(p.height))
                        {
                            continue;
                        }
                        const std::uint32_t j = world.elevation.index_of(
                            static_cast<std::uint32_t>(nx),
                            static_cast<std::uint32_t>(ny));
                        const auto key = static_cast<std::size_t>(nz) * n + j;
                        if (world.cavern[static_cast<std::size_t>(nz)].at(j)
                            && !seen[key])
                        {
                            seen[key] = true;
                            queue.push_back({nx, ny, nz});
                        }
                    }
                }
                if (component.size() < kMinCavernVolume)
                {
                    for (const Cell c : component)
                    {
                        const std::uint32_t j = world.elevation.index_of(
                            static_cast<std::uint32_t>(c.x),
                            static_cast<std::uint32_t>(c.y));
                        world.cavern[static_cast<std::size_t>(c.z)].at(j) =
                            false;
                    }
                }
            }
        }
    }
}

}  // namespace

void make_caverns(World& world)
{
    const WorldParams& p = world.params();
    if (p.z_levels < 2)
    {
        return;
    }

    Pcg32 rng(stage_seed(p.seed, kCavernStage));
    for (std::uint32_t z = 1; z < p.z_levels; ++z)
    {
        for (std::uint32_t y = 0; y < p.height; ++y)
        {
            for (std::uint32_t x = 0; x < p.width; ++x)
            {
                const std::uint32_t i = world.elevation.index_of(x, y);
                world.cavern[z].at(i) = rng.unit() < kCavernFill;
            }
        }
    }

    // 元胞自动机把随机点磨成成片的洞室：多数邻居实心则变实心，
    // 多数邻居为空则变空，边界上的格子保持原样。
    for (int iter = 0; iter < kCavernIterations; ++iter)
    {
        auto next = world.cavern;
        for (std::uint32_t z = 1; z < p.z_levels; ++z)
        {
            for (std::uint32_t y = 0; y < p.height; ++y)
            {
                for (std::uint32_t x = 0; x < p.width; ++x)
                {
                    const std::uint32_t i = world.elevation.index_of(x, y);
                    const std::int64_t n = solid_neighbours(world, {x, y, z});
                    next[z].at(i) = n >= 5 ? true : (n <= 2 ? false
                                                          : world.cavern[z].at(i));
                }
            }
        }
        world.cavern = std::move(next);
    }

    drop_fragments(world);
}

}  // namespace gamev2::world
```

在 include 列表中追加 `<utility>`（`std::move`）。

- [ ] **Step 5: 在 `generate.cpp` 接入编排**

追加一行 `make_caverns(world);`

- [ ] **Step 6: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/caverns.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_caverns.cpp`。

- [ ] **Step 7: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `45 test(s), 0 failed`

- [ ] **Step 8: 提交**

```bash
git add src/world include/gamev2/world tests/test_world_caverns.cpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 12: 阶段 9 矿脉与阶段 10 含水层

**Files:**
- Modify: `include/gamev2/world/tables.hpp`
- Create: `src/world/deposits.cpp`
- Create: `tests/test_world_deposits.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `src/world/generate.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `stone`、`cavern`（Task 10、11）、`aquifer_top`（Task 10）
- Produces:
  - `struct DepositSpec` 与 `kDepositSpecs`（加进 `tables.hpp`）
  - `void make_deposits(World&)`，写 `deposit`
  - `void finalize_aquifers(World&)`，写 `aquifer`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_deposits.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"
#include "test_framework.hpp"

#include <algorithm>

namespace {

using gamev2::world::Deposit;
using gamev2::world::DepositSpec;
using gamev2::world::kDepositSpecs;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;

World build()
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = 6;
    p.seed = 77;
    World w(p);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_soils(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_caverns(w);
    gamev2::world::make_deposits(w);
    gamev2::world::finalize_aquifers(w);
    return w;
}

const DepositSpec* spec_of(Deposit kind)
{
    for (const auto& spec : kDepositSpecs)
    {
        if (spec.kind == kind)
        {
            return &spec;
        }
    }
    return nullptr;
}

TEST(deposits_exist_and_never_sit_inside_a_cavern)
{
    const World w = build();
    std::uint32_t cells = 0;
    bool clear_of_caverns = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            if (w.deposit[z].at(i) == Deposit::none)
            {
                continue;
            }
            ++cells;
            clear_of_caverns = clear_of_caverns && !w.cavern[z].at(i);
        }
    }
    CHECK(cells > 0);
    CHECK(clear_of_caverns);
}

TEST(every_deposit_honours_its_host_rock)
{
    const World w = build();
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            const Deposit kind = w.deposit[z].at(i);
            const DepositSpec* spec = spec_of(kind);
            if (spec == nullptr)
            {
                continue;
            }
            const Stone host = w.stone[z].at(i);
            ok = ok
                 && (std::ranges::find(spec->hosts.begin(), spec->hosts.end(),
                                       host)
                     != spec->hosts.end());
        }
    }
    CHECK(ok);
}

TEST(deposits_stay_underground)
{
    const World w = build();
    bool below_surface = true;
    for (std::uint32_t i = 0; i < w.deposit[0].size(); ++i)
    {
        below_surface = below_surface && w.deposit[0].at(i) == Deposit::none;
    }
    CHECK(below_surface);
}

TEST(aquifers_sit_below_their_top_and_outside_caverns)
{
    const World w = build();
    bool ok = true;
    std::uint32_t cells = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            const auto top = static_cast<std::uint32_t>(w.aquifer_top.at(i));
            const auto first = top == 0 ? 0u : top / w.params().z_cell_depth;
            for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
            {
                if (!w.aquifer[z].at(i))
                {
                    continue;
                }
                ++cells;
                ok = ok && top != 0 && z >= first;
                ok = ok && !w.cavern[z].at(i);
            }
        }
    }
    CHECK(cells > 0);
    CHECK(ok);
}

TEST(aquifers_absent_where_the_top_is_zero)
{
    const World w = build();
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.aquifer[z].size(); ++i)
        {
            if (w.aquifer[z].at(i))
            {
                ok = ok && w.aquifer_top.at(i) != 0;
            }
        }
    }
    CHECK(ok);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build
```

Expected: 编译失败，报 `kDepositSpecs` 未声明。

- [ ] **Step 3: 把矿脉定义表加进 `tables.hpp`**

在 `tables.hpp` 的 include 列表追加 `#include <span>`，并在
`}  // namespace gamev2::world` 之前追加：

```cpp
struct DepositSpec
{
    Deposit kind;
    /// 每张地图的撒点尝试次数，不是概率。次数写死才能让不同矿种的
    /// 相对密度可预期。
    std::uint16_t vein_count;
    /// 只到这个 z 层为止，越深的矿越稀有。
    std::uint8_t max_z;
    /// 是否允许大矿脉。DF 对部分矿种禁止大脉，本表照此收窄。
    bool allow_large;
    std::span<const Stone> hosts;
};

inline constexpr std::array kIgneousHosts{
    Stone::granite, Stone::basalt, Stone::slate,
    Stone::igneous, Stone::igneous_intrusive};

inline constexpr std::array kSedimentaryHosts{
    Stone::limestone, Stone::sandstone, Stone::shale, Stone::marble};

inline constexpr std::array kCoalHosts{
    Stone::shale, Stone::sandstone, Stone::gravel};

inline constexpr std::array kGemHosts{
    Stone::marble, Stone::granite, Stone::igneous};

inline constexpr std::array<DepositSpec, 12> kDepositSpecs{{
    {Deposit::copper, 90, 5, true, kIgneousHosts},
    {Deposit::iron, 140, 5, true, kSedimentaryHosts},
    {Deposit::gold, 30, 5, false, kIgneousHosts},
    {Deposit::silver, 45, 5, false, kIgneousHosts},
    {Deposit::tin, 50, 4, false, kSedimentaryHosts},
    {Deposit::lead, 60, 4, false, kSedimentaryHosts},
    {Deposit::zinc, 55, 4, false, kSedimentaryHosts},
    {Deposit::coal, 180, 5, true, kCoalHosts},
    {Deposit::diamond, 18, 5, false, kGemHosts},
    {Deposit::sapphire, 14, 5, false, kGemHosts},
    {Deposit::emerald, 14, 5, false, kGemHosts},
    {Deposit::adamantine, 6, 5, true, kGemHosts},
}};
```

- [ ] **Step 4: 在 `generate.hpp` 声明两个阶段**

追加：

```cpp
/// 阶段 9。读 stone 与 cavern，写 deposit。矿脉不落进洞室。
void make_deposits(World& world);

/// 阶段 10。读 aquifer_top、cavern，写 aquifer。洞室不蓄水，因此这一步
/// 必须排在 make_caverns 之后、make_deposits 之后由调用方保证。
void finalize_aquifers(World& world);
```

- [ ] **Step 5: 写 `deposits.cpp`**

创建 `src/world/deposits.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

namespace gamev2::world {
namespace {

constexpr std::uint32_t kDepositStage = 9;

/// 三个全局尺寸档。DF 是每矿种各自的尺寸表，本轮没有那份原始数据，
/// 统一成三档比凭空编 36 条更诚实。
std::uint32_t vein_cells(bool allow_large, Pcg32& rng)
{
    const std::uint32_t roll = rng.below(10);
    if (allow_large || roll >= 7)
    {
        return 30 + rng.below(31);
    }
    if (roll >= 3)
    {
        return 10 + rng.below(11);
    }
    return 3 + rng.below(4);
}

/// 由目标体素数反解等效球半径，比手调半径更贴近「多少格」这个直觉。
int radius_for(std::uint32_t cells)
{
    const double r =
        std::cbrt(static_cast<double>(cells) * 3.0 / (4.0 * std::numbers::pi));
    return std::max(1, static_cast<int>(std::ceil(r)));
}

void fill_vein(World& world, Deposit kind, std::uint32_t cx, std::uint32_t cy,
               std::uint32_t cz, int radius, std::span<const Stone> hosts)
{
    const WorldParams& p = world.params();
    const int reach = radius * radius;
    for (int dz = -radius; dz <= radius; ++dz)
    {
        const auto z = static_cast<int>(cz) + dz;
        if (z < 1 || z >= static_cast<int>(p.z_levels))
        {
            continue;
        }
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                if (dx * dx + dy * dy + dz * dz > reach)
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
                const auto zz = static_cast<std::size_t>(z);
                if (world.cavern[zz].at(i))
                {
                    continue;
                }
                const Stone host = world.stone[zz].at(i);
                if (std::ranges::find(hosts, host) == hosts.end())
                {
                    continue;
                }
                world.deposit[zz].at(i) = kind;
            }
        }
    }
}

}  // namespace

void make_deposits(World& world)
{
    const WorldParams& p = world.params();
    if (p.z_levels < 2)
    {
        return;
    }

    Pcg32 rng(stage_seed(p.seed, kDepositStage));
    for (const auto& spec : kDepositSpecs)
    {
        const auto max_z = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(spec.max_z), p.z_levels - 1);
        for (std::uint32_t n = 0; n < spec.vein_count; ++n)
        {
            const std::uint32_t cz = 1 + rng.below(max_z);
            fill_vein(world, spec.kind, rng.below(p.width),
                      rng.below(p.height), cz,
                      radius_for(vein_cells(spec.allow_large, rng)),
                      spec.hosts);
        }
    }
}

void finalize_aquifers(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const auto top = static_cast<std::uint32_t>(world.aquifer_top.at(i));
            if (top == 0)
            {
                continue;
            }
            // 含水层是逐格深度的场，但存储按 z 层，因此取顶面所在的那一层
            // 作为起点，含水层自该层整体开始。
            const auto first =
                static_cast<std::uint32_t>(top / p.z_cell_depth);
            for (std::uint32_t z = first; z < p.z_levels; ++z)
            {
                world.aquifer[z].at(i) = !world.cavern[z].at(i);
            }
        }
    }
}

}  // namespace gamev2::world
```

`deposits.cpp` 需要 `#include "gamev2/world/rng.hpp"`（用 `Pcg32` 与
`stage_seed`），把它加进 include 列表。

- [ ] **Step 6: 在 `generate.cpp` 接入编排**

追加两行：

```cpp
    make_deposits(world);
    finalize_aquifers(world);
```

- [ ] **Step 7: 在 CMakeLists 中登记新文件**

`WORLDGEN_SOURCES` 追加 `src/world/deposits.cpp`；
`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_deposits.cpp`。

- [ ] **Step 8: 构建并运行测试确认通过**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `50 test(s), 0 failed`

- [ ] **Step 9: 提交**

```bash
git add include/gamev2/world/tables.hpp src/world \
        tests/test_world_deposits.cpp include/gamev2/world/generate.hpp \
        CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 13: 全流程确定性

**Files:**
- Create: `tests/test_world_determinism.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `generate_world`（Task 1、3）与全部十个阶段
- Produces: 无。这是设计文档里第 1 条测试，独立成任务是因为它是唯一一条
  能一次覆盖全部阶段的断言，坏掉时能立刻定位到是哪个阶段引入了非确定性

- [ ] **Step 1: 写测试**

创建 `tests/test_world_determinism.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::generate_world;
using gamev2::world::World;
using gamev2::world::WorldParams;

template<typename T>
bool same_grid(const Grid<T>& a, const Grid<T>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::uint32_t i = 0; i < a.size(); ++i)
    {
        if (a.at(i) != b.at(i))
        {
            return false;
        }
    }
    return true;
}

template<typename T>
bool same_layers(const std::vector<Grid<T>>& a,
                 const std::vector<Grid<T>>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t z = 0; z < a.size(); ++z)
    {
        if (!same_grid(a[z], b[z]))
        {
            return false;
        }
    }
    return true;
}

bool same_world(const World& a, const World& b)
{
    return same_grid(a.elevation, b.elevation)
           && same_grid(a.rainfall, b.rainfall)
           && same_grid(a.temperature, b.temperature)
           && same_grid(a.biome, b.biome)
           && same_grid(a.downstream, b.downstream)
           && same_grid(a.flow, b.flow)
           && same_grid(a.river_width, b.river_width)
           && same_grid(a.lake_depth, b.lake_depth)
           && same_grid(a.drainage, b.drainage)
           && same_grid(a.soil_depth, b.soil_depth)
           && same_grid(a.aquifer_top, b.aquifer_top)
           && same_layers(a.stone, b.stone)
           && same_layers(a.deposit, b.deposit)
           && same_layers(a.cavern, b.cavern)
           && same_layers(a.aquifer, b.aquifer)
           && a.sea_level() == b.sea_level();
}

TEST(full_pipeline_is_identical_for_the_same_seed)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 5;
    p.seed = 2026;

    const auto a = generate_world(p);
    const auto b = generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (a && b)
    {
        CHECK(same_world(*a, *b));
    }
}

TEST(different_seeds_produce_different_worlds)
{
    WorldParams a_params;
    a_params.width = 64;
    a_params.height = 64;
    a_params.z_levels = 5;

    WorldParams b_params = a_params;
    b_params.seed = a_params.seed + 1;

    const auto a = generate_world(a_params);
    const auto b = generate_world(b_params);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (a && b)
    {
        CHECK(!same_world(*a, *b));
    }
}

}  // namespace
```

- [ ] **Step 2: 运行测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

Expected: `52 test(s), 0 failed`

- [ ] **Step 3: 在 CMakeLists 中登记测试文件**

`WORLDGEN_TEST_SOURCES` 追加 `tests/test_world_determinism.cpp`。

- [ ] **Step 4: 提交**

```bash
git add tests/test_world_determinism.cpp CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

### Task 14: demo —— 统计与 PPM 导出

**Files:**
- Create: `src/demo/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `generate_world` 与全部十个阶段
- Produces: 目标 `worldgen_demo`，可执行文件

**为什么单独成任务**：阈值与分档是经验值，只能看着导出结果调。设计文档
11.2 把这一步列为既定工作，没有这个 demo 就没有反馈回路。

- [ ] **Step 1: 写 `main.cpp`**

创建 `src/demo/main.cpp`：

```cpp
/// @file main.cpp
/// @brief 世界生成的统计与 PPM 导出。生成器的阈值是经验值，只能看着图调，
///        这个程序就是那条反馈回路。
#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gamev2::world;

constexpr std::array<std::string_view, kBiomeCount> kBiomeNames{
    "ocean", "lake", "mountain", "marshland", "swamp",
    "temperate_coniferous", "temperate_broadleaf", "tropical_coniferous",
    "tropical_dry_broadleaf", "tropical_moist_broadleaf", "grassland",
    "meadowland", "savanna", "shrubland", "pine_barrens", "sand_desert",
    "rocky_wasteland", "badlands", "tundra", "glacier",
};

static_assert(static_cast<std::size_t>(Biome::glacier) + 1 == kBiomeCount);
static_assert(kBiomeNames.back() == "glacier");

constexpr std::array<std::array<std::uint8_t, 3>, kBiomeCount> kBiomeColor{{
    {20, 40, 120}, {30, 90, 160}, {130, 130, 130}, {90, 110, 70},
    {70, 100, 60}, {40, 80, 60}, {30, 100, 40}, {20, 120, 50},
    {150, 130, 60}, {20, 140, 40}, {120, 170, 70}, {150, 190, 90},
    {190, 180, 90}, {160, 150, 90}, {60, 100, 60}, {220, 200, 130},
    {150, 130, 110}, {170, 110, 90}, {200, 210, 210}, {235, 240, 245},
}};

constexpr std::array<std::string_view, 12> kStoneNames{
    "sandstone", "shale", "limestone", "granite", "basalt", "slate",
    "gravel", "marble", "clay", "ice", "igneous", "igneous_intrusive",
};

constexpr std::array<std::string_view, 13> kDepositNames{
    "none", "copper", "iron", "gold", "silver", "tin", "lead", "zinc",
    "coal", "diamond", "sapphire", "emerald", "adamantine",
};

using Rgb = std::vector<std::uint8_t>;

void write_ppm(const std::filesystem::path& path, std::uint32_t width,
               std::uint32_t height, const Rgb& rgb)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::println(stderr, "写不了 {}", path.string());
        return;
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
}

/// 蓝到白的五段渐变。够看出趋势即可，不必追求视觉准确。
Rgb ramp(std::uint8_t value)
{
    constexpr std::array<std::array<std::uint8_t, 3>, 5> kRamp{{
        {0, 0, 40}, {0, 90, 160}, {40, 150, 60}, {210, 200, 60}, {255, 255, 255},
    }};
    const double t = static_cast<double>(value) / 255.0 * (kRamp.size() - 1);
    const auto lo = static_cast<std::size_t>(t);
    const auto hi = std::min(lo + 1, kRamp.size() - 1);
    const double f = t - static_cast<double>(lo);

    Rgb rgb;
    rgb.reserve(3);
    for (std::size_t c = 0; c < 3; ++c)
    {
        const double mixed = kRamp[lo][c] * (1.0 - f) + kRamp[hi][c] * f;
        rgb.push_back(static_cast<std::uint8_t>(mixed));
    }
    return rgb;
}

/// 岩性是枚举，取值 0..12，直接喂给 ramp 会全落在最低一段看不出差别，
/// 因此放大到整个渐变范围。
constexpr std::uint8_t kStoneRampScale = 20;

template<typename T>
void export_scalars(const World& world,
                    const std::filesystem::path& dir,
                    std::string_view label, const Grid<T>& grid)
{
    // 按本图最大值归一化：elevation 的上限是 100 而 flow 能到几万，
    // 固定刻度会让其中一张全黑。
    std::uint32_t peak = 1;
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        peak = std::max(peak, static_cast<std::uint32_t>(grid.at(i)));
    }

    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(grid.size()) * 3);
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        const auto scaled = static_cast<std::uint8_t>(
            static_cast<std::uint32_t>(grid.at(i)) * 255 / peak);
        const Rgb px = ramp(scaled);
        rgb.insert(rgb.end(), px.begin(), px.end());
    }
    write_ppm(dir / (std::string(label) + ".ppm"), world.params().width,
              world.params().height, rgb);
}

void append_flag(Rgb& target, bool on)
{
    const Rgb px = ramp(on ? 255 : 0);
    target.insert(target.end(), px.begin(), px.end());
}

void export_layers(const World& world, const std::filesystem::path& dir)
{
    // 三张图分别写循环而不是拼成一张表：Grid<Stone> 与 Grid<bool> 元素
    // 类型不同，混在一个容器里既推不出公共类型也没法统一取值。
    for (std::uint32_t z = 1; z < world.params().z_levels; ++z)
    {
        const auto n = world.stone[z].size();
        Rgb stone;
        Rgb cavern;
        Rgb aquifer;
        stone.reserve(static_cast<std::size_t>(n) * 3);
        cavern.reserve(static_cast<std::size_t>(n) * 3);
        aquifer.reserve(static_cast<std::size_t>(n) * 3);

        for (std::uint32_t i = 0; i < n; ++i)
        {
            const Rgb rock = ramp(static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(world.stone[z].at(i))
                * kStoneRampScale));
            stone.insert(stone.end(), rock.begin(), rock.end());
            append_flag(cavern, world.cavern[z].at(i));
            append_flag(aquifer, world.aquifer[z].at(i));
        }

        const std::string suffix = "_z" + std::to_string(z) + ".ppm";
        write_ppm(dir / ("stone" + suffix), world.params().width,
                  world.params().height, stone);
        write_ppm(dir / ("cavern" + suffix), world.params().width,
                  world.params().height, cavern);
        write_ppm(dir / ("aquifer" + suffix), world.params().width,
                  world.params().height, aquifer);
    }
}

void print_surface_stats(const World& world)
{
    std::array<std::uint32_t, kBiomeCount> biomes{};
    std::uint32_t rivers = 0;
    std::uint8_t widest = 0;
    std::uint32_t lake_cells = 0;
    std::uint32_t lake_bodies = 0;
    std::uint32_t aquifer_top_sum = 0;
    std::uint32_t aquifer_count = 0;

    for (std::uint32_t y = 0; y < world.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < world.params().width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            ++biomes[static_cast<std::size_t>(world.biome.at(i))];
            if (world.river_width.at(i) > 0)
            {
                ++rivers;
                widest = std::max(widest, world.river_width.at(i));
            }
            if (world.lake_depth.at(i) > 0)
            {
                ++lake_cells;
                const std::uint32_t d = world.downstream.at(i);
                lake_bodies += (d == kNoDownstream) ? 1u : 0u;
            }
            if (world.aquifer_top.at(i) != 0)
            {
                aquifer_top_sum += world.aquifer_top.at(i);
                ++aquifer_count;
            }
        }
    }

    std::println("地表 {} x {}，海平面 {}", world.params().width,
                 world.params().height, world.sea_level());
    std::println("群系面积占比：");
    for (std::size_t i = 0; i < kBiomeCount; ++i)
    {
        if (biomes[i] == 0)
        {
            continue;
        }
        const double pct = 100.0 * biomes[i] / world.elevation.size();
        std::println("  {:<26} {:>8} 格  {:>5.1f}%", kBiomeNames[i],
                     biomes[i], pct);
    }
    std::println("河流：{} 格，最宽 {}，湖泊 {} 格 / {} 处", rivers, widest,
                 lake_cells, lake_bodies);
    if (aquifer_count > 0)
    {
        std::println("平均含水层顶面：{:.1f} 格深",
                     static_cast<double>(aquifer_top_sum) / aquifer_count);
    }
    else
    {
        std::println("平均含水层顶面：无含水层");
    }
}

void print_underground_stats(const World& world)
{
    std::array<std::uint32_t, kStoneNames.size()> stones{};
    std::array<std::uint32_t, kDepositNames.size()> deposits{};
    std::uint32_t cavern_cells = 0;
    std::uint32_t aquifer_cells = 0;
    std::uint32_t total = 0;

    for (std::uint32_t z = 0; z < world.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < world.stone[z].size(); ++i)
        {
            ++stones[static_cast<std::size_t>(world.stone[z].at(i))];
            ++deposits[static_cast<std::size_t>(world.deposit[z].at(i))];
            cavern_cells += world.cavern[z].at(i) ? 1u : 0u;
            aquifer_cells += world.aquifer[z].at(i) ? 1u : 0u;
            ++total;
        }
    }

    std::println("地下 {} 层，共 {} 格", world.params().z_levels, total);
    std::println("岩性占比：");
    for (std::size_t i = 0; i < kStoneNames.size(); ++i)
    {
        if (stones[i] == 0)
        {
            continue;
        }
        std::println("  {:<20} {:>8} 格  {:>5.1f}%", kStoneNames[i], stones[i],
                     100.0 * stones[i] / total);
    }
    std::println("矿脉：");
    for (std::size_t i = 1; i < kDepositNames.size(); ++i)
    {
        if (deposits[i] == 0)
        {
            continue;
        }
        std::println("  {:<14} {:>8} 格", kDepositNames[i], deposits[i]);
    }
    std::println("洞室 {:.1f}%，含水层 {:.1f}%",
                 100.0 * cavern_cells / total,
                 100.0 * aquifer_cells / total);
}

void export_biome(const World& world, const std::filesystem::path& dir)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(world.biome.size()) * 3);
    for (std::uint32_t i = 0; i < world.biome.size(); ++i)
    {
        const auto& c = kBiomeColor[static_cast<std::size_t>(world.biome.at(i))];
        rgb.push_back(c[0]);
        rgb.push_back(c[1]);
        rgb.push_back(c[2]);
    }
    write_ppm(dir / "biome.ppm", world.params().width, world.params().height,
              rgb);
}

}  // namespace

int main(int argc, char** argv)
{
    WorldParams params;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--seed" && i + 1 < argc)
        {
            params.seed = std::stoull(argv[++i]);
        }
        else if (arg == "--size" && i + 1 < argc)
        {
            params.width = std::stoul(argv[++i]);
            params.height = params.width;
        }
        else
        {
            std::println(stderr, "用法: worldgen_demo [--seed N] [--size N]");
            return 2;
        }
    }

    auto generated = generate_world(params);
    if (!generated)
    {
        std::println(stderr, "生成失败：参数非法");
        return 1;
    }
    const World& world = *generated;

    const std::filesystem::path dir = "agent-temp/worldgen";
    std::filesystem::create_directories(dir);

    print_surface_stats(world);
    print_underground_stats(world);

    export_scalars(world, dir, "elevation", world.elevation);
    export_scalars(world, dir, "rainfall", world.rainfall);
    export_scalars(world, dir, "temperature", world.temperature);
    export_scalars(world, dir, "drainage", world.drainage);
    export_scalars(world, dir, "flow", world.flow);
    export_biome(world, dir);
    export_layers(world, dir);

    std::println("图像已导出到 {}", dir.string());
    return 0;
}
```

- [ ] **Step 2: 在 CMakeLists 中登记 demo**

在 `add_executable(worldgen_tests ...)` 之后追加：

```cmake
add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE worldgen)
```

- [ ] **Step 3: 构建并运行 demo**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 链接成功，无告警。

- [ ] **Step 4: 运行 demo 并检查输出**

Run: `./build/worldgen_demo.exe --seed 1`

Expected: 打印群系占比、河流湖泊统计、地下岩性与矿脉统计；并在
`agent-temp/worldgen/` 下生成 `elevation.ppm`、`rainfall.ppm`、
`temperature.ppm`、`drainage.ppm`、`flow.ppm`、`biome.ppm`、
`stone_z1..z5.ppm`、`cavern_z1..z5.ppm`、`aquifer_z1..z5.ppm`。

**这一步是本计划的调参环节**：打开 `elevation.ppm` 与 `biome.ppm`，
检查大陆轮廓是否连贯、河网是否合理、群系分布是否符合直觉。若不符，
调整 `terrain.cpp` / `hydrology.cpp` 顶部的常量区（设计文档 11.2 列出的
就是这些），重跑本步。**不要**为了让图好看去改测试断言。

- [ ] **Step 5: 跑全量测试确认仍然全绿**

Run: `./build/worldgen_tests.exe`

Expected: `52 test(s), 0 failed`

- [ ] **Step 6: 提交**

```bash
git add src/demo CMakeLists.txt
```

向用户报告，**等待提交指令**。

---

## 完成标准

全部十四个任务完成后：

- `./build/worldgen_tests.exe` 全绿，且 `ecs_tests.exe` 未受影响
- `./build/worldgen_demo.exe --seed 1` 在 `agent-temp/worldgen/` 下产出
  全部图像与统计
- `git status` 只包含预期文件，`agent-temp/` 未被跟踪










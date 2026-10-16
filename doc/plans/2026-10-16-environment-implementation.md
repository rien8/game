# 环境模块实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 gamev2 实现环境模块：64×64 格级气候场与资源场、季内连续 K 步
摄食、由环境承载力决定的季末子代预算，并通过 ECS 适配层把实测摄入喂给
遗传层的存活判定。

**Architecture:** `environment/` 是纯内核，不 include 任何 `gamev2/genetics/`
或 `gamev2/ecs/` 头文件。气候与资源都是格级场，遍历 4096 格与个体数解耦。
资源在季内被真实吃穿，`feeding` 经 `Feeding` ECS 组件传给遗传层；出生数由
`capacity - occupants`（承载力）封顶，取代遗传层 `N × Fecundity` 的线性外推。
`ecs/adapt/` 是唯一同时看到两边的地方。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-16-environment-design.md`

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 输出用 `std::print` / `std::println`（`<print>`），不用 printf / cout
- 容器操作用 `std::ranges` 算法与视图
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- 任何一行不得超过 90 个字符，超长行必须手动折行
- Windows 环境下通过 `pwsh` 调用终端
- **不得执行 `git commit`，提交时机由用户决定。** 每个任务的最后一步是
  「向用户报告，等待提交决定」，不是提交本身
- `environment/` 下任何文件不得 include `gamev2/genetics/` 或 `gamev2/ecs/`
  的头文件。这是可测性要求：气候、资源、承载力的断言必须在没有 `Genome`、
  没有 `Phenotype`、没有 `RandomStreams` 的前提下写成

构建命令（在仓库根目录 `E:\gamev2` 执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

运行测试（三个可执行文件都要跑）：

```powershell
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

每个 Expected 只要求 `0 failed`。**用例总数在每次任务末尾都要记下来，
下一次任务的 Expected 用它核对**——用例数变了说明有人误删了断言。

已知工具链限制：`std::views::sort` 在该 STL 上不可用，需改用 `std::ranges::sort`。

**工作树 diff 代替 commit 区间**：AGENTS.md 禁止 `git commit`，因此每次
review 用 `git diff HEAD -- <本任务涉及的文件>>` 导出一份 review 包，
而不是 `review-package BASE HEAD`。`.superpowers/sdd/progress.md` 里
记录的是上一份计划的 ledger，本计划的进度另起一节。

### 既有 API 事实（实施时直接依赖，不要重新推断）

| 事实 | 位置 |
|---|---|
| `World::add<T>(e, args...)` 返回 `std::expected<void, EcsError>` | `world.hpp:79` |
| `World::get_if<T>(e)` 缺组件返回 `nullptr` | `world.hpp:77` |
| `World::for_each_entity(f)` 按槽位下标序遍历，回调收到 `EntityId` | `world.hpp:119` |
| `CommandBuffer::create(out, components...)` 的组件**必须一次性给全**，事后 `add` 不成立 | `command_buffer.hpp:41` |
| `World` 没有 `command_buffer()` 方法，测试里 `CommandBuffer cmd; cmd.flush(w);` | — |
| `BreedingSystem` 现构造签名 `(table, species, config, root_seed, season_index)` | `genetics_systems.hpp:81` |
| `AgingSystem` 可默认构造 | `test_genetics_systems.cpp:144` |
| `progeny_for(pool)` 是 `N × 平均繁殖力`，已被 `kMaxProgenyPerPool` 截断 | `genetics_systems.cpp:66` |
| `settle` 把 `ParentPool::progeny == 0` 按 1 处理 | `population.cpp:104` |
| `tests/test_genetics_systems.cpp` 的匿名 namespace 有 `all_loci()` / `neutral_table()` / `default_config()` / `Harness` | 同文件 :24-104 |

### 允许修改的既有文件

| 文件 | 改动 | 见 |
|---|---|---|
| `CMakeLists.txt` | 追加 environment 库与 environment_tests 目标；ASan 源列表补两项 | Task 1 |
| `include/gamev2/genetics/environment.hpp` | 删除 | Task 1 |
| `include/gamev2/genetics/expression.hpp` | include 路径 | Task 1 |
| `include/gamev2/genetics/survival.hpp` | include 路径 | Task 1 |
| `include/gamev2/genetics/breeding.hpp` | include 路径 + 一处过期注释 | Task 1 |
| `include/gamev2/genetics/population.hpp` | include 路径 + 追加 `feedings` 字段 | Task 1 / Task 6 |
| `include/gamev2/ecs/adapt/genetics_systems.hpp` | include 路径 + `BreedingTarget` / 新构造函数 / 新生个体落位 | Task 1 / Task 8 |
| `src/genetics/expression.cpp` | include 路径 | Task 1 |
| `src/genetics/population.cpp` | include 路径 + `feeding` 取值 | Task 1 / Task 6 |
| `src/ecs/adapt/genetics_systems.cpp` | include 路径 + 站点分组 + feeding + 新生个体落位 | Task 1 / Task 8 |
| `tests/test_genetics_*.cpp` | include 路径 | Task 1 |
| `tests/test_genetics_population.cpp` | 新增 1 条测试 | Task 6 |
| `tests/test_genetics_systems.cpp` | 新增 2 条测试 | Task 8 |

## 文件结构

| 路径 | 职责 |
|---|---|
| `include/gamev2/environment/config.hpp` | EnvironmentConfig |
| `include/gamev2/environment/climate.hpp` | EnvironmentSnapshot / CellIndex / ClimateCell / ClimateField |
| `src/environment/climate.cpp` | 气候场构造与季节求值 |
| `include/gamev2/environment/resource.hpp` | ResourceCell / ResourceField |
| `src/environment/resource.cpp` | 承载力构造、再生、扣减、占用计数 |
| `include/gamev2/environment/sites.hpp` | Forager / BreedingSite / run_season / build_sites |
| `src/environment/sites.cpp` | 季内摄食循环与承载力预算 |
| `include/gamev2/ecs/adapt/components.hpp` | Position / Feeding / BreedingTarget |
| `include/gamev2/ecs/adapt/environment_systems.hpp` | EnvironmentWorld / EnvironmentSystem |
| `src/ecs/adapt/environment_systems.cpp` | 适配层：取组件 → 调纯函数 → 写回 |
| `tests/test_environment_smoke.cpp` | 快照默认值 |
| `tests/test_environment_climate.cpp` | 气候确定性与季节演化 |
| `tests/test_environment_resource.cpp` | 再生不越界与占用计数 |
| `tests/test_environment_sites.cpp` | 摄食、顺序无关、承载力、站点稳定 |
| `tests/test_environment_systems.cpp` | 适配层装配与端到端规模 |

---

## Task 1: EnvironmentSnapshot 搬家 + 构建骨架

只做机械搬家，不引入任何新行为。独立成任务是因为它可以被单独否决：
若否决，Task 2 起的代码路径不变。

**Files:**
- Create: `include/gamev2/environment/climate.hpp`
- Delete: `include/gamev2/genetics/environment.hpp`
- Modify: `CMakeLists.txt`
- Modify: 既有头源文件的 include（见下）
- Test: `tests/test_environment_smoke.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `gamev2::environment::EnvironmentSnapshot`（三个字段与原
  `gamev2::genetics::EnvironmentSnapshot` 完全相同）。后续全部任务都从
  `gamev2/environment/climate.hpp` 取它

- [ ] **Step 1: 建立新头文件**

Create `include/gamev2/environment/climate.hpp`:

```cpp
/// @file climate.hpp
/// @brief 气候场：EnvironmentSnapshot 是格级气候在某一季的取值，
///        CellIndex 是格的下标，ClimateField 持有逐格的基准值并按季节求值。
#pragma once

namespace gamev2::environment {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
/// 三项环境量的取值范围约定：temperature 是相对该物种适宜温度的偏移，
/// fertility 与 light 是 0..1 的充足度，默认值即「无环境影响」。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

}  // namespace gamev2::environment
```

Delete `include/gamev2/genetics/environment.hpp`.

- [ ] **Step 2: 改 include 路径**

把

```
#include "gamev2/genetics/environment.hpp"
```

替换为

```
#include "gamev2/environment/climate.hpp"
```

涉及 8 个文件：

```
include/gamev2/genetics/expression.hpp
include/gamev2/genetics/survival.hpp
include/gamev2/genetics/breeding.hpp
include/gamev2/genetics/population.hpp
include/gamev2/ecs/adapt/genetics_systems.hpp
src/genetics/expression.cpp
src/genetics/population.cpp
src/ecs/adapt/genetics_systems.cpp
```

再把其余文件里出现的 `EnvironmentSnapshot` 限定名改为
`gamev2::environment::EnvironmentSnapshot`（`genetics/*.hpp` 与
`src/genetics/*.cpp` 在 `namespace gamev2::genetics` 内，可简写为
`environment::EnvironmentSnapshot`；`src/ecs/adapt/genetics_systems.cpp`
在 `gamev2::ecs::adapt` 内，同样可简写）。涉及：

```
tests/test_genetics_determinism.cpp
tests/test_genetics_expression.cpp
tests/test_genetics_performance.cpp
tests/test_genetics_population.cpp
tests/test_genetics_survival.cpp
```

- [ ] **Step 3: 修正 breeding.hpp 的过期注释**

`include/gamev2/genetics/breeding.hpp` 中 `ParentPool::environment` 字段
上写着「本模块不读它」。这句已不成立——`population.cpp` 的
`site_environment` 会读它并交给子代表型。改为：

```cpp
    /// 待繁殖位置的环境快照，由环境 spec 采样后传入。表型层的 g(E) 读它。
    std::optional<EnvironmentSnapshot> environment{};
```

- [ ] **Step 4: 建两个适配层空壳**

Create `include/gamev2/ecs/adapt/environment_systems.hpp`:

```cpp
/// @file environment_systems.hpp
/// @brief 环境模块的 ECS 适配层。Task 7 填充 EnvironmentWorld 与
///        EnvironmentSystem。
#pragma once

#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {}  // namespace gamev2::ecs::adapt
```

Create `src/ecs/adapt/environment_systems.cpp`:

```cpp
// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/ecs/adapt/environment_systems.hpp"

namespace gamev2::ecs::adapt {}  // namespace gamev2::ecs::adapt
```

- [ ] **Step 5: 建 environment 库目标**

Create `tests/test_environment_smoke.cpp`:

```cpp
#include "gamev2/environment/climate.hpp"

#include "test_framework.hpp"

using gamev2::environment::EnvironmentSnapshot;

TEST(environment_snapshot_defaults_mean_no_effect)
{
    const EnvironmentSnapshot s{};
    CHECK_EQ(s.temperature, 0.0f);
    CHECK_EQ(s.fertility, 1.0f);
    CHECK_EQ(s.light, 1.0f);
}
```

在 `CMakeLists.txt` 中 `add_executable(genetics_tests ...)` 之后、
`option(GAMEV2_ENABLE_ASAN ...)` 之前插入：

```cmake
set(ENVIRONMENT_SOURCES
    src/ecs/adapt/environment_systems.cpp
)
add_library(environment STATIC ${ENVIRONMENT_SOURCES})
target_include_directories(environment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(environment PUBLIC genetics)

set(ENVIRONMENT_TEST_SOURCES
    tests/main.cpp
    tests/test_environment_smoke.cpp
)
add_executable(environment_tests ${ENVIRONMENT_TEST_SOURCES})
target_link_libraries(environment_tests PRIVATE environment)
```

- [ ] **Step 6: 把 environment 源纳入 ASan 构建**

`CMakeLists.txt` 中 ASan 段的 `foreach` 改为：

```cmake
    foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES
                  GENETICS_SOURCES GENETICS_TEST_SOURCES
                  ENVIRONMENT_SOURCES ENVIRONMENT_TEST_SOURCES)
```

- [ ] **Step 7: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `1 test(s), 0 failed`。
**记下 `ecs_tests` 与 `genetics_tests` 的用例总数**，后续每个任务的 Expected
都拿它核对。用例数变了说明 Step 2 漏改或误改。

- [ ] **Step 8: 报告并等待提交决定**

向用户报告：改了哪些文件、三个测试的用例数与失败数。**不执行 git commit。**

---

## Task 2: ClimateField

**Files:**
- Create: `include/gamev2/environment/config.hpp`
- Create: `src/environment/climate.cpp`
- Modify: `include/gamev2/environment/climate.hpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_environment_climate.cpp`

**Interfaces:**
- Consumes: `EnvironmentSnapshot`（Task 1）
- Produces:
  - `gamev2::environment::EnvironmentConfig`（本任务只用到它的三个气候项，
    其余成员留给 Task 3 / 4 / 5 / 8）
  - `kGridSide = 64`、`kCellCount = 4096`
  - `struct CellIndex { std::uint16_t value; static CellIndex of(float, float) noexcept; }`
  - `struct ClimateCell { float base_temperature, base_fertility, base_light; }`
  - `class ClimateField { ClimateField(std::uint64_t, const EnvironmentConfig&); void advance(std::uint32_t) noexcept; EnvironmentSnapshot at(CellIndex) const noexcept; const ClimateCell& base(CellIndex) const noexcept; }`

- [ ] **Step 1: 写失败测试**

Create `tests/test_environment_climate.cpp`:

```cpp
#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

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

TEST(climate_base_fertility_is_never_zero)
{
    const ClimateField field{99, EnvironmentConfig{}};
    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        // 零承载力的格永远不会有生物，地图上是一块谁也解释不了的死地。
        CHECK(field.base(index).base_fertility > 0.0f);
        CHECK(field.base(index).base_fertility <= 1.0f);
    }
}

TEST(climate_season_changes_temperature_within_amplitude)
{
    const EnvironmentConfig config{};
    ClimateField field{7, config};
    field.advance(0);
    const auto warm = field.at(CellIndex::of(3.0f, 5.0f));

    field.advance(2);
    const auto other = field.at(CellIndex::of(3.0f, 5.0f));

    CHECK(warm.temperature != other.temperature);

    const float base = field.base(CellIndex::of(3.0f, 5.0f)).base_temperature;
    CHECK(std::fabs(other.temperature - base) <= config.temperature_amplitude
                                                + 1e-4f);
}

TEST(climate_advance_is_a_pure_function_of_the_season)
{
    // 季节只改派生量、不累加：任意一季都能直接算出，读到的是哪一季与
    // 之前推进过几季无关。
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
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`config.hpp` 不存在。

- [ ] **Step 3: 写 config.hpp**

Create `include/gamev2/environment/config.hpp`:

```cpp
/// @file config.hpp
/// @brief 环境模块的调平衡旋钮，随配置加载一次。
///
/// 不做成编译期常量，与 GeneticsConfig 同一个理由：这些恰恰是需要反复调整的
/// 参数，固化进二进制会让每次调参都变成一次重编。
#pragma once

#include <cstdint>

namespace gamev2::environment {

struct EnvironmentConfig
{
    std::uint32_t steps_per_season = 8;
    /// fertility = 1 时一格支持几个体。默认种群稳定在
    /// Σcapacity ≈ 4096 × 3 ≈ 12k，略高于 1 万个体的规模定位。
    float capacity_base = 3.0f;
    /// 每步向 capacity 回归的比例，(0, 1]。
    float regen_rate = 0.25f;
    /// 单体单步最多吃掉本地存量的比例。
    float demand_rate = 0.35f;
    /// 每个体本季最多产几个子代。
    float offspring_scale = 2.0f;
    /// 局部交配半径，单位为格。0 表示只有本格内个体互相交配。
    float mating_radius_cells = 1.0f;
    float temperature_amplitude = 6.0f;
    float temperature_period = 8.0f;  ///< 季节数 / 周期
    float light_amplitude = 0.15f;
};

}  // namespace gamev2::environment
```

- [ ] **Step 4: 扩展 climate.hpp**

把 `#pragma once` 之后的整个文件替换为：

```cpp
/// @file climate.hpp
/// @brief 气候场：EnvironmentSnapshot 是格级气候在某一季的取值，
///        CellIndex 是格的下标，ClimateField 持有逐格的基准值并按季节求值。
#pragma once

#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::environment {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
/// 三项环境量的取值范围约定：temperature 是相对该物种适宜温度的偏移，
/// fertility 与 light 是 0..1 的充足度，默认值即「无环境影响」。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

/// 编译期固定的格数。场要能定长进 ECS 组件，字段集合与尺寸都必须在编译期确定。
inline constexpr std::size_t kGridSide = 64;
inline constexpr std::size_t kCellCount = kGridSide * kGridSide;

/// 世界坐标空间即格坐标空间，[0, kGridSide)。个体在格内浮动，
/// 气候与资源是格级的。
struct CellIndex
{
    std::uint16_t value = 0;

    /// 世界坐标 -> 所在格。截断即向下取整，因为坐标非负。
    ///
    /// \pre x, y ∈ [0, kGridSide)。越界输入不是可恢复的错误状态，
    /// 因此不返回错误，只在这里写明。
    [[nodiscard]] static CellIndex of(float x, float y) noexcept
    {
        const auto ix = static_cast<std::uint16_t>(x);
        const auto iy = static_cast<std::uint16_t>(y);
        return CellIndex{static_cast<std::uint16_t>(iy * kGridSide + ix)};
    }
};

/// 逐季不变的气候基准值。季节只在求值时给它们加偏移，不写回。
struct ClimateCell
{
    float base_temperature = 0.0f;
    float base_fertility = 1.0f;
    float base_light = 1.0f;
};

class ClimateField
{
public:
    ClimateField(std::uint64_t root_seed,
                 const EnvironmentConfig& config);

    /// 只改派生量，不累加：温度与光照在基准值上加一个由季节决定的正弦偏移。
    /// 因此任意一季的气候都能直接算出，不需要历史，也就没有「忘掉推进过
    /// 几季」这种可能出错的状态。
    void advance(std::uint32_t season_index) noexcept;

    [[nodiscard]] EnvironmentSnapshot at(CellIndex cell) const noexcept;
    [[nodiscard]] const ClimateCell& base(CellIndex cell) const noexcept;

private:
    std::array<ClimateCell, kCellCount> cells_{};
    std::uint32_t season_ = 0;
    EnvironmentConfig config_{};
};

}  // namespace gamev2::environment
```

- [ ] **Step 5: 写 climate.cpp**

Create `src/environment/climate.cpp`:

```cpp
// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/climate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace gamev2::environment {

namespace {

/// splitmix64：把种子与格下标混成一个相关性不明显的 64 位值。
/// 不复用 genetics 的实现，因为 genetics 不被本模块包含，而这里只需要
/// 十几行——为一个单向依赖去 include 整个遗传内核不划算。
[[nodiscard]] std::uint64_t mix(std::uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/// 把哈希映射到 [lo, hi)。取高 24 位：float 的尾数只有 24 位，
/// 多取低位只是把它丢掉。
[[nodiscard]] float uniform(std::uint64_t bits, float lo, float hi) noexcept
{
    const auto unit = static_cast<float>(bits >> 40)
                      / static_cast<float>(1ULL << 24);
    return lo + unit * (hi - lo);
}

}  // namespace

ClimateField::ClimateField(const std::uint64_t root_seed,
                           const EnvironmentConfig& config)
    : config_{config}
{
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto salt = static_cast<std::uint64_t>(i)
                          * 0x9e3779b97f4a7c15ULL;
        const auto base = mix(root_seed ^ salt);
        cells_[i].base_temperature = uniform(mix(base + 1), -8.0f, 8.0f);
        cells_[i].base_fertility = uniform(mix(base + 2), 0.25f, 1.0f);
        cells_[i].base_light = uniform(mix(base + 3), 0.6f, 1.0f);
    }
}

void ClimateField::advance(const std::uint32_t season_index) noexcept
{
    season_ = season_index;
}

EnvironmentSnapshot ClimateField::at(const CellIndex cell) const noexcept
{
    const auto& b = cells_[cell.value];

    const float period = std::max(1.0f, config_.temperature_period);
    const float phase = 2.0f * std::numbers::pi_v<float>
                        * static_cast<float>(season_) / period;
    const float swing = std::sin(phase);

    EnvironmentSnapshot s{};
    s.temperature =
        b.base_temperature + config_.temperature_amplitude * swing;
    // fertility 逐季不变：食物承载力是生境属性，气候才是季节属性。
    s.fertility = b.base_fertility;
    // 光照与温度同相：季节是同一个太阳在走，把两者拆成独立相位会让
    // 「夏天暗」这种组合自然出现，而它没有物理来源。
    s.light = b.base_light
              * (1.0f - config_.light_amplitude
                 + config_.light_amplitude * swing);
    return s;
}

const ClimateCell& ClimateField::base(const CellIndex cell) const noexcept
{
    return cells_[cell.value];
}

}  // namespace gamev2::environment
```

- [ ] **Step 6: 注册源文件**

`CMakeLists.txt` 中 `ENVIRONMENT_SOURCES` 改为：

```cmake
set(ENVIRONMENT_SOURCES
    src/environment/climate.cpp
    src/ecs/adapt/environment_systems.cpp
)
```

`ENVIRONMENT_TEST_SOURCES` 改为：

```cmake
set(ENVIRONMENT_TEST_SOURCES
    tests/main.cpp
    tests/test_environment_smoke.cpp
    tests/test_environment_climate.cpp
)
```

- [ ] **Step 7: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `7 test(s), 0 failed`
（smoke 1 + climate 6）。`genetics_tests` 与 `ecs_tests` 用例数与 Task 1
Step 7 记下的一致。

若 `climate_advance_is_a_pure_function_of_the_season` 失败：说明
`advance` 写回了基准值而不是只存季节号，检查 `ClimateField` 的成员。

- [ ] **Step 8: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 3: ResourceField

**Files:**
- Create: `include/gamev2/environment/resource.hpp`
- Create: `src/environment/resource.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_environment_resource.cpp`

**Interfaces:**
- Consumes: `ClimateField` / `CellIndex` / `EnvironmentConfig`（Task 2）
- Produces: `struct ResourceCell { float stock, capacity; }`、
  `class ResourceField`，接口见 Step 4 的头文件

- [ ] **Step 1: 写失败测试**

Create `tests/test_environment_resource.cpp`:

```cpp
#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

using gamev2::environment::CellIndex;
using gamev2::environment::ClimateField;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::ResourceField;

TEST(resource_capacity_scales_with_base_fertility)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    const ResourceField field{climate, config};

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
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    const ResourceField field{climate, config};

    const auto index = CellIndex::of(20.0f, 20.0f);
    // 否则第一季所有个体都从饥饿开始，而那不是任何配置意图表达的初始条件。
    CHECK_EQ(field.stock(index), field.capacity(index));
}

TEST(resource_regrow_never_exceeds_capacity)
{
    const EnvironmentConfig config{};
    const ClimateField climate{11, config};
    ResourceField field{climate, config};
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
    const EnvironmentConfig config{};
    const ClimateField climate{3, config};
    ResourceField field{climate, config};
    const auto index = CellIndex::of(1.0f, 1.0f);

    field.draw(index, 1e9f);
    CHECK_EQ(field.stock(index), 0.0f);
}

TEST(resource_occupants_start_at_zero)
{
    const EnvironmentConfig config{};
    const ClimateField climate{3, config};
    const ResourceField field{climate, config};

    CHECK_EQ(field.occupants(CellIndex::of(0.5f, 0.5f)),
             std::uint16_t{0});
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`resource.hpp` 不存在。

- [ ] **Step 3: 写 resource.hpp**

Create `include/gamev2/environment/resource.hpp`:

```cpp
/// @file resource.hpp
/// @brief 资源场：每格的食物存量与它能支持多少个体。存量以「个体」为单位，
///        因此 stock / occupants 直接读作人均可得食物，量纲不需要转换。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gamev2::environment {

/// 定义在 sites.hpp。count_occupants 需要它的坐标，这里只前置声明，
/// 避免 resource.hpp 与 sites.hpp 互相 include。
struct Forager;

struct ResourceCell
{
    float stock = 0.0f;     ///< 当前存量，[0, capacity]
    float capacity = 0.0f;  ///< = capacity_base × 该格 base_fertility
};

class ResourceField
{
public:
    ResourceField(const ClimateField& climate,
                  const EnvironmentConfig& config);

    /// stock += regen_rate × (capacity - stock)。指数逼近而非线性：
    /// 线性在贴近 capacity 时仍以固定速率加，会在边界上反复穿越。
    /// regen_rate ∈ (0, 1] 保证单调不越界。
    void regrow() noexcept;

    /// 扣减，夹到 0。扣减超过存量时只会把该格吃空，不会透支成负数：
    /// 负存量会让下一轮的 want 与 got 同时为负，喂食满意度反而变正。
    void draw(CellIndex cell, float amount) noexcept;

    [[nodiscard]] float stock(CellIndex cell) const noexcept;
    [[nodiscard]] float capacity(CellIndex cell) const noexcept;

    /// 本格当前有多少个体。季内没有移动，季初数一次即可；
    /// run_season 与 build_sites 共用同一份计数。
    [[nodiscard]] std::uint16_t occupants(CellIndex cell) const noexcept;

    /// 清零后按 foragers 的坐标重数一遍。
    void count_occupants(std::span<const Forager> foragers);

private:
    std::array<ResourceCell, kCellCount> cells_{};
    std::array<std::uint16_t, kCellCount> occupants_{};
    float regen_rate_ = 0.25f;
};

}  // namespace gamev2::environment
```

- [ ] **Step 4: 写 resource.cpp**

Create `src/environment/resource.cpp`:

```cpp
// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/resource.hpp"

#include "gamev2/environment/sites.hpp"

#include <algorithm>

namespace gamev2::environment {

ResourceField::ResourceField(const ClimateField& climate,
                             const EnvironmentConfig& config)
    : regen_rate_{config.regen_rate}
{
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        cells_[i].capacity =
            config.capacity_base * climate.base(index).base_fertility;
        cells_[i].stock = cells_[i].capacity;
    }
}

void ResourceField::regrow() noexcept
{
    for (auto& cell : cells_)
    {
        cell.stock += regen_rate_ * (cell.capacity - cell.stock);
    }
}

void ResourceField::draw(const CellIndex cell, const float amount) noexcept
{
    cells_[cell.value].stock =
        std::max(0.0f, cells_[cell.value].stock - amount);
}

float ResourceField::stock(const CellIndex cell) const noexcept
{
    return cells_[cell.value].stock;
}

float ResourceField::capacity(const CellIndex cell) const noexcept
{
    return cells_[cell.value].capacity;
}

std::uint16_t ResourceField::occupants(const CellIndex cell) const noexcept
{
    return occupants_[cell.value];
}

void ResourceField::count_occupants(const std::span<const Forager> foragers)
{
    occupants_.fill(0);
    for (const auto& f : foragers)
    {
        ++occupants_[CellIndex::of(f.x, f.y).value];
    }
}

}  // namespace gamev2::environment
```

`count_occupants` 定义在 .cpp 而非头文件，因此**不需要**在 .cpp 里
重复声明——但 `Forager` 的完整定义必须在 .cpp 可见，所以上面的
`#include "gamev2/environment/sites.hpp"` 是必需的。Task 4 落地
`sites.hpp` 之前本文件会编译失败，Step 5 一次性把两者都接上。

- [ ] **Step 5: 写 sites.hpp 的最小 Forager 定义（Task 4 会扩展）**

Create `include/gamev2/environment/sites.hpp`:

```cpp
/// @file sites.hpp
/// @brief 季内摄食与承载力预算。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::environment {

/// 一个摄食者。demand 由表型算出（SenseRange × Fecundity），环境层不读
/// Phenotype —— 这是 genetics 依赖方向不被反转的关键。
struct Forager
{
    float x = 0.0f;
    float y = 0.0f;
    float demand = 0.0f;
};

}  // namespace gamev2::environment
```

- [ ] **Step 6: 注册源文件**

`ENVIRONMENT_SOURCES` 追加 `src/environment/resource.cpp`；
`ENVIRONMENT_TEST_SOURCES` 追加 `tests/test_environment_resource.cpp`。

- [ ] **Step 7: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `12 test(s), 0 failed`
（smoke 1 + climate 6 + resource 5）。其余两个用例数不变。

若 `resource_regrow_never_exceeds_capacity` 失败：`regen_rate_` 必须来自
构造参数而非默认初始值。

- [ ] **Step 8: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 4: 季内摄食循环

**Files:**
- Modify: `include/gamev2/environment/sites.hpp`
- Create: `src/environment/sites.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_environment_sites.cpp`

**Interfaces:**
- Consumes: `ResourceField` / `Forager`（Task 3）
- Produces: `std::vector<float> run_season(ResourceField&, std::span<const Forager>, const EnvironmentConfig&)`——
  返回与 `foragers` 同序的累计 feeding（0..1，1 = 从未受限）

- [ ] **Step 1: 写失败测试**

Create `tests/test_environment_sites.cpp`:

```cpp
#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/sites.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using gamev2::environment::CellIndex;
using gamev2::environment::ClimateField;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::Forager;
using gamev2::environment::ResourceField;
using gamev2::environment::run_season;

namespace {

/// 把一批个体全部塞进同一格附近，方便构造「拥挤」与「空旷」两种极端。
/// 位置在格内抖动，避免所有人落在同一个浮点坐标上。
std::vector<Forager> crowd(std::size_t n, float demand)
{
    std::vector<Forager> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto jitter = static_cast<float>(i % 60) * 0.01f;
        out.push_back(Forager{1.0f + jitter, 1.0f, demand});
    }
    return out;
}

}  // namespace

TEST(feeding_is_one_when_supply_exceeds_demand)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    // 一个个体在近乎空旷的格子里，存量足够它随便吃。
    const auto foragers = crowd(1, 0.1f);
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    CHECK_EQ(feeding.size(), std::size_t{1});
    CHECK_EQ(feeding[0], 1.0f);
}

TEST(feeding_drops_when_crowding_exceeds_capacity)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    // 个体数远超该格承载力，且每个都很能吃。
    const auto foragers = crowd(60, 5.0f);
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    CHECK(feeding[0] < 1.0f);
    CHECK(feeding[0] > 0.0f);
}

TEST(feeding_is_independent_of_forager_order)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    const auto foragers = crowd(40, 2.0f);

    ResourceField a{climate, config};
    a.count_occupants(foragers);
    const auto first = run_season(a, foragers, config);

    auto shuffled = foragers;
    std::ranges::reverse(shuffled);
    ResourceField b{climate, config};
    b.count_occupants(shuffled);
    const auto second = run_season(b, shuffled, config);

    // 逐位相同，且资源场终态相同——不然换个 archetype 的遍历顺序
    // 就换一批个体的命运。
    CHECK(std::ranges::equal(first, second));
    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        CHECK_EQ(a.stock(index), b.stock(index));
    }
}

TEST(zero_demand_forager_is_never_food_limited)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    const std::vector<Forager> foragers{Forager{1.0f, 1.0f, 0.0f}};
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    // 什么都不需要的个体不该被饿死。
    CHECK_EQ(feeding[0], 1.0f);
}

TEST(run_season_returns_one_entry_per_forager)
{
    const EnvironmentConfig config{};
    const ClimateField climate{6, config};
    ResourceField field{climate, config};
    const auto foragers = crowd(17, 1.0f);
    field.count_occupants(foragers);

    CHECK_EQ(run_season(field, foragers, config).size(), foragers.size());
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`run_season` 未声明。

- [ ] **Step 3: 扩展 sites.hpp**

把 `include/gamev2/environment/sites.hpp` 替换为：

```cpp
/// @file sites.hpp
/// @brief 季内摄食与承载力预算：run_season 跑完一季的 K 步，产出每个体的
///        实测 feeding；build_sites 按格与物种算出本季的子代预算。
///
/// 环境层不读 Phenotype、不认 SpeciesId、不依赖 ECS —— Forager::demand 由
/// 调用方从表型算出，BreedingSite::species_key 是裸整数。这是 genetics 依赖
/// 方向不被反转的关键，也是这些断言能在没有 World 的前提下写出的原因。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::environment {

/// 一个摄食者。demand 由表型算出（SenseRange × Fecundity）。
struct Forager
{
    float x = 0.0f;
    float y = 0.0f;
    float demand = 0.0f;
};

/// 跑完 steps_per_season 步：先再生，再逐格按存量均分摄食。
/// 返回与 foragers 同序的累计 feeding（0..1，1 = 从未受限）。
///
/// \pre `field.count_occupants(foragers)` 已调用，且季内无移动。
[[nodiscard]] std::vector<float>
run_season(ResourceField& field, std::span<const Forager> foragers,
           const EnvironmentConfig& config);

}  // namespace gamev2::environment
```

- [ ] **Step 4: 写 sites.cpp**

Create `src/environment/sites.cpp`:

```cpp
// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/sites.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::environment {

std::vector<float>
run_season(ResourceField& field, const std::span<const Forager> foragers,
           const EnvironmentConfig& config)
{
    std::vector<float> satisfaction(foragers.size(), 0.0f);
    std::vector<float> got(foragers.size(), 0.0f);
    std::vector<float> want_of(foragers.size(), 0.0f);
    std::vector<float> per_cell(kCellCount, 0.0f);

    for (std::uint32_t step = 0; step < config.steps_per_season; ++step)
    {
        field.regrow();
        std::ranges::fill(got, 0.0f);
        std::ranges::fill(want_of, 0.0f);
        std::ranges::fill(per_cell, 0.0f);

        // 第一趟：按扣减前的存量算每个人的份。只算不扣。
        for (std::size_t i = 0; i < foragers.size(); ++i)
        {
            const auto& f = foragers[i];
            const auto cell = CellIndex::of(f.x, f.y);
            const auto n = field.occupants(cell);
            if (n == 0)
            {
                continue;
            }
            const float share =
                field.stock(cell) / static_cast<float>(n);
            const float want = config.demand_rate * f.demand * share;
            want_of[i] = want;
            got[i] = std::min(want, share);
            per_cell[cell.value] += got[i];
        }

        // 第二趟：按格统一扣减。必须与第一趟分开——边算边扣会让同格内
        // 先遍历到的个体吃掉后面个体的份额，结果依赖实体遍历顺序，
        // 而顺序依赖在 ECS 里是换一次批就换一次结果的隐患。
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            if (per_cell[c] > 0.0f)
            {
                field.draw(CellIndex{static_cast<std::uint16_t>(c)},
                           per_cell[c]);
            }
        }

        for (std::size_t i = 0; i < foragers.size(); ++i)
        {
            if (want_of[i] > 0.0f)
            {
                // want 为 0 表示本格存量已空：那就是饿，记 0 分。
                satisfaction[i] += got[i] / want_of[i];
            }
            else if (foragers[i].demand <= 0.0f)
            {
                // 什么都不需要的个体不该被饿死。
                satisfaction[i] += 1.0f;
            }
        }
    }

    const float steps = static_cast<float>(config.steps_per_season);
    for (auto& value : satisfaction)
    {
        value = std::clamp(value / steps, 0.0f, 1.0f);
    }
    return satisfaction;
}

}  // namespace gamev2::environment
```

- [ ] **Step 5: 注册源文件**

`ENVIRONMENT_SOURCES` 追加 `src/environment/sites.cpp`；
`ENVIRONMENT_TEST_SOURCES` 追加 `tests/test_environment_sites.cpp`。

- [ ] **Step 6: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `18 test(s), 0 failed`
（smoke 1 + climate 6 + resource 5 + sites 6）。其余两个用例数不变。

若 `feeding_is_one_when_supply_exceeds_demand` 失败：低占用下
`want = 0.35 × 0.1 × share` 必然远小于 `share`，因此 `got == want`，
每步满意度为 1。失败说明 `demand_rate` 或 `steps_per_season` 没从传入的
`config` 读。

若 `feeding_is_independent_of_forager_order` 失败：说明第一趟与第二趟
被合并了。

- [ ] **Step 7: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 5: 承载力与待繁殖位置

**Files:**
- Modify: `include/gamev2/environment/sites.hpp`
- Modify: `src/environment/sites.cpp`
- Modify: `tests/test_environment_sites.cpp`

**Interfaces:**
- Consumes: `Forager` / `ResourceField` / `ClimateField`（Task 2–4）
- Produces:
  - `struct BreedingSite { CellIndex cell; std::uint32_t species_key; std::uint32_t progeny; EnvironmentSnapshot environment; }`
  - `std::vector<BreedingSite> build_sites(const ResourceField&, const ClimateField&, std::span<const Forager>, std::span<const std::uint32_t>, const EnvironmentConfig&)`

- [ ] **Step 1: 写失败测试**

在 `tests/test_environment_sites.cpp` 的 `using` 区补：

```cpp
using gamev2::environment::BreedingSite;
using gamev2::environment::build_sites;
```

在匿名 namespace 内追加：

```cpp
std::vector<BreedingSite> sites_of(
    const EnvironmentConfig& config, const std::vector<Forager>& foragers,
    const std::vector<std::uint32_t>& keys)
{
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    field.count_occupants(foragers);
    return build_sites(field, climate, foragers, keys, config);
}

std::uint32_t progeny_of(const std::vector<BreedingSite>& sites,
                         const std::uint32_t species_key)
{
    std::uint32_t total = 0;
    for (const auto& s : sites)
    {
        if (s.species_key == species_key)
        {
            total += s.progeny;
        }
    }
    return total;
}
```

追加测试：

```cpp
TEST(sites_are_empty_when_no_creatures)
{
    const auto sites = sites_of(EnvironmentConfig{}, {}, {});
    CHECK(sites.empty());
}

TEST(over_capacity_cell_emits_no_site)
{
    const EnvironmentConfig config{};
    // capacity_base = 3，base_fertility <= 1，单格容量不超过 3 个体。
    // 塞 20 个进去必然过载，headroom 归零。
    auto foragers = crowd(20, 1.0f);
    std::vector<std::uint32_t> keys(foragers.size(), 1);

    const auto sites = sites_of(config, foragers, keys);

    // 不发站点，而不是发一个 progeny = 0 的站点：settle 把
    // ParentPool::progeny == 0 按 1 处理，0 预算送进去会凭空生出一个后代，
    // 承载力封顶因此失效。
    CHECK(sites.empty());
}

TEST(progeny_of_a_lone_creature_is_capped_by_offspring_scale)
{
    const EnvironmentConfig config{};
    const std::vector<Forager> foragers{Forager{1.0f, 1.0f, 1.0f}};
    const auto sites = sites_of(config, foragers, {1});

    CHECK_EQ(sites.size(), std::size_t{1});
    // 繁殖上限：独苗不能产出一满窝后代，那会是一次纯自交。
    const auto cap =
        static_cast<std::uint32_t>(std::lround(config.offspring_scale));
    CHECK(sites[0].progeny <= cap);
    CHECK(sites[0].progeny > 0);
}

TEST(capacity_is_split_between_species_by_current_share)
{
    const EnvironmentConfig config{};
    std::vector<Forager> foragers;
    std::vector<std::uint32_t> keys;
    // 同格 3 个 A、1 个 B。
    for (int i = 0; i < 3; ++i)
    {
        foragers.push_back(
            Forager{1.0f + static_cast<float>(i) * 0.01f, 1.0f, 1.0f});
        keys.push_back(1);
    }
    foragers.push_back(Forager{1.5f, 1.0f, 1.0f});
    keys.push_back(2);

    const auto sites = sites_of(config, foragers, keys);

    CHECK_EQ(sites.size(), std::size_t{2});
    // share 按当前构成分配，A 拿 3/4，B 拿 1/4。
    CHECK(progeny_of(sites, 1) > progeny_of(sites, 2));
    CHECK_EQ(progeny_of(sites, 1) + progeny_of(sites, 2), 3u * 2u);
}

TEST(sites_are_identical_under_forager_permutation)
{
    const EnvironmentConfig config{};
    std::vector<Forager> foragers;
    std::vector<std::uint32_t> keys;
    for (int i = 0; i < 12; ++i)
    {
        foragers.push_back(
            Forager{static_cast<float>(i % 4), static_cast<float>(i / 4), 1.0f});
        keys.push_back(static_cast<std::uint32_t>(1 + i % 3));
    }
    const auto straight = sites_of(config, foragers, keys);

    std::vector<Forager> flipped = foragers;
    std::ranges::reverse(flipped);
    const std::vector<std::uint32_t> flipped_keys(keys.rbegin(), keys.rend());
    const auto shuffled = sites_of(config, flipped, flipped_keys);

    CHECK_EQ(straight.size(), shuffled.size());
    for (std::size_t i = 0; i < straight.size(); ++i)
    {
        CHECK_EQ(straight[i].cell.value, shuffled[i].cell.value);
        CHECK_EQ(straight[i].species_key, shuffled[i].species_key);
        CHECK_EQ(straight[i].progeny, shuffled[i].progeny);
    }
}

TEST(each_site_carries_its_cell_weather)
{
    EnvironmentConfig config{};
    config.temperature_amplitude = 0.0f;
    const ClimateField climate{5, config};
    ResourceField field{climate, config};

    const std::vector<Forager> foragers{Forager{7.0f, 9.0f, 1.0f}};
    field.count_occupants(foragers);
    const auto sites = build_sites(field, climate, foragers,
                                   std::vector<std::uint32_t>{1}, config);

    CHECK_EQ(sites.size(), std::size_t{1});
    const auto expected = climate.at(CellIndex::of(7.0f, 9.0f));
    CHECK_EQ(sites[0].environment.fertility, expected.fertility);
    CHECK_EQ(sites[0].environment.temperature, expected.temperature);
    CHECK_EQ(sites[0].environment.light, expected.light);
}
```

顶部补 `#include <cmath>`。

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`build_sites` 未声明。

- [ ] **Step 3: 写声明**

在 `include/gamev2/environment/sites.hpp` 的 `run_season` 之后、
`}  // namespace` 之前插入：

```cpp
struct BreedingSite
{
    CellIndex cell;
    /// 外部物种标签，值域与 genetics::SpeciesId::value 一致。
    /// 环境层不认识 SpeciesId，翻译发生在 ecs/adapt。
    std::uint32_t species_key = 0;
    std::uint32_t progeny = 0;
    EnvironmentSnapshot environment;
};

/// 对每个有占用的格、每个出现在该格的物种算一次子代预算。
/// 算出 0 的格不发站点——settle 把 progeny == 0 按 1 处理。
///
/// 输出按 (cell, species_key) 排序后成组，因此与输入实体顺序无关。
///
/// \pre `foragers` 与 `species_keys` 等长同序；
/// `field.count_occupants(foragers)` 已调用。
[[nodiscard]] std::vector<BreedingSite>
build_sites(const ResourceField& field, const ClimateField& climate,
            std::span<const Forager> foragers,
            std::span<const std::uint32_t> species_keys,
            const EnvironmentConfig& config);
```

- [ ] **Step 4: 写实现**

在 `src/environment/sites.cpp` 的 `run_season` 之后、
`}  // namespace` 之前插入：

```cpp
std::vector<BreedingSite>
build_sites(const ResourceField& field, const ClimateField& climate,
            const std::span<const Forager> foragers,
            const std::span<const std::uint32_t> species_keys,
            const EnvironmentConfig& config)
{
    // 先把 (格, 物种) 对收集出来排序成组，再逐组出站点。排序保证输出
    // 与输入实体顺序无关；就地插入做不到这一点。
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    pairs.reserve(foragers.size());
    for (std::size_t i = 0; i < foragers.size(); ++i)
    {
        pairs.emplace_back(CellIndex::of(foragers[i].x, foragers[i].y).value,
                           species_keys[i]);
    }
    std::ranges::sort(pairs);

    std::vector<BreedingSite> sites;
    for (std::size_t begin = 0; begin < pairs.size();)
    {
        std::size_t end = begin;
        while (end < pairs.size() && pairs[end] == pairs[begin])
        {
            ++end;
        }

        const auto cell = CellIndex{pairs[begin].first};
        const auto here = static_cast<float>(end - begin);
        const auto all = static_cast<float>(field.occupants(cell));

        // 生态上限：生境承载力减去在场数，负的部分不算数。
        const float headroom = std::max(0.0f, field.capacity(cell) - all);
        // share = here / all，按当前物种构成分配空余容量：新物种无法
        // 凭空出现在一个已经被占满的格子里。
        const std::uint32_t eco_ceiling = static_cast<std::uint32_t>(
            std::lround(headroom * here / all));

        // 繁殖上限保证一个独苗产不出满窝后代：那会是一次纯自交，
        // 直接毁掉多样性，而这正是遗传系统最不该丢掉的东西。
        const std::uint32_t parent_ceiling = static_cast<std::uint32_t>(
            std::lround(here * config.offspring_scale));

        const auto progeny = std::min(eco_ceiling, parent_ceiling);
        if (progeny > 0)
        {
            sites.push_back(BreedingSite{cell, pairs[begin].second, progeny,
                                         climate.at(cell)});
        }
        begin = end;
    }

    return sites;
}
```

顶部补 `#include <cmath>` 与 `#include <utility>`。

- [ ] **Step 5: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `24 test(s), 0 failed`
（smoke 1 + climate 6 + resource 5 + sites 12）。其余两个用例数不变。

`capacity_is_split_between_species_by_current_share` 的总数断言依赖
`capacity_base = 3`、该格 `base_fertility ≈ 1`、`offspring_scale = 2`：
headroom 取 2、A 拿 round(2 × 0.75) = 2、B 拿 round(2 × 0.25) = 0，
总和为 2 而非 6。若该断言失败，先打印实际值再判断是公式错还是
`base_fertility` 让 capacity 小于 4；后者是正常的，把断言改成
`CHECK(progeny_of(sites, 1) > progeny_of(sites, 2))` 即可，
前一条已经覆盖了比例本身。

- [ ] **Step 6: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 6: genetics 接受实测 feeding

**Files:**
- Modify: `include/gamev2/genetics/population.hpp`
- Modify: `src/genetics/population.cpp`
- Test: `tests/test_genetics_population.cpp`

**Interfaces:**
- Consumes: 无
- Produces: `SeasonInput::feedings`（`std::span<const float>`，默认空）

- [ ] **Step 1: 写失败测试**

在 `tests/test_genetics_population.cpp` 末尾追加。该文件的既有测试全部用
局部变量而非 fixture struct（如 `test_genetics_population.cpp:229`
`settle_keeps_offspring_in_staging_until_returned`），照同一写法：

```cpp
TEST(empty_feedings_matches_explicit_all_ones)
{
    const std::vector<Genome> genomes(8, Genome{});
    const std::vector<Phenotype> phenotypes(8, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(8, EnvironmentSnapshot{});
    const std::vector<float> threats(8, 0.0f);
    const std::vector<std::uint32_t> species_ids(8, 1);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, std::nullopt, 2}};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    const auto without = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    false},
        neutral_table(), registry, GeneticsConfig{}, RandomStreams{31, 0});

    const std::vector<float> ones(genomes.size(), 1.0f);
    const auto with = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    false, ones},
        neutral_table(), registry, GeneticsConfig{}, RandomStreams{31, 0});

    // 空 span 的回落路径必须与显式全 1 等价，否则「没接环境层」这条降级
    // 路径的行为取决于调用方是忘了填还是故意填。
    CHECK(without.survived == with.survived);
    CHECK(without.survival_rates == with.survival_rates);
    CHECK(without.season_fitness == with.season_fitness);
    CHECK_EQ(without.offspring.size(), with.offspring.size());
    CHECK_EQ(without.offspring.size(), std::size_t{2});
}
```

用到的 `robust_phenotype()` / `all_loci()` / `neutral_table()` 都已在该文件
的匿名 namespace 里（`:57` / `:17` / `:28`）。最后那条 `offspring.size() == 2`
不是凑数：它证明这条测试真的跑到了繁殖阶段，而不是在存活筛选里就短路了。

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`SeasonInput` 没有 `feedings` 成员。

- [ ] **Step 3: 加字段**

`include/gamev2/genetics/population.hpp` 的 `SeasonInput` 末尾追加：

```cpp
    /// 与 genomes 同序，本季的实际摄入满足度（0..1）。
    ///
    /// 空 span 表示调用方未接入环境层，逐个按理论摄食上限 1.0 处理。
    /// 放在末尾而非与 threats 并列，是为了不动既有调用方的位置参数顺序
    /// —— 语义分组上的不便，不值二十几处与本次改动无关的测试编辑。
    std::span<const float> feedings{};
```

- [ ] **Step 4: 用它替换硬编码的 1.0**

`src/genetics/population.cpp` 中把

```cpp
        const SurvivalInput s{input.phenotypes[i], input.environments[i],
                              1.0f, input.threats[i]};
```

改为

```cpp
        const SurvivalInput s{
            input.phenotypes[i], input.environments[i],
            // 空 span 是「未接入环境层」的降级路径，逐个回落理论摄食上限。
            input.feedings.size() == input.genomes.size() ? input.feedings[i]
                                                           : 1.0f,
            input.threats[i]};
```

- [ ] **Step 5: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`genetics_tests` 用例数比 Task 5 结束时
多 1，其余两个不变。

- [ ] **Step 6: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 7: EnvironmentSystem

**Files:**
- Create: `include/gamev2/ecs/adapt/components.hpp`
- Rewrite: `include/gamev2/ecs/adapt/environment_systems.hpp`
- Rewrite: `src/ecs/adapt/environment_systems.cpp`
- Create: `tests/test_environment_systems.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ClimateField` / `ResourceField` / `run_season` / `build_sites`
  （Task 2–5）、`SpeciesTag` / `CreaturePhenotype`（既有 adapt 组件）
- Produces:
  - `components.hpp`：`struct Position { float x, y; }`、
    `struct Feeding { float satisfaction; }`、
    `struct BreedingTarget { SpeciesId species; CellIndex cell; std::uint32_t progeny; EnvironmentSnapshot environment; }`
  - `class EnvironmentWorld`
  - `class EnvironmentSystem final : public System`，暴露
    `const std::vector<BreedingTarget>& targets() const noexcept`

- [ ] **Step 1: 写失败测试**

Create `tests/test_environment_systems.cpp`:

```cpp
#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::World;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::EnvironmentSystem;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::Feeding;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::genetics::Phenotype;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kTraitCount;
using gamev2::genetics::trait_index;

namespace {

/// 造一批有位置、有物种、有表型的个体。SenseRange 与 Fecundity 各自 0.5，
/// 因此 demand 恒为 0.25。
void seed_creatures(World& world, const std::size_t count,
                    const std::uint64_t seed)
{
    std::mt19937_64 rng{seed};
    std::uniform_real_distribution<float> pos{0.0f, 63.9f};

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{pos(rng), pos(rng)});
        (void)world.add<SpeciesTag>(e, SpeciesTag{SpeciesId{1}});

        Phenotype p{};
        p.traits.fill(1.0f);
        p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
        p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
        (void)world.add<CreaturePhenotype>(e, CreaturePhenotype{p});
    }
}

}  // namespace

TEST(environment_system_writes_feeding_for_every_creature)
{
    World world;
    seed_creatures(world, 200, 1);

    EnvironmentWorld env{31, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    std::size_t seen = 0;
    world.for_each_entity([&](const EntityId e)
    {
        const auto* f = world.get_if<Feeding>(e);
        if (f == nullptr)
        {
            return;
        }
        ++seen;
        CHECK(f->satisfaction >= 0.0f);
        CHECK(f->satisfaction <= 1.0f);
    });
    CHECK_EQ(seen, std::size_t{200});
}

TEST(environment_system_emits_targets_for_every_cell_and_species)
{
    World world;
    seed_creatures(world, 50, 2);

    EnvironmentWorld env{32, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    const auto& targets = system.targets();
    CHECK(!targets.empty());
    for (const auto& t : targets)
    {
        CHECK_EQ(t.species.value, std::uint32_t{1});
        CHECK(t.progeny > 0);
    }
}

TEST(environment_system_is_deterministic)
{
    World a;
    seed_creatures(a, 120, 9);
    EnvironmentWorld env_a{77, EnvironmentConfig{}};
    EnvironmentSystem sys_a{env_a, 3};

    CommandBuffer cmd_a;
    sys_a.execute(a, cmd_a);
    cmd_a.flush(a);

    World b;
    seed_creatures(b, 120, 9);
    EnvironmentWorld env_b{77, EnvironmentConfig{}};
    EnvironmentSystem sys_b{env_b, 3};

    CommandBuffer cmd_b;
    sys_b.execute(b, cmd_b);
    cmd_b.flush(b);

    const auto& ta = sys_a.targets();
    const auto& tb = sys_b.targets();
    CHECK_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i)
    {
        CHECK_EQ(ta[i].cell.value, tb[i].cell.value);
        CHECK_EQ(ta[i].species.value, tb[i].species.value);
        CHECK_EQ(ta[i].progeny, tb[i].progeny);
        CHECK_EQ(ta[i].environment.temperature,
                 tb[i].environment.temperature);
        CHECK_EQ(ta[i].environment.fertility, tb[i].environment.fertility);
        CHECK_EQ(ta[i].environment.light, tb[i].environment.light);
    }
}

TEST(total_birth_budget_stays_under_total_capacity)
{
    World world;
    seed_creatures(world, 6000, 12);

    EnvironmentWorld env{64, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    std::uint64_t budget = 0;
    for (const auto& t : system.targets())
    {
        budget += t.progeny;
    }
    // 出生数必须被承载力封住——这是本模块存在的全部理由。
    // capacity_base 默认 3，两倍余量足够宽到不会因浮点抖动翻车。
    CHECK(budget <= 2ull * static_cast<std::uint64_t>(kCellCount) * 3ull);
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`components.hpp` 不存在。

- [ ] **Step 3: 写 components.hpp**

Create `include/gamev2/ecs/adapt/components.hpp`:

```cpp
/// @file components.hpp
/// @brief 环境与遗传两个 system 共享的组件与输入结构。
///
/// 单独立一个头是因为它们互相要用：EnvironmentSystem 的 ReadSet 需要
/// genetics_systems.hpp 里的 SpeciesTag / CreaturePhenotype，因此
/// environment_systems.hpp 已经 include 了它；BreedingSystem 又要用
/// BreedingTarget 与 Position，反向 include 就是头文件环。
#pragma once

#include "gamev2/environment/climate.hpp"

#include "gamev2/genetics/species.hpp"

#include <cstdint>

namespace gamev2::ecs::adapt {

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

/// 本季实际摄入满足度。EnvironmentSystem 写，BreedingSystem 读。
///
/// 必须走组件而不是跨 system 传下标数组：两个 system 各自 for_each_entity
/// 一遍，遍历顺序是各自的实现细节，拿同一个下标去对齐它们是隐患。
struct Feeding
{
    float satisfaction = 1.0f;
};

/// 一个待繁殖位置的输入。EnvironmentSystem 填，BreedingSystem 读。
struct BreedingTarget
{
    gamev2::genetics::SpeciesId species;
    gamev2::environment::CellIndex cell;
    std::uint32_t progeny = 0;
    gamev2::environment::EnvironmentSnapshot environment;
};

}  // namespace gamev2::ecs::adapt
```

- [ ] **Step 4: 写 environment_systems.hpp**

Create `include/gamev2/ecs/adapt/environment_systems.hpp`:

```cpp
/// @file environment_systems.hpp
/// @brief 环境模块的 ECS 适配层：推进气候、跑完季内摄食、算承载力预算，
///        并把逐个体的 Feeding 与逐站点的 BreedingTarget 交给 BreedingSystem。
///
/// 跨 system 传递的一律是 ECS 组件或按物种分组后的站点表，不传下标数组：
/// 两个 system 各自 for_each_entity 一遍，遍历顺序是各自的实现细节。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/sites.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {

/// 气候场与资源场。整局唯一一份，不进 ECS——它是单一实例的场景状态，
/// 放进组件只会让「每个实体各有一份气候」这个错误写法看起来合法。
class EnvironmentWorld
{
public:
    EnvironmentWorld(std::uint64_t root_seed,
                     const gamev2::environment::EnvironmentConfig& config);

    [[nodiscard]] gamev2::environment::ClimateField& climate() noexcept;
    [[nodiscard]] gamev2::environment::ResourceField& resources() noexcept;
    [[nodiscard]] const gamev2::environment::EnvironmentConfig& config()
        const noexcept;

private:
    gamev2::environment::EnvironmentConfig config_;
    gamev2::environment::ClimateField climate_;
    gamev2::environment::ResourceField resources_;
};

/// **必须排在 BreedingSystem 之前**。与 AgingSystem 必须排在 BreedingSystem
/// 之前是同一类约束：WriteSet 只能让调度器分批，表达不了批次之间的先后。
/// 顺序反了不会崩，只会让本季出生数为零。
class EnvironmentSystem final : public System
{
public:
    EnvironmentSystem(EnvironmentWorld& env, std::uint32_t season_index)
        : env_{env}, season_index_{season_index}
    {
    }

    using ReadSet = std::tuple<const Position, const CreaturePhenotype,
                               const SpeciesTag>;
    using WriteSet = std::tuple<Feeding>;

    void execute(World& world, CommandBuffer& cmd) override;

    /// 传给 BreedingSystem 的站点表。环境层只说「哪个格、哪个物种、
    /// 该产几个」，空间分桶与 Parent::index 的翻译留在 adapt 层。
    [[nodiscard]] const std::vector<BreedingTarget>& targets() const noexcept
    {
        return targets_;
    }

private:
    EnvironmentWorld& env_;
    std::uint32_t season_index_;
    std::vector<BreedingTarget> targets_;
};

}  // namespace gamev2::ecs::adapt
```

- [ ] **Step 5: 写 environment_systems.cpp**

Create `src/ecs/adapt/environment_systems.cpp`:

```cpp
// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/ecs/adapt/environment_systems.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::ecs::adapt {

using gamev2::environment::EnvironmentConfig;
using gamev2::environment::Forager;

EnvironmentWorld::EnvironmentWorld(
    const std::uint64_t root_seed, const EnvironmentConfig& config)
    : config_{config}, climate_{root_seed, config_}, resources_{climate_, config_}
{
}

gamev2::environment::ClimateField& EnvironmentWorld::climate() noexcept
{
    return climate_;
}

gamev2::environment::ResourceField& EnvironmentWorld::resources() noexcept
{
    return resources_;
}

const EnvironmentConfig& EnvironmentWorld::config() const noexcept
{
    return config_;
}

void EnvironmentSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    env_.climate().advance(season_index_);

    // 一次遍历收齐摄食所需的全部输入。缺 Position 或 SpeciesTag 的实体
    // 本季不参与环境结算：BreedingSystem 的空间分桶用同一把尺子量它们，
    // 两个 system 对「谁参与繁殖」必须给出同一个答案。
    std::vector<EntityId> handles;
    std::vector<Forager> foragers;
    std::vector<std::uint32_t> species_keys;

    world.for_each_entity([&](const EntityId e)
    {
        const auto* pos = world.get_if<Position>(e);
        const auto* phe = world.get_if<CreaturePhenotype>(e);
        const auto* tag = world.get_if<SpeciesTag>(e);
        if (pos == nullptr || phe == nullptr || tag == nullptr
            || tag->species.value == 0)
        {
            return;
        }
        // 摄食量正比于感知范围与繁殖力，与遗传层能量收支约束同源；
        // 公式住在遗传层，环境层只收一个标量。
        const float demand =
            phe->phenotype.traits[trait_index(TraitKind::SenseRange)]
            * phe->phenotype.traits[trait_index(TraitKind::Fecundity)];

        handles.push_back(e);
        foragers.push_back(Forager{pos->x, pos->y, demand});
        species_keys.push_back(tag->species.value);
    });

    env_.resources().count_occupants(foragers);

    const auto feeding =
        gamev2::environment::run_season(env_.resources(), foragers,
                                        env_.config());

    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        // 首次见到就加上组件，之后各季只改值。add 会把实体迁往新
        // archetype，而本函数随后还要按 handles 取组件，因此 add 必须在
        // 全部遍历完成之后做。
        (void)world.add<Feeding>(handles[i], Feeding{feeding[i]});
    }

    const auto sites = gamev2::environment::build_sites(
        env_.resources(), env_.climate(), foragers, species_keys, env_.config());

    targets_.clear();
    targets_.reserve(sites.size());
    for (const auto& s : sites)
    {
        targets_.push_back(BreedingTarget{
            gamev2::genetics::SpeciesId{s.species_key}, s.cell, s.progeny,
            s.environment});
    }
}

}  // namespace gamev2::ecs::adapt
```

顶部补 `#include "gamev2/genetics/expression.hpp"`（`trait_index` 的声明
在其头文件里）。

- [ ] **Step 6: 注册测试文件**

`ENVIRONMENT_TEST_SOURCES` 追加 `tests/test_environment_systems.cpp`。

- [ ] **Step 7: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`environment_tests` 输出 `28 test(s), 0 failed`
（24 + systems 4）。其余两个用例数不变。

若 `environment_system_writes_feeding_for_every_creature` 的 `seen != 200`：
`Feeding` 的 `add` 若在遍历过程中发生会让 archetype 迁移，中途的
`for_each_entity` 会漏掉或重复实体。把 add 挪到遍历结束后（Step 5 的
写回段已经在遍历之后，确认它没有和第一段的 `for_each_entity` 交错）。

若 `total_birth_budget_stays_under_total_capacity` 失败：说明
`build_sites` 的生态上限没生效，检查 `headroom` 是不是算成了 `capacity`。

- [ ] **Step 8: 报告并等待提交决定**

**不执行 git commit。**

---

## Task 8: BreedingSystem 接入环境

**Files:**
- Modify: `include/gamev2/ecs/adapt/genetics_systems.hpp`
- Modify: `src/ecs/adapt/genetics_systems.cpp`
- Test: `tests/test_genetics_systems.cpp`

**Interfaces:**
- Consumes: `BreedingTarget` / `Position` / `Feeding`（Task 7）
- Produces: `BreedingSystem` 的 7 参数构造函数；原 5 参数构造函数保留，
  委托到它并传 `{nullptr, 0.0f}`

- [ ] **Step 1: 写失败测试**

在 `tests/test_genetics_systems.cpp` 末尾追加：

```cpp
TEST(adapt_breeding_uses_environment_targets_when_provided)
{
    World w;
    Harness h;
    for (int i = 0; i < 12; ++i)
    {
        const auto e = h.spawn(w);
        (void)w.add<Position>(e, Position{static_cast<float>(i % 4),
                                          static_cast<float>(i / 4)});
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.05f);
    }

    EnvironmentWorld env{31, gamev2::environment::EnvironmentConfig{}};
    EnvironmentSystem environment{env, 0};

    CommandBuffer env_cmd;
    environment.execute(w, env_cmd);
    env_cmd.flush(w);

    BreedingSystem sys{h.table, h.species, h.config, 7, 0,
                       &environment.targets(), 1.0f};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 环境侧至少给出了一个站点，且预算被承载力封顶后仍有出生。
    CHECK(!environment.targets().empty());
    CHECK(w.entity_count() > 12);
}

TEST(adapt_breeding_without_environment_still_works)
{
    World w;
    Harness h;
    for (int i = 0; i < 4; ++i)
    {
        const auto e = h.spawn(w);
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.5f);
    }

    BreedingSystem sys{h.table, h.species, h.config, 7, 0};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 降级路径必须与本轮之前逐位相同：仍然按 progeny_for 出子代。
    CHECK(w.entity_count() > 4);
}
```

文件顶部补 `#include "gamev2/ecs/adapt/components.hpp"`、
`#include "gamev2/ecs/adapt/environment_systems.hpp"`、
`#include "gamev2/environment/config.hpp"`。

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`BreedingSystem` 没有 7 参数构造函数。

- [ ] **Step 3: 加构造函数与成员**

`include/gamev2/ecs/adapt/genetics_systems.hpp` 顶部加
`#include "gamev2/ecs/adapt/components.hpp"`。

`BreedingSystem` 的构造函数区替换为：

```cpp
    /// 原有构造函数：不接入环境层，父池退化为「一物种一池 + progeny_for」，
    /// 行为与本轮之前逐位相同。
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index}
    {
    }

    /// 接入环境层：父池改用 targets 给的站点，feeding 取自 Feeding 组件，
    /// 新生个体落在其站点的格内。`targets` 为 nullptr 时行为与上一个
    /// 构造函数完全一致。
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index,
                   const std::vector<BreedingTarget>* targets,
                   float mating_radius_cells)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index},
          targets_{targets}, mating_radius_cells_{mating_radius_cells}
    {
    }
```

私有成员区追加：

```cpp
    /// nullptr 表示未接入环境层。指针而非引用：调用方可能这一季还没有
    /// 环境系统跑过，那时给一个空引用比给 nullptr 更难查。
    const std::vector<BreedingTarget>* targets_ = nullptr;
    float mating_radius_cells_ = 0.0f;
```

顶部补 `#include <vector>`（已有则跳过）。

- [ ] **Step 4: 收集位置与 feeding**

`src/ecs/adapt/genetics_systems.cpp` 的收集遍历里，与 `push_back(threats…)`
相邻处补：

```cpp
        const auto* pos = world.get_if<Position>(e);
        positions.push_back(pos == nullptr ? Position{}
                                            : Position{pos->x, pos->y});
        // 缺 Feeding 组件按理论摄食上限处理，与「未接入环境层」是同一条
        // 降级路径，两者必须给出同一个答案。
        const auto* fed = world.get_if<Feeding>(e);
        feedings.push_back(fed == nullptr ? 1.0f : fed->satisfaction);
```

并在这两个 vector 声明处补上 `std::vector<Position> positions;` 与
`std::vector<float> feedings;`。

按物种切池时，在 `pool_threats.push_back(threats[i]);` 之后补：

```cpp
            pool_feedings.push_back(feedings[i]);
```

并在切池的局部声明区补 `std::vector<float> pool_feedings;`。

- [ ] **Step 5: 加空间分桶辅助**

`src/ecs/adapt/genetics_systems.cpp` 的匿名 namespace 内追加：

```cpp
/// 格距离是否在半径内。切比雪夫距离——正方形邻域，与「半径 R 格」的
/// 自然读法一致。
[[nodiscard]] bool in_radius(const gamev2::environment::CellIndex from,
                             const gamev2::environment::CellIndex to,
                             const float radius_cells) noexcept
{
    const auto side = static_cast<std::uint16_t>(
        gamev2::environment::kGridSide);
    const auto dx = std::fabs(static_cast<float>(from.value % side)
                              - static_cast<float>(to.value % side));
    const auto dy = std::fabs(static_cast<float>(from.value / side)
                              - static_cast<float>(to.value / side));
    return std::max(dx, dy) <= radius_cells;
}

/// 子代落在它所属格里的坐标。抖动只由出生序号决定，同一种子两次运行
/// 必须落在同一处，否则确定性测试挂在个体位置而不是算法上。
[[nodiscard]] Position place_in(const gamev2::environment::CellIndex cell,
                                const std::size_t birth_index) noexcept
{
    const auto side = static_cast<std::uint16_t>(
        gamev2::environment::kGridSide);
    const auto slot =
        static_cast<std::uint32_t>((birth_index * 2654435761ULL) % 1000U);
    const float jitter = static_cast<float>(slot) / 1000.0f * 0.9f;
    return Position{static_cast<float>(cell.value % side) + jitter,
                    static_cast<float>(cell.value / side) + jitter};
}
```

顶部补 `#include "gamev2/ecs/adapt/components.hpp"`、
`#include "gamev2/environment/climate.hpp"`。

- [ ] **Step 6: 用站点替掉单池**

把 `src/ecs/adapt/genetics_systems.cpp` 中构造

```cpp
        const std::vector<ParentPool> sites{ParentPool{
            parents, SpeciesId{s}, std::nullopt, progeny_for(pool_phenotypes)}};
```

的那一段（连同它上面的 `std::vector<Parent> parents; parents.reserve(...);`
循环）整体替换为：

```cpp
        std::vector<ParentPool> sites;
        // 与 sites 同序：sites[k] 的子代落在 target_cells[k]。
        std::vector<gamev2::environment::CellIndex> target_cells;
        if (targets_ != nullptr)
        {
            // 空间分桶在 adapt 层完成：Parent::index 必须是本物种数组内的
            // 下标，而那个数组由本函数切出，环境层无从知道它的形状。
            // 缺 Position 的个体进不了任何桶——环境层同样只为有 Position
            // 的实体产出站点，两个 system 对「谁参与繁殖」答案一致。
            for (const auto& target : *targets_)
            {
                if (target.species.value != s)
                {
                    continue;
                }
                std::vector<Parent> bucket;
                for (std::size_t i = 0; i < members.size(); ++i)
                {
                    const auto& p = positions[members[i]];
                    if (in_radius(CellIndex::of(p.x, p.y), target.cell,
                                  mating_radius_cells_))
                    {
                        bucket.push_back(Parent{i, &pool_genomes[i], 1.0f});
                    }
                }
                if (bucket.size() < 2)
                {
                    // 池内不足两个个体时 make_offspring 必然失败，
                    // 与其让它空转一轮，不如不产出子代——省下的格子
                    // 才能让子代认对位置。
                    continue;
                }
                sites.push_back(ParentPool{bucket, SpeciesId{s},
                                           target.environment,
                                           target.progeny});
                // 与 sites 同序：sites[k] 的子代落在 target_cells[k]。
                target_cells.push_back(target.cell);
            }
        }
        else
        {
            std::vector<Parent> parents;
            parents.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                parents.push_back(Parent{i, &pool_genomes[i], 1.0f});
            }
            sites.push_back(ParentPool{parents, SpeciesId{s}, std::nullopt,
                                       progeny_for(pool_phenotypes)});
        }
```

- [ ] **Step 7: 传 feedings 进 settle**

把 `settle` 调用处改为：

```cpp
        const auto out = settle(
            SeasonInput{pool_genomes, pool_phenotypes, pool_environments,
                        pool_threats, pool_species, sites, false,
                        std::span<const float>{pool_feedings}},
            table_, species_, config_, streams);
```

顶部补 `#include <span>`。

- [ ] **Step 8: 新生个体落位**

`StagedOffspring` 追加一个字段：

```cpp
struct StagedOffspring
{
    SpeciesId species;
    Genome genome;
    Phenotype phenotype;
    EnvironmentSnapshot environment;
    gamev2::environment::CellIndex cell;
};
```

在 `sites` 构造完成之后、`settle` 调用之前，预先算出每个子代该落在
哪一格：

```cpp
        // 子代认回自己的格靠顺序：settle 按 sites 顺序逐个追加子代。
        // 池内不足两个个体的站点在 Step 6 里已被剔除，因此每个站点恰好
        // 贡献 target.progeny 个子代或零个，两种情况都能对上。
        std::vector<gamev2::environment::CellIndex> birth_cells;
        if (targets_ != nullptr)
        {
            for (std::size_t k = 0; k < sites.size(); ++k)
            {
                for (std::uint32_t n = 0; n < sites[k].progeny; ++n)
                {
                    birth_cells.push_back(target_cells[k]);
                }
            }
        }
```

`settle` 返回后，收集子代处改为：

```cpp
        for (std::size_t i = 0; i < out.offspring.size(); ++i)
        {
            const auto cell =
                birth_cells.empty()
                    ? gamev2::environment::CellIndex{}
                    : birth_cells[std::min(i, birth_cells.size() - 1)];
            born.push_back(StagedOffspring{SpeciesId{s}, out.offspring[i],
                                           out.offspring_phenotypes[i],
                                           out.offspring_environments[i],
                                           cell});
        }
```

最后，`cmd.create` 处补 `Position`——组件必须随 create 同一条命令提交，
事后 `add` 不成立：

```cpp
    newborn_.assign(born.size(), EntityId{});
    for (std::size_t i = 0; i < born.size(); ++i)
    {
        const auto place = place_in(born[i].cell, i);
        cmd.create(newborn_[i], SpeciesTag{born[i].species},
                   Environment{born[i].environment}, born[i].genome,
                   CreaturePhenotype{born[i].phenotype}, Vitality{},
                   Position{place.x, place.y});
    }
```

- [ ] **Step 9: 构建并跑全部测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
Write-Output ('genetics exit=' + $LASTEXITCODE)
& .\build\environment_tests.exe
Write-Output ('environment exit=' + $LASTEXITCODE)
& .\build\ecs_tests.exe
Write-Output ('ecs exit=' + $LASTEXITCODE)
```

Expected: 三个都 `exit=0`。`genetics_tests` 用例数比 Task 7 结束时
多 2，其余两个不变。

`genetics_tests` 出现失败说明 Step 6 的 `else` 分支被改坏了——它必须与
原代码逐字等价。

- [ ] **Step 10: 报告并等待提交决定**

向用户报告三个测试可执行文件的用例数与失败数。**不执行 git commit。**

---

## 验收标准

全部任务完成后，以下每一条都必须成立：

1. `cmake --build build` 无警告无错误
2. `& .\build\environment_tests.exe` 输出 `0 failed`
3. `& .\build\genetics_tests.exe` 输出 `0 failed`
4. `& .\build\ecs_tests.exe` 输出 `0 failed`
5. ASan 构建下三个测试可执行文件均无 sanitizer 报告：

   ```powershell
   cmake -S . -B build-asan -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DGAMEV2_ENABLE_ASAN=ON
   cmake --build build-asan
   & .\build-asan\cmake\asan\game_tests_asan.exe
   Write-Output ('asan exit=' + $LASTEXITCODE)
   ```

   **只有 `build-asan\cmake\asan\game_tests_asan.exe` 是插桩的。**
   `build-asan\` 根目录下也有一份 `environment_tests.exe` / `genetics_tests.exe` /
   `ecs_tests.exe`，它们是普通的未插桩 Debug 目标——跑它们会得到一个
   虚假的绿色。`game_tests_asan` 的源列表由 CMakeLists 里那个四表
   `foreach` 喂出，本任务把 `ENVIRONMENT_SOURCES` / `ENVIRONMENT_TEST_SOURCES`
   加进去，因此它同时包含三个测试套件。
6. `run_season` 对 1 万个体 × 8 步在 Debug 构建下 1 秒内完成
7. 换个体遍历顺序不改变 `feedings` 与资源场终态
8. 未接入环境层时 `settle` 与 `BreedingSystem` 的行为与本轮之前逐位相同
9. `environment/` 下无任何文件 include `gamev2/genetics/` 或 `gamev2/ecs/`
10. 全部代码无一行超过 90 字符

## 已知偏离与遗留

- `ParentPool::environment` 与 `settle` 第 2 步的 `site_environment` 早于本
  轮就已接通；本轮只是让它们第一次拿到真实采样值。
- `progeny_for`（遗传设计文档决策 12 的 `N × Fecundity`）在无环境层时仍是
  默认路径。它保留下来正好是承载力的对照基线。
- `threats` 仍由调用方给，环境层不产生捕食威胁。
- 个体在格内的移动不在本轮范围：`count_occupants` 季初数一次，
  季内个体不换格。
- `EnvironmentWorld` 不进 ECS，是整局唯一一份的场景状态。
- `settle` 把 `ParentPool::progeny == 0` 按 1 处理这条老行为仍然保留。
  环境层改为不发出 0 预算站点来绕开它，而不是改遗传层——改它会动到
  既有测试里依赖「一物种一池 + progeny_for」的用例。

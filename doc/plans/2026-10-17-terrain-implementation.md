# 阶段 2（地形 + 移动重写）实现计划

> **本文档是阶段 2 的实施快照，不是活文档。** 它记录当时打算怎么做，其中若干
> 结论后来被实测推翻或更正（梯度「填坑」的说法、「定向行程足以跛到对岸」的
> 论证、`best_stock` 的初值等）。**读结论请以
> `doc/2026-10-17-game-design.md` 为准** —— 那里带撤回标注与修正节。

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给世界装上第一份空间结构——由 `root_seed` 生成的地形场，以及会
看地形与会追资源存量的 `MovementSystem`，使同一物种在屏障两侧分化出不同
的占位与遗传结构。

**Architecture:** `environment/` 新增 `TerrainField`（值噪声铺高程，按全图
分位数切水陆），`ResourceField` 改吃它并把水格 capacity 归零，让
`Σcapacity` 如实反映可用面积。`MovementSystem` 重写为「掷硬币 → 跟存量
梯度或随机游走 → 整数步长逐格推进、不可通行则沿墙滑」。地形与趋向互相
成就：个体朝富格走，贴水陆地格的可选项天然更少，海岸与内陆因此自发分化
——**地理结构不靠玩家画墙就已经出现**。

**Tech Stack:** C++23、clang++ 23、CMake + Ninja、手写测试框架
（`TEST` / `CHECK` / `CHECK_EQ`，见 `tests/test_framework.hpp`）。**零第三方
依赖**：地形噪声手写，仓库至今没有任何外部库，不要为一个高度场引一个。

**Spec:** `doc/2026-10-17-game-design.md`（阶段 2 对应 §2.1 实测、决策 15、
§4.1、§4.2、§9 的 1/1b/2/3/3b/4 六条测试、§8 确定性）

## 范围

本计划只做 spec §10 的**阶段 2**。阶段 3（`SpeciationSystem`）、阶段 4
（玩家画墙 / 气候笔刷 / 基因库编辑）各出一份独立计划。

同种趋向着陆阶段 3：它服务的是裂变时的空间连通分量判据，阶段 2 单独交付时
看不见效果，且会让空而肥的格更空（实测那 519 个空格里没有同种个体可循）。

## Global Constraints

- 每行不超过 90 字符；长条件在 `&&` / `||` 处断行，每个操作数独占一行
- 注释只写「为什么」与约束，不写「做什么」；既有注释一律保留
- 不新增 `shared_ptr` / `unique_ptr`；不引入任何第三方库
- **不执行 `git commit`**。每个任务末尾照常给出命令，由用户自己决定何时提交
- 改动既有代码时只碰本次需求牵动的行，不顺手重构相邻代码
- **`src/environment/` 不得 include 任何 `genetics/` 或 `ecs/` 头文件**
  （`CMakeLists.txt` 里那条硬约束：给 genetics 加
  `target_link_libraries(... environment)` 会在链接期以 undefined reference
  爆出来）。因此 `terrain.cpp` 自带散列，不许用 `genetics/random.hpp`
- 构建：`pwsh -File scripts/build.ps1 -Test`
- 单套件运行：`& build/environment_tests.exe`（其余同理 `ecs_tests` /
  `genetics_tests` / `world_tests` / `game_tests`）
- 新增源文件必须同时加进 `CMakeLists.txt` 的目标源列表与
  `GAMEV2_ENABLE_ASAN` 的 foreach 列表。本计划新增的 `terrain.cpp` 与
  `test_movement.cpp` 分别落进 `ENVIRONMENT_SOURCES` 与
  `ENVIRONMENT_TEST_SOURCES`，而 foreach 迭代的正是这两个变量本身，因此
  只要加进变量就自动进了 ASan 列表——**不需要另加一行，但要在改完
  CMakeLists 后回头确认这一点**

## 文件地图

| 文件 | 责任 |
|------|------|
| `include/gamev2/environment/terrain.hpp` | 新建。`TerrainField` 声明 |
| `src/environment/terrain.cpp` | 新建。值噪声 + 分位数海平面 |
| `tests/test_terrain.cpp` | 新建。地形自身的确定性、水陆比、连通结构 |
| `include/gamev2/environment/config.hpp` | 改。加 `water_fraction` / `gradient_weight` |
| `include/gamev2/environment/resource.hpp` | 改。构造函数多吃一个地形 |
| `src/environment/resource.cpp` | 改。水格 capacity 归零 |
| `tests/test_environment_resource.cpp` | 改。21 处构造点之一（7 处） |
| `tests/test_environment_sites.cpp` | 改。21 处构造点之二（14 处） |
| `include/gamev2/ecs/adapt/environment_systems.hpp` | 改。`EnvironmentWorld` 持地形 |
| `src/ecs/adapt/environment_systems.cpp` | 改。同上 |
| `include/gamev2/ecs/adapt/movement_system.hpp` | 改。构造函数 + 私有辅助 |
| `src/ecs/adapt/movement_system.cpp` | 改。execute 重写 |
| `tests/test_movement.cpp` | 新建。移动的单元测试 |
| `src/world/world.cpp` | 改。给 `movement_` 喂地形与资源 |
| `tests/test_world_module.cpp` | 改。集成测试 + 夹具适配 |
| `src/sim/main.cpp` | 改。播种跳过水格 + 重取读数 |
| `doc/2026-10-17-game-design.md` | 改。回填基线读数 |

---

### Task 1: TerrainField——值噪声地形场

**Files:**
- Modify: `include/gamev2/environment/config.hpp`
- Create: `include/gamev2/environment/terrain.hpp`
- Create: `src/environment/terrain.cpp`
- Create: `tests/test_terrain.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 既有的 `EnvironmentConfig`、`CellIndex`、`kGridSide`、`kCellCount`
- Produces:
  - `EnvironmentConfig::water_fraction`（`float`，默认 `0.35f`）
  - `EnvironmentConfig::gradient_weight`（`float`，默认 `0.5f`；本任务
    只加字段，Task 3 才消费它）
  - `class gamev2::environment::TerrainField`，公开成员：
    `static TerrainField generate(std::uint64_t root_seed, const EnvironmentConfig& config) noexcept`
    / `[[nodiscard]] bool passable(CellIndex) const noexcept`
    / `[[nodiscard]] float elevation(CellIndex) const noexcept`

- [ ] **Step 1: 写失败测试**

新建 `tests/test_terrain.cpp`：

```cpp
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/terrain.hpp"

#include "test_framework.hpp"

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

        CHECK(sizes.size() >= 2);
        CHECK(sizes[1] > 200);
    }
}
```

**某个种子上 `sizes[1] <= 200` 时不要放宽断言** —— 那说明那张图真的没有能
充当屏障的次大陆。把种子报上来，换种子或记录该图的形态，由人来定。

该文件的 using 列表里还需要 `#include <algorithm>`（`std::min`）与
`#include <vector>`。

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`gamev2/environment/terrain.hpp` 不存在。

- [ ] **Step 3: 给 EnvironmentConfig 加两个字段**

在 `include/gamev2/environment/config.hpp` 的 `capacity_base` 之后插入：

```cpp
    /// 地形中水格应占的比例。海平面取**全图高程的这个百分位**而不是写死
    /// 一个常数：写死的话不同种子的水陆比会在 15%~50% 之间乱跳，这个值就
    /// 成了一个平均数而不是保证值。
    ///
    /// **默认 0.70 而不是凭直觉的「三成水」**：陆地是否碎成多块由海平面落在
    /// 高程分布的哪个位置决定，而海平面取的就是这个百分位。实测（见
    /// terrain.cpp 的基频注释）在 0.35 下六个种子有三个给出 2640 格的单一
    /// 大陆，阶段 2 的屏障无从成立；0.70 下每个种子都有一块 226~474 格的
    /// 次大陆，而 Σcapacity 落到 2995，正是设计文档 §10 写的游戏规模目标。
    ///
    /// 0 表示全陆地（既有 environment 测试靠这个配置保持原有断言），
    /// 1 表示除最高一格外全水。
    ///
    /// \pre 取值 ∈ [0, 1]：它直接当分位数下标用，越界会索引出 std::array 之外。
    float water_fraction = 0.70f;
    /// 个体每季跟资源存量梯度的概率，其余走随机游走。
    ///
    /// 0.5 让趋向与随机游走势均力敌；再高则同格的个体会高度同步（它们看到的邻域完全相同），
    /// 全军爬上同一个局部极大。0 表示关闭趋向，退回纯随机游走——集成测试
    /// 的对照组正是它。
    ///
    /// \pre 取值 ∈ [0, 1]。本模块没有配置期校验机制，只在这里写明。
    float gradient_weight = 0.5f;
```

- [ ] **Step 4: 声明 TerrainField**

新建 `include/gamev2/environment/terrain.hpp`：

```cpp
/// @file terrain.hpp
/// @brief 地形场：逐格海拔与通行性。与 ClimateField / ResourceField 同形状
///        的定长场，本身不含任何模拟逻辑。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.1。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::environment {

/// 生成一次，此后只读：地形完全由 root_seed 定出，不随季节变化，也不接受
/// 任何修改入口（玩家改写地形属阶段 4）。
class TerrainField
{
public:
    /// 值噪声 3 个八度铺 elevation，再按全图第 config.water_fraction
    /// 百分位切成水陆。
    ///
    /// \pre config.water_fraction ∈ [0, 1]（见 EnvironmentConfig 的说明）。
    static TerrainField generate(std::uint64_t root_seed,
                                 const EnvironmentConfig& config) noexcept;

    /// 不可通行的唯一来源是水。山脉不额外处理——这样「屏障」在语义上只有
    /// 一个，玩家也能一眼看懂屏障为什么有效。
    ///
    /// 与 ClimateField::base 同样，`\pre` 只约束走 CellIndex::of 的调用方，
    /// 不是类型级不变量：聚合初始化能绕过它（CellIndex{5000}）。
    [[nodiscard]] bool passable(CellIndex cell) const noexcept;

    /// 供渲染做水墨晕染与玩家画屏障时的合法性提示。本模块自己不用它，
    /// 因此它只被测试与将来的渲染层读到。
    ///
    /// 取值范围是 [0, 1 + gain + gain²] = [0, 1.75]，不是 [0, 1]：三个
    /// 八度的权重按 0.5 递减而和为 1.75。渲染层要归一化得按 1.75。
    [[nodiscard]] float elevation(CellIndex cell) const noexcept;

private:
    std::array<float, kCellCount> elevation_{};
    std::array<bool, kCellCount> passable_{};
};

}  // namespace gamev2::environment
```

- [ ] **Step 5: 实现 TerrainField**

新建 `src/environment/terrain.cpp`：

```cpp
#include "gamev2/environment/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gamev2::environment {

namespace {

/// 八度参数照 doc/美术风格规格.md §2.4：3 个八度、lacunarity 2.0、
/// gain 0.5，噪声族取该节许可的 Perlin / 值噪声一脉。
///
/// **基频取 0.10 而不是规格给的 0.02，也不取中间值 0.06**：真正决定陆地
/// 碎裂程度的不是基频，而是海平面落在高程分布的哪个位置 —— 阈值远低于
/// 中位数时，平滑场的超水平集几乎总是连成一整块，抬八度能量与降基频都
/// 动不了它。实测扫过 (基频, 八度数, gain) 共 30 组，largest/land 的取值
/// 范围永远在 74%~99%；把水量提到 70% 之后，基频 0.10 是唯一一档六个
/// 种子都给出「主大陆（陆地 33%~53%）+ 一块 226~474 格的次大陆」的组合。
/// 0.06 在同样的水量下最坏种子仍给出 98% 的单块大陆。
constexpr float kBaseFrequency = 0.10f;
constexpr std::size_t kOctaves = 3;
constexpr float kLacunarity = 2.0f;
constexpr float kGain = 0.5f;

/// splitmix64 的终段混函数。**本模块不许 include genetics/ 头**
/// （CMakeLists 里那条硬约束），因此自带一个散列——它只负责把 root_seed
/// 变成一张置换表的种子。
[[nodiscard]] std::uint64_t mix(const std::uint64_t v) noexcept
{
    auto x = v;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

/// 256 项置换表。值噪声要的是「任意两格点取值不同且看不出规律」，置换表
/// 比拿格点下标直接混一次更均匀，也让噪声与 root_seed 解耦。
class Permutation
{
public:
    explicit Permutation(const std::uint64_t seed) noexcept
    {
        for (std::size_t i = 0; i < kSize; ++i)
        {
            table_[i] = static_cast<std::uint8_t>(i);
        }
        // Fisher-Yates，倒着走。与顺序无关的置换表不要求任何一种特定
        // 洗牌方向，但固定方向才能让同 seed 逐位复现。
        for (std::size_t i = kSize - 1; i > 0; --i)
        {
            const auto j = static_cast<std::size_t>(
                mix(seed + i) % static_cast<std::uint64_t>(i + 1));
            std::swap(table_[i], table_[j]);
        }
    }

    [[nodiscard]] std::uint8_t at(const std::size_t i) const noexcept
    {
        return table_[i % kSize];
    }

private:
    static constexpr std::size_t kSize = 256;
    std::array<std::uint8_t, kSize> table_{};
};

/// 5 次平滑。值噪声格内插值若用线性，二阶导不连续，抬高八度后能看出
/// 方格棱；smoothstep 让一阶导也连续。
[[nodiscard]] constexpr float smooth(const float t) noexcept
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// 单个八度的值噪声：格点取整坐标，格内对四角做 smoothstep 双线性插。
[[nodiscard]] float octave(const Permutation& perm, const float x,
                           const float y) noexcept
{
    const auto gx = static_cast<std::int64_t>(std::floor(x));
    const auto gy = static_cast<std::int64_t>(std::floor(y));
    const auto fx = smooth(x - static_cast<float>(gx));
    const auto fy = smooth(y - static_cast<float>(gy));

    const auto corner = [&perm](const std::int64_t dx, const std::int64_t dy)
    {
        // 负下标转 unsigned 是模运算（well-defined），不进 UB；两个大质数
        // 相乘再异或是标准的二维格点散列。
        const auto h = static_cast<std::uint64_t>(dx) * 73856093ull
                       ^ static_cast<std::uint64_t>(dy) * 19349663ull;
        return static_cast<float>(perm.at(static_cast<std::size_t>(mix(h))))
               / 255.0f;
    };

    const auto c00 = corner(gx, gy);
    const auto c10 = corner(gx + 1, gy);
    const auto c01 = corner(gx, gy + 1);
    const auto c11 = corner(gx + 1, gy + 1);

    const auto top = c00 + (c10 - c00) * fx;
    const auto bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}

}  // namespace

TerrainField TerrainField::generate(
    const std::uint64_t root_seed, const EnvironmentConfig& config) noexcept
{
    const Permutation perm{root_seed};
    TerrainField out{};

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        // CellIndex::value 的布局是 iy * kGridSide + ix，直接拆回 (x, y)。
        const auto px = static_cast<float>(i % kGridSide);
        const auto py = static_cast<float>(i / kGridSide);

        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = kBaseFrequency;
        for (std::size_t o = 0; o < kOctaves; ++o)
        {
            sum += octave(perm, px * frequency, py * frequency) * amplitude;
            amplitude *= kGain;
            frequency *= kLacunarity;
        }
        out.elevation_[i] = sum;
    }

    // 海平面取分位数。抄一份排序后 nth_element 就够——全序在构造期跑一次，
    // 4096 个 float，代价可忽略，而它换来的是「任意种子的水陆比都落在
    // 目标附近」这条保证。
    auto sorted = out.elevation_;
    const auto cut = static_cast<std::size_t>(
        static_cast<float>(kCellCount - 1) * config.water_fraction);
    // ranges 版收的是迭代器而不是下标，别写成 nth_element(sorted, cut)。
    std::ranges::nth_element(sorted,
                             sorted.begin()
                                 + static_cast<std::ptrdiff_t>(cut));
    const float sea_level = sorted[cut];

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        // 陆地取 >= 而不是 >：water_fraction = 0 时 cut 落在最低值上，
        // sea_level 就是全图最小高程，写 > 会把那一格单独判成水，
        // 「全陆地世界」就差一格，而 Task 2 的既有测试正建立在它上面。
        out.passable_[i] = out.elevation_[i] >= sea_level;
    }
    return out;
}

bool TerrainField::passable(const CellIndex cell) const noexcept
{
    return passable_[cell.value];
}

float TerrainField::elevation(const CellIndex cell) const noexcept
{
    return elevation_[cell.value];
}

}  // namespace gamev2::environment
```

需要 `#include <array>`（经由 terrain.hpp 传递包含即可，但显式写出更稳）。
`std::ranges::nth_element` 需要 `<algorithm>`。

- [ ] **Step 6: 接 CMake**

`CMakeLists.txt` 的 `ENVIRONMENT_SOURCES` 加 `src/environment/terrain.cpp`，
`ENVIRONMENT_TEST_SOURCES` 加 `tests/test_terrain.cpp`。

**不必改 ASan 的 foreach** —— 它迭代的正是这两个变量。但改完请确认：

```bash
pwsh -File scripts/build.ps1 -Test -Asan
```

Expected: 五个套件全过且 `build/` 下确有 `environment_tests.exe`。

- [ ] **Step 7: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: `environment_tests` 里 5 条新测试通过；其余四套件一字未改仍过。

- [ ] **Step 8: 提交（由用户决定）**

```bash
git add include/gamev2/environment/config.hpp include/gamev2/environment/terrain.hpp src/environment/terrain.cpp tests/test_terrain.cpp CMakeLists.txt
git commit -m "feat(environment): add a value-noise terrain field with a quantile sea level"
```

---

### Task 2: ResourceField 吃地形

**Files:**
- Modify: `include/gamev2/environment/resource.hpp`
- Modify: `src/environment/resource.cpp`
- Modify: `include/gamev2/ecs/adapt/environment_systems.hpp`
- Modify: `src/ecs/adapt/environment_systems.cpp`
- Modify: `tests/test_environment_resource.cpp`
- Modify: `tests/test_environment_sites.cpp`
- Modify: `tests/test_environment_systems.cpp`（**清单初稿漏了它**：它也构造
  `EnvironmentWorld`，默认 70% 水会让把个体放在水格里的测试变红）
- Test: `tests/test_terrain.cpp`（加一条）

**Interfaces:**
- Consumes: Task 1 的 `TerrainField`
- Produces:
  - `ResourceField(const ClimateField&, const EnvironmentConfig&,
    const TerrainField&)` —— 旧的 `(climate, config)` 签名**删除**
  - `EnvironmentWorld::terrain()`（const 通道）
- 消费方：Task 3 的 `MovementSystem`

**这个任务会改掉 21 处既有测试的构造调用。** 处理办法只有一个，且不许有
第二种：既有 environment 测试全部改成 `water_fraction = 0` 的全陆地配置。
它们的断言一个字都不动——`capacity_base × base_fertility` 这个关系在
`water_fraction = 0` 下与改前逐位相同，因此它们仍是原来那些断言。

- [ ] **Step 1: 写失败测试**

加到 `tests/test_terrain.cpp` 末尾（`namespace` 块已经关闭，直接跟在
`TEST(full_water_fraction_leaves_exactly_one_land_cell)` 之后）：

```cpp
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
```

文件顶部的 using 列表补两行：

```cpp
using gamev2::environment::ClimateField;
using gamev2::environment::ResourceField;
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`ResourceField` 的三参数构造函数不存在。

- [ ] **Step 3: 改 ResourceField 的声明**

`include/gamev2/environment/resource.hpp`，把构造函数换成：

```cpp
    /// \pre config.regen_rate ∈ (0, 1]：指数回归的收敛系数，越界会让
    ///      stock 冲出 [0, capacity]。本模块没有配置期校验机制，
    ///      因此这里只写明，不在运行时拦。
    ///
    /// 不可通行的格 capacity 恒 0，stock 因此也恒 0。
    ResourceField(const ClimateField& climate,
                  const EnvironmentConfig& config,
                  const TerrainField& terrain);
```

并在 include 列表加 `#include "gamev2/environment/terrain.hpp"`。

- [ ] **Step 4: 改 ResourceField 的实现**

`src/environment/resource.cpp`，把构造函数整个换成：

```cpp
ResourceField::ResourceField(const ClimateField& climate,
                             const EnvironmentConfig& config,
                             const TerrainField& terrain)
    : regen_rate_{config.regen_rate}
{
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto index = CellIndex{static_cast<std::uint16_t>(i)};
        // 不可通行的格 capacity 恒 0：它永远站不了人，headroom 会算出一份
        // 永远用不到的承载力，于是 Σcapacity 虚高、sim 那句百分比失去意义。
        //
        // build_sites 无需跟着改：空格里没有亲代，parent_ceiling 本来就是
        // lround(0 × offspring_scale) = 0，子代预算取 min(eco, parent)
        // 已经是 0。
        cells_[i].capacity = terrain.passable(index)
                                ? config.capacity_base
                                      * climate.base(index).base_fertility
                                : 0.0f;
        cells_[i].stock = cells_[i].capacity;
    }
}
```

- [ ] **Step 5: 让 EnvironmentWorld 持地形**

`include/gamev2/ecs/adapt/environment_systems.hpp` 的 `EnvironmentWorld`
加成员与 const 访问器：

```cpp
    /// 只读地形。**不给可变重载**：玩家改写地形属阶段 4，届时再补。
    /// MovementSystem 与 World::environment() 走的就是这一条。
    [[nodiscard]] const gamev2::environment::TerrainField& terrain()
        const noexcept;
```

```cpp
private:
    gamev2::environment::EnvironmentConfig config_;
    gamev2::environment::TerrainField terrain_;
    gamev2::environment::ClimateField climate_;
    gamev2::environment::ResourceField resources_;
```

**成员顺序不能动。** `terrain_` 必须声明在 `resources_` 之前——初始化
列表按声明顺序求值，而 `ResourceField` 的构造要吃 `terrain_`。

`src/ecs/adapt/environment_systems.cpp` 里，把

```cpp
EnvironmentWorld::EnvironmentWorld(
    const std::uint64_t root_seed, const EnvironmentConfig& config)
    : config_{config}, climate_{root_seed, config_}, resources_{climate_, config_}
{
}
```

整个换成：

```cpp
EnvironmentWorld::EnvironmentWorld(
    const std::uint64_t root_seed, const EnvironmentConfig& config)
    : config_{config},
      terrain_{gamev2::environment::TerrainField::generate(root_seed,
                                                           config_)},
      climate_{root_seed, config_},
      resources_{climate_, config_, terrain_}
{
}
```

并把 `terrain()` 的定义放在 `resources()` 的 const 重载之后：

```cpp
const gamev2::environment::TerrainField& EnvironmentWorld::terrain()
    const noexcept
{
    return terrain_;
}
```

该文件的 include 列表加 `#include "gamev2/environment/terrain.hpp"`。

- [ ] **Step 6: 改 21 处测试构造**

`tests/test_environment_resource.cpp` 与 `tests/test_environment_sites.cpp`
各加一个 helper。`test_environment_resource.cpp`：

```cpp
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
```

然后逐个把

```cpp
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    const ResourceField field{climate, config};
```

改成

```cpp
    const auto config = flat();
    const ClimateField climate{5, config};
    const TerrainField terrain = TerrainField::generate(5, config);
    const ResourceField field{climate, config, terrain};
```

**注意**：这些测试里每个 `ClimateField`/`ResourceField` 用的是不同的种子
（5、11、20……）。**terrain 必须用与 climate 同一个种子**，否则同一行里
两张图对不上号，将来排查会绕远。种子照抄原处即可。

`TerrainField` 的数据成员是 private，**不是聚合类型** —— `TerrainField
terrain{5, config}` 编译不过，只能走 `TerrainField::generate(seed, config)`。

using 列表加 `using gamev2::environment::TerrainField;`。

`tests/test_environment_sites.cpp` 同样加一个 `flat()`，14 处构造逐个改。

`tests/test_environment_systems.cpp`（初稿漏列）：它有约 20 处
`EnvironmentWorld env{N, EnvironmentConfig{}}`。默认 70% 水下，**把个体放在
水格里的那几条会变红**——水格 capacity 为 0 而 `count_occupants` 照常计数。
给这几处的 config 补 `water_fraction = 0.0f`，**断言一字不动**。其余不把个体
放在固定格上的测试可以不动，但**要逐条确认它们没有因此变得空洞**。
其中 `run_season` / `SeasonFeeder` 的几条测试不碰地形（它们只用到
ResourceField 的 stock/capacity），所以只要构造处补上 terrain 参数即可，
断言不动。

- [ ] **Step 7: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五个套件全过。`test_environment_resource.cpp` 那 7 条与
`test_environment_sites.cpp` 那 14 条**断言一字未改**——它们仍绿正是
「全陆地配置与改前逐位相同」的证据。

- [ ] **Step 8: 提交（由用户决定）**

```bash
git add include/gamev2/environment/resource.hpp src/environment/resource.cpp include/gamev2/ecs/adapt/environment_systems.hpp src/ecs/adapt/environment_systems.cpp tests/test_terrain.cpp tests/test_environment_resource.cpp tests/test_environment_sites.cpp
git commit -m "feat(environment): give water cells zero capacity"
```

---

### Task 3: MovementSystem 重写

**Files:**
- Modify: `include/gamev2/ecs/adapt/movement_system.hpp`
- Modify: `src/ecs/adapt/movement_system.cpp`
- Modify: `src/world/world.cpp`
- Create: `tests/test_movement.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_world_module.cpp`

**Interfaces:**
- Consumes: Task 1 的 `TerrainField`、Task 2 的 `ResourceField`、
  `EnvironmentConfig::gradient_weight`
- Produces:
  - `MovementSystem(std::uint64_t root_seed, std::uint32_t season_index,
    const gamev2::environment::ResourceField& resources,
    const gamev2::environment::TerrainField& terrain,
    float gradient_weight)` —— **旧的二参数构造函数删除**
  - `EnvironmentWorld::terrain()` / `resources()` 的 const 通道（Task 2 已给）

**这一步会改掉一条既有断言，先说清楚：** `test_world_module.cpp` 的
`population_never_exceeds_the_capacity_left_below_it` 断的是
`population ≤ Σcapacity`。引入地形后 `Σcapacity` 少掉水的份额，这条界
只会更松，**应当仍绿**。若它变红，说明水格没有被正确排除（capacity 没归
零），回去查 Task 2。

- [ ] **Step 1: 写失败测试**

新建 `tests/test_movement.cpp`：

```cpp
#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/movement_system.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/terrain.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::CommandBuffer;
using gamev2::ecs::World;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::MovementSystem;
using gamev2::ecs::adapt::Position;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::genetics::Phenotype;
using gamev2::genetics::TraitKind;
using gamev2::genetics::trait_index;

/// 只有 Speed 非默认，其余全给中性值——本测试只关心移动。
Phenotype mover(const float speed)
{
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::Speed)] = speed;
    return p;
}

/// 把一个个体放进指定坐标。缺组件会被 MovementSystem 跳过，因此六个里
/// 只需要 Position 与 CreaturePhenotype 两个。
EntityId place(World& world, const float x, const float y, const float speed)
{
    const EntityId e = world.create();
    (void)world.add<Position>(e, Position{x, y});
    (void)world.add<CreaturePhenotype>(e, CreaturePhenotype{mover(speed)});
    return e;
}

/// ecs::World 只有 get_if，没有 get。本文件造出来的个体都挂了 Position，
/// 因此这里不解引用 nullptr——测试若改到别处造个体，要先把这一点守住。
[[nodiscard]] const Position& at(World& world, const EntityId e)
{
    return *world.get_if<Position>(e);
}

/// 随机游走 20 季的平均位移，speed 固定。
[[nodiscard]] float mean_displacement(const float speed,
                                      const EnvironmentConfig& config,
                                      const std::uint64_t seed)
{
    EnvironmentWorld env{seed, config};
    World entities;
    const EntityId e = place(entities, 32.0f, 32.0f, speed);

    MovementSystem movement{seed, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
    CommandBuffer cmd;
    float total = 0.0f;
    for (std::uint32_t season = 0; season < 20; ++season)
    {
        movement.advance_season(season);
        const auto before = at(entities, e);
        const float bx = before.x;
        const float by = before.y;
        movement.execute(entities, cmd);
        const auto after = at(entities, e);
        total += std::hypot(after.x - bx, after.y - by);
    }
    cmd.flush(entities);
    return total / 20.0f;
}

}  // namespace

TEST(movement_is_deterministic_per_seed)
{
    // 逐位确定性契约。地形一旦引入就有可能把同一个种子的两次运行拉开，
    // 因此这条在重写当天就要立起来。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;  // 排除地形，只测随机流本身
    config.gradient_weight = 0.5f;

    const auto run = [&]()
    {
        EnvironmentWorld env{31, config};
        World entities;
        const std::vector<EntityId> ids{
            place(entities, 10.5f, 10.5f, 0.4f),
            place(entities, 32.5f, 20.5f, 1.5f),
            place(entities, 50.5f, 44.5f, 0.9f),
        };
        MovementSystem movement{31, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
        CommandBuffer cmd;
        for (std::uint32_t s = 0; s < 12; ++s)
        {
            movement.advance_season(s);
            movement.execute(entities, cmd);
        }
        std::vector<float> out;
        for (const auto e : ids)
        {
            const auto& p = at(entities, e);
            out.push_back(p.x);
            out.push_back(p.y);
        }
        return out;
    };

    const auto a = run();
    const auto b = run();
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK_EQ(a[i], b[i]);
    }
}

TEST(movement_never_leaves_a_creature_in_water)
{
    // 设计文档 §9 测试 2。地形是本阶段唯一的新障碍，这条是它的全部意义。
    EnvironmentConfig config{};  // 默认 water_fraction = 0.35
    EnvironmentWorld env{20261016, config};
    World entities;

    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < 40; ++i)
    {
        const auto e = place(entities, static_cast<float>(i % 64) + 0.5f,
                             static_cast<float>(i / 64) + 0.5f, 1.6f);
        ids.push_back(e);
    }

    MovementSystem movement{20261016, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
    CommandBuffer cmd;
    for (std::uint32_t s = 0; s < 50; ++s)
    {
        movement.advance_season(s);
        movement.execute(entities, cmd);
    }
    cmd.flush(entities);

    // 起点里本来就有落在水上的——那些必须被挤出水面，这是设计文档
    // §7.2 契约 3 的静默钳回。
    for (const auto e : ids)
    {
        const auto& p = at(entities, e);
        CHECK(env.terrain().passable(CellIndex::of(p.x, p.y)));
    }
}

TEST(movement_respects_the_speed_trait)
{
    // 设计文档 §9 测试 3，**可变异性守卫**：没有它，趋向写死了也全绿。
    //
    // 上下界卡得紧：步长取整后只剩 {1, 2} 两种取值，比值上限就是 2.0，
    // 1.5 已经贴着天花板。因此 speed 取 0.3（必得 1 格）与 2.9（必得 3 格）
    // 两个极端，而不用 0.3 对 1.5——后者取整后可能都落进同一档。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    config.gradient_weight = 0.0f;  // 关掉趋向，只量步长本身

    const auto slow = mean_displacement(0.3f, config, 31);
    const auto fast = mean_displacement(2.9f, config, 31);

    CHECK(fast > slow * 1.5f);
}

TEST(a_gradient_bearing_creature_repeatedly_returns_to_rich_cells)
{
    // 趋向的最小可观察效果。group 比较两组同种个体跑同样多季之后，
    // 邻域的平均存量：跟梯度的那组应该待在高存量区，随机游走的那组不会。
    //
    // 判据用**比值**而不是绝对阈值——存量场的绝对值随种子与季号漂移，
    // 只有「跟梯度的待得更好」这一条与参数无关。
    const auto settle = [](const float weight)
    {
        EnvironmentConfig config{};
        config.gradient_weight = weight;
        EnvironmentWorld env{20261016, config};
        World entities;

        std::vector<EntityId> ids;
        for (std::size_t i = 0; i < 120; ++i)
        {
            ids.push_back(place(entities, static_cast<float>(i % 12) * 5.0f + 0.5f,
                                static_cast<float>(i / 12) * 5.0f + 0.5f, 1.0f));
        }

        MovementSystem movement{20261016, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
        CommandBuffer cmd;
        for (std::uint32_t s = 0; s < 30; ++s)
        {
            movement.advance_season(s);
            movement.execute(entities, cmd);
        }
        cmd.flush(entities);

        float sum = 0.0f;
        std::size_t n = 0;
        for (const auto e : ids)
        {
            const auto& p = at(entities, e);
            sum += env.resources().stock(CellIndex::of(p.x, p.y));
            ++n;
        }
        return sum / static_cast<float>(n);
    };

    const auto wandering = settle(0.0f);
    const auto seeking = settle(1.0f);

    CHECK(seeking > wandering);
}
```

需要 `#include "gamev2/genetics/phenotype.hpp"`（`Phenotype` 的完整定义
在那里；`CreaturePhenotype` 与 `Position` 来自 `components.hpp`），以及
`<cmath>`（`std::hypot`）、`<utility>`、`<array>`、`<algorithm>`。

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`MovementSystem` 的四参数构造函数不存在。

- [ ] **Step 3: 改 MovementSystem 的声明**

`include/gamev2/ecs/adapt/movement_system.hpp`，把 include 列表里的
`#include "gamev2/genetics/phenotype.hpp"` 之后补上：

```cpp
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/terrain.hpp"
```

把类头注释里那句「**随机游走。**」整段换成：

```cpp
/// 随机游走 + 资源存量梯度。每季每个体先掷硬币：命中梯度就在 4 个可通行
/// 邻格里挑存量最高的那个，否则按原来的随机方向走；两者随后走同一条推进
/// 路径——整数格步长、逐格判通行、撞墙沿墙滑。
///
/// 步长改为整数是被沿墙滑行逼出来的：浮点步长没法逐格判碰撞。
///
/// 梯度追的是**存量**而不是容量。存量会被吃穿，因此带负反馈——挤进富格
/// → 吃光 → 梯度翻转 → 离开；容量静态、永远给一个干净的梯度，却没有反向
/// 作用力，所有个体会一起爬向同一批格并留在那里。实测（设计文档 §2.1）也
/// （初稿在此接了一句「实测显示存量梯度指向的正是那些『空而肥』的格子，
/// 是填坑而不是拥挤」，**已被设计文档 §2.2 推翻** —— 梯度是重新分配而非
/// 填充：富格被填上、中等格被腾空，空格数反而上升。）
```

构造函数换成（`gradient_weight` 那一形参见本步骤末尾的说明）：

```cpp
    /// \pre resources 与 terrain 的生命周期必须覆盖本对象——它们随
    ///       EnvironmentWorld 构造一次即固定，World 持有后者。
    MovementSystem(std::uint64_t root_seed, std::uint32_t season_index,
                   const gamev2::environment::ResourceField& resources,
                   const gamev2::environment::TerrainField& terrain,
                   const float gradient_weight)
        : root_seed_{root_seed},
          season_index_{season_index},
          resources_{resources},
          terrain_{terrain},
          gradient_weight_{gradient_weight}
    {
    }
```

私有段换成：

```cpp
private:
    /// 这个坐标能不能站人。越界与水下都算不能。
    [[nodiscard]] bool standable(const float x, const float y) const noexcept;

    /// 4 个可通行邻格里**比本格更富**者的方向。返回 {0, 0} 表示无处可去
    /// （四面环水）或存量全同，调用方据此退回随机游走。
    ///
    /// 同存量时取遍历序里的第一个（+x, −x, +y, −y）：固定遍历序让平手有
    /// 确定答案，否则同种子的两次运行可能各挑一个不同的平手方向。
    [[nodiscard]] std::pair<float, float>
    richest_neighbour(const float x, const float y) const noexcept;

    /// 朝 (dx, dy) 推进 stride 格，逐格判通行；撞墙先试只走 x 再试只走 y，
    /// 两边都不通就停在最后一个能站的格。
    void advance(Position& position, const float dx, const float dy,
                 const int stride) const noexcept;

    std::uint64_t root_seed_;
    std::uint32_t season_index_;
    const gamev2::environment::ResourceField& resources_;
    const gamev2::environment::TerrainField& terrain_;
    float gradient_weight_ = 0.0f;
};
```

还需要 `#include <utility>`（`std::pair`）。

- [ ] **Step 4: 重写 execute 与三个辅助**

`src/ecs/adapt/movement_system.cpp`，把 `clamp_to_map` 那个匿名
namespace helper 整个删掉，换成三个文件内辅助的实现，`execute` 换成：

```cpp
namespace gamev2::ecs::adapt {

namespace {

/// 移动自用的用途标签。遗传那 5 条子流用的是 0..4，这里取一个够不着的
/// 标签，两条流因此不会因为共用标签而互相干扰。
constexpr std::uint64_t kMovementPurpose = 0x4D4F5645ull;

/// 坐标上界取 kGridSide - 1：CellIndex::of 截断取整，x == 64.0 会被当成
/// 第 64 列，算出的下标落进下一行、越出 kCellCount。
constexpr float kMapLimit =
    static_cast<float>(gamev2::environment::kGridSide) - 1.0f;

}  // namespace

bool MovementSystem::standable(const float x, const float y) const noexcept
{
    if (x < 0.0f || x > kMapLimit || y < 0.0f || y > kMapLimit)
    {
        return false;
    }
    return terrain_.passable(gamev2::environment::CellIndex::of(x, y));
}

std::pair<float, float>
MovementSystem::richest_neighbour(const float x, const float y) const noexcept
{
    static constexpr std::array<std::pair<float, float>, 4> kDirs{
        std::pair<float, float>{1.0f, 0.0f},
        std::pair<float, float>{-1.0f, 0.0f},
        std::pair<float, float>{0.0f, 1.0f},
        std::pair<float, float>{0.0f, -1.0f},
    };

    std::pair<float, float> best{0.0f, 0.0f};
    // **初值必须是本格存量，不是 -1.0f。** -1.0f 让「取 4 邻局部最大」变成
    // 「取遍历序第一个可通行邻格」—— 存量全同时 0 > -1 恒成立，于是梯度退化成
    // 一条恒定向东的漂移（实测 200 种子：128/22/21/29，64% 朝 +x）。个体站上
    // 局部峰顶后仍会被推向更贫的邻格，类头承诺的负反馈整个落空。
    // 由 the_gradient_direction_is_symmetric_when_no_neighbour_is_richer 守住。
    float best_stock = resources_.stock(
        gamev2::environment::CellIndex::of(x, y));
    for (const auto& [dx, dy] : kDirs)
    {
        const float nx = x + dx;
        const float ny = y + dy;
        if (!standable(nx, ny))
        {
            continue;
        }
        const auto stock = resources_.stock(
            gamev2::environment::CellIndex::of(nx, ny));
        // 严格大于：平手留给先来的那个，遍历序固定因此结果确定。
        if (stock > best_stock)
        {
            best_stock = stock;
            best = {dx, dy};
        }
    }
    return best;
}

void MovementSystem::advance(Position& position, const float dx,
                             const float dy, const int stride) const noexcept
{
    float x = position.x;
    float y = position.y;
    for (int s = 0; s < stride; ++s)
    {
        const float nx = x + dx;
        const float ny = y + dy;
        if (standable(nx, ny))
        {
            x = nx;
            y = ny;
            continue;
        }
        // 沿墙滑：先试只走 x，再试只走 y。两个都不通说明顶死了，停在这里。
        if (standable(nx, y))
        {
            x = nx;
            continue;
        }
        if (standable(x, ny))
        {
            y = ny;
            continue;
        }
        break;
    }
    position.x = x;
    position.y = y;
}

void MovementSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    using gamev2::genetics::TraitKind;
    using gamev2::genetics::mix_seed;
    using gamev2::genetics::trait_index;
    using gamev2::genetics::uniform_index;

    // 自成一条流。移动每季每人至少抽一次，若与繁殖共用一条，改一次移动
    // 规则就会连带改掉全部出生结果，而那种整体漂移极难定位到是哪一处改动。
    std::mt19937_64 rng{
        mix_seed(mix_seed(root_seed_, season_index_), kMovementPurpose)};

    world.for_each_entity([&](const EntityId e)
    {
        auto* position = world.get_if<Position>(e);
        const auto* phenotype = world.get_if<CreaturePhenotype>(e);
        if (position == nullptr || phenotype == nullptr)
        {
            return;
        }
        // 缺 Position 的实体原地不动，与 EnvironmentSystem 和
        // BreedingSystem 对它的处理保持一致——三者对「谁参与世界运转」
        // 必须给出同一个答案，否则会凭空多出一个只消耗不生产的幽灵。
        //
        // 起点落在水上（地形生成后由调用方播种时踩中，或阶段 4 玩家改写
        // 地形）时先找一条出路，否则它会在水里原地站一整季。
        if (!standable(position->x, position->y))
        {
            const auto escape = richest_neighbour(position->x, position->y);
            if (escape.first == 0.0f && escape.second == 0.0f)
            {
                return;
            }
            position->x += escape.first;
            position->y += escape.second;
        }

        // Speed 允许为负（表型区间配错时），先夹到 [0, 3] 再取整。
        // 至少一格：不足一格时个体可能整季留在原格，空格照旧填不上。
        const float speed = phenotype->phenotype.traits[trait_index(
            TraitKind::Speed)];
        const auto stride = static_cast<int>(
            std::max(1.0f, std::lround(std::clamp(speed, 0.0f, 3.0f))));

        float dx = 0.0f;
        float dy = 0.0f;
        // 掷一次硬币决定本季跟不跟梯度。随机流仍只有这一条——硬币取自它，
        // 因此同种子逐位可复现（设计文档 §8 的确定性一节）。
        if (gradient_weight_ > 0.0f && uniform_index(rng, 2) == 0)
        {
            const auto richer = richest_neighbour(position->x, position->y);
            dx = richer.first;
            dy = richer.second;
        }
        if (dx == 0.0f && dy == 0.0f)
        {
            switch (uniform_index(rng, 4))
            {
            case 0:
                dx = 1.0f;
                break;
            case 1:
                dx = -1.0f;
                break;
            case 2:
                dy = 1.0f;
                break;
            default:
                dy = -1.0f;
                break;
            }
        }

        advance(*position, dx, dy, stride);
    });
}

}  // namespace gamev2::ecs::adapt
```

需要 `#include <array>`（`kDirs`）与 `<utility>`。

**`gradient_weight` 怎么进来。** 它住在 `EnvironmentConfig` 里，而
`MovementSystem` 不持有 config——本 system 只被喂它真正读到的东西
（地形与资源），config 不在其中。因此构造签名再接一个标量：

```cpp
    MovementSystem(std::uint64_t root_seed, std::uint32_t season_index,
                   const gamev2::environment::ResourceField& resources,
                   const gamev2::environment::TerrainField& terrain,
                   const float gradient_weight)
        : root_seed_{root_seed},
          season_index_{season_index},
          resources_{resources},
          terrain_{terrain},
          gradient_weight_{gradient_weight}
    {
    }
```

私有段末尾加 `float gradient_weight_ = 0.0f;`。

`World` 从 `env_.config().gradient_weight` 取——它本来就是这么喂
`BreedingSystem` 的 `mating_radius_cells` 的，同一个路子。

**不取「让 MovementSystem 持 config 引用」那条**：那等于把整个配置
（承载力、摄食率、交配半径……）挂进一个只读两个标量的 system，与它那份
窄访问集是同一个原则的两面。

- [ ] **Step 5: 接线到 World**

`src/world/world.cpp` 的构造函数初始化列表里，把

```cpp
      movement_{root_seed, 0},
```

改成

```cpp
      movement_{root_seed, 0, env_.resources(), env_.terrain(),
                environment_config.gradient_weight},
```

`env_` 声明在 `movement_` 之前，因此这里取 `env_.resources()` 是安全的。

- [ ] **Step 6: 接 CMake 与跑测试**

`CMakeLists.txt` 的 `ENVIRONMENT_TEST_SOURCES` 加
`tests/test_movement.cpp`。

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五个套件全过。

**`test_world_module.cpp` 的既有 15 条应当一字不改全部仍过。** 它们是
「重写未改坏既有契约」的证据。特别地：

- `movement_and_aging_do_not_conflict_so_a_batch_can_run_them_in_parallel`
  断言的 `batch_count() == 1` **必须仍绿**——访问集没变（仍只读
  `CreaturePhenotype`、只写 `Position`），设计与文档 §8 点名要盯的就是它。
  它红了说明有人给本 system 加了新的访问集声明。
- `population_never_exceeds_the_capacity_left_below_it` 应当更松而非更紧。
- `run_seasons_equals_repeated_run_season` 与
  `equal_seeds_produce_identical_seasons` 必须仍绿。

若它们红了，**先不要改断言**：逐条查 `World::open_season` 的语句顺序与
`advance()` 的滑行逻辑，改测试去迁就实现是本任务最容易犯的错。

- [ ] **Step 7: 加一条集成测试**

加到 `tests/test_world_module.cpp` 的匿名 namespace 末尾（该文件已有
`Fixture`，但**它不能用于这条测试**——见下），并在该文件的 using 列表
补齐后面用到的那几个名字。

先在匿名 namespace 里加这个夹具：

```cpp
/// 最小夹具：造 World 并把 `gradient_weight` 在 World 建好**之前**写进
/// config。
///
/// **为什么不复用上面那个 `Fixture`**：它把 `config` 与 `world` 两个成员
/// 的构造顺序焊死了（`config` 声明在 `world` 之前，靠初始化列表就位），
/// 因此没有任何办法在 World 建好之后改 gradient_weight——而本测试要的正是
/// 「同一个夹具、两套参数」的对照。给它加构造参数会牵动既有 15 条测试，
/// 收益不值。
struct MovementProbe
{
    LocusTable table{neutral_table()};
    GeneticsConfig genetics{steady_genetics()};
    EnvironmentConfig config{roomy_config()};
    SpeciesId sid{};
    std::optional<gamev2::world::World> world;

    explicit MovementProbe(const float gradient_weight)
        : config{roomy_config()}
    {
        config.gradient_weight = gradient_weight;
        // emplace 在 config 赋值之后才发生，因此 World 看到的是改过的值。
        // World 不可移动，但 optional 的就地构造不需要移动，合法。
        world.emplace(table, genetics, config, 31);
        const auto reg = world->species().register_species(all_loci());
        CHECK(reg.has_value());
        sid = *reg;
    }

    [[nodiscard]] gamev2::world::World& get() { return *world; }
};
```

然后在匿名 namespace 之后加测试本体：

```cpp
TEST(the_gradient_keeps_productive_cells_from_sitting_empty)
{
    // 设计文档 §9 测试 3b，盯的是 §2.1 结论 4 那个病理：阶段 1 结束时
    // 有 519 个「空但养得起」的格永久空着，其中 331 个落在存量最高四分位。
    //
    // 判据是**差分**而非绝对数：可用面积已因水缩到 65%，绝对数本来就对不
    // 上 519；而「关掉趋向时空格更多」这一条与所有参数无关，也正是趋向配
    // 成 0 时唯一会红的守卫。
    const auto empty_productive = [](gamev2::world::World& w)
    {
        std::size_t empty = 0;
        const auto& env = w.environment();
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
            if (env.terrain().passable(cell)
                && env.resources().capacity(cell) >= 1.0f
                && env.resources().occupants(cell) == 0)
            {
                ++empty;
            }
        }
        return empty;
    };

    const auto run = [](const float weight)
    {
        MovementProbe probe{weight};
        auto& w = probe.get();
        // 铺满：population 越高，空格被随机游走「偶然撞上」的概率越高，
        // 对照组才不会出现假绿。
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
            if (!w.environment().terrain().passable(cell))
            {
                continue;
            }
            const auto n = static_cast<std::size_t>(std::max(
                2.0f, w.environment().resources().capacity(cell) * 0.6f));
            const auto bx =
                static_cast<float>(c % kGridSide) + 0.5f;
            const auto by =
                static_cast<float>(c / kGridSide) + 0.5f;
            for (std::size_t i = 0; i < n; ++i)
            {
                const EntityId e = w.entities().create();
                (void)w.entities().add<Position>(e, Position{bx, by});
                (void)w.entities().add<SpeciesTag>(e, SpeciesTag{probe.sid});
                (void)w.entities().add<Environment>(e, Environment{});
                (void)w.entities().add<Genome>(e, Genome{});
                (void)w.entities().add<Vitality>(e, Vitality{});
                (void)w.entities().add<CreaturePhenotype>(
                    e, CreaturePhenotype{creature_phenotype()});
            }
        }
        for (int season = 0; season < 60; ++season)
        {
            (void)w.run_season();
        }
        return empty_productive(w);
    };

    const auto wandering = run(0.0f);
    const auto seeking = run(1.0f);

    CHECK(seeking < wandering);
}
```

需要在该文件的 using 列表补
`gamev2::world::World`、`gamev2::ecs::adapt::{Position, SpeciesTag,
Environment, Genome, Vitality, CreaturePhenotype}`、
`gamev2::environment::{kCellCount, kGridSide, CellIndex}`、
`gamev2::genetics::Genome`，以及 `#include <optional>` 与
`#include <algorithm>`。

**跑之前先看一眼耗时**：这条铺满之后要跑 60 季。若单测超过 20 秒，把季数
降到 30 并在测试注释里记下实测耗时——不要用「跳过」或缩小种群来换时间，
缩小种群会让对照组失去假绿的机会。

- [ ] **Step 8: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五个套件全过，`test_world_module.cpp` 16 条（既有 15 + 新 1）。

- [ ] **Step 9: 提交（由用户决定）**

```bash
git add include/gamev2/ecs/adapt/movement_system.hpp src/ecs/adapt/movement_system.cpp src/world/world.cpp tests/test_movement.cpp tests/test_world_module.cpp CMakeLists.txt
git commit -m "feat(adapt): make movement seek resources and slide along terrain"
```

---

### Task 4: sim 适配、基线重取与文档回填

**Files:**
- Modify: `src/sim/main.cpp`
- Modify: `doc/2026-10-17-game-design.md`
- Modify: `doc/2026-10-16-world-design.md`

**Interfaces:**
- Consumes: Task 3 的 `MovementSystem` 与 Task 1 的 `TerrainField`
- Produces: 三个预设的新基准读数，回填进两份设计文档

- [ ] **Step 1: 让 seed() 跳过水格**

`src/sim/main.cpp` 的 `seed()` 循环体开头插入：

```cpp
        // 地形生成后水格站不了人，在这里铺个体等于开机就把一堆幽灵丢进
        // 水里——它们会被 MovementSystem 的静默钳回挤上岸，但那是纠错，
        // 不是初始化。
        if (!env.terrain().passable(cell))
        {
            continue;
        }
```

`env` 已经是 `const auto& env = world.environment();`，只改循环体即可。

同时把 `Preset` 上方那段读数注释**先整段删掉**，Step 3 会按新读数重写。
不要留着旧读数当对照——阶段 2 之后它们全部作废（设计文档 §11 已记）。

**另有一处必须改：`src/sim/main.cpp:535` 把 Σcapacity 写死成 `9984.0`。**
那是全陆地下 `4096 × capacity_base 3.9 × mean(base_fertility) 0.625` 的值。
地形生效后水格 capacity 归零，Σcapacity 落到约 2995（设计文档 §4.1 的六种子
实测），写死的 9984 会让那句「末季 X = Σcapacity 的 Y%」变成错的——而且是
**静默**错的，只是一个离谱的百分比，不会有任何报错。

改成按实际地图求和，一次遍历 4096 格：

```cpp
            // Σcapacity 按实地图求和，不能写死：地形生成后水格 capacity
            // 为 0，写死的全陆地值会让这句百分比静默变成错的。
            double sum_capacity = 0.0;
            for (std::size_t c = 0; c < kCellCount; ++c)
            {
                const auto cell =
                    CellIndex{static_cast<std::uint16_t>(c)};
                sum_capacity +=
                    world.environment().resources().capacity(cell);
            }
            const auto pct = static_cast<double>(previous) / sum_capacity * 100.0;
            std::println("\n末季 {:.0f} = Σcapacity({:.0f}) 的 {:.0f}%。",
                         static_cast<double>(previous), sum_capacity, pct);
```

`build_environment()` 里那句「Σcapacity = 4096 × 3.9 × 0.625 ≈ 9984」的注释
同样要改——它现在只是全陆地上的上限，不是实际天花板。

- [ ] **Step 2: 构建并确认 sim 跑得动**

```bash
pwsh -File scripts/build.ps1
& build/sim.exe evolve 5
```

Expected: 五行输出，种群不归零、不暴涨。若归零，多半是水格 capacity 归零
之后承载力不够——回去查 Task 2，而不是调 `capacity_base`。

- [ ] **Step 3: 重取三个预设的基准读数**

```bash
& build/sim.exe evolve 200
& build/sim.exe persist 200
& build/sim.exe fragile 200
```

把三个末季读数（population / born / died / H_obs / F / Ne）与
`fragile` 的归零季号记下来。

**预期方向无法预判**：可用面积降到 65% 会压低种群，而 519 个空而肥的格
被填会抬高它。哪个占上风只有跑出来才知道——这正是这一步存在的理由。

- [ ] **Step 4: 回填设计文档**

`doc/2026-10-17-game-design.md`：

1. `Preset` 那段读数块——本计划改的是 `src/sim/main.cpp` 里的同一段
   注释，逐字替换成 Step 3 的读数
2. §2.1 的表格**加一行**说明它是阶段 2 **之前**的基线（现在的地形图上
   不存在），不要删——它是决策 15 的证据
3. 决策 13 与决策 14 的读数块末尾那句「阶段 2 改完需重新取数并回来更新
   本行」删掉，改成指向 Step 3 的新读数
4. §11 第二条（阶段 2 落地后需重取的三处读数）标成已完成并写上新数

`doc/2026-10-16-world-design.md` §8 那条「空掉的格会被重新殖民」的说明
补一句：阶段 2 用实测（87.2%）验证过它仍然成立，并指向设计文档 §2.1。

- [ ] **Step 5: 跑一次 ASan 全量**

```bash
pwsh -File scripts/build.ps1 -Test -Asan
```

Expected: 五个套件全过，无 sanitizer 报告。`MovementSystem` 新增了地形
索引与数组访问，ASan 是这一批改动的第一道防线。

- [ ] **Step 6: 提交（由用户决定）**

```bash
git add src/sim/main.cpp doc/2026-10-17-game-design.md doc/2026-10-16-world-design.md
git commit -m "feat(sim): reseed the presets over terrain and re-baseline the readings"
```

---

## 阶段 2 完成后的验收

```bash
pwsh -File scripts/build.ps1 -Test
pwsh -File scripts/build.ps1 -Test -Asan
& build/sim.exe evolve 200
& build/sim.exe persist 200
& build/sim.exe fragile 200
```

逐条核对：

1. 五个套件全过，ASan 无报告
2. `test_world_module.cpp` 既有 15 条**一字未改**仍过——这是「重写没有
   改坏既有契约」的唯一证据
3. `the_map_has_a_landmass_big_enough_to_be_a_barrier` 在四个种子上都绿，
   且次大陆格数落在实测的 226~474 区间内（不是擦着 200 的线过）
4. 三个预设的读数已回填进 `src/sim/main.cpp` 的注释与设计文档

**两条不要用测试阈值强行兑现的东西**，如实记录即可：

- **`water_actually_splits_the_population`**（设计文档 §9 测试 4）。两个
  陆块上的遗传分化速度由出生/死亡动态主导，短季数内未必压得住出生带来的
  同步。如果 200 季内分化不显著，**把读数记进设计文档 §11 而不是调阈值
  把它逼绿**——那说明阶段 2 的招牌产出（地理分化）需要更多季数或更强的
  隔离才显现，属于阶段 3 的输入。
- **末季种群的绝对值**。阶段 2 把可用面积砍掉 35% 又打开了 519 个空格，
  两股力量方向相反。没有「正确」的数，只有「重取过并记下来了」的数。

跑完这些就可以出阶段 3 的计划。

## 不在本计划内

- 阶段 3：`SpeciationSystem`、运行中登记物种、同种趋向
- 阶段 4：`paint_terrain`（因此 `TerrainField` 只给 const 访问器）、
  气候笔刷、基因库编辑
- 渲染与 UI：美术规格见 `doc/美术风格规格.md`，另出计划
- 存档：`TerrainField` 的序列化格式仍没有（`world` 与 `environment`
  两份文档的未决事项里都记着这一条）

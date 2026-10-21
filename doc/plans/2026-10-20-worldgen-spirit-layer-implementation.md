# 灵气层世界生成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给既有十阶段 worldgen 模块追加一个「灵气层」——地脉网络、灵脉品阶、
灵眼、灵域、六类地貌、洞天、灵潮周期——使生成出的世界符合
`doc/2026-10-20-worldview-xianxia.md` 的设定。

**Architecture:** 七个新阶段以 11..17 追加在既有十阶段之后，单向读取既有输出，
不回写既有字段。（2026-10-21 拍板追加阶段 18「渡劫场」，见本文末尾 Task 11；
阶段总数因此为 18，编排顺序见 Task 11 的 Step 5。）
每个字段只有一个写入者：凡是「先发生的事导致后发生的结果」，
都让先发生的那一步在更早的阶段完成（越采标记在阶段 13、塌陷在阶段 15）。
地脉用**显式有向图**（`LeyChannel` / `LeyNode`）承载拓扑，另建**栅格镜像**
（`Grid<T>`）供空间查询；镜像由图单向导出。地脉节点图是一张 DAG（`next`
指针沿通道指向高程严格更低的节点），因此流量可按高程降序一次结算。

**Tech Stack:** C++23、clang++ 23.1.1、MSVC STL 14.51、CMake 4.4、
Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-20-worldgen-spirit-layer-design.md`
（本文的全部语义依据；冲突时以设计文档为准，并回报差异）

**既有设计文档：** `doc/2026-10-19-worldgen-design.md`

---

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
- **不得执行 `git commit`**，提交时机由用户决定。每个任务末尾的提交步骤
  改为「向用户报告并等待提交指令」
- 输出文件（PPM、统计）写入 `agent-temp/`，该目录已被 gitignore
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- 噪声一律走 `value_noise` / `fbm`，禁止在生成代码里另写哈希
- 每个新随机流必须同时登记进 `Stream` 枚举与 `kStreamIds` 数组；
  `static_assert(stream_ids_are_distinct())` 会拦住重复流号
- 任何读 `std::uint8_t` 网格当数值的算术，一律显式转成整数再算，
  不得依赖隐式提升

构建与测试命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/worldgen_tests.exe
```

只跑单个测试文件不需要额外手段：测试框架靠静态注册，删掉 CMakeLists 里的
其余条目即可。正常做法是全部构建。

---

## 文件总览

本计划创建下列文件。文件职责单一，遵循「一件事一个文件」。

| 文件 | 职责 |
|---|---|
| `include/gamev2/world/spirit.hpp` | 灵气层的枚举与 POD 类型 |
| `include/gamev2/world/spirit_tables.hpp` | 灵气层查表与共享常量 |
| `src/world/ley.cpp` | 阶段 11-12：灵潮状态 / 地脉网络 |
| `src/world/veins.cpp` | 阶段 13：灵脉切分、品阶、灵眼 |
| `src/world/domains.cpp` | 阶段 14：浓度场与灵域分等 |
| `src/world/land.cpp` | 阶段 15-16：历史疤痕 / 六类地貌 / 灵眼纯度 |
| `src/world/heavens.cpp` | 阶段 17：洞天 |
| `tests/test_world_spirit_tide.cpp` | 阶段 11 |
| `tests/test_world_ley.cpp` | 阶段 12 |
| `tests/test_world_veins.cpp` | 阶段 13 |
| `tests/test_world_domains.cpp` | 阶段 14 |
| `tests/test_world_land.cpp` | 阶段 15-16 |
| `tests/test_world_heavens.cpp` | 阶段 17 |

本计划修改下列文件：

| 文件 | 改动 |
|---|---|
| `include/gamev2/world/enums.hpp` | `Stream` 扩容到 17 条 |
| `include/gamev2/world/params.hpp` | 新增 `spirit_year`，`is_valid()` 收紧 |
| `include/gamev2/world/world.hpp` | 新增 13 个灵气层网格与 7 个非网格成员 |
| `include/gamev2/world/generate.hpp` | 新增七个阶段函数声明 |
| `src/world/world.cpp` | 构造函数初始化新网格 |
| `src/world/generate.cpp` | 编排追加七个阶段 |
| `src/demo/main.cpp` | 新增导出与统计 |
| `CMakeLists.txt` | 追加源文件与测试文件 |
| `tests/test_world_determinism.cpp` | 字段比较扩展 |

**每个字段只有一个写入者**（既有十阶段的既有性质，本计划必须守住）：

| 字段 | 唯一写入阶段 |
|---|---|
| `spirit_tide` | 11 |
| `ley_channels` / `ley_nodes` / `ley_strength` / `ley_depth` / `ley_element` | 12 |
| `veins` / `eyes` / `vein_rank` / `vein_id` / `eye_flow` | 13 |
| `domain_concentration` / `domain_grade` | 14 |
| `scars` / `scar_kind` / `spirit_suppression` | 15 |
| `land_type` / `eye_purity` | 16 |
| `cave_heaven` / `cave_heavens` | 17 |

---

### Task 1: 灵气层的类型骨架、参数与 World 容器

**Files:**
- Create: `include/gamev2/world/spirit.hpp`
- Create: `include/gamev2/world/spirit_tables.hpp`
- Modify: `include/gamev2/world/enums.hpp`
- Modify: `include/gamev2/world/params.hpp`
- Modify: `include/gamev2/world/world.hpp`
- Modify: `src/world/world.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_spirit_tide.cpp`

**Interfaces:**
- Consumes: 既有 `Stone`、`Biome`、`Grid<T>`、`WorldParams`、`World`
- Produces:
  - `enum class gamev2::world::Element : std::uint8_t`
    —— 取值 `none, metal, wood, water, fire, earth, thunder, ice, wind,
    light, dark, sound, poison`（共 13 个）
  - `inline constexpr std::size_t gamev2::world::kElementCount = 13`
  - `enum class gamev2::world::DomainGrade : std::uint8_t`
    —— 取值 `barren, thin, medium, thick, blessed`
  - `inline constexpr std::size_t gamev2::world::kDomainGradeCount = 5`
  - `enum class gamev2::world::LandType : std::uint8_t`
    —— 取值 `blessed, miasma, yin, dead, turbid, mundane`
  - `inline constexpr std::size_t gamev2::world::kLandTypeCount = 6`
  - `enum class gamev2::world::Scar : std::uint8_t`
    —— 取值 `none, sinkhole, tribulation, turbid, depleted_eye`
  - `inline constexpr std::size_t gamev2::world::kScarKindCount = 5`
  - `enum class gamev2::world::TidePhase : std::uint8_t`
    —— 取值 `tide, decline, trough`
  - `enum class gamev2::world::VeinRank : std::uint8_t`
    —— 取值 `none = 0, barren = 1, branch = 2, main = 3, dragon = 4`
  - `inline constexpr std::uint32_t gamev2::world::kNoVein / kNoNode / kNoEye`
    —— 三者皆为 `0xFFFFFFFFu`
  - `struct gamev2::world::SpiritTide`
    —— 成员 `year, cycle, phase, year_in_phase, years_to_next_tide,
    level, peak`
  - `struct gamev2::world::LeyNode`
    —— 成员 `cell, depth, flow, next, merges, element`
  - `struct gamev2::world::LeyChannel` —— 成员 `nodes`（`std::vector<std::uint32_t>`）
  - `struct gamev2::world::SpiritVein`
    —— 成员 `id, rank, channel, first_node, last_node, element, reserve,
    stability, eye, overmined`
  - `struct gamev2::world::SpiritEye`
    —— 成员 `vein, node, cell, flow, depleted`
  - `struct gamev2::world::ScarSite`
    —— 成员 `kind, cell, radius, target`
  - `struct gamev2::world::CaveHeaven`
    —— 成员 `cell, capacity, upkeep`
  - `struct gamev2::world::VeinTarget` —— 成员 `dragon, main, branch`
  - `constexpr gamev2::world::vein_targets(std::uint32_t area) -> VeinTarget`
  - `constexpr gamev2::world::domain_grade_of(std::uint8_t) -> DomainGrade`
  - `constexpr gamev2::world::effective_concentration(std::uint8_t raw,
    std::uint8_t suppression) -> std::uint8_t`
  - `constexpr gamev2::world::is_fivefold(Element) -> bool`
  - `constexpr gamev2::world::is_hetero(Element) -> bool`
  - `constexpr gamev2::world::is_dead_scar(Scar) -> bool`
  - `constexpr gamev2::world::element_of_stone(Stone) -> Element`
  - `inline constexpr std::array<Element, 12> gamev2::world::kStoneElement`
  - `WorldParams::spirit_year`（`std::uint32_t`，默认 `8914`）
  - `World` 新增 13 个网格：`ley_strength`、`ley_depth`、`ley_element`、
    `vein_rank`、`vein_id`、`eye_flow`、`domain_concentration`、
    `domain_grade`、`scar_kind`、`spirit_suppression`、`land_type`、
    `eye_purity`、`cave_heaven`
  - `World` 新增 7 个非网格成员：`spirit_tide`、`ley_channels`、
    `ley_nodes`、`veins`、`eyes`、`scars`、`cave_heavens`
  - `Stream` 新增七条：`tide = 10, ley = 11, vein = 12, domain = 13,
    scar = 14, heaven = 15, land = 16`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_spirit_tide.cpp`：

```cpp
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "gamev2/world/params.hpp"
#include "gamev2/world/world.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::Stone;
using gamev2::world::VeinRank;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::domain_grade_of;
using gamev2::world::effective_concentration;
using gamev2::world::element_of_stone;
using gamev2::world::is_dead_scar;
using gamev2::world::is_fivefold;
using gamev2::world::is_hetero;
using gamev2::world::kDomainGradeCount;
using gamev2::world::kElementCount;
using gamev2::world::kLandTypeCount;
using gamev2::world::kScarKindCount;
using gamev2::world::vein_targets;

TEST(enum_counts_match_their_declarations)
{
    CHECK_EQ(static_cast<std::size_t>(Element::poison) + 1, kElementCount);
    CHECK_EQ(static_cast<std::size_t>(DomainGrade::blessed) + 1,
             kDomainGradeCount);
    CHECK_EQ(static_cast<std::size_t>(LandType::mundane) + 1, kLandTypeCount);
    CHECK_EQ(static_cast<std::size_t>(Scar::depleted_eye) + 1, kScarKindCount);
    CHECK_EQ(static_cast<std::size_t>(VeinRank::dragon), 4u);
}

TEST(fivefold_and_hetero_partition_the_elements)
{
    // 五行：金木水火土。异质：雷冰风光暗音毒。none 两者都不是。
    // 两个谓词必须恰好覆盖除 none 之外的全部属性，否则污染扣减会漏算。
    std::size_t covered = 0;
    for (std::size_t i = 1; i < kElementCount; ++i)
    {
        const auto e = static_cast<Element>(i);
        const bool five = is_fivefold(e);
        const bool hetero = is_hetero(e);
        CHECK(!(five && hetero));
        if (five || hetero)
        {
            ++covered;
        }
    }
    CHECK_EQ(covered, kElementCount - 1);
    CHECK(!is_fivefold(Element::none));
    CHECK(!is_hetero(Element::none));
}

TEST(dead_scar_covers_exactly_the_three_death_causes)
{
    CHECK(is_dead_scar(Scar::sinkhole));
    CHECK(is_dead_scar(Scar::tribulation));
    CHECK(is_dead_scar(Scar::depleted_eye));
    CHECK(!is_dead_scar(Scar::none));
    // 浊域不是死地：它是浓度高但混乱，与「灵气枯竭」是两回事。
    CHECK(!is_dead_scar(Scar::turbid));
}

TEST(vein_targets_scale_with_area_and_cap_at_the_world_totals)
{
    // 默认预设 256x256 必须正好是设定给的 1 / 9 / 40。
    const auto full = vein_targets(256u * 256u);
    CHECK_EQ(full.dragon, 1u);
    CHECK_EQ(full.main, 9u);
    CHECK_EQ(full.branch, 40u);

    // 超过参考面积后封顶：天下只有九条主脉、约四十条支脉。
    const auto huge = vein_targets(512u * 512u);
    CHECK_EQ(huge.main, 9u);
    CHECK_EQ(huge.branch, 40u);

    // 小图幅按面积线性缩放。
    const auto small = vein_targets(128u * 128u);
    CHECK_EQ(small.main, 9u * 128u * 128u / (256u * 256u));
    CHECK(small.dragon == 1u);
}

TEST(domain_grade_is_monotone_in_concentration)
{
    // 分等必须随浓度单调不降：更高的浓度不能拿到更低的分等。
    DomainGrade prev = DomainGrade::barren;
    for (std::uint32_t c = 0; c <= 255; ++c)
    {
        const auto g = domain_grade_of(static_cast<std::uint8_t>(c));
        CHECK(static_cast<std::size_t>(g) >= static_cast<std::size_t>(prev));
        prev = g;
    }
    CHECK(domain_grade_of(0) == DomainGrade::barren);
    CHECK(domain_grade_of(255) == DomainGrade::blessed);
}

TEST(effective_concentration_applies_suppression)
{
    CHECK_EQ(effective_concentration(200, 0), 200u);
    CHECK_EQ(effective_concentration(200, 255), 0u);
    // 200 * (255 - 128) / 255 = 99.6 -> 99。截断而非四舍五入，
    // 因此这里是 99 不是 100。
    CHECK_EQ(effective_concentration(200, 128), 99u);
    CHECK_EQ(effective_concentration(255, 128), 127u);
}

TEST(stone_maps_to_a_fivefold_or_hetero_element)
{
    // 每一个岩性都要有属性，否则地脉走到那里就断属性。
    for (std::size_t i = 0; i <= static_cast<std::size_t>(Stone::igneous_intrusive);
         ++i)
    {
        const auto e = element_of_stone(static_cast<Stone>(i));
        CHECK(is_fivefold(e) || is_hetero(e));
    }
}

TEST(params_reject_year_zero)
{
    WorldParams p;
    CHECK(p.is_valid());
    CHECK_EQ(p.spirit_year, 8914u);
    // 灵历 0 会让 (year - 1) 下溢，必须拦下。
    p.spirit_year = 0;
    CHECK(!p.is_valid());
}

TEST(world_carries_the_spirit_layer_containers)
{
    WorldParams p;
    p.width = 16;
    p.height = 12;
    p.z_levels = 3;

    World w(p);
    CHECK_EQ(w.ley_strength.size(), 192u);
    CHECK_EQ(w.ley_depth.size(), 192u);
    CHECK_EQ(w.ley_element.size(), 192u);
    CHECK_EQ(w.vein_rank.size(), 192u);
    CHECK_EQ(w.vein_id.size(), 192u);
    CHECK_EQ(w.eye_flow.size(), 192u);
    CHECK_EQ(w.domain_concentration.size(), 192u);
    CHECK_EQ(w.domain_grade.size(), 192u);
    CHECK_EQ(w.scar_kind.size(), 192u);
    CHECK_EQ(w.spirit_suppression.size(), 192u);
    CHECK_EQ(w.land_type.size(), 192u);
    CHECK_EQ(w.eye_purity.size(), 192u);
    CHECK_EQ(w.cave_heaven.size(), 192u);
    CHECK(w.ley_channels.empty());
    CHECK(w.ley_nodes.empty());
    CHECK(w.veins.empty());
    CHECK(w.eyes.empty());
    CHECK(w.scars.empty());
    CHECK(w.cave_heavens.empty());
    // 默认态：凡地、无疤、无洞天、浓度为零。
    CHECK(w.land_type.at(0) == LandType::mundane);
    CHECK(w.scar_kind.at(0) == Scar::none);
    CHECK(!w.cave_heaven.at(0));
    CHECK_EQ(w.domain_concentration.at(0), 0u);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 编译失败，报 `gamev2/world/spirit.hpp` 不存在。

- [ ] **Step 3: 写 `spirit.hpp`**

创建 `include/gamev2/world/spirit.hpp`：

```cpp
/// @file spirit.hpp
/// @brief 灵气层的数据类型：地脉、灵脉、灵眼、灵域、地貌与灵潮状态。
///
/// 单独成头文件而不并入 enums.hpp：那里的枚举全部服务于地表/地下自然层，
/// 灵气层是另一个子系统，挤在一起会让两个子系统互相牵连。
#pragma once

#include "gamev2/world/enums.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 地脉属性。五行为主，异质稀有（设定 2.1）。
enum class Element : std::uint8_t
{
    none,
    metal, wood, water, fire, earth,
    thunder, ice, wind, light, dark, sound, poison,
};

inline constexpr std::size_t kElementCount = 13;

/// 灵域分等（设定 2.4）。
enum class DomainGrade : std::uint8_t
{
    barren,    ///< 废域：几近无灵，凡人域
    thin,      ///< 薄域
    medium,    ///< 中域
    thick,     ///< 厚域
    blessed,   ///< 福地：可开洞天
};

inline constexpr std::size_t kDomainGradeCount = 5;

/// 六类地貌（设定 4.1）。与 Biome 正交，不互相覆盖。
enum class LandType : std::uint8_t
{
    blessed,   ///< 福地
    miasma,    ///< 瘴域
    yin,       ///< 阴地
    dead,      ///< 死地
    turbid,    ///< 浊域
    mundane,   ///< 凡地
};

inline constexpr std::size_t kLandTypeCount = 6;

/// 历史疤痕。设定只点名三种具象（枯墟、渡劫废墟、沸海）；
/// 第四种「枯眼周边」是枯眼的必然后果。
enum class Scar : std::uint8_t
{
    none,
    sinkhole,      ///< 越采塌陷（枯墟式）
    tribulation,   ///< 渡劫失败
    turbid,        ///< 灵潮搅动（沸海式）
    depleted_eye,  ///< 枯眼周边
};

inline constexpr std::size_t kScarKindCount = 5;

/// 灵潮周期阶段（设定 2.6）。
enum class TidePhase : std::uint8_t
{
    tide,     ///< 灵潮期：约 300 年
    decline,  ///< 衰退期：约 500 年
    trough,   ///< 谷底期：约 200 年
};

/// 灵脉品阶（设定 2.2）。数值即排序，dragon 最大。
enum class VeinRank : std::uint8_t
{
    none = 0,
    barren = 1,  ///< 荒脉：残余矿化，储量极低
    branch = 2,  ///< 支脉：郡县级
    main = 3,    ///< 主脉：跨州级
    dragon = 4,  ///< 龙脉：贯通全境
};

/// 灵脉、节点、灵眼编号的哨兵值。沿用既有 kNoDownstream 的做法：不用
/// optional，因为这些字段与地脉图是一一对应的稠密索引。
inline constexpr std::uint32_t kNoVein = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoEye = 0xFFFFFFFFu;

/// 灵潮周期状态。世界的「年」以第一轮灵潮之始为元年。
struct SpiritTide
{
    std::uint32_t year = 0;
    std::uint32_t cycle = 0;  ///< 轮次索引，0 = 元潮
    TidePhase phase = TidePhase::trough;
    /// 相位内第几年，按「与相位起点的差」计：灵历 8914 落谷底期，起点
    /// 8801，差 113，设定称「谷底期第一百一十三年」。所以它已经是年数，
    /// 打印时不能再加一。
    std::uint32_t year_in_phase = 0;
    std::uint32_t years_to_next_tide = 0;
    std::uint8_t level = 0;   ///< 当下浓度比例 0..255
    std::uint8_t peak = 0;    ///< 本轮峰值比例 0..255
};

/// 地脉节点。节点是三维的：cell 给出地表投影，depth 给出埋深。
struct LeyNode
{
    std::uint32_t cell = 0;       ///< 地表格子的线性索引
    std::uint32_t depth = 0;      ///< 埋深，格
    std::uint32_t flow = 0;       ///< 累计流量
    std::uint32_t next = kNoNode; ///< 下游节点；汇流点与下游共用同一节点
    std::uint8_t merges = 0;      ///< 汇入本节点的通道数
    Element element = Element::none;
};

/// 一条地脉。nodes 是沿线有序的节点索引序列。
struct LeyChannel
{
    std::vector<std::uint32_t> nodes;
};

/// 一条灵脉：某条地脉上的一段。
struct SpiritVein
{
    std::uint32_t id = 0;
    VeinRank rank = VeinRank::none;
    std::uint32_t channel = 0;
    std::uint32_t first_node = 0;  ///< 段首，闭区间
    std::uint32_t last_node = 0;   ///< 段尾，闭区间
    Element element = Element::none;
    std::uint32_t reserve = 0;     ///< 储量
    std::uint32_t stability = 0;   ///< 稳定年限
    std::uint32_t eye = kNoEye;    ///< 灵眼索引
    /// 已被越采。设定 4.2 的枯墟正是「灵脉被越采，三百里地陷」，
    /// 因果上是采在先、塌在后，所以标记落在阶段 13，塌陷落在阶段 15。
    bool overmined = false;
};

/// 灵眼：灵脉的地表出露点。
struct SpiritEye
{
    std::uint32_t vein = 0;
    std::uint32_t node = 0;
    std::uint32_t cell = 0;
    std::uint32_t flow = 0;   ///< 流量：单位时间可采灵气量
    bool depleted = false;    ///< 枯眼：已抽干，不可复生
};

/// 一处历史疤痕。land_type 是它的结果，scar_kind 是它本身。
///
/// 名字带 Site 后缀：枚举已占了 Scar，结构体不能同名。
struct ScarSite
{
    Scar kind = Scar::none;
    std::uint32_t cell = 0;    ///< 中心
    std::uint32_t radius = 0;  ///< 影响半径，格
    std::uint32_t target = 0;  ///< 关联的灵脉或灵眼编号
};

/// 洞天。设定 2.5：本质是用灵石供养的蓄电池。
struct CaveHeaven
{
    std::uint32_t cell = 0;
    std::uint32_t capacity = 0;  ///< 可锁住的灵气量
    std::uint32_t upkeep = 0;    ///< 年耗灵石
};

}  // namespace gamev2::world
```

- [ ] **Step 4: 写 `spirit_tables.hpp`**

创建 `include/gamev2/world/spirit_tables.hpp`：

```cpp
/// @file spirit_tables.hpp
/// @brief 灵气层的查表与共享常量。
///
/// 放头文件并用 inline constexpr，是为了让测试能引用同一份数据核对，
/// 而不是复制一份会与实现漂移的副本。调参项集中在这里，与既有
/// tables.hpp 的做法一致。
#pragma once

#include "gamev2/world/enums.hpp"
#include "gamev2/world/spirit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::world {

/// 参考图幅面积。灵脉数量按面积相对它缩放。
inline constexpr std::uint32_t kReferenceArea = 256u * 256u;

/// 天下龙脉、主脉、支脉的总数（设定 2.2）。数量是硬约束，不是调参项。
inline constexpr std::uint32_t kDragonVeinTotal = 1;
inline constexpr std::uint32_t kMainVeinTotal = 9;
inline constexpr std::uint32_t kBranchVeinTotal = 40;

/// 灵潮每轮的损耗比例。设定 2.6：「每轮损耗约一成」，于是九轮之后
/// 0.9^9 = 0.387，不足元潮时期的四成。
inline constexpr double kTideLossPerCycle = 0.9;

/// 谷底期保留的浓度比例。谷底期不是绝对零——设定说「灵脉半死」。
inline constexpr double kTroughFloor = 0.3;

/// 灵域分等的浓度阈值。调参项。
inline constexpr std::uint8_t kThinThreshold = 8;
inline constexpr std::uint8_t kMediumThreshold = 24;
inline constexpr std::uint8_t kThickThreshold = 60;
inline constexpr std::uint8_t kBlessedThreshold = 120;

/// 流量参考值。灵眼流量换算成浓度贡献时以它为基准。
/// 调参项：Task 10 用 demo 导出的 PPM 对着图调。
inline constexpr std::uint32_t kReferenceFlow = 2000;

/// 灵域影响半径的基准值。实际半径随流量开方增长。
///
/// 它是浓度场的衰减尺度，单位是绝对格数。定得太大时，天下几十只灵眼的
/// 尾巴会叠成一片背景高原，「福地」就从「强眼所在地」退化成「众眼尾巴
/// 的叠加区」。红线 8 要求稀缺性不为剧情让路，福地面积必须小。
inline constexpr double kDomainRadius = 16.0;

/// 浓度场盖章的最大半径。超过它的贡献按 0 计。
inline constexpr std::uint32_t kDomainMaxRadius = 64;

/// 岩性到地脉属性的映射。顺序必须与 Stone 枚举逐项对齐。
///
/// 一对一映射：设定说属性「由流经的岩性决定」，没有给一岩多属的机制。
inline constexpr std::array<Element, 12> kStoneElement{{
    Element::earth,  // sandstone  厚重
    Element::water,  // shale     寒润
    Element::water,  // limestone 寒润
    Element::metal,  // granite   刚锐
    Element::fire,   // basalt    暴烈
    Element::metal,  // slate     刚锐
    Element::earth,  // gravel    厚重
    Element::earth,  // marble    厚重
    Element::wood,   // clay      绵长
    Element::water,  // ice       寒润
    Element::fire,   // igneous   暴烈
    Element::metal,  // igneous_intrusive 刚锐
}};

static_assert(kStoneElement.size()
              == static_cast<std::size_t>(Stone::igneous_intrusive) + 1);

/// 该岩性对应的地脉属性。
[[nodiscard]] constexpr Element element_of_stone(Stone stone) noexcept
{
    return kStoneElement[static_cast<std::size_t>(stone)];
}

/// 五行：金木水火土。
[[nodiscard]] constexpr bool is_fivefold(Element element) noexcept
{
    switch (element)
    {
    case Element::metal:
    case Element::wood:
    case Element::water:
    case Element::fire:
    case Element::earth:
        return true;
    default:
        return false;
    }
}

/// 异质：雷冰风光暗音毒。稀有且极难驾驭（设定 2.1）。
[[nodiscard]] constexpr bool is_hetero(Element element) noexcept
{
    switch (element)
    {
    case Element::thunder:
    case Element::ice:
    case Element::wind:
    case Element::light:
    case Element::dark:
    case Element::sound:
    case Element::poison:
        return true;
    default:
        return false;
    }
}

/// 该疤痕是否让地面成为死地。死地的三种成因：越采塌陷、渡劫失败、枯眼。
[[nodiscard]] constexpr bool is_dead_scar(Scar scar) noexcept
{
    return scar == Scar::sinkhole || scar == Scar::tribulation
           || scar == Scar::depleted_eye;
}

/// 浓度分等。
[[nodiscard]] constexpr DomainGrade domain_grade_of(
    std::uint8_t concentration) noexcept
{
    if (concentration >= kBlessedThreshold)
    {
        return DomainGrade::blessed;
    }
    if (concentration >= kThickThreshold)
    {
        return DomainGrade::thick;
    }
    if (concentration >= kMediumThreshold)
    {
        return DomainGrade::medium;
    }
    if (concentration >= kThinThreshold)
    {
        return DomainGrade::thin;
    }
    return DomainGrade::barren;
}

/// 有效浓度：地脉网络给出的浓度，扣掉历史疤痕造成的压制。
/// 派生量而非字段——它一旦成为 World 的成员就会有第二个写入者。
[[nodiscard]] constexpr std::uint8_t effective_concentration(
    std::uint8_t raw, std::uint8_t suppression) noexcept
{
    const int scaled = static_cast<int>(raw) * (255 - static_cast<int>(suppression))
                       / 255;
    return static_cast<std::uint8_t>(scaled);
}

/// 灵脉目标数量。面积相对参考图幅线性缩放，并以天下总数封顶。
struct VeinTarget
{
    std::uint32_t dragon;
    std::uint32_t main;
    std::uint32_t branch;
};

[[nodiscard]] constexpr VeinTarget vein_targets(std::uint32_t area) noexcept
{
    const auto scaled = [area](std::uint32_t total) noexcept {
        const std::uint64_t n =
            (static_cast<std::uint64_t>(total) * area + kReferenceArea / 2)
            / kReferenceArea;
        return static_cast<std::uint32_t>(
            n < total ? n : total);
    };
    return VeinTarget{kDragonVeinTotal, scaled(kMainVeinTotal),
                      scaled(kBranchVeinTotal)};
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 给 `enums.hpp` 扩容 `Stream`**

在 `include/gamev2/world/enums.hpp` 的 `Stream` 枚举尾部追加：

```cpp
    deposit = 9,
    /// 灵潮状态阶段不取随机数，编号先占住。
    tide = 10,
    ley = 11,
    vein = 12,
    domain = 13,
    scar = 14,
    heaven = 15,
    /// 地貌判定阶段不取随机数，编号先占住。
    land = 16,
};
```

把 `kStreamIds` 改成 `std::array<std::uint32_t, 17>`，并在数组尾部追加：

```cpp
    static_cast<std::uint32_t>(Stream::tide),
    static_cast<std::uint32_t>(Stream::ley),
    static_cast<std::uint32_t>(Stream::vein),
    static_cast<std::uint32_t>(Stream::domain),
    static_cast<std::uint32_t>(Stream::scar),
    static_cast<std::uint32_t>(Stream::heaven),
    static_cast<std::uint32_t>(Stream::land),
};
```

`static_assert(stream_ids_are_distinct())` 保持原样，它会自动拦住重复流号。

- [ ] **Step 6: 给 `params.hpp` 加灵历年**

在 `include/gamev2/world/params.hpp` 的 `WorldParams` 里，`ocean_percent` 之后追加：

```cpp
    /// 灵历年。世界的「年」以第一轮灵潮之始为元年。
    /// 当代为灵历 8914 年（谷底期第一百一十三年）。
    std::uint32_t spirit_year = 8914;
```

并把 `is_valid()` 改成：

```cpp
    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return width > 0 && height > 0 && z_levels > 0
               && z_cell_depth > 0 && ocean_percent <= 100
               && spirit_year >= 1;
    }
```

灵历 0 会让 `(year - 1) / 1000` 下溢，必须在同一道关口拦下。

- [ ] **Step 7: 给 `world.hpp` 加灵气层字段**

在 `include/gamev2/world/world.hpp` 里：

1. 头部加 `#include "gamev2/world/spirit.hpp"`
2. 在 `soil_depth` / `aquifer_top` 之后、`/// ── 地下量` 之前插入：

```cpp
    // ── 灵气层，地表量 ──
    Grid<std::uint8_t>  ley_strength;
    Grid<std::uint8_t>  ley_depth;
    Grid<Element>       ley_element;
    Grid<VeinRank>      vein_rank;
    Grid<std::uint32_t> vein_id;
    Grid<std::uint32_t> eye_flow;
    Grid<std::uint8_t>  domain_concentration;
    Grid<DomainGrade>   domain_grade;
    Grid<Scar>          scar_kind;
    Grid<std::uint8_t>  spirit_suppression;
    Grid<LandType>      land_type;
    Grid<std::uint8_t>  eye_purity;
    Grid<bool>          cave_heaven;
```

3. 在 `std::vector<Grid<bool>> aquifer;` 之后插入：

```cpp
    // ── 灵气层，非网格量 ──
    SpiritTide spirit_tide;
    std::vector<LeyChannel>  ley_channels;
    std::vector<LeyNode>     ley_nodes;
    std::vector<SpiritVein>  veins;
    std::vector<SpiritEye>   eyes;
    std::vector<ScarSite>    scars;
    std::vector<CaveHeaven>  cave_heavens;
```

- [ ] **Step 8: 在 `world.cpp` 里初始化新网格**

把 `src/world/world.cpp` 的构造函数初始化列表改成（新增行夹在 `aquifer_top`
与 `stone` 之间，末尾追加灵气层网格）：

```cpp
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
      ley_strength(params.width, params.height, 0),
      ley_depth(params.width, params.height, 0),
      ley_element(params.width, params.height, Element::none),
      vein_rank(params.width, params.height, VeinRank::none),
      vein_id(params.width, params.height, kNoVein),
      eye_flow(params.width, params.height, 0),
      domain_concentration(params.width, params.height, 0),
      domain_grade(params.width, params.height, DomainGrade::barren),
      scar_kind(params.width, params.height, Scar::none),
      spirit_suppression(params.width, params.height, 0),
      land_type(params.width, params.height, LandType::mundane),
      eye_purity(params.width, params.height, 0),
      cave_heaven(params.width, params.height, false),
      stone(make_layers(params, Stone::granite)),
      deposit(make_layers(params, Deposit::none)),
      cavern(make_layers(params, false)),
      aquifer(make_layers(params, false))
{
}
```

**成员声明顺序必须与初始化列表顺序一致**，否则 `-Wreorder` 会报警。Step 7
的插入位置就是为此。

- [ ] **Step 9: 在 `generate.hpp` 声明七个阶段**

在 `include/gamev2/world/generate.hpp` 的 `finalize_aquifers` 声明之后追加：

```cpp
/// 阶段 11。只读 params.spirit_year，写 spirit_tide。
void make_spirit_tide(World& world);

/// 阶段 12。读 elevation、sea_level、downstream、flow、stone，
/// 写 ley_channels、ley_nodes 与三张地脉镜像网格。
void make_ley_network(World& world);

/// 阶段 13。读 ley_channels、ley_nodes、elevation，写 veins、eyes、
/// vein_rank、vein_id、eye_flow。灵脉品阶由排名决定，数量是硬约束。
void make_spirit_veins(World& world);

/// 阶段 14。读 eyes 与 spirit_tide，写 domain_concentration、domain_grade。
/// 浓度场是灵眼的衰减叠加，不得凭空生成。
void make_spirit_domains(World& world);

/// 阶段 15。读 veins、eyes、elevation、domain_grade，
/// 写 scars、scar_kind、spirit_suppression。
void make_historical_scars(World& world);

/// 阶段 16。读 domain_grade、domain_concentration、ley_element、scars、
/// eyes，写 land_type、eye_purity。纯度由阶段 16 写，因为它依赖周边地貌。
void make_land_types(World& world);

/// 阶段 17。读 land_type、domain_grade、veins、domain_concentration，
/// 写 cave_heaven、cave_heavens。洞天只落在福地上。
void make_cave_heavens(World& world);
```

此时这些函数只有声明没有定义。`generate_world()` 还没调用它们，因此不会
产生链接错误——Task 9 才接线。

- [ ] **Step 10: 把新文件加进 CMakeLists**

在 `CMakeLists.txt` 的 `WORLDGEN_SOURCES` 里追加：

```cmake
    src/world/ley.cpp
    src/world/veins.cpp
    src/world/domains.cpp
    src/world/land.cpp
    src/world/heavens.cpp
```

在 `WORLDGEN_TEST_SOURCES` 里追加：

```cmake
    tests/test_world_spirit_tide.cpp
```

本任务只加这一个测试文件。后续任务各自追加自己的。

- [ ] **Step 11: 构建并运行测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 编译失败——`src/world/ley.cpp` 等五个源文件还不存在。

先建五个空文件占位，使构建能通过：

```powershell
$new = 'src/world/ley.cpp', 'src/world/veins.cpp', 'src/world/domains.cpp',
       'src/world/land.cpp', 'src/world/heavens.cpp'
New-Item -ItemType File -Path $new -Force
```

再次构建并运行：

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

- [ ] **Step 12: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 报告本任务完成的内容与新增文件，等待用户决定
提交时机。供用户参考的暂存命令：

```powershell
git add include/gamev2/world/spirit.hpp include/gamev2/world/spirit_tables.hpp
git add include/gamev2/world/enums.hpp include/gamev2/world/params.hpp
git add include/gamev2/world/world.hpp include/gamev2/world/generate.hpp
git add src/world/world.cpp CMakeLists.txt
git add tests/test_world_spirit_tide.cpp
```

---

### Task 2: 灵潮周期状态

**Files:**
- Create: `src/world/ley.cpp`
- Modify: `include/gamev2/world/spirit_tables.hpp`
- Test: `tests/test_world_spirit_tide.cpp`

**Interfaces:**
- Consumes: `WorldParams::spirit_year`、`SpiritTide`、`TidePhase`
- Produces:
  - `void gamev2::world::make_spirit_tide(World& world)`
  - `constexpr gamev2::world::compute_spirit_tide(std::uint32_t year)
    -> SpiritTide`（纯函数，放在 `spirit_tables.hpp`）

- [ ] **Step 1: 写失败的测试**

在 `tests/test_world_spirit_tide.cpp` 的匿名命名空间内追加：

```cpp
// constexpr 不是装饰：这条链必须真能用于常量求值。若实现里混进了
// 非 constexpr 的调用（如 std::lround），下面几条会直接编译失败，
// 而不是等到下游写 static_assert 时才炸。
static_assert(gamev2::world::compute_spirit_tide(8914).cycle == 8u);
static_assert(gamev2::world::compute_spirit_tide(8914).year_in_phase == 113u);
static_assert(gamev2::world::compute_spirit_tide(8001).peak == 110u);
static_assert(gamev2::world::compute_spirit_tide(9001).peak == 99u);
static_assert(gamev2::world::compute_spirit_tide(9300).year_in_phase == 299u);

TEST(contemporary_year_matches_the_setting)
{
    // 设定 2.6 与 9：当代是灵历 8914 年，谷底期第一百一十三年，
    // 距下一轮灵潮尚有八十七年。五个数必须同时对上。
    const auto tide = gamev2::world::compute_spirit_tide(8914);
    CHECK_EQ(tide.year, 8914u);
    CHECK_EQ(tide.cycle, 8u);  // 索引从 0 起，8 即第九轮
    CHECK(tide.phase == gamev2::world::TidePhase::trough);
    CHECK_EQ(tide.year_in_phase, 113u);
    CHECK_EQ(tide.years_to_next_tide, 87u);
}

TEST(tide_loss_reproduces_the_setting_figure)
{
    // 设定 2.6：「天机阁测得每轮损耗约一成——九轮之后，下一轮灵潮的
    // 峰值不足元潮时期的四成」。0.9^9 = 0.387 < 0.4。
    //
    // 注意轮次语义：第九轮的峰值是 0.9^8，设定说的「不足四成」指的是
    // 下一轮，也就是 0.9^9。两个指数差一，写错哪一个都会红。
    const auto ninth = gamev2::world::compute_spirit_tide(8001);
    CHECK_EQ(ninth.peak, 110u);  // 0.9^8 = 0.43046721 -> 109.77 -> 110

    const auto tenth = gamev2::world::compute_spirit_tide(9001);
    CHECK_EQ(tenth.peak, 99u);   // 0.9^9 = 0.387420489 -> 98.79 -> 99
    CHECK(tenth.peak < 102u);    // 0.4 * 255 = 102
}

TEST(phase_boundaries_land_on_the_setting_years)
{
    // 灵潮期 300 年、衰退期 500 年、谷底期 200 年，合计千年。
    // 第九轮灵潮期是灵历 8001—8301，衰退期 8301—8801，谷底期 8801—9001。
    CHECK(gamev2::world::compute_spirit_tide(8001).phase
          == gamev2::world::TidePhase::tide);
    CHECK(gamev2::world::compute_spirit_tide(8300).phase
          == gamev2::world::TidePhase::tide);
    CHECK(gamev2::world::compute_spirit_tide(8301).phase
          == gamev2::world::TidePhase::decline);
    CHECK(gamev2::world::compute_spirit_tide(8800).phase
          == gamev2::world::TidePhase::decline);
    CHECK(gamev2::world::compute_spirit_tide(8801).phase
          == gamev2::world::TidePhase::trough);
    CHECK(gamev2::world::compute_spirit_tide(9000).phase
          == gamev2::world::TidePhase::trough);
    CHECK(gamev2::world::compute_spirit_tide(9001).phase
          == gamev2::world::TidePhase::tide);
}

TEST(peak_year_of_the_tide_phase_reaches_the_peak_ratio)
{
    // 灵潮期的最后一年（灵历 9300）应当正好落在本轮峰值上。
    // 9301 已经是衰退期的第一年——阶段边界年归后一个阶段。
    const auto peak = gamev2::world::compute_spirit_tide(9300);
    CHECK(peak.phase == gamev2::world::TidePhase::tide);
    CHECK_EQ(peak.year_in_phase, 299u);
    CHECK_EQ(peak.level, peak.peak);
}

TEST(trough_is_low_but_not_empty)
{
    // 谷底期不是绝对零——设定说「灵脉半死」，不是死透。
    const auto trough = gamev2::world::compute_spirit_tide(8914);
    CHECK(trough.level > 0u);
    CHECK(trough.level < trough.peak);
}

TEST(the_year_boundaries_of_the_tide_cycle)
{
    // 合法年份的下界。灵历元年是元潮，峰值满格 255，潮位按相位
    // 走到第 1/300 处。
    const auto first = gamev2::world::compute_spirit_tide(1);
    CHECK_EQ(first.cycle, 0u);
    CHECK_EQ(first.peak, 255u);
    CHECK_EQ(first.level, 77u);
    CHECK(first.phase == gamev2::world::TidePhase::tide);
    CHECK_EQ(first.years_to_next_tide, 1000u);

    // 第 19 轮之后损耗封顶：9^20 起放不进 uint64，比例停在 0.9^19。
    // 封顶是已知的模型边界，不是精度问题——把它钉住，免得以后有人
    // 以为「再往后也在正常衰减」。
    const auto nineteenth = gamev2::world::compute_spirit_tide(19001);
    const auto twentieth = gamev2::world::compute_spirit_tide(20001);
    CHECK_EQ(nineteenth.peak, 34u);   // 0.9^19 = 0.13508 -> 34
    CHECK_EQ(twentieth.peak, 34u);    // 封顶，与第十九轮同值
    CHECK_EQ(twentieth.cycle, 20u);
}

TEST(the_stage_writes_the_same_state_as_the_pure_function)
{
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 1;
    p.spirit_year = 8914;

    World w(p);
    gamev2::world::make_spirit_tide(w);

    const auto expected = gamev2::world::compute_spirit_tide(8914);
    CHECK_EQ(w.spirit_tide.year, expected.year);
    CHECK_EQ(w.spirit_tide.cycle, expected.cycle);
    CHECK(w.spirit_tide.phase == expected.phase);
    CHECK_EQ(w.spirit_tide.year_in_phase, expected.year_in_phase);
    CHECK_EQ(w.spirit_tide.years_to_next_tide, expected.years_to_next_tide);
    CHECK_EQ(w.spirit_tide.level, expected.level);
    CHECK_EQ(w.spirit_tide.peak, expected.peak);
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，报 `compute_spirit_tide` 未声明。

- [ ] **Step 3: 写 `compute_spirit_tide`**

在 `include/gamev2/world/spirit_tables.hpp` 追加（放在 `vein_targets` 之前）：

```cpp
/// 每轮的损耗比例，用整数幂精确表示。
///
/// 不用 std::pow：libm 的末位差异与 FMA 收缩都会让同一串数字在不同的
/// 编译选项下算出不同的字节，破坏「同 seed 同世界」。9^n / 10^n 可以
/// 整除到底，代价是分母 10^n 必须放得进 uint64。
///
/// 第 20 轮起封顶：分母 10^20 = 1e20 已经超过 UINT64_MAX（约 1.8e19），
/// 再乘就回绕。再往后每轮的损耗就停在 0.9^19 不再下降，等于「设定里的
/// 世界比这个模型活得久」，需要更长的轮次就得起用大整数。
[[nodiscard]] constexpr double tide_decay_ratio(std::uint32_t cycle) noexcept
{
    const std::uint32_t capped = cycle > 19u ? 19u : cycle;
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
    for (std::uint32_t i = 0; i < capped; ++i)
    {
        numerator *= 9u;
        denominator *= 10u;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

/// 比例转字节。四舍五入，不做截断——截断会让 0.5 永远差一格。
///
/// 不用 std::lround：它要到 C++26 才 constexpr 化，用了这条链就永远
/// 只能在运行时求值，签名上的 constexpr 成了空话。对本模型能产出的
/// 比例（0.9^n 与相位插值）而言，`x + 0.5` 再截断与 lround 的
/// 「远离零四舍五入」结果完全相同。
[[nodiscard]] constexpr std::uint8_t ratio_to_byte(double ratio) noexcept
{
    const double scaled = ratio * 255.0;
    const long rounded = scaled >= 0.0 ? static_cast<long>(scaled + 0.5) : 0L;
    return static_cast<std::uint8_t>(rounded > 255L ? 255L : rounded);
}

/// 由灵历年算出灵潮周期状态。
///
/// 周期是千年一轮：灵潮期 300 年、衰退期 500 年、谷底期 200 年。
/// 轮次索引从 0 起，0 即元潮（灵历元年）。
///
/// 合法年份是 1..UINT32_MAX。year 必须 >= 1：`(year - 1) / 1000` 在
/// year == 0 时下溢，会算出一个貌似合理的「第 4294967 轮」而不报错。
/// WorldParams::is_valid() 已经拦下这个值。
[[nodiscard]] constexpr SpiritTide compute_spirit_tide(
    std::uint32_t year) noexcept
{
    SpiritTide tide;
    tide.year = year;
    tide.cycle = (year - 1u) / 1000u;

    const std::uint32_t offset = year - tide.cycle * 1000u - 1u;
    if (offset < 300u)
    {
        tide.phase = TidePhase::tide;
        tide.year_in_phase = offset;
    }
    else if (offset < 800u)
    {
        tide.phase = TidePhase::decline;
        tide.year_in_phase = offset - 300u;
    }
    else
    {
        tide.phase = TidePhase::trough;
        tide.year_in_phase = offset - 800u;
    }
    tide.years_to_next_tide = 1000u - offset;

    const double peak = tide_decay_ratio(tide.cycle);
    double shape = 0.0;
    switch (tide.phase)
    {
    case TidePhase::tide:
        // +1 让阶段的最后一年正好取到 1.0：灵潮期只有 300 个相位年，
        // 不加一的话峰值永远差 1/300 达不到。
        shape = static_cast<double>(tide.year_in_phase + 1u) / 300.0;
        break;
    case TidePhase::decline:
        shape = 1.0 - static_cast<double>(tide.year_in_phase) / 500.0;
        break;
    case TidePhase::trough:
        shape = 0.0;
        break;
    }
    const double level =
        peak * (kTroughFloor + (1.0 - kTroughFloor) * shape);

    tide.peak = ratio_to_byte(peak);
    tide.level = ratio_to_byte(level);
    return tide;
}
```

**不要加 `#include <cmath>`**：取整刻意不用 `std::lround`（它要到 C++26 才
constexpr 化），因此这个头文件用不到任何 `<cmath>` 的东西。

- [ ] **Step 4: 写 `make_spirit_tide`**

创建 `src/world/ley.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

namespace gamev2::world {

void make_spirit_tide(World& world)
{
    world.spirit_tide = compute_spirit_tide(world.params().spirit_year);
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/world/ley.cpp include/gamev2/world/spirit_tables.hpp
git add tests/test_world_spirit_tide.cpp
```

---

### Task 3: 地脉网络

**Files:**
- Modify: `src/world/ley.cpp`
- Modify: `include/gamev2/world/spirit_tables.hpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_ley.cpp`

**Interfaces:**
- Consumes: `World::elevation`、`World::sea_level()`、`World::downstream`、
  `World::flow`、`World::lake_depth`、`World::stone`、
  `Stream::ley`、`element_of_stone(Stone)`
- Produces:
  - `World::ley_channels`（`std::vector<LeyChannel>`）
  - `World::ley_nodes`（`std::vector<LeyNode>`）
  - `World::ley_strength`（`Grid<std::uint8_t>`，0..255）
  - `World::ley_depth`（`Grid<std::uint8_t>`）
  - `World::ley_element`（`Grid<Element>`）
  - `spirit_tables.hpp` 新增 `kMinLeyDepth`、`kMaxLeyDepth`、`kHeteroChance`
  - `required_ley_channels(std::uint32_t area)` —— 阶段 13 需要的**通道数下界**
    （随面积缩放，故不是常量），以及「通道数不足时逐步放宽源点间距重选」的兜底。
    见设计文档 §3.2 步骤 1

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_ley.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::Element;
using gamev2::world::LeyChannel;
using gamev2::world::LeyNode;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::element_of_stone;
using gamev2::world::is_hetero;
using gamev2::world::kHeteroChance;
using gamev2::world::kMaxLeyDepth;
using gamev2::world::kMinLeyDepth;

WorldParams params_of(std::uint32_t width, std::uint32_t height,
                      std::uint64_t seed)
{
    WorldParams p;
    p.width = width;
    p.height = height;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

/// 沿 next 走到底，返回走过的步数。成环时步数会顶到上限（节点数 + 1）。
std::uint32_t walk_to_end(const World& w, std::uint32_t start)
{
    std::uint32_t steps = 0;
    std::uint32_t cur = start;
    const std::uint32_t limit =
        static_cast<std::uint32_t>(w.ley_nodes.size()) + 1u;
    while (w.ley_nodes[cur].next != gamev2::world::kNoNode
           && steps < limit)
    {
        cur = w.ley_nodes[cur].next;
        ++steps;
    }
    return steps;
}

TEST(ley_network_produces_channels_and_nodes)
{
    const WorldParams p = params_of(64, 64, 2026);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    CHECK(!w.ley_channels.empty());
    CHECK(!w.ley_nodes.empty());

    // 通道的节点序列必须真的存在，且首尾有序。
    for (const LeyChannel& c : w.ley_channels)
    {
        CHECK(!c.nodes.empty());
        CHECK(c.nodes.front() < w.ley_nodes.size());
        CHECK(c.nodes.back() < w.ley_nodes.size());
    }
}

TEST(ley_nodes_sit_on_land)
{
    const WorldParams p = params_of(64, 64, 77);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        // 地脉不出海：河口不是灵眼。
        CHECK(w.elevation.at(n.cell) >= w.sea_level());
    }
}

TEST(ley_next_always_points_downhill)
{
    const WorldParams p = params_of(64, 64, 5);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            const LeyNode& up = w.ley_nodes[c.nodes[i]];
            const LeyNode& down = w.ley_nodes[c.nodes[i + 1]];
            // 通道内相邻节点必须由 next 相连。只查「有 next 才断言」会在
            // next 缺失时整条空跑——那正是节点图退化成孤立点集时的样子。
            CHECK(up.next == c.nodes[i + 1]);
            // 高程严格更低是节点图无环的唯一保证。
            CHECK(w.elevation.at(down.cell) < w.elevation.at(up.cell));
            ++links;
        }
    }
    // 相邻性断言必须真的跑过，否则这条测试等于空跑。
    CHECK(links > 0);
}

TEST(ley_graph_is_acyclic)
{
    const WorldParams p = params_of(64, 64, 11);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            // 节点图的边必须沿通道铺开，否则「图」只是一堆孤立节点。
            CHECK(w.ley_nodes[c.nodes[i]].next == c.nodes[i + 1]);
            ++links;
        }
    }
    CHECK(links > 0);
    for (std::uint32_t i = 0; i < w.ley_nodes.size(); ++i)
    {
        // 走到底的步数必须小于节点总数，否则说明有环。
        CHECK(walk_to_end(w, i) < w.ley_nodes.size());
    }
}

TEST(ley_flow_grows_downstream)
{
    const WorldParams p = params_of(64, 64, 33);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            const LeyNode& up = w.ley_nodes[c.nodes[i]];
            const LeyNode& down = w.ley_nodes[c.nodes[i + 1]];
            // 通道相邻节点必须由 next 相连，否则无从谈「下游」。
            CHECK(up.next == c.nodes[i + 1]);
            // 下游承接了本节点的全部流量，还多出自己那份，因此严格更大。
            // 只断言「不少于」的话，流量恒为常量也能过——那正是
            // 「流量沿通道累加」被破坏时的样子。
            CHECK(down.flow > up.flow);
            ++links;
        }
    }
    CHECK(links > 0);
}

TEST(ley_depth_stays_inside_its_bounds)
{
    const WorldParams p = params_of(64, 64, 91);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        CHECK(n.depth >= kMinLeyDepth);
        CHECK(n.depth <= kMaxLeyDepth);
    }
}

TEST(ley_element_comes_from_the_rock_it_passes_through)
{
    const WorldParams p = params_of(64, 64, 404);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        const auto z = std::min<std::uint32_t>(p.z_levels - 1u,
                                                n.depth / p.z_cell_depth);
        const Element from_rock = element_of_stone(w.stone[z].at(n.cell));
        // 属性要么就是岩性给的，要么是整条通道被注入的异质。
        CHECK(n.element == from_rock || is_hetero(n.element));
    }
}

TEST(hetero_elements_cover_whole_channels_only)
{
    // fixture 选 128x128/seed 43：实测 33 条通道里 8 条带异质，覆盖面够。
    const WorldParams p = params_of(128, 128, 43);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t hetero_channels = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        Element hetero = Element::none;
        for (const std::uint32_t index : c.nodes)
        {
            const Element e = w.ley_nodes[index].element;
            if (!is_hetero(e))
            {
                continue;
            }
            if (hetero == Element::none)
            {
                hetero = e;
                continue;
            }
            // (a) 同一条通道内不得出现两种不同的异质元素。
            CHECK(e == hetero);
        }
        if (hetero == Element::none)
        {
            // 非异质通道的属性逐节点由岩性给出，同一条通道上五行不同
            // 是正常的（设计文档 §3.2），不必强求一致。
            continue;
        }
        ++hetero_channels;
        // (b) 一旦出现异质，该通道所有节点都必须是它。
        for (const std::uint32_t index : c.nodes)
        {
            CHECK(w.ley_nodes[index].element == hetero);
        }
    }
    // 守卫：fixture 一旦不再产出异质通道，上面两条断言就整条空跑，
    // 这时测试必须变红，而不是静默通过。
    CHECK(hetero_channels > 0);
}

TEST(hetero_channels_stay_a_small_fraction)
{
    // fixture 选 128x128/seed 97：实测 34 条通道里 7 条带异质，
    // 占比 7/34 在上界之内，且离「一半」还有余量。
    const WorldParams p = params_of(128, 128, 97);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t hetero = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        if (is_hetero(w.ley_nodes[c.nodes.front()].element))
        {
            ++hetero;
        }
    }
    // 守卫：没有异质通道时，占比断言恒真，等于空跑。
    CHECK(hetero > 0);
    // 概率是 8%，实现在一个通道上整体判定。上界给到一半已经宽松到
    // 不可能被运气突破：真的占一半说明判定逻辑坏了。
    CHECK(hetero * 2 <= w.ley_channels.size());
}

TEST(ley_mirror_matches_the_graph)
{
    const WorldParams p = params_of(64, 64, 606);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t mirrored = 0;
    for (std::uint32_t i = 0; i < w.ley_strength.size(); ++i)
    {
        if (w.ley_strength.at(i) == 0)
        {
            continue;
        }
        ++mirrored;
        CHECK(w.ley_depth.at(i) >= kMinLeyDepth);
        CHECK(w.ley_depth.at(i) <= kMaxLeyDepth);
    }
    // 镜像上非零的格数不得超过节点数（节点可能共享汇流点，
    // 但一个格子最多一个节点）。
    CHECK(mirrored <= w.ley_nodes.size());
    // 逐节点比对：镜像的每个值都必须等于图上的对应值。
    for (const LeyNode& n : w.ley_nodes)
    {
        // flow 是 uint32，镜像是 uint8：显式转换，别让隐式窄化混过去。
        const auto expected_strength = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(255u, n.flow));
        CHECK_EQ(w.ley_strength.at(n.cell), expected_strength);
        CHECK_EQ(w.ley_depth.at(n.cell), static_cast<std::uint8_t>(n.depth));
        CHECK(w.ley_element.at(n.cell) == n.element);
    }
}

TEST(ley_layer_is_deterministic)
{
    const WorldParams p = params_of(48, 48, 1234);
    const auto a = gamev2::world::generate_world(p);
    const auto b = gamev2::world::generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    CHECK_EQ(a->ley_nodes.size(), b->ley_nodes.size());
    for (std::size_t i = 0; i < a->ley_nodes.size(); ++i)
    {
        CHECK_EQ(a->ley_nodes[i].cell, b->ley_nodes[i].cell);
        CHECK_EQ(a->ley_nodes[i].depth, b->ley_nodes[i].depth);
        CHECK_EQ(a->ley_nodes[i].flow, b->ley_nodes[i].flow);
        CHECK_EQ(a->ley_nodes[i].element, b->ley_nodes[i].element);
    }
    for (std::uint32_t i = 0; i < a->ley_strength.size(); ++i)
    {
        CHECK_EQ(a->ley_strength.at(i), b->ley_strength.at(i));
        CHECK_EQ(a->ley_element.at(i), b->ley_element.at(i));
    }
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 里追加：

```cmake
    tests/test_world_ley.cpp
```

然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 全部测试通过——**因为阶段 12 还没接线，`generate_world` 不生成地脉，
而测试断言的是非空**。因此必须先让测试失败：把 `ley_network_produces_channels_and_nodes`
里的 `CHECK(!w.ley_channels.empty())` 改成 `CHECK(w.ley_channels.empty())`
跑一次确认它红（说明测试确实在观察地脉层），然后改回来。

更直接的做法：跳过这一步的「确认失败」，在 Step 3 实现完之后 Step 4 的
「确认通过」会真实验证。但为了守住 TDD 节奏，请按上面的方式先红一次。

- [ ] **Step 3: 把埋深与异质的常量加进 `spirit_tables.hpp`**

在 `include/gamev2/world/spirit_tables.hpp` 的 `kDomainMaxRadius` 之后追加：

```cpp
/// 地脉埋深的上下界，单位是格。
inline constexpr std::uint32_t kMinLeyDepth = 2;
inline constexpr std::uint32_t kMaxLeyDepth = 64;

/// 异质地脉出现的概率。设定 2.1：「少数地脉夹带异质」，稀有且难驾驭。
inline constexpr double kHeteroChance = 0.08;
```

- [ ] **Step 4: 实现地脉网络**

把 `src/world/ley.cpp` 改成下面这样。**注意：不是整体替换**——Task 2 已经写进去的
`make_spirit_tide`（阶段 11）必须保留，否则链接期会报未定义符号：Task 3 Step 5 的
编排代码和 `tests/test_world_spirit_tide.cpp` 都要调用它。阶段 11 排在阶段 12
之前，与阶段号顺序一致：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 源点至少要比海平面高这么多。低于这个高度盖层太厚，地脉埋得太深，
/// 出不了灵眼。
constexpr std::uint8_t kLeySourceRelief = 12;

/// 源点之间的最小间距（切比雪夫距离）。防止源点挤成一团、通道互相踩。
///
/// 取 6 而不是 8 或 12：间距才是地脉密度实际的约束（面积预算 1/300 比它
/// 宽，不起作用），而阶段 13 的灵脉数量是硬约束——龙脉 1、主脉 9、支脉 40
/// 共 50 条，每条要占一条通道。实测 256×256：间距 8 时最差的一批种子只
/// 出得出 43 条通道（切不出 50 段），间距 6 时同一批种子的通道数最低
/// 69 条，缺口消失。
constexpr std::uint32_t kLeySourceSpacing = 6;

/// 每格面积产出的源点数。让「地脉密度」在任何图幅上都指同一件事，
/// 与既有设计里 kElevationFeatures 的做法一致。
///
/// 这是上限而非实际密度：实际源点数由 kLeySourceSpacing 与地形决定，
/// 256×256 的图上两者给出约一百条通道。
constexpr double kLeySourcePerCell = 1.0 / 300.0;

/// 源点数量的绝对下限。小图幅上也要有几条通道才谈得上灵脉。
constexpr std::uint32_t kMinLeySources = 4;

/// 本图幅需要的地脉通道数下界。
///
/// 阶段 13 的灵脉数量是硬约束：龙脉 1、主脉 9、支脉 40 共 50 条，每条
/// 灵脉占一条通道，通道不够就切不出那么多段，品阶数量就凑不齐。多出的
/// 一条是给荒脉留的位置——荒脉也得有，否则「残余矿化」无处落地。
std::uint32_t required_ley_channels(std::uint32_t area)
{
    const auto target = vein_targets(area);
    return target.dragon + target.main + target.branch + 1u;
}

/// 单条通道的最大步数。
constexpr std::uint32_t kLeyMaxSteps = 512;

/// 埋深基准。relief 为 0（汇水区底部）时的埋深。
constexpr std::uint32_t kLeyBaseDepth = 40;

/// 埋深对 relief 的敏感度。relief 越大盖层越薄，埋得越浅。
constexpr std::uint32_t kLeyReliefShallow = 1;

/// 断层处的额外埋深。设定 2.1：「断层是裂口」。
constexpr std::uint32_t kLeyFaultDepth = 12;

/// 每个源点贡献的流量。
constexpr std::uint32_t kLeySourceFlow = 100;

/// 8 邻域偏移。顺序固定，择序与遍历才有一致的结果。
constexpr int kDx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

/// 追踪过程中的临时结果。
struct TraceResult
{
    std::vector<LeyChannel> channels;
    std::vector<LeyNode> nodes;
    /// 每格对应的节点索引。kNoNode 表示该格不在任何地脉上。
    std::vector<std::uint32_t> node_of_cell;
};

/// 汇水区底部的高程：沿 downstream 走到终点（海洋或湖底）的那一格。
///
/// 地脉埋深按「高出汇水区底部多少」反推，因此每格都要这个值。
/// 同一条链上的格子共享同一个终点，记忆化避免重复走。
std::vector<std::uint8_t> compute_basin_floor(const World& world)
{
    const std::uint32_t n = world.elevation.size();
    std::vector<std::uint8_t> floor(n, 0);
    std::vector<char> known(n, 0);

    for (std::uint32_t i = 0; i < n; ++i)
    {
        if (known[i] != 0)
        {
            continue;
        }
        std::uint32_t cur = i;
        std::uint32_t steps = 0;
        while (world.downstream.at(cur) != kNoDownstream && steps <= n)
        {
            cur = world.downstream.at(cur);
            ++steps;
        }
        const std::uint8_t base = world.elevation.at(cur);
        floor[cur] = base;
        known[cur] = 1;

        cur = i;
        steps = 0;
        while (known[cur] == 0 && steps <= n)
        {
            floor[cur] = base;
            known[cur] = 1;
            const std::uint32_t next = world.downstream.at(cur);
            if (next == kNoDownstream)
            {
                break;
            }
            cur = next;
            ++steps;
        }
    }
    return floor;
}

/// 找出地脉源点：8 邻域内无更高者，且高过海平面一个余量。
///
/// 取「无更高者」而不是「唯一最高」：等高的平台可以有多个源点，
/// 只取唯一最高会把整块高原当成一个源。
///
/// spacing 是源点之间的最小间距。调用方在通道数不够时会放宽它重选，
/// 因此这里不做任何兜底：放宽与收紧都走同一条路径。
std::vector<std::uint32_t> pick_sources(const World& world,
                                         std::uint32_t spacing)
{
    const WorldParams& p = world.params();
    std::vector<std::uint32_t> candidates;

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.elevation.at(i) < world.sea_level() + kLeySourceRelief)
            {
                continue;
            }
            bool peak = true;
            for (int d = 0; d < 8 && peak; ++d)
            {
                const int nx = static_cast<int>(x) + kDx8[d];
                const int ny = static_cast<int>(y) + kDy8[d];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                    || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (world.elevation.at(j) > world.elevation.at(i))
                {
                    peak = false;
                }
            }
            if (peak)
            {
                candidates.push_back(i);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [&world](std::uint32_t a, std::uint32_t b) {
                  if (world.elevation.at(a) != world.elevation.at(b))
                  {
                      return world.elevation.at(a) > world.elevation.at(b);
                  }
                  return a < b;
              });

    const std::uint32_t area = p.width * p.height;
    const auto wanted = static_cast<std::uint32_t>(std::max<double>(
        static_cast<double>(std::max(
            kMinLeySources, required_ley_channels(area))),
        std::lround(static_cast<double>(area) * kLeySourcePerCell)));

    std::vector<std::uint32_t> sources;
    for (const std::uint32_t cell : candidates)
    {
        if (sources.size() >= wanted)
        {
            break;
        }
        const auto x = static_cast<int>(cell % p.width);
        const auto y = static_cast<int>(cell / p.width);
        bool too_close = false;
        for (const std::uint32_t taken : sources)
        {
            const auto tx = static_cast<int>(taken % p.width);
            const auto ty = static_cast<int>(taken / p.width);
            const int dx = std::abs(x - tx);
            const int dy = std::abs(y - ty);
            if (std::max(dx, dy) < static_cast<int>(spacing))
            {
                too_close = true;
                break;
            }
        }
        if (!too_close)
        {
            sources.push_back(cell);
        }
    }
    return sources;
}

/// 埋深。盖层在山脊处薄、盆地处厚，地脉贴着盖层底走，因此埋深由
/// 「高出汇水区底部多少」反推：relief 越大，埋得越浅。
///
/// 断层另加一段埋深：设定 2.1 说「断层是裂口」，地脉在断层处沉入
/// 深部，抬升的浓度被打断——灵脉的切分边界由此而来。
std::uint32_t depth_of(const World& world, std::uint32_t cell,
                        std::uint8_t basin_floor)
{
    const WorldParams& p = world.params();
    const int relief = static_cast<int>(world.elevation.at(cell))
                       - static_cast<int>(basin_floor);

    const auto x = static_cast<int>(cell % p.width);
    const auto y = static_cast<int>(cell / p.width);
    int gradient = 0;
    for (int d = 0; d < 8; ++d)
    {
        const int nx = x + kDx8[d];
        const int ny = y + kDy8[d];
        if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
            || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        const std::uint32_t j = world.elevation.index_of(
            static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
        const int delta = static_cast<int>(world.elevation.at(cell))
                          - static_cast<int>(world.elevation.at(j));
        gradient = std::max(gradient, std::abs(delta));
    }

    const int raw = static_cast<int>(kLeyBaseDepth)
                    - static_cast<int>(kLeyReliefShallow) * relief
                    + static_cast<int>(kLeyFaultDepth) * gradient / 16;
    return static_cast<std::uint32_t>(std::clamp(
        raw, static_cast<int>(kMinLeyDepth), static_cast<int>(kMaxLeyDepth)));
}

/// 全图埋深。埋深是每格自身的属性，与路径无关，因此可以预先算好——择序
/// 第三条要比的正是它，每步现算会把追踪的代价放大九倍。
std::vector<std::uint32_t> compute_ley_depths(
    const World& world, const std::vector<std::uint8_t>& floor)
{
    const std::uint32_t n = world.elevation.size();
    std::vector<std::uint32_t> depths(n);
    for (std::uint32_t i = 0; i < n; ++i)
    {
        depths[i] = depth_of(world, i, floor[i]);
    }
    return depths;
}

/// 节点属性由流经的岩性决定（设定 2.1）。埋深可能超过世界的总格深，
/// 超出部分取最深的岩层——那本来就是「地脉在更深的地方」。
Element element_of_node(const World& world, std::uint32_t cell,
                         std::uint32_t depth)
{
    const WorldParams& p = world.params();
    const auto z = std::min<std::uint32_t>(p.z_levels - 1u,
                                            depth / p.z_cell_depth);
    return element_of_stone(world.stone[z].at(cell));
}

/// 逐源点追踪通道。
///
/// 择序：先要求严格下降（否则通道会爬坡），再按汇流量大者优先，
/// 最后按埋深浅者优先。等价条件时靠 kDx8/kDy8 的固定顺序打破平局，
/// 因此结果与遍历顺序无关。
///
/// 第三条是「埋深浅」而不是「高程低」：两者在同一汇水区内是反的
/// （高程越高 → relief 越大 → 埋得越浅）。按高程低择优恰好会把通道推向
/// 河口，埋深随之变大。
TraceResult trace_channels(const World& world,
                            const std::vector<std::uint32_t>& depths,
                            std::uint32_t spacing)
{
    const WorldParams& p = world.params();
    const std::uint32_t n = world.elevation.size();

    TraceResult result;
    result.node_of_cell.assign(n, kNoNode);

    for (const std::uint32_t source : pick_sources(world, spacing))
    {
        LeyChannel channel;
        std::uint32_t cur = source;
        std::uint32_t prev = kNoNode;
        std::uint32_t steps = 0;

        while (steps < kLeyMaxSteps)
        {
            // 这一格已经在别的通道上：并入它，通道到此为止。
            // 这里的 next 覆盖通道内的边：末节点改指汇流目标。
            const std::uint32_t existing = result.node_of_cell[cur];
            if (existing != kNoNode)
            {
                if (prev != kNoNode)
                {
                    result.nodes[prev].next = existing;
                }
                ++result.nodes[existing].merges;
                break;
            }

            LeyNode node;
            node.cell = cur;
            node.depth = depths[cur];
            node.element = element_of_node(world, cur, node.depth);
            node.flow = kLeySourceFlow;
            const auto index = static_cast<std::uint32_t>(result.nodes.size());
            result.nodes.push_back(node);
            result.node_of_cell[cur] = index;
            channel.nodes.push_back(index);
            // 通道内相邻节点由 next 相连：流量沿通道累加的基础，也是
            // 「下游节点高程更低」这条无环保证的载体。走到的下一格必然
            // 更低——择序时只收严格下降的邻格。
            if (prev != kNoNode)
            {
                result.nodes[prev].next = index;
            }
            prev = index;

            const auto x = static_cast<int>(cur % p.width);
            const auto y = static_cast<int>(cur / p.width);
            std::uint32_t best = kNoNode;
            std::uint32_t best_flow = 0;
            std::uint32_t best_depth = 0;
            for (int d = 0; d < 8; ++d)
            {
                const int nx = x + kDx8[d];
                const int ny = y + kDy8[d];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                    || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (world.elevation.at(j) >= world.elevation.at(cur))
                {
                    continue;
                }
                // 地脉不出海：河口不是灵眼。
                if (world.elevation.at(j) < world.sea_level())
                {
                    continue;
                }
                // 已在别的通道上：并入它。
                if (result.node_of_cell[j] != kNoNode)
                {
                    best = j;
                    break;
                }
                if (best == kNoNode || world.flow.at(j) > best_flow
                    || (world.flow.at(j) == best_flow && depths[j] < best_depth))
                {
                    best = j;
                    best_flow = world.flow.at(j);
                    best_depth = depths[j];
                }
            }

            if (best == kNoNode)
            {
                break;  // 闭流死端
            }
            cur = best;
            ++steps;
        }

        if (!channel.nodes.empty())
        {
            result.channels.push_back(std::move(channel));
        }
    }
    return result;
}

/// 给少数地脉注入异质属性。
///
/// 异质不来自岩性——设定说「少数地脉夹带异质」，是地脉本身带着的。
/// 按通道整体判定，命中则整条通道的属性换成异质。整条换而不逐节点换，
/// 是为了保证「异质只整条通道出现」这条可断言的不变式。
void assign_hetero(TraceResult& trace, Pcg32& rng)
{
    constexpr std::array<Element, 7> kHeteroElements{
        Element::thunder, Element::ice,  Element::wind, Element::light,
        Element::dark,    Element::sound, Element::poison};

    for (const LeyChannel& channel : trace.channels)
    {
        if (channel.nodes.empty())
        {
            continue;
        }
        if (rng.unit() >= kHeteroChance)
        {
            continue;
        }
        const Element hetero =
            kHeteroElements[rng.below(static_cast<std::uint32_t>(
                kHeteroElements.size()))];
        for (const std::uint32_t index : channel.nodes)
        {
            trace.nodes[index].element = hetero;
        }
    }
}

/// 结算节点流量。
///
/// 节点图是一张 DAG：next 指针沿通道指向高程严格更低的节点，因此按
/// 高程降序处理时，每个节点被处理之前，它的全部上游都已把流量交给了它。
/// 这比逐通道结算省事，也不受通道创建顺序影响。
void settle_flows(const World& world, TraceResult& trace)
{
    const std::size_t n = trace.nodes.size();
    if (n == 0)
    {
        return;
    }

    std::vector<std::vector<std::uint32_t>> incoming(n);
    for (std::uint32_t v = 0; v < n; ++v)
    {
        if (trace.nodes[v].next != kNoNode)
        {
            incoming[trace.nodes[v].next].push_back(v);
        }
    }

    std::vector<std::uint32_t> order(n);
    for (std::uint32_t i = 0; i < n; ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&trace, &world](std::uint32_t a, std::uint32_t b) {
                  const std::uint8_t ea = world.elevation.at(trace.nodes[a].cell);
                  const std::uint8_t eb = world.elevation.at(trace.nodes[b].cell);
                  if (ea != eb)
                  {
                      return ea > eb;
                  }
                  return a < b;
              });

    for (const std::uint32_t u : order)
    {
        std::uint32_t inflow = 0;
        for (const std::uint32_t v : incoming[u])
        {
            inflow += trace.nodes[v].flow;
        }
        // flow 起手是本节点自己的贡献（kLeySourceFlow），加上上游汇来的。
        trace.nodes[u].flow += inflow;
    }
}

/// 把地脉图导出到栅格镜像。图是唯一的真相，镜像只写一次、只读。
void mirror_to_grids(World& world, const TraceResult& trace)
{
    for (const LeyNode& node : trace.nodes)
    {
        const std::uint32_t i = node.cell;
        world.ley_strength.at(i) = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(255u, node.flow));
        world.ley_depth.at(i) = static_cast<std::uint8_t>(node.depth);
        world.ley_element.at(i) = node.element;
    }
}

}  // namespace

void make_spirit_tide(World& world)
{
    world.spirit_tide = compute_spirit_tide(world.params().spirit_year);
}

void make_ley_network(World& world)
{
    const WorldParams& p = world.params();
    const std::vector<std::uint8_t> floor = compute_basin_floor(world);
    const std::vector<std::uint32_t> depths = compute_ley_depths(world, floor);

    // 通道数要有下界：阶段 13 的灵脉数量是硬约束，每条灵脉占一条通道，
    // 通道不够就切不出那么多段。地形平缓时按固定间距选不出这么多通道，
    // 就逐步放宽间距重选。绝大多数世界第一轮就够，长尾里的世界才重选。
    const std::uint32_t wanted = required_ley_channels(p.width * p.height);
    TraceResult trace;
    std::uint32_t best = 0;
    for (std::uint32_t spacing = kLeySourceSpacing; spacing >= 1; --spacing)
    {
        TraceResult relaxed = trace_channels(world, depths, spacing);
        if (relaxed.channels.size() > best)
        {
            best = relaxed.channels.size();
            trace = std::move(relaxed);
        }
        if (best >= wanted)
        {
            break;
        }
    }

    Pcg32 rng(stage_seed(world.params().seed,
                          static_cast<std::uint32_t>(Stream::ley)));
    assign_hetero(trace, rng);
    settle_flows(world, trace);
    mirror_to_grids(world, trace);

    world.ley_channels = std::move(trace.channels);
    world.ley_nodes = std::move(trace.nodes);
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 把阶段 12 接进编排**

在 `src/world/generate.cpp` 的 `finalize_aquifers(world);` 之后追加：

```cpp
    make_spirit_tide(world);
    make_ley_network(world);
```

在 `include/gamev2/world/generate.hpp` 的 `make_spirit_tide` 声明之后追加：

```cpp
/// 阶段 12。读 elevation、sea_level、downstream、flow、stone，
/// 写 ley_channels、ley_nodes 与三张地脉镜像网格。
void make_ley_network(World& world);
```

（Task 1 的 Step 9 已经声明过这七个阶段，因此这里只需确认声明存在；
若 Task 1 已写好，**不要重复声明**。）

- [ ] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

- [ ] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/world/ley.cpp include/gamev2/world/spirit_tables.hpp
git add src/world/generate.cpp CMakeLists.txt tests/test_world_ley.cpp
```

---

### Task 4: 灵脉品阶与灵眼

**Files:**
- Create: `src/world/veins.cpp`
- Modify: `include/gamev2/world/spirit_tables.hpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_veins.cpp`

**Interfaces:**
- Consumes: `World::ley_nodes`、`World::ley_channels`、`World::elevation`、
  `World::spirit_tide`、`vein_targets(std::uint32_t)`、`Stream::vein`
- Produces:
  - `World::veins`（`std::vector<SpiritVein>`）
  - `World::eyes`（`std::vector<SpiritEye>`）
  - `World::vein_rank`（`Grid<VeinRank>`）
  - `World::vein_id`（`Grid<std::uint32_t>`）
  - `World::eye_flow`（`Grid<std::uint32_t>`）
  - `spirit_tables.hpp` 新增 `kVeinThreshold`、`kVeinThresholdStep`、
    `kVeinThresholdFloor`、`kStabilityMined`、`kOverminedChance`、
    `kDepletedRatio`
  - `generate.hpp` 新增 `VeinCount`、`VeinCensus`、`vein_census(const World&)`
  - `veins_complete(const World&)` —— **纯查询**。数量凑不齐不是错误
    （设计文档 §5）：缺口由 `vein_census()` 报出，世界照常生成
- 不变式：`WorldError` 只有 `invalid_params` 一个变体。
  灵气层**不引入新的失败模式**

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_veins.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::LeyNode;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritVein;
using gamev2::world::VeinRank;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::kDepletedRatio;
using gamev2::world::kNoEye;
using gamev2::world::kOverminedChance;
using gamev2::world::kStabilityMined;
using gamev2::world::vein_targets;

WorldParams big_params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    // 灵气层只吃地表与岩层，z_levels 取 1 让这个用例跑得快些。
    p.z_levels = 1;
    p.seed = seed;
    return p;
}

std::uint32_t count_rank(const World& w, VeinRank rank)
{
    std::uint32_t n = 0;
    for (const SpiritVein& v : w.veins)
    {
        if (v.rank == rank)
        {
            ++n;
        }
    }
    return n;
}

std::uint32_t count_depleted(const World& w)
{
    std::uint32_t n = 0;
    for (const SpiritEye& e : w.eyes)
    {
        if (e.depleted)
        {
            ++n;
        }
    }
    return n;
}

/// 这条脉有没有被抽干。枯眼是「灵眼被抽干」的结果，它压低该脉的稳定年限。
bool is_depleted(const World& w, const SpiritVein& v)
{
    return v.eye != kNoEye && w.eyes[v.eye].depleted;
}

TEST(vein_counts_match_the_setting_exactly)
{
    // 设定 2.2：龙脉 1、主脉 9、支脉约 40。这三个数是硬约束，
    // 由排名保证，不靠阈值碰巧产生。
    const WorldParams p = big_params(2026);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    CHECK_EQ(count_rank(w, VeinRank::dragon), 1u);
    CHECK_EQ(count_rank(w, VeinRank::main), 9u);
    CHECK_EQ(count_rank(w, VeinRank::branch), 40u);
}

TEST(vein_target_formula_agrees_with_the_generated_world)
{
    const WorldParams p = big_params(4242);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const auto target = vein_targets(p.width * p.height);
    CHECK_EQ(count_rank(w, VeinRank::dragon), target.dragon);
    CHECK_EQ(count_rank(w, VeinRank::main), target.main);
    CHECK_EQ(count_rank(w, VeinRank::branch), target.branch);
}

TEST(every_vein_is_a_contiguous_run_of_one_channel)
{
    const WorldParams p = big_params(7);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const SpiritVein& v : w.veins)
    {
        CHECK(v.channel < w.ley_channels.size());
        CHECK(v.first_node <= v.last_node);
        const auto& nodes = w.ley_channels[v.channel].nodes;
        CHECK(v.last_node < nodes.size());
        // 不变式 19：这条脉覆盖的节点在所属通道里必须是**相邻的一段**。
        // 段内每一格都标着它，段外紧挨着的格不能标着它。
        for (std::uint32_t j = v.first_node; j <= v.last_node; ++j)
        {
            const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
            CHECK(w.vein_id.at(cell) == v.id);
            CHECK(w.vein_rank.at(cell) == v.rank);
        }
        if (v.first_node > 0)
        {
            const std::uint32_t before =
                w.ley_nodes[nodes[v.first_node - 1]].cell;
            CHECK(w.vein_id.at(before) != v.id);
        }
        if (v.last_node + 1 < nodes.size())
        {
            const std::uint32_t after =
                w.ley_nodes[nodes[v.last_node + 1]].cell;
            CHECK(w.vein_id.at(after) != v.id);
        }
    }
}

TEST(dragon_main_and_branch_veins_each_have_one_eye)
{
    const WorldParams p = big_params(31337);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t eyes = 0;
    for (const SpiritVein& v : w.veins)
    {
        if (v.rank == VeinRank::barren || v.rank == VeinRank::none)
        {
            // 荒脉储量太低，出不了灵眼。
            CHECK(v.eye == kNoEye);
            continue;
        }
        CHECK(v.eye != kNoEye);
        CHECK(v.eye < w.eyes.size());
        ++eyes;
    }
    CHECK_EQ(eyes, w.eyes.size());
}

TEST(barren_veins_exist)
{
    // 荒脉「不可考」——数量不定，但必须存在，否则「残余矿化」无处落地。
    // 它是「曾经有油、被采完了」的残骸，因此一条都不能没有。
    //
    // 覆盖多个种子：切段循环若退回「段数达标即停」（`>=` 而不是 `>`），
    // 恰好切出定额条数时荒脉就是 0 条。单一种子未必撞得上那个数，
    // 所以这里把几个已知会撞上的种子一并钉住。
    for (const std::uint64_t seed : {555ull, 15202ull, 23306ull, 27358ull,
                                     40527ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        CHECK(count_rank(*generated, VeinRank::barren) > 0u);
    }
}

TEST(eye_flow_is_positive_unless_the_eye_is_depleted)
{
    const WorldParams p = big_params(8888);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t depleted = 0;
    for (const SpiritEye& e : w.eyes)
    {
        if (e.depleted)
        {
            // 枯眼：抽干后不可复生，流量必须是零。
            CHECK_EQ(e.flow, 0u);
            CHECK_EQ(w.eye_flow.at(e.cell), 0u);
            ++depleted;
        }
        else
        {
            CHECK(e.flow > 0u);
            CHECK_EQ(w.eye_flow.at(e.cell), e.flow);
        }
    }
    // 枯眼必须真的存在：设定 2.3 专门为它写了一条红线。
    CHECK(depleted > 0u);
}

TEST(depleted_eye_count_follows_the_setting_ratio)
{
    // 不变式 21：枯眼数 == round(灵眼数 × kDepletedRatio)。灵眼数是固定的
    // （有品阶的脉恒为 1+9+40 = 50），所以枯眼数也是固定的。
    const WorldParams p = big_params(4242);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const auto expected = static_cast<std::uint32_t>(std::lround(
        static_cast<double>(w.eyes.size()) * kDepletedRatio));
    CHECK_EQ(count_depleted(w), expected);
    CHECK(expected > 0u);
}

TEST(every_world_has_its_depleted_eyes)
{
    // 设定 2.3 的红线不是「看运气」：枯眼数只由灵眼数决定，所以每个世界
    // 都必须有枯眼。越采与否影响不到这一点——两者是两条独立的因果。
    for (const std::uint64_t seed : {2026ull, 4242ull, 7ull, 31337ull, 555ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        CHECK(count_depleted(*generated) > 0u);
    }
}

TEST(depleted_eyes_crush_the_vein_stability)
{
    // 设定 2.2：被采过的脉稳定年限骤降。现在「被采」有两处，各打一个品阶
    // 因子——越采打储量、枯眼打稳定年限，因此稳定年限骤降的是枯眼那条脉。
    const WorldParams p = big_params(24680);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const SpiritVein& v : w.veins)
    {
        if (is_depleted(w, v))
        {
            CHECK_EQ(v.stability, kStabilityMined);
        }
        else
        {
            CHECK(v.stability > kStabilityMined);
        }
    }
    // 反方向同样要成立：稳定年限塌到 kStabilityMined 的，一定是枯眼那条脉。
    // 枯眼一个都没有时，就不该有任何脉的稳定年限塌到那个值。
    for (const SpiritVein& v : w.veins)
    {
        if (v.stability == kStabilityMined)
        {
            CHECK(is_depleted(w, v));
        }
    }
}

TEST(some_veins_are_overmined)
{
    // 越采判据是均匀抽样，命中率应当接近 kOverminedChance。抽样源若换成
    // 插值噪声，命中率会塌到 3% 左右——这条用例就是把那种退化钉住。
    // 这里是聚合统计：四个世界两百来个样本上的 15% 期望值，区间取
    // ±3.3σ 以外才算异常。
    std::uint32_t total = 0;
    std::uint32_t overmined = 0;
    for (const std::uint64_t seed : {2026ull, 4242ull, 7ull, 31337ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        const World& w = *generated;
        for (const SpiritVein& v : w.veins)
        {
            ++total;
            if (v.overmined)
            {
                ++overmined;
            }
        }
    }
    CHECK(total > 0u);
    const double rate = static_cast<double>(overmined)
                        / static_cast<double>(total);
    CHECK(rate > 0.07);
    CHECK(rate < 0.24);
    // 名义概率本身也要钉住：改小它，这条用例就该红。
    CHECK(kOverminedChance > 0.0);
}

TEST(vein_grids_agree_with_the_vein_list)
{
    const WorldParams p = big_params(99);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (std::uint32_t i = 0; i < w.vein_rank.size(); ++i)
    {
        const auto rank = w.vein_rank.at(i);
        if (rank == VeinRank::none)
        {
            CHECK(w.vein_id.at(i) == gamev2::world::kNoVein);
            continue;
        }
        CHECK(w.vein_id.at(i) < w.veins.size());
        CHECK(w.veins[w.vein_id.at(i)].rank == rank);
    }
    // 反方向：每条脉覆盖的格都标着它，而且格数不多不少。
    std::uint32_t grid_marked = 0;
    for (std::uint32_t i = 0; i < w.vein_id.size(); ++i)
    {
        if (w.vein_id.at(i) != gamev2::world::kNoVein)
        {
            ++grid_marked;
        }
    }
    std::uint32_t vein_marked = 0;
    for (const SpiritVein& v : w.veins)
    {
        const auto& nodes = w.ley_channels[v.channel].nodes;
        for (std::uint32_t j = v.first_node; j <= v.last_node; ++j)
        {
            const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
            CHECK(w.vein_id.at(cell) == v.id);
            CHECK(w.vein_rank.at(cell) == v.rank);
        }
        vein_marked += v.last_node - v.first_node + 1;
    }
    CHECK_EQ(grid_marked, vein_marked);
}

TEST(vein_layer_is_deterministic)
{
    const WorldParams p = big_params(2718);
    const auto a = gamev2::world::generate_world(p);
    const auto b = gamev2::world::generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    CHECK_EQ(a->veins.size(), b->veins.size());
    for (std::size_t i = 0; i < a->veins.size(); ++i)
    {
        CHECK_EQ(a->veins[i].rank, b->veins[i].rank);
        CHECK_EQ(a->veins[i].reserve, b->veins[i].reserve);
        CHECK_EQ(a->veins[i].stability, b->veins[i].stability);
        CHECK_EQ(a->veins[i].overmined, b->veins[i].overmined);
        // 位置与身份也要比对：灵眼落在哪、属于哪条脉、覆盖哪一段，
        // 都不能在两次生成之间漂移。
        CHECK_EQ(a->veins[i].channel, b->veins[i].channel);
        CHECK_EQ(a->veins[i].first_node, b->veins[i].first_node);
        CHECK_EQ(a->veins[i].last_node, b->veins[i].last_node);
        CHECK_EQ(a->veins[i].element, b->veins[i].element);
        CHECK_EQ(a->veins[i].eye, b->veins[i].eye);
    }
    CHECK_EQ(a->eyes.size(), b->eyes.size());
    for (std::size_t i = 0; i < a->eyes.size(); ++i)
    {
        CHECK_EQ(a->eyes[i].vein, b->eyes[i].vein);
        CHECK_EQ(a->eyes[i].node, b->eyes[i].node);
        CHECK_EQ(a->eyes[i].cell, b->eyes[i].cell);
        CHECK_EQ(a->eyes[i].flow, b->eyes[i].flow);
        CHECK_EQ(a->eyes[i].depleted, b->eyes[i].depleted);
    }
    for (std::uint32_t i = 0; i < a->eye_flow.size(); ++i)
    {
        CHECK_EQ(a->eye_flow.at(i), b->eye_flow.at(i));
    }
}

TEST(an_incomplete_vein_layer_is_reported_as_incomplete)
{
    // 数量不足必须**看得见**：vein_census 要报出缺口，veins_complete 要
    // 据此说「不够」。设计文档 §5 说灵气层不引入新的失败模式，所以这
    // 是诊断信号而不是生成失败——世界照常产出。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 1;
    World w(p);
    // 什么都没有：三条定额一条都不满足，缺口等于应得数。
    const gamev2::world::VeinCensus empty = gamev2::world::vein_census(w);
    CHECK_EQ(empty.dragon.got, 0u);
    CHECK_EQ(empty.dragon.expected, 1u);
    CHECK_EQ(empty.dragon.shortfall(), 1u);
    CHECK_EQ(empty.main.shortfall(), 9u);
    CHECK_EQ(empty.branch.shortfall(), 40u);
    CHECK_EQ(empty.total_shortfall(), 50u);
    CHECK(!gamev2::world::veins_complete(w));

    // 有地脉却切不出灵脉：同样按定额报缺口。
    w.ley_nodes.push_back(LeyNode{});
    const gamev2::world::VeinCensus short1 = gamev2::world::vein_census(w);
    CHECK_EQ(short1.total_shortfall(), 50u);
    CHECK(!gamev2::world::veins_complete(w));
}

TEST(a_complete_vein_layer_reports_no_shortfall)
{
    // 参考图幅上定额必须凑齐：缺口为零，且实得数与 vein_targets 一致。
    const WorldParams p = big_params(79203);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const gamev2::world::VeinCensus census = gamev2::world::vein_census(w);
    CHECK_EQ(census.dragon.got, 1u);
    CHECK_EQ(census.main.got, 9u);
    CHECK_EQ(census.branch.got, 40u);
    CHECK_EQ(census.total_shortfall(), 0u);
    CHECK(gamev2::world::veins_complete(w));
    // 荒脉与枯眼也要在诊断量里看得见：小图幅上应得数可能不到一条，
    // 那时「应得 0、实得 0」比沉默更清楚。
    CHECK(census.barren > 0u);
    CHECK_EQ(census.eyes, 50u);
    CHECK_EQ(census.depleted.expected, 8u);
    CHECK_EQ(census.depleted.got, 8u);
    CHECK_EQ(census.depleted.shortfall(), 0u);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 里追加：

```cmake
    tests/test_world_veins.cpp
```

```powershell
cmake --build build
```

Expected: 编译失败，报 `gamev2::world::VeinRank` 相关断言用到的
`w.veins` 不存在——实际上 `w.veins` 在 Task 1 就建好了，因此本步的失败
表现为 `vein_counts_match_the_setting_exactly` 失败（`veins` 为空）。

- [ ] **Step 3: 把切段与稳定年限的常量加进 `spirit_tables.hpp`**

在 `include/gamev2/world/spirit_tables.hpp` 追加：

```cpp
/// 灵脉切段阈值（字节刻度）。得分超过它的连续节点算一段。
/// 下调它会扩大合格集，但也会把原本分开的极大区间并起来，段数因此
/// 未必单调增加——切段时必须留段数最多的那组，不能只看最后一次。
inline constexpr std::uint8_t kVeinThreshold = 64;
inline constexpr std::uint8_t kVeinThresholdStep = 4;
inline constexpr std::uint8_t kVeinThresholdFloor = 4;

/// 枯眼那条脉的稳定年限。设定 2.2 的品阶标准是「储量 × 稳定年限」，
/// 被抽干的脉稳定年限骤降。
///
/// 它是**枯眼**的后果，不是越采的后果：两处「被采」各打一个品阶因子——
/// 越采削储量，枯眼压稳定年限。设定 2.3 说抽干后只剩枯眼、不可复生。
inline constexpr std::uint32_t kStabilityMined = 30;

/// 候选段被越采的概率。设定没给数，这是经验值。
inline constexpr double kOverminedChance = 0.15;

/// 枯眼占灵眼数的比例。设定 2.3 说「抽干后只剩枯眼，不可复生」，
/// 但当代有多少枯眼设定未规定，这是经验值。枯眼数 = round(灵眼数 × 它)，
/// 而灵眼数是固定的（有品阶的脉恒为 1+9+40 条），所以枯眼数也是固定的。
inline constexpr double kDepletedRatio = 0.15;
```

- [ ] **Step 4: 实现灵脉切分与灵眼**

创建 `src/world/veins.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

/// 浅埋权重。分越浅，得分越高。
constexpr std::uint32_t kShallownessScale = 1000;

/// 汇流加成。每多一条通道汇入，浓度跳升一档。
constexpr std::uint32_t kConfluenceBonus = 2;

/// 稳定年限的基数与每节点增量。浓度持续得越久，这条脉越稳定。
constexpr std::uint32_t kStabilityBase = 100;
constexpr std::uint32_t kStabilityPerNode = 50;

/// 被越采后储量的削减比例（百分比）。
constexpr std::uint32_t kOverminePenalty = 60;

/// 一段候选灵脉：某条通道上的一段连续节点。
struct Segment
{
    std::uint32_t channel = 0;
    std::uint32_t first = 0;   ///< 段首在 channel.nodes 中的下标
    std::uint32_t last = 0;    ///< 段尾，闭区间
    std::uint32_t reserve = 0;
    std::uint32_t stability = 0;
    std::uint32_t peak_node = 0;  ///< 段内得分最高的节点，即灵眼位置
    bool overmined = false;
};

/// 浓度得分。三个因子相乘：任何一项为零则该格不成脉，
/// 符合「灵脉是地脉抬升段」的定义。
std::uint64_t raw_score(const LeyNode& node)
{
    const std::uint32_t shallowness =
        kShallownessScale - node.depth * kShallownessScale / kMaxLeyDepth;
    const std::uint32_t confluence =
        1u + static_cast<std::uint32_t>(kConfluenceBonus) * node.merges;
    return static_cast<std::uint64_t>(node.flow) * shallowness * confluence;
}

/// 按阈值把每条通道切成若干段。
std::vector<Segment> segment_channels(const World& world,
                                      const std::vector<std::uint32_t>& scores,
                                      std::uint8_t threshold)
{
    std::vector<Segment> segments;
    for (std::uint32_t c = 0; c < world.ley_channels.size(); ++c)
    {
        const std::vector<std::uint32_t>& nodes = world.ley_channels[c].nodes;
        std::uint32_t start = 0;
        bool inside = false;
        for (std::uint32_t k = 0; k < nodes.size(); ++k)
        {
            const bool high = scores[nodes[k]] >= threshold;
            if (high && !inside)
            {
                start = k;
                inside = true;
            }
            else if (!high && inside)
            {
                Segment s;
                s.channel = c;
                s.first = start;
                s.last = k - 1;
                s.peak_node = nodes[start];
                for (std::uint32_t j = start; j <= s.last; ++j)
                {
                    s.reserve += scores[nodes[j]];
                    if (scores[nodes[j]] > scores[s.peak_node])
                    {
                        s.peak_node = nodes[j];
                    }
                }
                s.stability = kStabilityBase
                              + kStabilityPerNode * (s.last - s.first + 1);
                segments.push_back(s);
                inside = false;
            }
        }
        if (inside)
        {
            Segment s;
            s.channel = c;
            s.first = start;
            s.last = static_cast<std::uint32_t>(nodes.size()) - 1;
            s.peak_node = nodes[start];
            for (std::uint32_t j = start; j <= s.last; ++j)
            {
                s.reserve += scores[nodes[j]];
                if (scores[nodes[j]] > scores[s.peak_node])
                {
                    s.peak_node = nodes[j];
                }
            }
            s.stability = kStabilityBase
                          + kStabilityPerNode * (s.last - s.first + 1);
            segments.push_back(s);
        }
    }
    return segments;
}

/// 段的身份种子。段的身份是「通道号 + 段首下标」：与名次无关，也与段
/// 被发现的顺序无关，因此同一个段在任何世界里都得到同一个种子。
std::uint64_t segment_seed(std::uint64_t vein_seed, const Segment& s)
{
    const std::uint64_t by_channel = stage_seed(vein_seed, s.channel);
    return stage_seed(by_channel, s.first);
}

/// 标记被越采的段，并相应压低储量。
///
/// 判据只看段的身份（通道号 + 段首下标）而**不看名次**：名次要靠储量与
/// 稳定年限算出来，若「谁被采」又依赖名次，就成了一圈套。
///
/// 抽样走 Pcg32 的均匀源，不走 value_noise：value_noise 是四个格点哈希
/// 的插值，样本挤在 0.5 附近，拿它跟 0.15 比实际只命中约 3%。
///
/// 越采只削储量，不动稳定年限：设计文档步骤 5.1 只说削减储量。两处「被采」
/// 各打一个品阶因子——越采打储量，枯眼才打稳定年限。
void mark_overmined(std::vector<Segment>& segments, std::uint64_t vein_seed)
{
    for (Segment& s : segments)
    {
        Pcg32 rng(segment_seed(vein_seed, s));
        if (rng.unit() >= kOverminedChance)
        {
            continue;
        }
        s.overmined = true;
        s.reserve = s.reserve * (100u - kOverminePenalty) / 100u;
    }
}

/// 灵眼的身份键。灵眼的身份是它的出露点节点：与名次无关，也与灵眼在
/// 列表里的位置无关，因此同一个出露点在任何世界里都得到同一个键。
double eye_key(std::uint64_t vein_seed, std::uint32_t node)
{
    Pcg32 rng(stage_seed(vein_seed, node));
    return rng.unit();
}

/// 挑出枯眼。枯眼数 = round(灵眼数 × kDepletedRatio)。
///
/// 判据是灵眼的**身份**（出露点所在的节点），不是名次：灵眼集合本身就是
/// 排名的产物，按名次挑等于「排在前面的更容易被抽干」。给每个灵眼一个
/// 身份键、再取键最小的若干个，等价于从全部灵眼里均匀抽一个子集。
///
/// 枯眼必须排在排名之后：它会让该脉的稳定年限骤降，若在排名前定，这些
/// 脉会被挤出名次之外，而没有品阶就没有灵眼，枯眼反而无从产生。
void mark_depleted_eyes(World& world, std::uint64_t vein_seed)
{
    if (world.eyes.empty())
    {
        return;
    }
    const auto wanted = static_cast<std::size_t>(std::lround(
        static_cast<double>(world.eyes.size()) * kDepletedRatio));
    if (wanted == 0)
    {
        return;
    }

    std::vector<std::pair<double, std::uint32_t>> keyed;
    keyed.reserve(world.eyes.size());
    for (std::uint32_t i = 0; i < world.eyes.size(); ++i)
    {
        keyed.emplace_back(eye_key(vein_seed, world.eyes[i].node), i);
    }
    // 键相等时按下标定序，保证同一批键总是挑出同一批眼。
    std::sort(keyed.begin(), keyed.end());

    const std::size_t take = std::min(wanted, keyed.size());
    for (std::size_t k = 0; k < take; ++k)
    {
        SpiritEye& eye = world.eyes[keyed[k].second];
        eye.depleted = true;
        eye.flow = 0;
        // 稳定年限降下来是「本轮看到的状态」，不回灌本轮的排名。
        world.veins[eye.vein].stability = kStabilityMined;
    }
}

}  // namespace

void make_spirit_veins(World& world)
{
    const WorldParams& p = world.params();
    world.veins.clear();
    world.eyes.clear();
    for (std::uint32_t i = 0; i < world.vein_rank.size(); ++i)
    {
        world.vein_rank.at(i) = VeinRank::none;
        world.vein_id.at(i) = kNoVein;
        world.eye_flow.at(i) = 0u;
    }
    if (world.ley_nodes.empty())
    {
        return;
    }

    // 得分归一化到 0..255。归一化让切段阈值不随图幅规模漂移。
    std::vector<std::uint64_t> raws(world.ley_nodes.size(), 0);
    std::uint64_t max_raw = 0;
    for (std::uint32_t i = 0; i < world.ley_nodes.size(); ++i)
    {
        raws[i] = raw_score(world.ley_nodes[i]);
        max_raw = std::max(max_raw, raws[i]);
    }
    std::vector<std::uint32_t> scores(world.ley_nodes.size(), 0);
    if (max_raw > 0)
    {
        for (std::uint32_t i = 0; i < world.ley_nodes.size(); ++i)
        {
            scores[i] = static_cast<std::uint32_t>(raws[i] * 255u / max_raw);
        }
    }

    const auto target = vein_targets(p.width * p.height);
    const std::uint32_t wanted =
        target.dragon + target.main + target.branch;

    // 切段。阈值下调会扩大合格集，但也会把原本分开的极大区间并起来，
    // 段数因此未必单调增加——所以每次切段都和「目前最好的那组」比较，
    // 留下段数最多的那组，段数相同时保留先到的那组。
    //
    // 目标是「比定额多一条」：多出来的那条进荒脉桶，否则荒脉可能一条都
    // 没有，设定里的「残余矿化」就无处落地。
    std::uint8_t threshold = kVeinThreshold;
    std::vector<Segment> segments;
    for (;;)
    {
        std::vector<Segment> cut = segment_channels(world, scores, threshold);
        if (cut.size() > segments.size())
        {
            segments = std::move(cut);
        }
        if (segments.size() > wanted)
        {
            break;
        }
        if (threshold <= kVeinThresholdFloor)
        {
            break;
        }
        const auto next = static_cast<int>(threshold) - kVeinThresholdStep;
        threshold = static_cast<std::uint8_t>(
            std::max<int>(next, kVeinThresholdFloor));
    }

    const std::uint64_t vein_seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::vein));
    mark_overmined(segments, vein_seed);

    // 排名键：储量 × 稳定年限。设定 2.2 的品阶标准。
    std::vector<std::uint32_t> order(segments.size());
    for (std::uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&segments](std::uint32_t a, std::uint32_t b) {
                  const std::uint64_t ka =
                      static_cast<std::uint64_t>(segments[a].reserve)
                      * segments[a].stability;
                  const std::uint64_t kb =
                      static_cast<std::uint64_t>(segments[b].reserve)
                      * segments[b].stability;
                  if (ka != kb)
                  {
                      return ka > kb;
                  }
                  return a < b;
              });

    for (std::uint32_t rank = 0; rank < order.size(); ++rank)
    {
        const Segment& s = segments[order[rank]];
        SpiritVein vein;
        vein.id = rank;
        vein.channel = s.channel;
        vein.first_node = s.first;
        vein.last_node = s.last;
        vein.reserve = s.reserve;
        vein.stability = s.stability;
        vein.overmined = s.overmined;
        vein.element = world.ley_nodes[s.peak_node].element;

        if (rank == 0)
        {
            vein.rank = VeinRank::dragon;
        }
        else if (rank < target.dragon + target.main)
        {
            vein.rank = VeinRank::main;
        }
        else if (rank < target.dragon + target.main + target.branch)
        {
            vein.rank = VeinRank::branch;
        }
        else
        {
            vein.rank = VeinRank::barren;
        }

        // 灵眼只出露在龙、主、支三条品阶上；荒脉储量太低，采不出东西。
        if (vein.rank != VeinRank::barren)
        {
            SpiritEye eye;
            eye.vein = vein.id;
            eye.node = s.peak_node;
            eye.cell = world.ley_nodes[s.peak_node].cell;
            // 流量是这条脉的物理吞吐，不随潮位变。潮位缩放的是环境浓度场
            // （阶段 14），两处都乘就等于把灵潮算了两遍。
            eye.flow = world.ley_nodes[s.peak_node].flow;
            vein.eye = static_cast<std::uint32_t>(world.eyes.size());
            world.eyes.push_back(eye);
        }

        // 镜像是格级的：段覆盖的每一格都标上品阶与编号。
        const std::vector<std::uint32_t>& nodes =
            world.ley_channels[s.channel].nodes;
        for (std::uint32_t j = s.first; j <= s.last; ++j)
        {
            const std::uint32_t cell = world.ley_nodes[nodes[j]].cell;
            world.vein_rank.at(cell) = vein.rank;
            world.vein_id.at(cell) = vein.id;
        }

        world.veins.push_back(vein);
    }

    // 枯眼在灵眼集合确定之后挑：它压低稳定年限，而稳定年限是排名键。
    mark_depleted_eyes(world, vein_seed);

    // 灵眼流量也写进格级网格，方便按坐标查询。枯眼的流量已经是零。
    for (const SpiritEye& eye : world.eyes)
    {
        world.eye_flow.at(eye.cell) = eye.flow;
    }
}

VeinCensus vein_census(const World& world)
{
    const WorldParams& p = world.params();
    const auto target = vein_targets(p.width * p.height);

    VeinCensus census;
    census.dragon.expected = target.dragon;
    census.main.expected = target.main;
    census.branch.expected = target.branch;
    for (const SpiritVein& v : world.veins)
    {
        switch (v.rank)
        {
        case VeinRank::dragon:
            ++census.dragon.got;
            break;
        case VeinRank::main:
            ++census.main.got;
            break;
        case VeinRank::branch:
            ++census.branch.got;
            break;
        case VeinRank::barren:
            ++census.barren;
            break;
        case VeinRank::none:
            break;
        }
    }

    census.eyes = static_cast<std::uint32_t>(world.eyes.size());
    for (const SpiritEye& e : world.eyes)
    {
        if (e.depleted)
        {
            ++census.depleted.got;
        }
    }
    // 应得枯眼数由灵眼数定：灵眼数是固定的（有品阶的脉恒为 1+9+40 条），
    // 枯眼数因此也是固定的。小图幅上灵眼少，应得枯眼数可能不到一条——
    // 那时「应得 0、实得 0」比沉默更清楚。
    census.depleted.expected = static_cast<std::uint32_t>(std::lround(
        static_cast<double>(census.eyes) * kDepletedRatio));
    return census;
}

bool veins_complete(const World& world)
{
    // 无地脉的世界（极端参数下源点选不出）灵脉数自然为 0，缺口等于应得
    // 数——这是设计文档 §5 说的「退化」，世界仍然有效，缺口由诊断量报出，
    // 不是生成失败。
    return vein_census(world).total_shortfall() == 0u;
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 把阶段 13 接进编排**

在 `src/world/generate.cpp` 的 `make_ley_network(world);` 之后追加：

```cpp
    make_spirit_veins(world);
```

- [ ] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

**若 `vein_counts_match_the_setting_exactly` 红了**，先看 `kVeinThreshold`
是否还能再降：`kVeinThresholdFloor = 4` 是下限，若降到下限仍不足，说明
通道数或段数太少，需要调 `kLeySourcePerCell`（源点密度）或放宽
`kShallownessScale`。**不要把断言改成「至少 1 条」来绕过**——数量是设定
给的硬约束。

- [ ] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/world/veins.cpp include/gamev2/world/spirit_tables.hpp
git add src/world/generate.cpp CMakeLists.txt tests/test_world_veins.cpp
```

---

### Task 5: 阶段 14：灵域浓度场与分等

**Files:**
- Create: `src/world/domains.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_domains.cpp`

**Interfaces:**
- Consumes:
  - `World::eyes`（`std::vector<SpiritEye>`）——读 `cell`、`flow`、`depleted`
  - `World::spirit_tide`（`SpiritTide`）——读 `level`
  - `World::params()`——读 `width`、`height`、`seed`
  - `spirit_tables.hpp`：`kReferenceFlow`、`kDomainRadius`、`kDomainMaxRadius`、
    `domain_grade_of(std::uint8_t) -> DomainGrade`
- Produces:
  - `void gamev2::world::make_spirit_domains(World& world)`
  - `World::domain_concentration`（`Grid<std::uint8_t>`，0..255）——阶段 14 的
    唯一写入者
  - `World::domain_grade`（`Grid<DomainGrade>`）——阶段 14 的唯一写入者
  - 后续任务（15/16/17）只读这两个网格，不得回写
  - 不变式：`domain_grade.at(i) == domain_grade_of(domain_concentration.at(i))`
    对每一格成立
  - 不变式：`World::eye_flow` 是阶段 13 的唯一写入者，本阶段只读不写

**本任务依赖上游任务已满足的契约**（不满足则本任务的实现要改）：

- 阶段 12 的 `LeyNode::flow` 不含潮位——潮位只改读法，不改网络
- 阶段 13 的眼流量按 `kReferenceFlow = 2000` 的量级标定：眼心浓度是
  `flow * 潮位 / kReferenceFlow`，最强的一批眼在灵潮期必须越过薄域门槛
- 阶段 13 必须为**每一个**灵眼（含枯眼）留下 `cell` 与 `flow`

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_domains.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::SpiritEye;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::domain_grade_of;
using gamev2::world::kReferenceFlow;
using gamev2::world::kThinThreshold;
using gamev2::world::make_spirit_domains;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    // 阶段 14 只读地表量与灵眼，z_levels 取 1 让这个用例跑得快些。
    p.z_levels = 1;
    return p;
}

/// 阶段 14 的前置状态。
///
/// 地脉网络与灵脉灵眼是阶段 12/13 的产出，阶段 14 只读它们。这里显式按
/// 依赖顺序调用各阶段，而不是用 generate_world()——那条线要到编排任务
/// 才接上。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    make_spirit_domains(w);
    return w;
}

std::uint64_t total_concentration(const World& w)
{
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        sum += w.domain_concentration.at(i);
    }
    return sum;
}

std::uint32_t count_grade(const World& w, DomainGrade grade)
{
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
    {
        if (w.domain_grade.at(i) == grade)
        {
            ++n;
        }
    }
    return n;
}

/// 福地格的四邻接连通块数。标定回归要数它：福地块数对应「几处福地」，
/// 面积只说得出「多大」。
std::uint32_t blessed_components(const World& w)
{
    const auto width = static_cast<std::size_t>(w.domain_concentration.width());
    const auto height = static_cast<std::size_t>(w.domain_concentration.height());
    std::vector<bool> seen(w.domain_concentration.size(), false);
    std::uint32_t components = 0;
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            const std::size_t start = y * width + x;
            if (seen[start]
                || w.domain_grade.at(static_cast<std::uint32_t>(start))
                       != DomainGrade::blessed)
            {
                continue;
            }
            ++components;
            std::deque<std::size_t> queue{start};
            seen[start] = true;
            while (!queue.empty())
            {
                const std::size_t cell = queue.front();
                queue.pop_front();
                const std::size_t cx = cell % width;
                const std::size_t cy = cell / width;
                const std::size_t neighbours[4] = {
                    cy > 0 ? cell - width : cell,
                    cy + 1 < height ? cell + width : cell,
                    cx > 0 ? cell - 1 : cell,
                    cx + 1 < width ? cell + 1 : cell};
                for (const std::size_t n : neighbours)
                {
                    if (n == cell || seen[n])
                    {
                        continue;
                    }
                    if (w.domain_grade.at(static_cast<std::uint32_t>(n))
                        != DomainGrade::blessed)
                    {
                        continue;
                    }
                    seen[n] = true;
                    queue.push_back(n);
                }
            }
        }
    }
    return components;
}

/// 浓度场的分布快照。百分比是面积占比，连通块数是绝对值。
struct Calibration
{
    double barren = 0.0;
    double thin = 0.0;
    double medium = 0.0;
    double thick = 0.0;
    double blessed = 0.0;
    double saturated = 0.0;
    double components = 0.0;

    void add(const Calibration& other)
    {
        barren += other.barren;
        thin += other.thin;
        medium += other.medium;
        thick += other.thick;
        blessed += other.blessed;
        saturated += other.saturated;
        components += other.components;
    }

    void average(std::uint32_t count)
    {
        const auto k = 1.0 / static_cast<double>(count);
        barren *= k;
        thin *= k;
        medium *= k;
        thick *= k;
        blessed *= k;
        saturated *= k;
        components *= k;
    }
};

Calibration measure(const World& w)
{
    const double total = static_cast<double>(w.domain_concentration.size());
    Calibration c;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        const std::uint8_t v = w.domain_concentration.at(i);
        if (v == 255u)
        {
            c.saturated += 1.0;
        }
        switch (w.domain_grade.at(i))
        {
        case DomainGrade::barren: c.barren += 1.0; break;
        case DomainGrade::thin: c.thin += 1.0; break;
        case DomainGrade::medium: c.medium += 1.0; break;
        case DomainGrade::thick: c.thick += 1.0; break;
        case DomainGrade::blessed: c.blessed += 1.0; break;
        }
    }
    c.barren = c.barren * 100.0 / total;
    c.thin = c.thin * 100.0 / total;
    c.medium = c.medium * 100.0 / total;
    c.thick = c.thick * 100.0 / total;
    c.blessed = c.blessed * 100.0 / total;
    c.saturated = c.saturated * 100.0 / total;
    c.components = static_cast<double>(blessed_components(w));
    return c;
}

TEST(domain_concentration_is_the_eye_decay_field)
{
    // 合成一只眼：流量等于参考流量，于是眼心的贡献正好是满格浓度。
    // 灵历 300 是元潮的第三百年，潮位 255，眼心浓度就应当正好是潮位。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 300;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK_EQ(w.spirit_tide.level, 255u);

    SpiritEye eye;
    eye.cell = w.elevation.index_of(64, 64);
    eye.flow = kReferenceFlow;
    eye.depleted = false;
    w.eyes.push_back(eye);

    make_spirit_domains(w);

    CHECK_EQ(w.domain_concentration.at(eye.cell), w.spirit_tide.level);
    CHECK(w.domain_grade.at(eye.cell) == DomainGrade::blessed);

    // 指数衰减：离眼越远，浓度越低。
    const std::uint32_t one = w.elevation.index_of(65, 64);
    const std::uint32_t ten = w.elevation.index_of(74, 64);
    CHECK(w.domain_concentration.at(one)
          < w.domain_concentration.at(eye.cell));
    CHECK(w.domain_concentration.at(ten)
          < w.domain_concentration.at(one));

    // 盖章半径之外的格没有贡献。
    CHECK_EQ(w.domain_concentration.at(w.elevation.index_of(0, 0)), 0u);
}

TEST(domain_grade_always_agrees_with_the_concentration)
{
    const World w = build_world(7, 9300);
    bool agrees = true;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        agrees = agrees
                 && w.domain_grade.at(i)
                    == domain_grade_of(w.domain_concentration.at(i));
    }
    CHECK(agrees);
}

TEST(domain_grade_is_monotone_in_concentration)
{
    // 纯函数层面：浓度更高处，分等不低于更低处。
    DomainGrade prev = DomainGrade::barren;
    for (std::uint32_t c = 0; c <= 255u; ++c)
    {
        const auto g = domain_grade_of(static_cast<std::uint8_t>(c));
        CHECK(static_cast<std::size_t>(g) >= static_cast<std::size_t>(prev));
        prev = g;
    }
}

TEST(every_live_eye_sits_in_a_concentrated_cell)
{
    // 潮位 ≥ 71 时，每一个活眼都落在薄域以上——不取「最强的一批」。
    //
    // 谷底期（潮位 33）这条不成立：弱眼自己的贡献只有 flow × 33 / 2000，
    // flow ≤ 400 时是 5~7，够不到薄域门槛 8。见
    // a_weak_eye_falls_below_the_thin_threshold_in_the_trough。
    // 潮位 99 时最弱的活眼（流量 200）自己就有 9，于是这条是标定给的，
    // 不是碰巧。
    const World w = build_world(7, 9300);
    CHECK(w.spirit_tide.level >= 71u);

    std::uint32_t live = 0;
    bool floor_holds = true;
    bool all_in_thin = true;
    std::uint32_t weakest_flow = 0xFFFFFFFFu;
    for (const SpiritEye& eye : w.eyes)
    {
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        ++live;
        const std::uint8_t c = w.domain_concentration.at(eye.cell);
        // 浓度下限由公式给出：眼心至少是 flow * 潮位 / 参考流量。
        const std::uint32_t floor_c =
            static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
            / kReferenceFlow;
        floor_holds = floor_holds && c >= floor_c;
        all_in_thin = all_in_thin && c > 0u && c >= kThinThreshold;
        weakest_flow = std::min(weakest_flow, eye.flow);
    }
    CHECK(live > 0u);
    CHECK(floor_holds);
    CHECK(all_in_thin);

    // 上游契约检查（阶段 13 的标定，寄居在这里）：潮位 99 时一只眼要
    // 自己越过薄域门槛需要 flow ≥ 162，最弱的活眼也得够这个数，否则
    // 上面那条就得靠邻居凑。
    const std::uint32_t needed =
        (static_cast<std::uint32_t>(kThinThreshold) * kReferenceFlow
         + w.spirit_tide.level - 1u)
        / w.spirit_tide.level;
    CHECK(weakest_flow >= needed);
}

TEST(every_live_eye_crosses_the_thin_threshold_at_the_tide_peak)
{
    // 设计文档 §6 第 22 条：每个非枯眼灵眼格的浓度 > 0 且 ≥ 薄域阈值，
    // 潮位 ≥ 71 时成立。
    //
    // 谷底期的潮位只有灵潮期的三分之一，最弱的一只眼自己的贡献只有
    // flow × 33 / 2000，flow ≤ 400 时只有 5~7，够不到薄域门槛 8。
    // 所以这条不变式在潮位 ≥ 71 时成立，在谷底期不成立——这不是实现的
    // 缺陷，是「灵脉半死」的读法。设计 §6 第 22 条已按此改写。
    const World w = build_world(7, 9300);
    CHECK_EQ(w.spirit_tide.level, 99u);

    std::uint32_t live = 0;
    bool all_above = true;
    bool field_follows_flow = true;
    std::uint32_t weakest_flow = 0xFFFFFFFFu;
    for (const SpiritEye& eye : w.eyes)
    {
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        ++live;
        const std::uint8_t c = w.domain_concentration.at(eye.cell);
        all_above = all_above && c > 0u && c >= kThinThreshold;
        // 眼心浓度至少是这只眼自己的贡献 flow × 潮位 / 参考流量。
        // 光写「≥ 薄域门槛」的话，任意一份够大的浓度场都能蒙混过关；
        // 这条把浓度场钉回「它是灵眼的函数」这条链路上。
        const std::uint32_t own =
            static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
            / kReferenceFlow;
        field_follows_flow = field_follows_flow && c >= own;
        weakest_flow = std::min(weakest_flow, eye.flow);
    }
    CHECK(live > 0u);
    CHECK(all_above);
    CHECK(field_follows_flow);

    // 上面的不变式不是靠邻居凑出来的：最弱的一只活眼，它自己的贡献
    // flow × 潮位 / 参考流量 就足以越过薄域门槛。潮位 99 时这需要
    // flow ≥ 162，实测最弱的一只有 200，于是这条下限本身就站得住。
    const std::uint64_t own =
        static_cast<std::uint64_t>(weakest_flow) * w.spirit_tide.level
        / kReferenceFlow;
    CHECK(own >= kThinThreshold);
}

TEST(every_live_eye_stays_positive_at_every_tide)
{
    // 潮位只改读法，不改地脉网络，于是两条逐眼不变量在所有潮位都成立：
    // 眼心浓度 > 0，且 ≥ 这只眼自己的那份贡献 flow × 潮位 / 参考流量。
    //
    // 「≥ 薄域门槛」是潮位 ≥ 71 才有的额外性质，谷底期弱眼够不到
    // （见 a_weak_eye_falls_below_the_thin_threshold_in_the_trough）。
    for (const std::uint32_t year : {9300u, 8914u})
    {
        const World w = build_world(7, year);

        std::uint32_t live = 0;
        bool all_positive = true;
        bool field_follows_flow = true;
        for (const SpiritEye& eye : w.eyes)
        {
            if (eye.depleted || eye.flow == 0)
            {
                continue;
            }
            ++live;
            const std::uint8_t c = w.domain_concentration.at(eye.cell);
            all_positive = all_positive && c > 0u;
            const std::uint32_t own =
                static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
                / kReferenceFlow;
            field_follows_flow = field_follows_flow && c >= own;
        }
        CHECK(live > 0u);
        CHECK(all_positive);
        CHECK(field_follows_flow);
    }
}

TEST(a_weak_eye_falls_below_the_thin_threshold_in_the_trough)
{
    // 谷底期（潮位 33）一只孤立的弱眼：眼心浓度 = 300 × 33 / 2000
    // = 4.95 → 5，够不到薄域门槛 8。
    //
    // 把「谷底期弱眼够不到薄域门槛」写成规格，而不是让它靠运气成立：
    // 真实世界里确实有活眼跌破门槛（实测 256² 每世界至多 2 只），
    // 但那是眼与眼之间够不着，不是实现漏算了这只眼。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 8914;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK_EQ(w.spirit_tide.level, 33u);

    SpiritEye eye;
    eye.cell = w.elevation.index_of(64, 64);
    eye.flow = 300;
    eye.depleted = false;
    w.eyes.push_back(eye);

    make_spirit_domains(w);

    const std::uint8_t c = w.domain_concentration.at(eye.cell);
    CHECK(c > 0u);
    CHECK(c < kThinThreshold);
    CHECK(c >= 4u);
    CHECK(c <= 7u);
}

TEST(a_world_without_eyes_is_all_barren)
{
    // 灵脉数量凑不齐不是生成失败（设计文档 §5），极端参数下可能一条灵脉
    // 都没有。阶段 14 对这种世界必须有定义：浓度全 0，分等全废域。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 9300;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK(w.eyes.empty());
    make_spirit_domains(w);

    bool all_zero = true;
    bool all_barren = true;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        all_zero = all_zero && w.domain_concentration.at(i) == 0u;
        all_barren = all_barren
                     && w.domain_grade.at(i) == DomainGrade::barren;
    }
    CHECK(all_zero);
    CHECK(all_barren);
}

TEST(the_tide_level_gives_one_network_two_readings)
{
    const World trough = build_world(11, 8914);
    const World peak = build_world(11, 9300);
    CHECK_EQ(trough.spirit_tide.level, 33u);
    CHECK_EQ(peak.spirit_tide.level, 99u);

    // 地脉网络必须逐字节相同：潮位只改读法，不改网络。
    bool same_strength = true;
    for (std::uint32_t i = 0; i < trough.ley_strength.size(); ++i)
    {
        same_strength = same_strength
                        && trough.ley_strength.at(i)
                               == peak.ley_strength.at(i);
    }
    CHECK(same_strength);
    CHECK_EQ(trough.ley_nodes.size(), peak.ley_nodes.size());

    bool same_nodes = true;
    for (std::size_t i = 0; i < trough.ley_nodes.size(); ++i)
    {
        same_nodes = same_nodes
                     && trough.ley_nodes[i].cell == peak.ley_nodes[i].cell;
        same_nodes = same_nodes
                     && trough.ley_nodes[i].flow == peak.ley_nodes[i].flow;
        same_nodes = same_nodes
                     && trough.ley_nodes[i].element == peak.ley_nodes[i].element;
    }
    CHECK(same_nodes);

    // 浓度总量随潮位上升：潮位是乘子，逐格单调不减。
    const std::uint64_t trough_mass = total_concentration(trough);
    const std::uint64_t peak_mass = total_concentration(peak);
    CHECK(peak_mass >= trough_mass);
    CHECK(peak_mass > trough_mass);

    // 福地面积只增不减：浓度逐格单调，阈值只可能被向上跨越。
    CHECK(count_grade(peak, DomainGrade::blessed)
          >= count_grade(trough, DomainGrade::blessed));
}

TEST(the_tide_level_separates_the_blessed_area)
{
    // 合成一只大流量的眼：谷底期眼心落在厚域，灵潮期落在福地。
    // 同一张网、同一只眼，两个潮位读出的福地面积必然不同。
    WorldParams p;
    p.width = 128;
    p.height = 128;

    p.spirit_year = 8914;
    World trough(p);
    gamev2::world::make_spirit_tide(trough);
    SpiritEye eye;
    eye.cell = trough.elevation.index_of(64, 64);
    eye.flow = 2u * kReferenceFlow;
    trough.eyes.push_back(eye);
    make_spirit_domains(trough);

    p.spirit_year = 9300;
    World peak(p);
    gamev2::world::make_spirit_tide(peak);
    peak.eyes.push_back(eye);
    make_spirit_domains(peak);

    CHECK_EQ(count_grade(trough, DomainGrade::blessed), 0u);
    CHECK(count_grade(peak, DomainGrade::blessed) > 0u);
}

TEST(domains_are_deterministic)
{
    const World a = build_world(5, 8914);
    const World b = build_world(5, 8914);
    bool same = true;
    for (std::uint32_t i = 0; i < a.domain_concentration.size(); ++i)
    {
        same = same
               && a.domain_concentration.at(i) == b.domain_concentration.at(i);
        same = same && a.domain_grade.at(i) == b.domain_grade.at(i);
    }
    CHECK(same);
}

TEST(the_concentration_field_keeps_its_calibration)
{
    // 标定回归：这几条数值锁住阶段 14 的分布形状。
    // 没有它们，一次「忠实于简报」的实现就能把福地糊成 21%~36%，
    // 而所有旧用例照样全绿。
    //
    // 断言的是**跨种子平均**：福地面积取决于眼落在哪儿，单个种子落在
    // 区间外是常事，要锁的是形状而不是某一个种子的巧合。
    constexpr std::uint32_t kSeeds = 5;
    Calibration peak;
    Calibration trough;
    for (std::uint32_t i = 0; i < kSeeds; ++i)
    {
        peak.add(measure(build_world(i + 1u, 9300)));
        trough.add(measure(build_world(i + 1u, 8914)));
    }
    peak.average(kSeeds);
    trough.average(kSeeds);

    // 灵潮期末（潮位 99）的分布形状：
    //
    // **废域过半是设定要的**：天下灵气本就稀薄，凡人是这个世界的主体
    // （设定 2.4 / 7.3）。这一条同时是**背景高原回归的哨兵**——尾巴
    // 留到 3λ 时废域会塌到 32%~38%，半径放回 24 时塌到 38%~41%，
    // 而正确标定下它稳定在 57%~62%。实测 7 组不同的 5~6 种子集，
    // 正确态与两个回归态在这一条上完全不重叠。
    CHECK(peak.barren >= 45.0);
    // 薄/中两档都得有量，任何一档塌掉都说明阈值阶梯切歪了。
    CHECK(peak.thin >= 3.0);
    CHECK(peak.medium >= 3.0);
    // 厚域是第二条哨针：尾巴留到 3λ 或半径放回 24 时它会涨到 15%~21%
    // （背景高原的顶），正确标定下只有 8.6%~12.2%。
    CHECK(peak.thick >= 3.0);
    CHECK(peak.thick <= 14.0);
    // 福地是唯一一档要「少」的：红线 8 说稀缺性不为剧情让路，设定里天下
    // 只有几处福地，铺开两成就不是「几处」。
    //
    // 下界只能取 1.0%：福地面积对种子集极其敏感。实测 7 组不同的 5~6
    // 种子集，256² 灵潮期末的福地占比在 1.383%（种子 101..105）到
    // 4.384%（种子 201..205）之间摆动，3.2 倍的差距——断言若按「五档
    // 都 >= 3%」写，就只对某一批种子成立，换一批种子便假红。
    // 「几处福地」这个语义由下面的**连通块数**承载，它比面积稳得多
    // （实测 7.6~9.4）。
    CHECK(peak.blessed >= 1.0);
    CHECK(peak.blessed <= 6.0);
    // 饱和说明浓度场被削平成一片白，是标定崩掉的信号。
    CHECK(peak.saturated < 1.0);
    // 连通块数对应「几处福地」：九主脉一龙脉，一脉一处。
    CHECK(peak.components >= 5.0);
    CHECK(peak.components <= 15.0);

    // 谷底期（潮位 33）没有福地：设定说福地与洞天是随灵潮出现并扩张的
    // 东西，「灵脉半死」的读法里没有洞天。
    CHECK_EQ(trough.blessed, 0.0);
    CHECK_EQ(trough.components, 0.0);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 编译失败，报 `make_spirit_domains` 未定义（`src/world/domains.cpp` 里
还没有这个函数），或链接失败报符号缺失。

- [ ] **Step 3: 把测试文件加进 CMakeLists**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 列表**最后一行之后**追加
一行（源文件 `src/world/domains.cpp` 已在 Task 1 加进 `WORLDGEN_SOURCES`，
此处不动它）：

```cmake
    tests/test_world_domains.cpp
```

- [ ] **Step 4: 写 `src/world/domains.cpp`**

创建 `src/world/domains.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 贡献衰减到可以忽略的截止距离，以衰减尺度 lambda 为单位。
///
/// 截止距离按单只眼算与按叠加场算是两回事：单眼而言 exp(-3) 只有
/// 0.05，看着微不足道，但天下几十只眼叠起来就是 0.05 × 50 = 2.5 倍
/// 于一只眼的峰值，背景被抬成一片高原，福地门槛于是被「众眼尾巴」
/// 越过。1.5 的意思是尾巴仍留 22% 贡献（exp(-1.5) ≈ 0.22），为的是
/// 浓度场处处连续、没有硬边（设计 §3.4），不是为了把覆盖范围铺满全图。
constexpr double kDomainFalloffLambdas = 1.5;

/// 满流量灵眼在眼心贡献的字节数。流量除以参考流量后乘 255，于是
/// 「一只满流量的眼」在眼心贡献 255——这是**潮位缩放之前**的量，
/// 潮位 33 时它最终只落 33。
constexpr double kDomainByteScale = 255.0;

/// 潮位换算的除数。浓度场整体乘 level / 255。
constexpr double kTideByteScale = 255.0;

}  // namespace

void make_spirit_domains(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    // 累加值用 double 存：单眼贡献可以远超 255，**逐眼从不封顶**，
    // 只有总和在潮位缩放之后才封顶。先夹紧会把灵潮期与谷底期读成
    // 同一张图。
    std::vector<double> acc(n, 0.0);

    for (const SpiritEye& eye : world.eyes)
    {
        // 枯眼没有流量，也不再生：它不贡献任何灵气。
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        if (eye.cell >= n)
        {
            // 眼位越界说明阶段 13 违约。跳过比越界写内存便宜。
            continue;
        }

        const double flow = static_cast<double>(eye.flow);
        const double lambda =
            kDomainRadius
            * std::sqrt(flow / static_cast<double>(kReferenceFlow));
        // 流量大的灵眼影响半径更大；上限是 kDomainMaxRadius。
        const int radius = static_cast<int>(
            std::min(static_cast<double>(kDomainMaxRadius),
                     std::ceil(lambda * kDomainFalloffLambdas)));
        if (radius <= 0)
        {
            continue;
        }

        const double peak =
            flow * kDomainByteScale / static_cast<double>(kReferenceFlow);
        const int ex = static_cast<int>(eye.cell % p.width);
        const int ey = static_cast<int>(eye.cell / p.width);

        for (int dy = -radius; dy <= radius; ++dy)
        {
            const int ny = ey + dy;
            if (ny < 0 || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int nx = ex + dx;
                if (nx < 0 || nx >= static_cast<int>(p.width))
                {
                    continue;
                }
                const double dist = std::sqrt(static_cast<double>(dx) * dx
                                             + static_cast<double>(dy) * dy);
                if (dist > static_cast<double>(radius))
                {
                    continue;
                }
                // 线性下标与眼位解码用的是同一个宽度，两者必须一致。
                const auto index =
                    static_cast<std::size_t>(ny) * p.width
                    + static_cast<std::size_t>(nx);
                acc[index] += peak * std::exp(-dist / lambda);
            }
        }
    }

    // 潮位缩放放在夹紧之前：累加值可以远超 255，先乘比例再夹，灵潮期与
    // 谷底期读出的才是同一张地脉网络的两种浓度。
    const double tide =
        static_cast<double>(world.spirit_tide.level) / kTideByteScale;
    for (std::size_t i = 0; i < n; ++i)
    {
        const long rounded = std::lround(acc[i] * tide);
        const auto byte = static_cast<std::uint8_t>(
            rounded < 0L ? 0L : (rounded > 255L ? 255L : rounded));
        world.domain_concentration.at(static_cast<std::uint32_t>(i)) = byte;
        world.domain_grade.at(static_cast<std::uint32_t>(i)) =
            domain_grade_of(byte);
    }
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。若 `every_live_eye_sits_in_a_concentrated_cell` 报
`CHECK(eye.flow >= needed)` 失败，说明阶段 13 的眼流量标定低于薄域门槛所需的
量级——最强的一批眼都越不过门槛，是上游标定偏低，不是本阶段的公式错。

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 报告本任务完成的内容与新增文件，等待用户决定
提交时机。供用户参考的暂存命令：

```powershell
git add src/world/domains.cpp CMakeLists.txt
git add tests/test_world_domains.cpp
```

---

### Task 6: 阶段 15：历史疤痕与灵气压制

**Files:**
- Create: `src/world/land.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_land.cpp`

**Interfaces:**
- Consumes:
  - `World::veins`（`std::vector<SpiritVein>`）——读 `id`、`overmined`
  - `World::eyes`（`std::vector<SpiritEye>`）——读 `cell`、`flow`、`depleted`
  - `World::vein_id`（`Grid<std::uint32_t>`）——灵脉覆盖格的栅格镜像
  - `World::domain_grade`（`Grid<DomainGrade>`）——只读
  - `World::elevation`、`World::sea_level()`、`World::lake_depth`、
    `World::is_ocean(x, y)`
- Produces:
  - `void gamev2::world::make_historical_scars(World& world)`
  - `World::scars`（`std::vector<ScarSite>`）——阶段 15 的唯一写入者
  - `World::scar_kind`（`Grid<Scar>`）——阶段 15 的唯一写入者
  - `World::spirit_suppression`（`Grid<std::uint8_t>`）——阶段 15 的唯一写入者
  - `ScarSite::target` 的语义：沉陷疤与渡劫疤填**灵脉 id**；浊域疤与枯眼
    疤填**灵眼在 `World::eyes` 里的下标**
  - `World::scars` 的顺序固定：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤
  - 浊域（沸海）的规格：半径 = **14**；候选 = **全部临水的活眼**，按流量
    降序排列，**不截断**；选点 = **greedy**，取自由候选中流量降序的第一个
  - 放置策略：**先滤后抽**——先把与已放圆盘不相交的候选筛成自由候选，再在
    自由候选里选。抽中冲突即丢弃等于把「这里放不下」当成「没有这个候选」，
    凭空消耗候选名额
  - 不变式：任一格 `scar_kind != Scar::none` 当且仅当它被某处疤盖住；
    死地类疤（沉陷/渡劫/枯眼）盖住的格 `spirit_suppression > 0`；
    浊域疤盖住的格 `spirit_suppression == 0`（浊域不压制灵气）
  - 两处疤的圆盘互不相交：中心距 ≥ 两半径之和。**同类枯眼疤除外**——
    两只枯眼挨得近时两处死地环都要落下，压制量取两者的较大者

**本任务依赖上游任务已满足的契约**（不满足则本任务的实现要改）：

- 阶段 13 必须把**所有**灵脉（含荒脉）覆盖的格写进 `World::vein_id`。
  被越采的脉通常排名靠后而成为荒脉，若栅格只记龙/主/支脉，本阶段就永远
  找不到可塌陷的脉。
- `SpiritVein::id` 等于该灵脉在 `World::veins` 中的下标；`SpiritEye::vein`
  记的是该下标。

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_land.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::ScarSite;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritVein;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::kNoVein;
using gamev2::world::make_historical_scars;
using gamev2::world::make_land_types;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 15 的前置状态。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    make_historical_scars(w);
    return w;
}

/// 21 个世界：7 个种子 × 3 个年份。
///
/// 灵历 8914 是谷底期、9100 是衰退期、9300 是灵潮期末。构建一个 256x256
/// 的世界要小半秒，五条用例各建一遍太慢，故整份测试只构建一次。
const std::vector<World>& survey_worlds()
{
    static const std::vector<World> worlds = [] {
        const std::uint64_t seeds[] = {1, 2, 3, 7, 42, 101, 777};
        const std::uint32_t years[] = {8914, 9100, 9300};
        std::vector<World> built;
        for (const std::uint64_t seed : seeds)
        {
            for (const std::uint32_t year : years)
            {
                built.push_back(build_world(seed, year));
            }
        }
        return built;
    }();
    return worlds;
}

/// 各疤中心的压制量。数值在这里独立写一遍是刻意的：测试要钉住规格，
/// 不是复述实现。
[[nodiscard]] std::uint8_t centre_suppression(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::sinkhole:
        return 220;
    case Scar::tribulation:
        return 180;
    case Scar::depleted_eye:
        return 200;
    case Scar::turbid:
        return 0;
    case Scar::none:
        return 0;
    }
    return 0;
}

/// 该格与某处疤的中心距是否小于「新疤半径 + 旧疤半径」，即放不下。
[[nodiscard]] bool blocked_by(const World& w, std::uint32_t cell, Scar kind,
                              std::uint32_t new_radius)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t gap =
            static_cast<std::int64_t>(new_radius) + s.radius;
        if (dx * dx + dy * dy < gap * gap)
        {
            return true;
        }
    }
    return false;
}

/// 该格是否落在某处同种疤的圆盘内。
///
/// 网格只记种类、不记归属，所以「半径之外的格不可能挂着这处疤的种类」
/// 只在同类疤仅一处时成立：甲疤半径之外的格完全可能落在乙疤的圆盘里。
/// 种子 3 上就有两处枯眼疤挨得够近，那条断言因此翻红。
[[nodiscard]] bool covered_by_same_kind(const World& w, std::uint32_t cell,
                                        Scar kind)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx = cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy = cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t r = static_cast<std::int64_t>(s.radius);
        if (dx * dx + dy * dy < r * r)
        {
            return true;
        }
    }
    return false;
}

/// 疤在顺序契约里的段号。
///
/// 契约规定 `World::scars` 的顺序固定：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
/// 段号单调不减就是「分段」。
[[nodiscard]] int scar_segment(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::depleted_eye:
        return 0;
    case Scar::turbid:
        return 1;
    case Scar::sinkhole:
        return 2;
    case Scar::tribulation:
        return 3;
    case Scar::none:
        break;
    }
    return -1;
}

/// 该格与某处疤的圆盘是否相交：中心距小于两半径之和。
///
/// 与 `blocked_by` 的区别是这里判的是「某处具体的疤」：重放放置顺序时
/// 已放的疤要按 kind 逐一判过去，不能一锅端。
[[nodiscard]] bool blocked_by_site(std::uint32_t cell, std::uint32_t radius,
                                   const ScarSite& site,
                                   std::uint32_t width) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    const std::int64_t dx = cx - static_cast<std::int64_t>(site.cell % width);
    const std::int64_t dy = cy - static_cast<std::int64_t>(site.cell / width);
    const std::int64_t gap = static_cast<std::int64_t>(radius) + site.radius;
    return dx * dx + dy * dy < gap * gap;
}

TEST(scar_cells_and_suppression_agree)
{
    const World w = build_world(3, 8914);
    bool agree = true;
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        const std::uint8_t sup = w.spirit_suppression.at(i);
        if (kind == Scar::none || kind == Scar::turbid)
        {
            // 无疤的格没有压制；浊域也不压制——它是浓度高但混乱。
            agree = agree && sup == 0u;
        }
        else
        {
            agree = agree && sup > 0u;
        }
    }
    CHECK(agree);
}

TEST(scar_suppression_is_strongest_at_the_centre)
{
    const World w = build_world(3, 8914);
    bool ok = true;
    for (const ScarSite& site : w.scars)
    {
        // 中心是压制最强处。
        ok = ok && w.spirit_suppression.at(site.cell)
                    == centre_suppression(site.kind);
    }

    // 带疤的格子必须落在某处同种疤的圆盘内：压制在半径处落到 0，圆盘之外
    // 不可能挂着这处疤的种类。
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        if (kind == Scar::none)
        {
            continue;
        }
        ok = ok && covered_by_same_kind(w, i, kind);
    }
    CHECK(ok);
}

TEST(sinkholes_fall_on_overmined_veins)
{
    const World w = build_world(3, 8914);

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind != Scar::sinkhole)
        {
            continue;
        }
        ++sinkholes;
        // 每处沉陷疤都落在一条被越采的灵脉上。
        CHECK(site.target < w.veins.size());
        CHECK(w.veins[site.target].overmined);
    }
    // 处数是规格给的硬上限。
    CHECK(sinkholes <= 3u);
}

TEST(overmined_veins_leave_sinkholes)
{
    const World w = build_world(3, 8914);

    // 触发条件：格落在被越采的灵脉上、地势低平、且没有被先放的枯眼疤
    // 挡住。条件是否凑齐取决于地形，故先把它算出来，再决定断言。
    const int sea_limit = static_cast<int>(w.sea_level()) + 20;
    std::uint32_t eligible = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (static_cast<int>(w.elevation.at(i)) >= sea_limit)
            {
                continue;
            }
            const std::uint32_t id = w.vein_id.at(i);
            if (id == kNoVein || id >= w.veins.size())
            {
                continue;
            }
            if (!w.veins[id].overmined)
            {
                continue;
            }
            if (blocked_by(w, i, Scar::depleted_eye, 24u))
            {
                continue;
            }
            ++eligible;
        }
    }

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind == Scar::sinkhole)
        {
            ++sinkholes;
        }
    }
    // 触发条件凑不齐时地形说了算，不算失败。
    if (eligible > 0u)
    {
        CHECK(sinkholes > 0u);
    }
}

TEST(depleted_eyes_are_surrounded_by_scars)
{
    const World w = build_world(3, 8914);
    std::uint32_t depleted = 0;
    std::uint32_t scarred = 0;
    for (std::uint32_t e = 0; e < w.eyes.size(); ++e)
    {
        if (!w.eyes[e].depleted)
        {
            continue;
        }
        ++depleted;
        if (w.scar_kind.at(w.eyes[e].cell) == Scar::depleted_eye)
        {
            ++scarred;
        }
    }
    CHECK(depleted > 0u);
    // 枯眼周边成死地是抽干的必然后果：每只枯眼都有一处自己的死地环，
    // 没有例外。两只枯眼挨得近时两处环叠在一起，而不是后一只被丢掉。
    CHECK_EQ(scarred, depleted);
}

/// 两只枯眼挨得近时，两处死地环都必须落下。
///
/// 枯眼周边的死地是抽干的必然后果，不是可选项。早先的写法把「与已放
/// 圆盘相交」一律当成放不下，后一只枯眼整处疤都被丢掉——它周边于是既
/// 不成死地也没有压制，「福地塌成死地」这条设定前提凭空少了几千格
/// （实测 90 个 256x256 世界里 10.4% 的枯眼中招）。
///
/// 叠放处的压制取两处环的较大者，不让后落的一处盖掉前一处。
TEST(close_depleted_eyes_each_get_their_own_dead_ring)
{
    // 合成：两只枯眼相距 5 格，小于两处死地环半径之和（20），必须都落下。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    const std::uint32_t a = w.elevation.index_of(20, 20);
    const std::uint32_t b = w.elevation.index_of(25, 20);

    SpiritEye eye;
    eye.vein = 0;
    eye.node = 0;
    eye.cell = a;
    eye.flow = 100;
    eye.depleted = true;
    w.eyes.push_back(eye);
    eye.cell = b;
    w.eyes.push_back(eye);

    make_historical_scars(w);

    // 两处环都落下：每只枯眼的所在格都挂着枯眼疤。
    CHECK(w.scar_kind.at(a) == Scar::depleted_eye);
    CHECK(w.scar_kind.at(b) == Scar::depleted_eye);
    // 环心压制取满值。后落的那处环在环心处只给出 200 * 5/10 = 100，
    // 若按覆写处理，先落的那处的 200 会被削掉。
    CHECK_EQ(w.spirit_suppression.at(a), 200u);
    CHECK_EQ(w.spirit_suppression.at(b), 200u);
}

TEST(scars_are_deterministic)
{
    const World a = build_world(3, 8914);
    const World b = build_world(3, 8914);
    bool same = a.scars.size() == b.scars.size();
    for (std::size_t i = 0; i < a.scars.size() && i < b.scars.size(); ++i)
    {
        same = same && a.scars[i].kind == b.scars[i].kind;
        same = same && a.scars[i].cell == b.scars[i].cell;
        same = same && a.scars[i].radius == b.scars[i].radius;
        same = same && a.scars[i].target == b.scars[i].target;
    }
    for (std::uint32_t i = 0; i < a.scar_kind.size(); ++i)
    {
        same = same && a.scar_kind.at(i) == b.scar_kind.at(i);
        same = same
               && a.spirit_suppression.at(i) == b.spirit_suppression.at(i);
    }
    CHECK(same);
}

/// 渡劫废墟的触发条件：厚域或福地才渡得起劫。
TEST(tribulation_scars_sit_on_thick_or_blessed_cells)
{
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::tribulation)
            {
                continue;
            }
            ++tribulations;
            const DomainGrade grade = w.domain_grade.at(site.cell);
            ok = ok && (grade == DomainGrade::thick
                        || grade == DomainGrade::blessed);
        }
    }
    CHECK(ok);
    // 断言不能空跑：确实要有渡劫疤可查。
    CHECK(tribulations > 0u);
}

/// 沉陷疤的触发条件：地势低平——高程低于海平面加 20。
///
/// 20 是「地势低平」的规格值，在测试里独立写一遍，不引用实现里的常量。
TEST(sinkhole_scars_sit_on_low_flat_ground)
{
    bool ok = true;
    std::uint32_t sinkholes = 0;
    for (const World& w : survey_worlds())
    {
        const int sea_limit = static_cast<int>(w.sea_level()) + 20;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::sinkhole)
            {
                continue;
            }
            ++sinkholes;
            ok = ok && static_cast<int>(w.elevation.at(site.cell)) < sea_limit;
        }
    }
    CHECK(ok);
    CHECK(sinkholes > 0u);
}

/// 两处疤的圆盘互不相交：中心距不低于两半径之和。
///
/// 这条不变式设计 §3.5 与计划 Task 6 都明列着，此前只靠「中心压制量」
/// 间接兜底——而中心压制量只在重叠恰好盖住某个疤中心时才报警。
///
/// 唯一的例外是**同类枯眼疤**：两只枯眼挨得近时，它们各自的死地环都要
/// 落下——「枯眼周边成死地」是抽干的必然后果，不是可选项。不相交这条
/// 不变式的理由是「重叠会让同一格挂上两种疤」，而两处枯眼疤是同一种，
/// 挂上去的种类不冲突，压制量由 stamp_scar 取两者的较大者。异类疤之间
/// 仍然一律不相交。
TEST(scars_never_overlap)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        for (std::size_t i = 0; i < w.scars.size(); ++i)
        {
            for (std::size_t j = i + 1; j < w.scars.size(); ++j)
            {
                if (w.scars[i].kind == Scar::depleted_eye
                    && w.scars[j].kind == Scar::depleted_eye)
                {
                    continue;
                }
                const std::int64_t dx =
                    static_cast<std::int64_t>(w.scars[i].cell % width)
                    - static_cast<std::int64_t>(w.scars[j].cell % width);
                const std::int64_t dy =
                    static_cast<std::int64_t>(w.scars[i].cell / width)
                    - static_cast<std::int64_t>(w.scars[j].cell / width);
                const std::int64_t gap =
                    static_cast<std::int64_t>(w.scars[i].radius)
                    + static_cast<std::int64_t>(w.scars[j].radius);
                ok = ok && (dx * dx + dy * dy >= gap * gap);
            }
        }
    }
    CHECK(ok);
}

/// target 的契约：沉陷疤与渡劫疤填灵脉 id，浊域疤与枯眼疤填灵眼下标。
///
/// 没有所属灵脉时填 kNoVein：结构体默认值 0 是有效的第 0 条脉下标，
/// 用 0 表示「无」会让下游把渡劫疤误读成「指向龙脉」。
TEST(scar_targets_are_valid_indices_or_sentinels)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::sinkhole || site.kind == Scar::tribulation)
            {
                ok = ok && (site.target == kNoVein
                            || site.target < w.veins.size());
            }
            else
            {
                ok = ok && site.target < w.eyes.size();
            }
        }
    }
    CHECK(ok);
}

/// 浊域的回归哨兵：沸海是设定 4.2 点名的具名地貌，每个世界都得有。
///
/// 浊域提前放置之后，它的处数对半径与选点方式都不敏感（半径 14 与 20、
/// greedy 与 uniform 都落在同一处数上），所以这条用例顺带把浊域的半径与
/// 「按流量降序取点」钉住——只有这两条规格能挡住那两类回归。
TEST(every_world_has_at_least_one_turbid_scar)
{
    bool every_world_has_one = true;
    bool radius_is_spec = true;
    bool flow_is_descending = true;
    std::uint32_t turbid_total = 0;
    for (const World& w : survey_worlds())
    {
        std::uint32_t turbid = 0;
        std::uint32_t previous_flow = 0;
        bool first = true;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::turbid)
            {
                continue;
            }
            ++turbid;
            // 浊域的半径是标定过的规格值，在测试里独立写一遍。
            radius_is_spec = radius_is_spec && site.radius == 14u;
            if (site.target >= w.eyes.size())
            {
                continue;
            }
            const std::uint32_t flow = w.eyes[site.target].flow;
            // greedy 选点：先落的浊域流量最大，后落的流量不更大。
            if (!first)
            {
                flow_is_descending = flow_is_descending && flow <= previous_flow;
            }
            previous_flow = flow;
            first = false;
        }
        every_world_has_one = every_world_has_one && turbid >= 1u;
        turbid_total += turbid;
    }
    CHECK(every_world_has_one);
    // 总量是规格给的下限：浊域不是稀有物。
    CHECK(turbid_total >= 36u);
    CHECK(radius_is_spec);
    CHECK(flow_is_descending);
}

/// 「疤痕压过福地」必须在数据上成立：福地格被死地类疤盖住之后，有效浓度
/// 必须掉到福地门槛以下——否则「死地」只是一张标签，兽潮的前提不存在。
///
/// 断言的是比例而不是「至少一处」：只要福地格被疤盖住却大半没跌到门槛
/// 以下，比例就会掉下来。灵历 9300（灵潮期末）福地最多，谷底期几乎没有。
TEST(a_scar_demotes_a_blessed_cell_below_the_threshold)
{
    std::uint64_t covered = 0;
    std::uint64_t demoted = 0;
    for (const World& w : survey_worlds())
    {
        for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
        {
            if (w.domain_grade.at(i) != DomainGrade::blessed)
            {
                continue;
            }
            if (!gamev2::world::is_dead_scar(w.scar_kind.at(i)))
            {
                continue;
            }
            ++covered;
            const std::uint8_t effective = gamev2::world::effective_concentration(
                w.domain_concentration.at(i), w.spirit_suppression.at(i));
            if (effective < gamev2::world::kBlessedThreshold)
            {
                ++demoted;
            }
        }
    }
    // 两个数都要断言：分母不为零（确实有福地格被疤盖住），分子不为零。
    CHECK(covered > 0u);
    CHECK(demoted > 0u);
    // 过半：被死地类疤盖住的福地格，一半以上要跌到福地门槛以下。
    CHECK(demoted * 2u >= covered);
}

/// 「先滤后抽」：渡劫疤只落在当时自由的渡劫候选上，自由候选没用完就不停手。
///
/// MUST-1 的第 2 要素是「先滤掉与已放圆盘不相交的候选，再在自由候选里选」。
/// 自由候选在这里独立复算：渡劫候选是 `domain_grade` 为 thick 或 blessed
/// 的格，自由候选是其中与已放的枯眼/浊域/沉陷疤圆盘不相交的格——按 kind
/// 区分，渡劫疤自己不参与判相交。
TEST(tribulation_scars_take_free_candidates)
{
    constexpr std::uint32_t radius = 8;  // 渡劫疤的半径，规格值
    constexpr std::size_t limit = 6;     // 渡劫疤的处数上限，规格值
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        std::vector<ScarSite> others;
        std::vector<ScarSite> tribs;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::tribulation)
            {
                tribs.push_back(site);
            }
            else
            {
                others.push_back(site);
            }
        }
        tribulations += static_cast<std::uint32_t>(tribs.size());

        // 自由候选：渡劫候选里与已放的非渡劫疤不相交的格。
        std::vector<std::uint32_t> live;
        for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
        {
            const DomainGrade grade = w.domain_grade.at(i);
            if (grade != DomainGrade::thick && grade != DomainGrade::blessed)
            {
                continue;
            }
            bool blocked = false;
            for (const ScarSite& s : others)
            {
                if (blocked_by_site(i, radius, s, width))
                {
                    blocked = true;
                    break;
                }
            }
            if (!blocked)
            {
                live.push_back(i);
            }
        }

        // 逐处重放：每处渡劫疤都占着一个当时自由的候选。
        for (const ScarSite& site : tribs)
        {
            ok = ok && (std::ranges::find(live, site.cell) != live.end());
            std::vector<std::uint32_t> keep;
            for (const std::uint32_t c : live)
            {
                if (!blocked_by_site(c, radius, site, width))
                {
                    keep.push_back(c);
                }
            }
            live = keep;
        }
        // 自由候选还有剩就不该停手：处数不到上限时自由候选必须已经耗尽。
        ok = ok && (tribs.size() >= limit || live.empty());
        ok = ok && (tribs.size() <= limit);
    }
    CHECK(ok);
    // 总量是第二道防线：抽中即丢会把渡劫名额白白消耗掉（实测 99 -> 96）。
    //
    // 基线随地脉网络重算：择序第三条由「高程低」改成「埋深浅」之后，
    // 厚域/福地面积与被疤覆盖的比例都变了，自由候选随之变少，21 个世界
    // 的渡劫疤总量从 99 落到 97。下界取 96，仍留 1 处的余量。
    CHECK(tribulations >= 96u);
}

/// `World::scars` 的顺序契约：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
///
/// 计划 Task 6 的契约段明写了这个顺序，此前没有任何用例检查它。段号单调
/// 不减就是「分段」：同一种疤连成一段，段与段之间不交错。
TEST(scars_are_grouped_by_kind)
{
    bool grouped = true;
    for (const World& w : survey_worlds())
    {
        int previous = 0;
        for (const ScarSite& site : w.scars)
        {
            const int segment = scar_segment(site.kind);
            grouped = grouped && segment >= previous;
            previous = segment;
        }
    }
    CHECK(grouped);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，报 `make_historical_scars` 未声明。

- [ ] **Step 3: 把测试文件加进 CMakeLists**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 列表**最后一行之后**追加
一行（源文件 `src/world/land.cpp` 已在 Task 1 加进 `WORLDGEN_SOURCES`，
此处不动它）：

```cmake
    tests/test_world_land.cpp
```

- [ ] **Step 4: 写 `src/world/land.cpp`**

创建 `src/world/land.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 沉陷疤的处数。设定 4.2 的枯墟是「三百里地陷」，折算成格数是调参项。
constexpr std::uint32_t kSinkholeCount = 3;
/// 沉陷疤的半径，格。
constexpr std::uint32_t kSinkholeRadius = 24;
/// 沉陷疤要求的「地势低平」：高程须低于海平面加这么多。
constexpr std::uint32_t kSinkholeMaxRelief = 20;
/// 渡劫废墟的处数。高阶修士才渡得起劫，疤因此小而多。
constexpr std::uint32_t kTribulationCount = 6;
constexpr std::uint32_t kTribulationRadius = 8;
/// 浊域的处数。沸海式的灵潮搅动区。
constexpr std::uint32_t kTurbidCount = 2;
/// 浊域的半径，格。两处浊域要求中心距不低于两半径之和，半径直接决定
/// 第二个浊域放不放得下。
constexpr std::uint32_t kTurbidRadius = 14;
/// 枯眼周边的半径。
constexpr std::uint32_t kDepletedEyeRadius = 10;

/// 各疤在中心的灵气压制量。死地之所以是死地，是因为那里的灵气真的
/// 没有了：压制是数据，不是标签。
constexpr std::uint8_t kSuppressionSinkhole = 220;
constexpr std::uint8_t kSuppressionTribulation = 180;
constexpr std::uint8_t kSuppressionDepletedEye = 200;
/// 浊域不压制灵气：它是浓度高但混乱，与「灵气枯竭」是两回事。
constexpr std::uint8_t kSuppressionTurbid = 0;

/// 自由候选的选法。
enum class ScarPick
{
    uniform,  ///< 在自由候选里均匀随机抽
    greedy,   ///< 取自由候选里的第一个
};

/// 两处疤的圆盘是否相交。中心距小于半径之和即相交——重叠会让同一格
/// 挂上两种疤，压制量也会互相覆盖。
///
/// 同类疤的相交不算冲突（`same_kind_ok`）：两只枯眼挨得近时，它们
/// 各自的死地环都要落下。压制量因此由 stamp_scar 取格上已有值与本次
/// 值的较大者，而不是让后落的一处盖掉前一处。
[[nodiscard]] bool overlaps_any(const std::vector<ScarSite>& placed,
                                std::uint32_t cell, std::uint32_t radius,
                                const WorldParams& p, Scar kind,
                                bool same_kind_ok) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const ScarSite& s : placed)
    {
        if (same_kind_ok && s.kind == kind)
        {
            continue;
        }
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(s.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(s.cell / p.width);
        const std::int64_t gap = static_cast<std::int64_t>(radius) + s.radius;
        if (dx * dx + dy * dy < gap * gap)
        {
            return true;
        }
    }
    return false;
}

/// 把一处疤盖到网格上。
///
/// 压制量在半径处正好落到 0，因此只标记严格在半径内的格：疤的格必有
/// 压制量，「有疤即有压制」这条不变式才立得住。
///
/// 压制量取格上已有值与本次值的**较大者**：同类疤可以重叠（两只枯眼
/// 挨得近时两处死地环都落下），叠放处若按后落的一处取值，远离该处中心
/// 的压制会被削掉一截。
void stamp_scar(World& world, const ScarSite& site,
                std::uint8_t centre_suppression)
{
    const WorldParams& p = world.params();
    const int cx = static_cast<int>(site.cell % p.width);
    const int cy = static_cast<int>(site.cell / p.width);
    const int r = static_cast<int>(site.radius);
    const double radius = static_cast<double>(r);
    for (int dy = -r; dy <= r; ++dy)
    {
        const int ny = cy + dy;
        if (ny < 0 || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int nx = cx + dx;
            if (nx < 0 || nx >= static_cast<int>(p.width))
            {
                continue;
            }
            const double dist = std::sqrt(static_cast<double>(dx) * dx
                                         + static_cast<double>(dy) * dy);
            if (dist >= radius)
            {
                continue;
            }
            const std::uint32_t i = world.elevation.index_of(
                static_cast<std::uint32_t>(nx),
                static_cast<std::uint32_t>(ny));
            world.scar_kind.at(i) = site.kind;
            const double scaled = static_cast<double>(centre_suppression)
                                  * (radius - dist) / radius;
            const long rounded = std::lround(scaled);
            const auto suppression = static_cast<std::uint8_t>(
                rounded < 0L ? 0L : (rounded > 255L ? 255L : rounded));
            if (suppression > world.spirit_suppression.at(i))
            {
                world.spirit_suppression.at(i) = suppression;
            }
        }
    }
}

/// 眼格或八邻域里有湖或海。
[[nodiscard]] bool near_water(const World& world,
                              std::uint32_t cell) noexcept
{
    const WorldParams& p = world.params();
    const int cx = static_cast<int>(cell % p.width);
    const int cy = static_cast<int>(cell / p.width);
    for (int dy = -1; dy <= 1; ++dy)
    {
        const int ny = cy + dy;
        if (ny < 0 || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int nx = cx + dx;
            if (nx < 0 || nx >= static_cast<int>(p.width))
            {
                continue;
            }
            const auto ux = static_cast<std::uint32_t>(nx);
            const auto uy = static_cast<std::uint32_t>(ny);
            const std::uint32_t i = world.elevation.index_of(ux, uy);
            if (world.lake_depth.at(i) > 0 || world.is_ocean(ux, uy))
            {
                return true;
            }
        }
    }
    return false;
}

/// 活灵眼的下标，按流量降序。
///
/// 不截断：临水的活眼只占活眼的一小半（实测 42 只活眼里只有 2~20 只
/// 临水），先按流量取前一半等于把大半候选凭空扔掉，很多世界的浊域候选
/// 会直接归零。
[[nodiscard]] std::vector<std::uint32_t> live_eyes_by_flow(const World& world)
{
    std::vector<std::uint32_t> live;
    for (std::uint32_t e = 0; e < world.eyes.size(); ++e)
    {
        const SpiritEye& eye = world.eyes[e];
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        live.push_back(e);
    }
    // 同流量按下标升序：流量相等时眼与眼没有区别，但「挑哪一只」会
    // 决定浊域落在哪里。不定一个次序，选点就落在 std::sort 的内部实现
    // 上——换一个标准库，同一个 seed 会给出不同的浊域落点。
    std::ranges::sort(live, [&world](std::uint32_t a, std::uint32_t b) {
        if (world.eyes[a].flow != world.eyes[b].flow)
        {
            return world.eyes[a].flow > world.eyes[b].flow;
        }
        return a < b;
    });
    return live;
}

/// 在候选格里落下若干处疤。
///
/// 两阶段：先把与已放圆盘不相交的候选筛成自由候选，再在自由候选里选。
/// 抽中冲突就丢弃等于把「这个位置放不下」当成「这个候选不存在」，凭空
/// 消耗候选名额。
///
/// `ScarPick::greedy` 取自由候选里的第一个：候选表按流量降序排好时，
/// 第一处疤就落在流量最大的候选上。rng 的消耗序列完全由候选表决定，与
/// 地形无关，同一个 seed 必然抽出同一串结果。
void place_scars(World& world, Pcg32& rng, Scar kind, std::uint32_t count,
                 std::uint32_t radius, std::uint8_t suppression,
                 const std::vector<std::uint32_t>& cells,
                 const std::vector<std::uint32_t>& targets, ScarPick pick)
{
    std::uint32_t placed = 0;
    while (placed < count && !cells.empty())
    {
        std::vector<std::uint32_t> free_slots;
        free_slots.reserve(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            if (!overlaps_any(world.scars, cells[i], radius, world.params(),
                              kind, false))
            {
                free_slots.push_back(static_cast<std::uint32_t>(i));
            }
        }
        if (free_slots.empty())
        {
            break;
        }
        const std::uint32_t slot = pick == ScarPick::greedy
            ? free_slots.front()
            : free_slots[rng.below(static_cast<std::uint32_t>(free_slots.size()))];
        const ScarSite site{kind, cells[slot], radius, targets[slot]};
        stamp_scar(world, site, suppression);
        world.scars.push_back(site);
        ++placed;
    }
}

}  // namespace

void make_historical_scars(World& world)
{
    const WorldParams& p = world.params();
    const std::uint32_t n = world.scar_kind.size();

    world.scars.clear();
    world.scar_kind = Grid<Scar>(p.width, p.height, Scar::none);
    world.spirit_suppression = Grid<std::uint8_t>(p.width, p.height, 0);

    Pcg32 rng(stage_seed(p.seed, static_cast<std::uint32_t>(Stream::scar)));

    // 灵脉 id -> 该脉覆盖的格。一次扫描建表，免得每条脉再扫一遍全图。
    std::vector<std::vector<std::uint32_t>> vein_cells(world.veins.size());
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const std::uint32_t id = world.vein_id.at(i);
            if (id == kNoVein || id >= world.veins.size())
            {
                continue;
            }
            vein_cells[id].push_back(i);
        }
    }

    // ── 枯眼疤 ──
    // 枯眼周边的死地是抽干的必然后果，不是一次选择，因此先放：它不该
    // 被后面三类疤挤掉。
    //
    // 同类枯眼疤允许重叠：两只枯眼挨得近时，两处死地环都要落下。早先
    // 的写法把「与已放圆盘相交」一律当成放不下，于是后一只枯眼整处疤
    // 都被丢掉——实测 90 个 256x256 世界里有 10.4% 的枯眼拿不到疤，
    // 周边既不成死地也没有压制，「福地塌成死地」这条设定前提因此凭空
    // 少了几千格。
    for (std::uint32_t e = 0; e < world.eyes.size(); ++e)
    {
        const SpiritEye& eye = world.eyes[e];
        if (!eye.depleted)
        {
            continue;
        }
        if (overlaps_any(world.scars, eye.cell, kDepletedEyeRadius, p,
                         Scar::depleted_eye, true))
        {
            continue;
        }
        const ScarSite site{Scar::depleted_eye, eye.cell, kDepletedEyeRadius, e};
        stamp_scar(world, site, kSuppressionDepletedEye);
        world.scars.push_back(site);
    }

    // ── 浊域 ──
    // 临水的活灵眼：沸海式的灵潮搅动区。候选不截断，按流量降序排好，
    // 于是第一处浊域落在流量最大的临水活眼上。
    //
    // 浊域排在沉陷与渡劫之前：沸海是当世现象（灵潮搅动），渡劫废墟与
    // 沉陷是历史残留，让当世现象给历史残留让路是顺序倒置。这样浊域只
    // 需避开枯眼疤——枯眼疤的落点完全确定、不消耗 rng——于是「这个世界
    // 有临水活眼」就等于「这个世界有浊域」，不再取决于 rng 的落点。
    std::vector<std::uint32_t> turbid_cells;
    std::vector<std::uint32_t> turbid_targets;
    for (const std::uint32_t e : live_eyes_by_flow(world))
    {
        if (!near_water(world, world.eyes[e].cell))
        {
            continue;
        }
        turbid_cells.push_back(world.eyes[e].cell);
        turbid_targets.push_back(e);
    }
    place_scars(world, rng, Scar::turbid, kTurbidCount, kTurbidRadius,
                kSuppressionTurbid, turbid_cells, turbid_targets,
                ScarPick::greedy);

    // ── 沉陷疤 ──
    // 触发条件：被越采的灵脉 + 地势低平。「被采」那一步在阶段 13，本阶段
    // 只负责「塌」的那一半——因果方向是先采后塌。
    const int sea_limit = static_cast<int>(world.sea_level())
                          + static_cast<int>(kSinkholeMaxRelief);
    std::vector<std::uint32_t> sinkhole_cells;
    std::vector<std::uint32_t> sinkhole_targets;
    for (std::uint32_t v = 0; v < world.veins.size(); ++v)
    {
        const SpiritVein& vein = world.veins[v];
        if (!vein.overmined || vein.id >= vein_cells.size())
        {
            continue;
        }
        for (const std::uint32_t i : vein_cells[vein.id])
        {
            if (static_cast<int>(world.elevation.at(i)) >= sea_limit)
            {
                continue;
            }
            sinkhole_cells.push_back(i);
            sinkhole_targets.push_back(vein.id);
        }
    }
    place_scars(world, rng, Scar::sinkhole, kSinkholeCount, kSinkholeRadius,
                kSuppressionSinkhole, sinkhole_cells, sinkhole_targets,
                ScarPick::uniform);

    // ── 渡劫废墟 ──
    // 厚域与福地才渡得起劫。目标填该格所属的灵脉，没有所属脉则填
    // kNoVein：结构体默认值 0 是有效的第 0 条脉下标，不能用它表示「无」。
    std::vector<std::uint32_t> tribulation_cells;
    std::vector<std::uint32_t> tribulation_targets;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        const DomainGrade grade = world.domain_grade.at(i);
        if (grade != DomainGrade::thick && grade != DomainGrade::blessed)
        {
            continue;
        }
        tribulation_cells.push_back(i);
        tribulation_targets.push_back(world.vein_id.at(i));
    }
    place_scars(world, rng, Scar::tribulation, kTribulationCount,
                kTribulationRadius, kSuppressionTribulation,
                tribulation_cells, tribulation_targets, ScarPick::uniform);
}

}  // namespace gamev2::world
```

**同步校验**：本任务的 Step 4 代码块是**创建 `src/world/land.cpp` 当时的
快照**，不是该文件的最终形态——Task 7 会往中间插入 `kLandProbeRadius`、
`vein_element` 与 `make_land_types` 三处。因此「整块逐字出现在
`src/world/land.cpp` 里」这条校验**只在 Task 6 完成时成立**，跨任务不再
成立。本轮起固定下来的不变式是**区域级**的：`live_eyes_by_flow` 函数
（含比较器）逐字等于仓库文件里的同名函数。校验脚本：
`agent-temp/sdd/tools/check_task6_blocks.py`。

- [ ] **Step 5: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。若 `overmined_veins_leave_sinkholes` 报
`CHECK(sinkholes > 0u)` 失败，看 `eligible` 是否为 0：为 0 说明这一轮地形
里没有「被越采的脉 + 低平地势」的组合，测试不会走到那条断言；若 `eligible`
不为 0 而仍无沉陷疤，那是实现的选址逻辑与测试的触发条件不一致。

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 报告本任务完成的内容与新增文件，等待用户决定
提交时机。供用户参考的暂存命令：

```powershell
git add src/world/land.cpp CMakeLists.txt
git add tests/test_world_land.cpp
```

---

### Task 7: 阶段 16：六类地貌与灵眼纯度

**Files:**
- Modify: `src/world/land.cpp`
- Modify: `include/gamev2/world/spirit_tables.hpp`
- Test: `tests/test_world_land.cpp`

**Interfaces:**
- Consumes:
  - `World::scar_kind`（`Grid<Scar>`）——只读，阶段 15 的产出
  - `World::domain_grade`（`Grid<DomainGrade>`）——只读，阶段 14 的产出
  - `World::ley_element`（`Grid<Element>`）——只读，阶段 12 的产出
  - `World::eyes`（`std::vector<SpiritEye>`）——读 `cell`、`vein`
  - `World::veins`（`std::vector<SpiritVein>`）——读 `element`
- Produces:
  - `void gamev2::world::make_land_types(World& world)`
  - `World::land_type`（`Grid<LandType>`）——阶段 16 的唯一写入者
  - `World::eye_purity`（`Grid<std::uint8_t>`）——阶段 16 的唯一写入者
  - 常量（`src/world/land.cpp` 顶部）：`kLandProbeRadius = 6`
  - 常量（`include/gamev2/world/spirit_tables.hpp`）：`kPurityProbeRadius = 8`、
    `kPurityBaseFivefold = 200`、`kPurityBaseHetero = 120`、
    `kPurityBaseUnknown = 160`、`kPurityMiasmaPenalty = 8`、
    `kPurityYinPenalty = 4`
    —— 纯度扣减表放共享头文件而不是阶段文件，是因为「浊域不降纯度」这条
    反直觉规则必须让读表的人一眼看见，否则会被当成 bug 修掉
    —— 幅度取小到「13 格污染」不会立刻扣穿，扣减表才是真的旋钮（依据：
    污染格数只要非零就至少 13 格）
  - 不变式（有序规则，第一个命中的生效）：
    - `land_type == dead` ⟺ `scar_kind ∈ {sinkhole, tribulation, depleted_eye}`
    - `land_type == turbid` ⟺ `scar_kind == turbid`
    - `land_type == blessed` ⟺ `scar_kind == none` 且
      `domain_grade == blessed`
    - `land_type == miasma` ⟺ 无疤、`domain_grade != blessed`、探测半径内
      有毒属性地脉
    - `land_type == yin` ⟺ 无疤、`domain_grade != blessed`、探测半径内无毒
      但有暗属性地脉
  - 不变式：本阶段**不得**改 `domain_grade` 与 `domain_concentration`——
    它们是阶段 14 的唯一写入者的产出，本阶段只读

- [ ] **Step 1: 写失败的测试**

在 `tests/test_world_land.cpp` 的匿名命名空间**内**追加下列测试（放在
`}  // namespace` 之前），并把 `build_world` 的末尾改成 4 行（末尾多一
行 `make_land_types(w);`）。改完的整个文件如下：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::ScarSite;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritVein;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::kNoVein;
using gamev2::world::make_historical_scars;
using gamev2::world::make_land_types;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 15 的前置状态。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    make_historical_scars(w);
    make_land_types(w);
    return w;
}

/// 21 个世界：7 个种子 × 3 个年份。
///
/// 灵历 8914 是谷底期、9100 是衰退期、9300 是灵潮期末。构建一个 256x256
/// 的世界要小半秒，五条用例各建一遍太慢，故整份测试只构建一次。
const std::vector<World>& survey_worlds()
{
    static const std::vector<World> worlds = [] {
        const std::uint64_t seeds[] = {1, 2, 3, 7, 42, 101, 777};
        const std::uint32_t years[] = {8914, 9100, 9300};
        std::vector<World> built;
        for (const std::uint64_t seed : seeds)
        {
            for (const std::uint32_t year : years)
            {
                built.push_back(build_world(seed, year));
            }
        }
        return built;
    }();
    return worlds;
}

/// 各疤中心的压制量。数值在这里独立写一遍是刻意的：测试要钉住规格，
/// 不是复述实现。
[[nodiscard]] std::uint8_t centre_suppression(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::sinkhole:
        return 220;
    case Scar::tribulation:
        return 180;
    case Scar::depleted_eye:
        return 200;
    case Scar::turbid:
        return 0;
    case Scar::none:
        return 0;
    }
    return 0;
}

/// 该格与某处疤的中心距是否小于「新疤半径 + 旧疤半径」，即放不下。
[[nodiscard]] bool blocked_by(const World& w, std::uint32_t cell, Scar kind,
                              std::uint32_t new_radius)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t gap =
            static_cast<std::int64_t>(new_radius) + s.radius;
        if (dx * dx + dy * dy < gap * gap)
        {
            return true;
        }
    }
    return false;
}

/// 该格是否落在某处同种疤的圆盘内。
///
/// 网格只记种类、不记归属，所以「半径之外的格不可能挂着这处疤的种类」
/// 只在同类疤仅一处时成立：甲疤半径之外的格完全可能落在乙疤的圆盘里。
/// 种子 3 上就有两处枯眼疤挨得够近，那条断言因此翻红。
[[nodiscard]] bool covered_by_same_kind(const World& w, std::uint32_t cell,
                                        Scar kind)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx = cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy = cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t r = static_cast<std::int64_t>(s.radius);
        if (dx * dx + dy * dy < r * r)
        {
            return true;
        }
    }
    return false;
}

/// 疤在顺序契约里的段号。
///
/// 契约规定 `World::scars` 的顺序固定：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
/// 段号单调不减就是「分段」。
[[nodiscard]] int scar_segment(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::depleted_eye:
        return 0;
    case Scar::turbid:
        return 1;
    case Scar::sinkhole:
        return 2;
    case Scar::tribulation:
        return 3;
    case Scar::none:
        break;
    }
    return -1;
}

/// 该格与某处疤的圆盘是否相交：中心距小于两半径之和。
///
/// 与 `blocked_by` 的区别是这里判的是「某处具体的疤」：重放放置顺序时
/// 已放的疤要按 kind 逐一判过去，不能一锅端。
[[nodiscard]] bool blocked_by_site(std::uint32_t cell, std::uint32_t radius,
                                   const ScarSite& site,
                                   std::uint32_t width) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    const std::int64_t dx = cx - static_cast<std::int64_t>(site.cell % width);
    const std::int64_t dy = cy - static_cast<std::int64_t>(site.cell / width);
    const std::int64_t gap = static_cast<std::int64_t>(radius) + site.radius;
    return dx * dx + dy * dy < gap * gap;
}

TEST(scar_cells_and_suppression_agree)
{
    const World w = build_world(3, 8914);
    bool agree = true;
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        const std::uint8_t sup = w.spirit_suppression.at(i);
        if (kind == Scar::none || kind == Scar::turbid)
        {
            // 无疤的格没有压制；浊域也不压制——它是浓度高但混乱。
            agree = agree && sup == 0u;
        }
        else
        {
            agree = agree && sup > 0u;
        }
    }
    CHECK(agree);
}

TEST(scar_suppression_is_strongest_at_the_centre)
{
    const World w = build_world(3, 8914);
    bool ok = true;
    for (const ScarSite& site : w.scars)
    {
        // 中心是压制最强处。
        ok = ok && w.spirit_suppression.at(site.cell)
                    == centre_suppression(site.kind);
    }

    // 带疤的格子必须落在某处同种疤的圆盘内：压制在半径处落到 0，圆盘之外
    // 不可能挂着这处疤的种类。
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        if (kind == Scar::none)
        {
            continue;
        }
        ok = ok && covered_by_same_kind(w, i, kind);
    }
    CHECK(ok);
}

TEST(sinkholes_fall_on_overmined_veins)
{
    const World w = build_world(3, 8914);

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind != Scar::sinkhole)
        {
            continue;
        }
        ++sinkholes;
        // 每处沉陷疤都落在一条被越采的灵脉上。
        CHECK(site.target < w.veins.size());
        CHECK(w.veins[site.target].overmined);
    }
    // 处数是规格给的硬上限。
    CHECK(sinkholes <= 3u);
}

TEST(overmined_veins_leave_sinkholes)
{
    const World w = build_world(3, 8914);

    // 触发条件：格落在被越采的灵脉上、地势低平、且没有被先放的枯眼疤
    // 挡住。条件是否凑齐取决于地形，故先把它算出来，再决定断言。
    const int sea_limit = static_cast<int>(w.sea_level()) + 20;
    std::uint32_t eligible = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (static_cast<int>(w.elevation.at(i)) >= sea_limit)
            {
                continue;
            }
            const std::uint32_t id = w.vein_id.at(i);
            if (id == kNoVein || id >= w.veins.size())
            {
                continue;
            }
            if (!w.veins[id].overmined)
            {
                continue;
            }
            if (blocked_by(w, i, Scar::depleted_eye, 24u))
            {
                continue;
            }
            ++eligible;
        }
    }

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind == Scar::sinkhole)
        {
            ++sinkholes;
        }
    }
    // 触发条件凑不齐时地形说了算，不算失败。
    if (eligible > 0u)
    {
        CHECK(sinkholes > 0u);
    }
}

TEST(depleted_eyes_are_surrounded_by_scars)
{
    const World w = build_world(3, 8914);
    std::uint32_t depleted = 0;
    std::uint32_t scarred = 0;
    for (std::uint32_t e = 0; e < w.eyes.size(); ++e)
    {
        if (!w.eyes[e].depleted)
        {
            continue;
        }
        ++depleted;
        if (w.scar_kind.at(w.eyes[e].cell) == Scar::depleted_eye)
        {
            ++scarred;
        }
    }
    CHECK(depleted > 0u);
    // 枯眼周边成死地是抽干的必然后果：每只枯眼都有一处自己的死地环，
    // 没有例外。两只枯眼挨得近时两处环叠在一起，而不是后一只被丢掉。
    CHECK_EQ(scarred, depleted);
}

/// 两只枯眼挨得近时，两处死地环都必须落下。
///
/// 枯眼周边的死地是抽干的必然后果，不是可选项。早先的写法把「与已放
/// 圆盘相交」一律当成放不下，后一只枯眼整处疤都被丢掉——它周边于是既
/// 不成死地也没有压制，「福地塌成死地」这条设定前提凭空少了几千格
/// （实测 90 个 256x256 世界里 10.4% 的枯眼中招）。
///
/// 叠放处的压制取两处环的较大者，不让后落的一处盖掉前一处。
TEST(close_depleted_eyes_each_get_their_own_dead_ring)
{
    // 合成：两只枯眼相距 5 格，小于两处死地环半径之和（20），必须都落下。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    const std::uint32_t a = w.elevation.index_of(20, 20);
    const std::uint32_t b = w.elevation.index_of(25, 20);

    SpiritEye eye;
    eye.vein = 0;
    eye.node = 0;
    eye.cell = a;
    eye.flow = 100;
    eye.depleted = true;
    w.eyes.push_back(eye);
    eye.cell = b;
    w.eyes.push_back(eye);

    make_historical_scars(w);

    // 两处环都落下：每只枯眼的所在格都挂着枯眼疤。
    CHECK(w.scar_kind.at(a) == Scar::depleted_eye);
    CHECK(w.scar_kind.at(b) == Scar::depleted_eye);
    // 环心压制取满值。后落的那处环在环心处只给出 200 * 5/10 = 100，
    // 若按覆写处理，先落的那处的 200 会被削掉。
    CHECK_EQ(w.spirit_suppression.at(a), 200u);
    CHECK_EQ(w.spirit_suppression.at(b), 200u);
}

TEST(scars_are_deterministic)
{
    const World a = build_world(3, 8914);
    const World b = build_world(3, 8914);
    bool same = a.scars.size() == b.scars.size();
    for (std::size_t i = 0; i < a.scars.size() && i < b.scars.size(); ++i)
    {
        same = same && a.scars[i].kind == b.scars[i].kind;
        same = same && a.scars[i].cell == b.scars[i].cell;
        same = same && a.scars[i].radius == b.scars[i].radius;
        same = same && a.scars[i].target == b.scars[i].target;
    }
    for (std::uint32_t i = 0; i < a.scar_kind.size(); ++i)
    {
        same = same && a.scar_kind.at(i) == b.scar_kind.at(i);
        same = same
               && a.spirit_suppression.at(i) == b.spirit_suppression.at(i);
    }
    CHECK(same);
}

/// 渡劫废墟的触发条件：厚域或福地才渡得起劫。
TEST(tribulation_scars_sit_on_thick_or_blessed_cells)
{
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::tribulation)
            {
                continue;
            }
            ++tribulations;
            const DomainGrade grade = w.domain_grade.at(site.cell);
            ok = ok && (grade == DomainGrade::thick
                        || grade == DomainGrade::blessed);
        }
    }
    CHECK(ok);
    // 断言不能空跑：确实要有渡劫疤可查。
    CHECK(tribulations > 0u);
}

/// 沉陷疤的触发条件：地势低平——高程低于海平面加 20。
///
/// 20 是「地势低平」的规格值，在测试里独立写一遍，不引用实现里的常量。
TEST(sinkhole_scars_sit_on_low_flat_ground)
{
    bool ok = true;
    std::uint32_t sinkholes = 0;
    for (const World& w : survey_worlds())
    {
        const int sea_limit = static_cast<int>(w.sea_level()) + 20;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::sinkhole)
            {
                continue;
            }
            ++sinkholes;
            ok = ok && static_cast<int>(w.elevation.at(site.cell)) < sea_limit;
        }
    }
    CHECK(ok);
    CHECK(sinkholes > 0u);
}

/// 两处疤的圆盘互不相交：中心距不低于两半径之和。
///
/// 这条不变式设计 §3.5 与计划 Task 6 都明列着，此前只靠「中心压制量」
/// 间接兜底——而中心压制量只在重叠恰好盖住某个疤中心时才报警。
///
/// 唯一的例外是**同类枯眼疤**：两只枯眼挨得近时，它们各自的死地环都要
/// 落下——「枯眼周边成死地」是抽干的必然后果，不是可选项。不相交这条
/// 不变式的理由是「重叠会让同一格挂上两种疤」，而两处枯眼疤是同一种，
/// 挂上去的种类不冲突，压制量由 stamp_scar 取两者的较大者。异类疤之间
/// 仍然一律不相交。
TEST(scars_never_overlap)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        for (std::size_t i = 0; i < w.scars.size(); ++i)
        {
            for (std::size_t j = i + 1; j < w.scars.size(); ++j)
            {
                if (w.scars[i].kind == Scar::depleted_eye
                    && w.scars[j].kind == Scar::depleted_eye)
                {
                    continue;
                }
                const std::int64_t dx =
                    static_cast<std::int64_t>(w.scars[i].cell % width)
                    - static_cast<std::int64_t>(w.scars[j].cell % width);
                const std::int64_t dy =
                    static_cast<std::int64_t>(w.scars[i].cell / width)
                    - static_cast<std::int64_t>(w.scars[j].cell / width);
                const std::int64_t gap =
                    static_cast<std::int64_t>(w.scars[i].radius)
                    + static_cast<std::int64_t>(w.scars[j].radius);
                ok = ok && (dx * dx + dy * dy >= gap * gap);
            }
        }
    }
    CHECK(ok);
}

/// target 的契约：沉陷疤与渡劫疤填灵脉 id，浊域疤与枯眼疤填灵眼下标。
///
/// 没有所属灵脉时填 kNoVein：结构体默认值 0 是有效的第 0 条脉下标，
/// 用 0 表示「无」会让下游把渡劫疤误读成「指向龙脉」。
TEST(scar_targets_are_valid_indices_or_sentinels)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::sinkhole || site.kind == Scar::tribulation)
            {
                ok = ok && (site.target == kNoVein
                            || site.target < w.veins.size());
            }
            else
            {
                ok = ok && site.target < w.eyes.size();
            }
        }
    }
    CHECK(ok);
}

/// 浊域的回归哨兵：沸海是设定 4.2 点名的具名地貌，每个世界都得有。
///
/// 浊域提前放置之后，它的处数对半径与选点方式都不敏感（半径 14 与 20、
/// greedy 与 uniform 都落在同一处数上），所以这条用例顺带把浊域的半径与
/// 「按流量降序取点」钉住——只有这两条规格能挡住那两类回归。
TEST(every_world_has_at_least_one_turbid_scar)
{
    bool every_world_has_one = true;
    bool radius_is_spec = true;
    bool flow_is_descending = true;
    std::uint32_t turbid_total = 0;
    for (const World& w : survey_worlds())
    {
        std::uint32_t turbid = 0;
        std::uint32_t previous_flow = 0;
        bool first = true;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::turbid)
            {
                continue;
            }
            ++turbid;
            // 浊域的半径是标定过的规格值，在测试里独立写一遍。
            radius_is_spec = radius_is_spec && site.radius == 14u;
            if (site.target >= w.eyes.size())
            {
                continue;
            }
            const std::uint32_t flow = w.eyes[site.target].flow;
            // greedy 选点：先落的浊域流量最大，后落的流量不更大。
            if (!first)
            {
                flow_is_descending = flow_is_descending && flow <= previous_flow;
            }
            previous_flow = flow;
            first = false;
        }
        every_world_has_one = every_world_has_one && turbid >= 1u;
        turbid_total += turbid;
    }
    CHECK(every_world_has_one);
    // 总量是规格给的下限：浊域不是稀有物。
    CHECK(turbid_total >= 36u);
    CHECK(radius_is_spec);
    CHECK(flow_is_descending);
}

/// 「疤痕压过福地」必须在数据上成立：福地格被死地类疤盖住之后，有效浓度
/// 必须掉到福地门槛以下——否则「死地」只是一张标签，兽潮的前提不存在。
///
/// 断言的是比例而不是「至少一处」：只要福地格被疤盖住却大半没跌到门槛
/// 以下，比例就会掉下来。灵历 9300（灵潮期末）福地最多，谷底期几乎没有。
TEST(a_scar_demotes_a_blessed_cell_below_the_threshold)
{
    std::uint64_t covered = 0;
    std::uint64_t demoted = 0;
    for (const World& w : survey_worlds())
    {
        for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
        {
            if (w.domain_grade.at(i) != DomainGrade::blessed)
            {
                continue;
            }
            if (!gamev2::world::is_dead_scar(w.scar_kind.at(i)))
            {
                continue;
            }
            ++covered;
            const std::uint8_t effective = gamev2::world::effective_concentration(
                w.domain_concentration.at(i), w.spirit_suppression.at(i));
            if (effective < gamev2::world::kBlessedThreshold)
            {
                ++demoted;
            }
        }
    }
    // 两个数都要断言：分母不为零（确实有福地格被疤盖住），分子不为零。
    CHECK(covered > 0u);
    CHECK(demoted > 0u);
    // 过半：被死地类疤盖住的福地格，一半以上要跌到福地门槛以下。
    CHECK(demoted * 2u >= covered);
}

/// 「先滤后抽」：渡劫疤只落在当时自由的渡劫候选上，自由候选没用完就不停手。
///
/// MUST-1 的第 2 要素是「先滤掉与已放圆盘不相交的候选，再在自由候选里选」。
/// 自由候选在这里独立复算：渡劫候选是 `domain_grade` 为 thick 或 blessed
/// 的格，自由候选是其中与已放的枯眼/浊域/沉陷疤圆盘不相交的格——按 kind
/// 区分，渡劫疤自己不参与判相交。
TEST(tribulation_scars_take_free_candidates)
{
    constexpr std::uint32_t radius = 8;  // 渡劫疤的半径，规格值
    constexpr std::size_t limit = 6;     // 渡劫疤的处数上限，规格值
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        std::vector<ScarSite> others;
        std::vector<ScarSite> tribs;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::tribulation)
            {
                tribs.push_back(site);
            }
            else
            {
                others.push_back(site);
            }
        }
        tribulations += static_cast<std::uint32_t>(tribs.size());

        // 自由候选：渡劫候选里与已放的非渡劫疤不相交的格。
        std::vector<std::uint32_t> live;
        for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
        {
            const DomainGrade grade = w.domain_grade.at(i);
            if (grade != DomainGrade::thick && grade != DomainGrade::blessed)
            {
                continue;
            }
            bool blocked = false;
            for (const ScarSite& s : others)
            {
                if (blocked_by_site(i, radius, s, width))
                {
                    blocked = true;
                    break;
                }
            }
            if (!blocked)
            {
                live.push_back(i);
            }
        }

        // 逐处重放：每处渡劫疤都占着一个当时自由的候选。
        for (const ScarSite& site : tribs)
        {
            ok = ok && (std::ranges::find(live, site.cell) != live.end());
            std::vector<std::uint32_t> keep;
            for (const std::uint32_t c : live)
            {
                if (!blocked_by_site(c, radius, site, width))
                {
                    keep.push_back(c);
                }
            }
            live = keep;
        }
        // 自由候选还有剩就不该停手：处数不到上限时自由候选必须已经耗尽。
        ok = ok && (tribs.size() >= limit || live.empty());
        ok = ok && (tribs.size() <= limit);
    }
    CHECK(ok);
    // 总量是第二道防线：抽中即丢会把渡劫名额白白消耗掉（实测 99 -> 96）。
    //
    // 基线随地脉网络重算：择序第三条由「高程低」改成「埋深浅」之后，
    // 厚域/福地面积与被疤覆盖的比例都变了，自由候选随之变少，21 个世界
    // 的渡劫疤总量从 99 落到 97。下界取 96，仍留 1 处的余量。
    CHECK(tribulations >= 96u);
}

/// `World::scars` 的顺序契约：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
///
/// 计划 Task 6 的契约段明写了这个顺序，此前没有任何用例检查它。段号单调
/// 不减就是「分段」：同一种疤连成一段，段与段之间不交错。
TEST(scars_are_grouped_by_kind)
{
    bool grouped = true;
    for (const World& w : survey_worlds())
    {
        int previous = 0;
        for (const ScarSite& site : w.scars)
        {
            const int segment = scar_segment(site.kind);
            grouped = grouped && segment >= previous;
            previous = segment;
        }
    }
    CHECK(grouped);
}

TEST(land_type_follows_the_ordered_rules)
{
    // 取灵潮期末（灵历 9300，潮位 99）：当代是谷底期，那里没有福地，
    // 「福地」这条规则会整条空跑。设定 2.6 说福地随灵潮出现并扩张。
    const World w = build_world(3, 9300);
    bool ok = true;
    for (std::uint32_t i = 0; i < w.land_type.size(); ++i)
    {
        const Scar scar = w.scar_kind.at(i);
        const LandType land = w.land_type.at(i);
        const bool dead_scar =
            scar == Scar::sinkhole || scar == Scar::tribulation
            || scar == Scar::depleted_eye;
        ok = ok && (land == LandType::dead) == dead_scar;
        ok = ok && (land == LandType::turbid) == (scar == Scar::turbid);
        ok = ok
             && (land == LandType::blessed)
                == (scar == Scar::none
                    && w.domain_grade.at(i) == DomainGrade::blessed);
    }
    CHECK(ok);
}

/// 当代（灵历 8914，谷底期）在默认种子上没有福地地貌。
///
/// 理由要说准：谷底期灵脉半死，**多数**种子的福地格数是 0，但不是所有
/// 种子——seed 777 在 8914 有 286 个福地格（复审实测）。所以这条只钉住
/// 默认种子，不声称「谷底期没有福地」这件事本身。
TEST(the_nadir_year_has_no_blessed_land_type)
{
    const World w = build_world(3, 8914);
    std::uint32_t blessed = 0;
    for (std::uint32_t i = 0; i < w.land_type.size(); ++i)
    {
        if (w.land_type.at(i) == LandType::blessed)
        {
            ++blessed;
        }
    }
    CHECK_EQ(blessed, 0u);
}

TEST(a_dead_scar_overrides_a_blessed_domain)
{
    // 合成：同一格既是福地又被疤盖住。规则 1 压过规则 3，地貌必须是
    // 死地，而分等不动——读取方要能分清「这里的网有多强」与「这里还能
    // 不能住」。设定 7.2 的兽潮前提正是这一格。
    WorldParams p;
    p.width = 16;
    p.height = 16;

    World w(p);
    const std::uint32_t i = w.elevation.index_of(8, 8);
    w.domain_grade.at(i) = DomainGrade::blessed;
    w.scar_kind.at(i) = Scar::sinkhole;

    make_land_types(w);

    CHECK(w.land_type.at(i) == LandType::dead);
    CHECK(w.domain_grade.at(i) == DomainGrade::blessed);
}

/// 「死地压过福地」必须在数据上成立：分等说这是福地的格，地貌却是死地。
///
/// 设定 7.2 的兽潮前提正是这一格——灵域还是那个灵域，地已经死了。
/// 复审指出旧写法在死地格上断言「它不是福地格」，那是编译期恒真的比较，
/// 永远不可能失败；这条反过来断言「有分等福地格被压成了死地」。
TEST(a_blessed_domain_can_be_dead_ground)
{
    // 灵潮期末才有福地：谷底期分等里几乎没有 blessed，这条会整条空跑。
    const World w = build_world(3, 9300);
    std::uint32_t blessed_grade = 0;
    std::uint32_t blessed_grade_but_dead = 0;
    for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
    {
        if (w.domain_grade.at(i) != DomainGrade::blessed)
        {
            continue;
        }
        ++blessed_grade;
        if (w.land_type.at(i) == LandType::dead)
        {
            ++blessed_grade_but_dead;
        }
    }
    CHECK(blessed_grade > 0u);
    // 死地压过福地不是理论上的可能，数据里必须真的发生。
    CHECK(blessed_grade_but_dead > 0u);
}

TEST(poison_and_dark_ley_elements_make_miasma_and_yin)
{
    // 合成世界：左半边铺毒属性地脉，右半边铺暗属性地脉。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.ley_element.at(i) =
                x < p.width / 2 ? Element::poison : Element::dark;
        }
    }
    make_land_types(w);

    bool ok = true;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            // 探测半径 6，边界带两侧的属性互相看得见，故只断言两侧的
            // 纯净部分。
            if (x + 6u < p.width / 2)
            {
                ok = ok && w.land_type.at(i) == LandType::miasma;
            }
            else if (x == p.width / 2 + 5u)
            {
                // 探测半径 6 的边界：这一列的方邻域还能看见 x = 31 的毒属性
                // 地脉（31 - 6 = 25，邻域右缘 37 覆盖到 31），所以它是瘴域
                // 而不是阴地。半径若改成 5，它就看不见毒脉、变成阴地，这
                // 条断言会翻红——半径因此被钉死。
                ok = ok && w.land_type.at(i) == LandType::miasma;
            }
            else if (x >= p.width / 2 + 6u)
            {
                ok = ok && w.land_type.at(i) == LandType::yin;
            }
        }
    }
    CHECK(ok);
}

TEST(miasma_and_yin_cells_sit_on_matching_ley_elements)
{
    const World w = build_world(3, 8914);
    bool ok = true;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            const LandType land = w.land_type.at(i);
            if (land != LandType::miasma && land != LandType::yin)
            {
                continue;
            }
            bool poison = false;
            bool dark = false;
            for (int dy = -6; dy <= 6; ++dy)
            {
                const int ny = static_cast<int>(y) + dy;
                if (ny < 0 || ny >= static_cast<int>(w.params().height))
                {
                    continue;
                }
                for (int dx = -6; dx <= 6; ++dx)
                {
                    const int nx = static_cast<int>(x) + dx;
                    if (nx < 0 || nx >= static_cast<int>(w.params().width))
                    {
                        continue;
                    }
                    const std::uint32_t j = w.elevation.index_of(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny));
                    const Element e = w.ley_element.at(j);
                    poison = poison || e == Element::poison;
                    dark = dark || e == Element::dark;
                }
            }
            if (land == LandType::miasma)
            {
                ok = ok && poison;
            }
            else
            {
                ok = ok && dark;
            }
        }
    }
    CHECK(ok);
}

TEST(eye_purity_follows_the_ley_element_and_its_neighbourhood)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    // 一条五行脉：基线纯度 200。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::metal;

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = 0;  // 下面按尺寸填
    eye.flow = 100;

    auto make_world = [&p, &vein, &eye]() {
        World w(p);
        w.veins.push_back(vein);
        SpiritEye e = eye;
        e.cell = w.elevation.index_of(32, 32);
        w.eyes.push_back(e);
        return w;
    };

    // 一、周围无污染：纯度就是基线。
    World clean = make_world();
    make_land_types(clean);
    CHECK_EQ(clean.eye_purity.at(clean.eyes[0].cell), 200u);

    // 二、周围全是瘴域：每格扣 60，扣穿为 0。
    World miasma = make_world();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            miasma.ley_element.at(miasma.elevation.index_of(x, y)) =
                Element::poison;
        }
    }
    make_land_types(miasma);
    CHECK_EQ(miasma.eye_purity.at(miasma.eyes[0].cell), 0u);

    // 三、周围全是浊域：浊域不扣纯度。设定 4.1 说沸海是「天下最纯的原
    // 料产地」——那里灵气被搅动而不是被污染，纯得不能用。这条反直觉，
    // 改动前先读这里的注释。
    World turbid = make_world();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = turbid.elevation.index_of(x, y);
            turbid.scar_kind.at(i) = Scar::turbid;
        }
    }
    make_land_types(turbid);
    CHECK_EQ(turbid.eye_purity.at(turbid.eyes[0].cell), 200u);
}

TEST(eye_purity_of_a_hetero_vein_is_lower)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    // 异质脉难驾驭，代价就是纯度低：基线 120。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::poison;
    w.veins.push_back(vein);

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 120u);
}

/// 查不到地脉属性时基线纯度是中性值 160，既不冒充五行也不冒充异质。
///
/// 这条分支在真实世界里走不到（实测 21 个世界 1050 只灵眼，属性全部查得
/// 到），所以只能合成。少了它，「查不到脉时纯度悄悄变了」没人会发现。
TEST(eye_purity_without_a_known_element_is_neutral)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    // 灵脉本身没有属性：查不到就是查不到，不许当成异质。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::none;
    w.veins.push_back(vein);

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 160u);
}

/// 灵眼找不到所属灵脉（下标越界）时同样是中性基线。
///
/// 与上一条是两条不同的路：上一条是「脉在、但没属性」，这一条是「脉根本
/// 不存在」。两条都走 `Element::none`，但走的是不同的判断。
TEST(eye_purity_without_a_vein_is_neutral)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    SpiritEye eye;
    eye.vein = 99;  // 越界：vein_element 返回 none
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 160u);
}

TEST(land_types_are_deterministic)
{
    const World a = build_world(3, 8914);
    const World b = build_world(3, 8914);
    bool same = true;
    for (std::uint32_t i = 0; i < a.land_type.size(); ++i)
    {
        same = same && a.land_type.at(i) == b.land_type.at(i);
        same = same && a.eye_purity.at(i) == b.eye_purity.at(i);
    }
    CHECK(same);
}

/// 纯度的三个常量：探测半径 8、瘴域每格扣 8、阴地每格扣 4。
///
/// 这三个数写在计划的交付物清单里，但此前的测试一条都依赖不到它们——
/// 复审造的四个变异体（半径 8→4、8→20，瘴域幅度 60→61，阴地 40→41）
/// 全部全绿漏网。
///
/// 期望值怎么来的：瘴域/阴地的判定半径是 6，毒属性地脉产生的瘴域块是
/// 13x13 格；灵眼在 (32,32)，纯度窗口是 17x17（x ∈ [24,40]）。
/// 毒属性地脉放在 (46,32)，瘴域块是 x ∈ [40,52]，与窗口只交出 x = 40
/// 这一列 —— 13 格。于是纯度 = 200 - 13 x 8 = 96，阴地 200 - 13 x 4 = 148。
///
/// 半径若改成 4，窗口够不着那一列 ⇒ 纯度 200；改成 20，整块 13x13 都
/// 进窗口 ⇒ 169 格 ⇒ 扣穿到 0。幅度若改成 9 或 5，13 格分别扣到 83 与
/// 135。四个变异体因此全部变红。
TEST(eye_purity_pins_the_probe_radius_and_the_penalties)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    // 一、五行脉的灵眼：基线纯度 200。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::metal;
    SpiritEye eye;
    eye.vein = 0;
    eye.cell = 0;
    eye.flow = 100;

    const auto build = [&p, &vein, &eye](Element around) {
        World w(p);
        w.veins.push_back(vein);
        SpiritEye e = eye;
        e.cell = w.elevation.index_of(32, 32);
        w.eyes.push_back(e);
        // 只在 (46,32) 放一格属性地脉：它自己变成瘴域/阴地，并在半径 6
        // 内带出一整块 13x13 的同属性地貌。
        w.ley_element.at(w.elevation.index_of(46, 32)) = around;
        make_land_types(w);
        return w;
    };

    const World miasma = build(Element::poison);
    CHECK_EQ(miasma.eye_purity.at(miasma.eyes[0].cell), 96u);

    const World yin = build(Element::dark);
    CHECK_EQ(yin.eye_purity.at(yin.eyes[0].cell), 148u);
}

/// 同流量的活眼之间必须有确定的次序。
///
/// 「挑哪一只」直接决定浊域落在哪里。不定次序，选点就落在 std::sort 的
/// 内部实现上——换一个标准库，同一个 seed 会给出不同的浊域疤。真实世界
/// 里同流量的活眼很常见（21 个世界每个都有 8~13 组并列，涉及 32~39 只
/// 眼），所以这条规格必须写死。
///
/// 合成：42 只活眼，眼 20 与眼 21 的流量并列最高，其余更低 ⇒ 浊域必须
/// 落在编号小的那只（眼 20）上。这一组是控制者实测挑出来的：比较器只比
/// 流量、没有全序 tiebreak 的现实现会挑到眼 21，改成「同流量按编号升序」
/// 之后才落到眼 20。
///
/// 眼位排成 7 列、间距 8：42 只眼铺满 6 行，全部落在 64x64 之内。
TEST(turbid_picks_the_lowest_indexed_eye_when_the_flows_tie)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    constexpr std::uint32_t kEyeCount = 42;
    for (std::uint32_t e = 0; e < kEyeCount; ++e)
    {
        const std::uint32_t x = 2u + (e % 7u) * 8u;
        const std::uint32_t y = 2u + (e / 7u) * 8u;
        const std::uint32_t cell = w.elevation.index_of(x, y);
        // 全部临水：浊域的候选是「临水的活眼」，不临水就进不了候选表。
        w.lake_depth.at(cell) = 1;
        SpiritEye eye;
        eye.vein = 0;
        eye.node = 0;
        eye.cell = cell;
        eye.flow = 1000u;
        eye.depleted = false;
        w.eyes.push_back(eye);
    }
    // 眼 20 与眼 21 并列最高：现实现挑到眼 21，规格要眼 20。
    w.eyes[20].flow = 9000u;
    w.eyes[21].flow = 9000u;

    make_historical_scars(w);

    std::uint32_t turbid_count = 0;
    std::uint32_t first_target = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind != Scar::turbid)
        {
            continue;
        }
        if (turbid_count == 0)
        {
            first_target = site.target;
        }
        ++turbid_count;
    }
    CHECK(turbid_count >= 1u);
    CHECK_EQ(first_target, 20u);
}

}  // namespace
```


另外要改测试文件里的 `build_world` 辅助：阶段 16 的产出必须进入测试世界，
否则 `land_type` 恒为 `mundane`，下面读地貌的用例会整条空跑——测试会通过，
但什么都没测到。把它的末尾改成：

```cpp
    gamev2::world::make_spirit_domains(w);
    make_historical_scars(w);
    make_land_types(w);
    return w;
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，报 `make_land_types` 未声明。

- [ ] **Step 3: 把 `make_land_types` 写进 `src/world/land.cpp`**

先在 `include/gamev2/world/spirit_tables.hpp` 追加纯度扣减表。这张表放共享头
文件而不是阶段文件，是因为「浊域不降纯度」这条反直觉规则必须让读表的人
一眼看见，否则会被当成 bug 修掉：

```cpp
inline constexpr std::uint32_t kPurityProbeRadius = 8;
/// 五行脉的基线纯度。设定 2.1：五行脉纯，异质脉稀有而难驾驭。
inline constexpr std::uint8_t kPurityBaseFivefold = 200;
/// 异质脉的基线纯度。难驾驭的代价就是纯度低。
inline constexpr std::uint8_t kPurityBaseHetero = 120;
/// 查不到地脉属性时的基线纯度。取中性值：既不冒充五行，也不冒充异质。
inline constexpr std::uint8_t kPurityBaseUnknown = 160;
/// 周边每格瘴域的纯度扣减。
///
/// 幅度按实测标定：污染块是毒属性地脉周围半径 6 的 13x13 方块，与灵眼
/// 半径 8 的 17x17 窗口相交最少 1 格、最多 169 格。幅度 60 时一格就扣掉
/// 基线 200 的三成、四格就扣穿到 0，1050 只眼里 88 只归零，扣减表退化成
/// 布尔量；幅度 8 让污染的多寡重新看得出来。
inline constexpr std::uint8_t kPurityMiasmaPenalty = 8;
/// 周边每格阴地的纯度扣减。取瘴域的一半（8 : 4），保持「大幅 : 中度」的
/// 比例；阴地块与灵眼窗口的相交格数同样从 1 到 169 不等，幅度 4 让阴地
/// 的扣减保持可测。
inline constexpr std::uint8_t kPurityYinPenalty = 4;
```

再在 `src/world/land.cpp` 的文件顶部（匿名命名空间**之内**，`kSuppressionTurbid`
那一行之后）追加常量：

```cpp
/// 判瘴域与阴地时的地脉属性探测半径，格。取方邻域（切比雪夫距离），
/// 边界行为明确。
constexpr std::uint32_t kLandProbeRadius = 6;
```

在匿名命名空间**之内**追加两个辅助函数：

```cpp
/// 灵眼所在灵脉的地脉属性。灵眼自己没有存属性，去它所属的灵脉上取。
[[nodiscard]] Element vein_element(const World& world,
                                   const SpiritEye& eye) noexcept
{
    if (eye.vein >= world.veins.size())
    {
        return Element::none;
    }
    return world.veins[eye.vein].element;
}
```

然后在 `make_historical_scars` **之后**、文件末尾 `}  // namespace gamev2::world`
**之前**追加：

```cpp
void make_land_types(World& world)
{
    const WorldParams& p = world.params();
    world.land_type = Grid<LandType>(p.width, p.height, LandType::mundane);
    // n 是纯度循环的越界闸门，必须取重建后的尺寸：放在重建之前等于依赖
    // World 构造函数恰好预分配了 land_type，那是个隐式约定。
    const std::uint32_t n = world.land_type.size();
    world.eye_purity = Grid<std::uint8_t>(p.width, p.height, 0);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const Scar scar = world.scar_kind.at(i);

            // 规则 1：死地压过一切。死地是「灵气枯竭，寸草不生」，它是一
            // 个否定，否定压过任何正面属性。
            if (is_dead_scar(scar))
            {
                world.land_type.at(i) = LandType::dead;
                continue;
            }
            // 规则 2：浊域优先于福地。它是高浓度但坏掉，与福地性质相反。
            if (scar == Scar::turbid)
            {
                world.land_type.at(i) = LandType::turbid;
                continue;
            }
            // 规则 3：福地。
            if (world.domain_grade.at(i) == DomainGrade::blessed)
            {
                world.land_type.at(i) = LandType::blessed;
                continue;
            }
            // 规则 4、5：瘴域与阴地看地脉属性，不看浓度——一片浓度很高的
            // 毒脉区是瘴域，不是福地。
            bool poison = false;
            bool dark = false;
            for (int dy = -static_cast<int>(kLandProbeRadius);
                 dy <= static_cast<int>(kLandProbeRadius); ++dy)
            {
                const int ny = static_cast<int>(y) + dy;
                if (ny < 0 || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                for (int dx = -static_cast<int>(kLandProbeRadius);
                     dx <= static_cast<int>(kLandProbeRadius); ++dx)
                {
                    const int nx = static_cast<int>(x) + dx;
                    if (nx < 0 || nx >= static_cast<int>(p.width))
                    {
                        continue;
                    }
                    const std::uint32_t j = world.elevation.index_of(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny));
                    const Element e = world.ley_element.at(j);
                    poison = poison || e == Element::poison;
                    dark = dark || e == Element::dark;
                }
            }
            if (poison)
            {
                world.land_type.at(i) = LandType::miasma;
            }
            else if (dark)
            {
                world.land_type.at(i) = LandType::yin;
            }
            else
            {
                world.land_type.at(i) = LandType::mundane;
            }
        }
    }

    // 灵眼纯度：基线纯度扣掉周边地貌的污染。
    //
    // 纯度必须等周边环境定下来之后才算，所以它落在本阶段而不是阶段 13。
    for (const SpiritEye& eye : world.eyes)
    {
        if (eye.cell >= n)
        {
            continue;
        }
        const Element element = vein_element(world, eye);
        // 查不到属性（Element::none）与「查到了是异质」必须分开：
        // 混成同一个基线，日后查不到脉时没人会发现纯度悄悄变了。
        int purity = kPurityBaseHetero;
        if (is_fivefold(element))
        {
            purity = kPurityBaseFivefold;
        }
        else if (element == Element::none)
        {
            purity = kPurityBaseUnknown;
        }
        const int cx = static_cast<int>(eye.cell % p.width);
        const int cy = static_cast<int>(eye.cell / p.width);
        for (int dy = -static_cast<int>(kPurityProbeRadius);
             dy <= static_cast<int>(kPurityProbeRadius); ++dy)
        {
            const int ny = cy + dy;
            if (ny < 0 || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            for (int dx = -static_cast<int>(kPurityProbeRadius);
                 dx <= static_cast<int>(kPurityProbeRadius); ++dx)
            {
                const int nx = cx + dx;
                if (nx < 0 || nx >= static_cast<int>(p.width))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                const LandType land = world.land_type.at(j);
                if (land == LandType::miasma)
                {
                    purity -= kPurityMiasmaPenalty;
                }
                else if (land == LandType::yin)
                {
                    purity -= kPurityYinPenalty;
                }
                // 浊域不扣纯度：设定 4.1 说沸海是「天下最纯的原料产地」。
                // 那里灵气被搅动而不是被污染，纯得不能用。把它当污染
                // 扣减是常见的误读，后来者会当成 bug 修掉。
            }
        }
        if (purity < 0)
        {
            purity = 0;
        }
        if (purity > 255)
        {
            purity = 255;
        }
        world.eye_purity.at(eye.cell) = static_cast<std::uint8_t>(purity);
    }
}
```

- [ ] **Step 4: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 报告本任务完成的内容与修改文件，等待用户决定
提交时机。供用户参考的暂存命令：

```powershell
git add src/world/land.cpp include/gamev2/world/spirit_tables.hpp
git add tests/test_world_land.cpp
```

---

### Task 8: 阶段 17：洞天

**Files:**
- Create: `src/world/heavens.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_heavens.cpp`

**Interfaces:**
- Consumes:
  - `World::land_type`（`Grid<LandType>`）——只读，阶段 16 的产出
  - `World::domain_concentration`（`Grid<std::uint8_t>`）——只读，阶段 14 的
    产出
  - `World::params()`——读 `width`、`height`、`seed`
- Produces:
  - `void gamev2::world::make_cave_heavens(World& world)`
  - `World::cave_heaven`（`Grid<bool>`）——阶段 17 的唯一写入者
  - `World::cave_heavens`（`std::vector<CaveHeaven>`）——阶段 17 的唯一
    写入者
  - 常量（`src/world/heavens.cpp` 顶部）：`kCaveHeavenArea = 400`、
    `kCaveHeavenCapacity = 8`、`kCaveHeavenUpkeep = 2`、
    `kCaveHeavenSpacing = 8`
  - 公式（后续的灵石收支模块直接取用）：
    - 洞天数 = 福地格数 / `kCaveHeavenArea`（整除）
    - `capacity` = 该格浓度 × `kCaveHeavenCapacity`
    - `upkeep` = `capacity` × `kCaveHeavenUpkeep`
  - **洞天数必然开满**：福地面积够就一定开够，间距约束不会让它开不满。
    选点按浓度降序贪心（设计文档 §3.7），间距不够就跳过这个候选看下一
    个，而不是只在一小块池子里选址。
  - 不变式：`cave_heaven` 网格上为真的格数 == `cave_heavens.size()`
  - 不变式：每个洞天所在格的 `land_type == LandType::blessed`
  - 不变量：两座洞天的中心距 ≥ `kCaveHeavenSpacing`
  - 不变式：`upkeep > 0` 且 `capacity > 0`——福地格浓度 ≥ 福地阈值 120，
    故容量与年耗都必然为正

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_heavens.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::CaveHeaven;
using gamev2::world::LandType;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::make_cave_heavens;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 17 的前置状态。
///
/// 世界级用例默认取**灵潮期末**（灵历 9300，潮位 99）。当代是谷底期，
/// 那里没有福地，洞天数恒为 0，用例会整条空跑。设定 2.6 说福地与洞天
/// 是随灵潮出现并扩张的，所以洞天必须在灵潮期取样。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    gamev2::world::make_historical_scars(w);
    gamev2::world::make_land_types(w);
    make_cave_heavens(w);
    return w;
}

/// 灵潮期末：谷底期没有福地，洞天随之不存在。
constexpr std::uint32_t kTideEndYear = 9300;
/// 谷底期：当代。灵气半死，没有福地。
constexpr std::uint32_t kNadirYear = 8914;

TEST(caves_only_sit_on_blessed_ground)
{
    const World w = build_world(3, kTideEndYear);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(w.land_type.at(cave.cell) == LandType::blessed);
    }
}

TEST(every_cave_costs_stones_every_year)
{
    const World w = build_world(3, kTideEndYear);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(cave.capacity > 0u);
        CHECK(cave.upkeep > 0u);
        // upkeep 就是设定红线 9 的兑现：开一座洞天要多花灵石。
        CHECK_EQ(cave.upkeep, cave.capacity * 2u);
    }
}

TEST(the_cave_grid_matches_the_cave_list)
{
    const World w = build_world(3, kTideEndYear);
    std::uint32_t marked = 0;
    for (std::uint32_t i = 0; i < w.cave_heaven.size(); ++i)
    {
        if (w.cave_heaven.at(i))
        {
            ++marked;
        }
    }
    CHECK_EQ(marked, w.cave_heavens.size());
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK(w.cave_heaven.at(cave.cell));
    }
}

TEST(caves_keep_their_distance)
{
    const World w = build_world(3, kTideEndYear);
    const std::uint32_t width = w.params().width;
    bool spaced = true;
    for (std::size_t a = 0; a < w.cave_heavens.size(); ++a)
    {
        for (std::size_t b = a + 1; b < w.cave_heavens.size(); ++b)
        {
            const std::int64_t dx =
                static_cast<std::int64_t>(w.cave_heavens[a].cell % width)
                - static_cast<std::int64_t>(w.cave_heavens[b].cell % width);
            const std::int64_t dy =
                static_cast<std::int64_t>(w.cave_heavens[a].cell / width)
                - static_cast<std::int64_t>(w.cave_heavens[b].cell / width);
            spaced = spaced && (dx * dx + dy * dy >= 64);
        }
    }
    CHECK(spaced);
}

TEST(the_nadir_year_has_no_cave_heavens)
{
    // 谷底期（灵历 8914，潮位 33）没有福地，因此没有洞天。
    //
    // 这不是缺陷，是模型的表达：潮位是纯乘子，谷底期的字节是灵潮末的
    // 三分之一；而字节上限 255 决定了「谷底期有福地」与「灵潮末福地
    // 占比 <= 6%」不可兼得。设定 2.6 说灵潮期「洞天自生、福地扩张」，
    // 谷底期「灵脉半死」，所以当代没有洞天正是设定要的样子。
    //
    // 写这一条是为了把「0 座」钉成规格，而不是让它成为没人注意的空跑。
    const World w = build_world(3, kNadirYear);
    CHECK_EQ(w.spirit_tide.level, 33u);
    CHECK_EQ(w.cave_heavens.size(), 0u);
}

TEST(cave_count_follows_the_blessed_area)
{
    // 合成：1600 格福地，每格浓度 200。
    // 洞天数 = 1600 / 400 = 4；容量 = 200 * 8 = 1600；年耗 = 1600 * 2。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    std::uint32_t blessed = 0;
    for (std::uint32_t y = 10; y < 50; ++y)
    {
        for (std::uint32_t x = 10; x < 50; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.land_type.at(i) = LandType::blessed;
            w.domain_concentration.at(i) = 200;
            ++blessed;
        }
    }
    make_cave_heavens(w);

    CHECK_EQ(blessed, 1600u);
    CHECK_EQ(w.cave_heavens.size(), 4u);
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        CHECK_EQ(cave.capacity, 1600u);
        CHECK_EQ(cave.upkeep, 3200u);
    }
}

/// 洞天的间距约束必须真的生效：高浓度福地挤在一起时也要散开。
///
/// 真实世界里浓度最高的福地格天然分散在几十只灵眼周围，彼此相距很远，
/// 于是「两座洞天间距 >= 8」这条规格在真实世界上几乎从不生效——把实现里
/// 的间距检查换成 `if (false)`，全部用例照样全绿。合成用例把三个高浓度格
/// 挤在 5~9 格之内，间距约束才显出形状。
///
/// 布局：64x64，福地格是 x ∈ [0,40)、y ∈ [0,20) 共 800 格 ⇒ 洞天数
/// 800 / 400 = 2。三个高浓度格 A(10,10)=250、B(15,10)=240、C(19,10)=230，
/// 其余 797 格浓度 200。
///
/// 按浓度降序贪心（设计文档 §3.7）：A 先落；B 距 A 5 格 < 8 ⇒ 跳过；
/// C 距 A 9 格 >= 8 ⇒ 落。于是洞天恰好是 A 与 C 两座。
TEST(caves_respect_the_spacing_when_the_best_ground_is_adjacent)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    std::uint32_t blessed = 0;
    for (std::uint32_t y = 0; y < 20; ++y)
    {
        for (std::uint32_t x = 0; x < 40; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.land_type.at(i) = LandType::blessed;
            w.domain_concentration.at(i) = 200;
            ++blessed;
        }
    }
    const std::uint32_t cell_a = w.elevation.index_of(10, 10);
    const std::uint32_t cell_b = w.elevation.index_of(15, 10);
    const std::uint32_t cell_c = w.elevation.index_of(19, 10);
    w.domain_concentration.at(cell_a) = 250;
    w.domain_concentration.at(cell_b) = 240;
    w.domain_concentration.at(cell_c) = 230;

    make_cave_heavens(w);

    CHECK_EQ(blessed, 800u);
    CHECK_EQ(w.cave_heavens.size(), 2u);
    // 落点就是 A 与 C：B 被间距约束挡掉了。
    CHECK(w.cave_heaven.at(cell_a));
    CHECK(w.cave_heaven.at(cell_c));
    CHECK(!w.cave_heaven.at(cell_b));
    // 间距是规格的一部分：任意两座洞天的中心距 >= 8 格。
    const std::uint32_t width = w.params().width;
    for (std::size_t i = 0; i < w.cave_heavens.size(); ++i)
    {
        for (std::size_t j = i + 1; j < w.cave_heavens.size(); ++j)
        {
            const std::int64_t dx =
                static_cast<std::int64_t>(w.cave_heavens[i].cell % width)
                - static_cast<std::int64_t>(w.cave_heavens[j].cell % width);
            const std::int64_t dy =
                static_cast<std::int64_t>(w.cave_heavens[i].cell / width)
                - static_cast<std::int64_t>(w.cave_heavens[j].cell / width);
            CHECK(dx * dx + dy * dy >= 64);
        }
    }
    // 洞天开在它落下去的那一格的浓度上。
    for (const CaveHeaven& cave : w.cave_heavens)
    {
        const std::uint32_t raw =
            static_cast<std::uint32_t>(w.domain_concentration.at(cave.cell));
        CHECK_EQ(cave.capacity, raw * 8u);
        CHECK_EQ(cave.upkeep, cave.capacity * 2u);
    }
}

TEST(cave_heavens_are_deterministic)
{
    const World a = build_world(3, kTideEndYear);
    const World b = build_world(3, kTideEndYear);
    bool same = a.cave_heavens.size() == b.cave_heavens.size();
    for (std::size_t i = 0; i < a.cave_heavens.size() && i < b.cave_heavens.size();
         ++i)
    {
        same = same && a.cave_heavens[i].cell == b.cave_heavens[i].cell;
        same = same && a.cave_heavens[i].capacity == b.cave_heavens[i].capacity;
        same = same && a.cave_heavens[i].upkeep == b.cave_heavens[i].upkeep;
    }
    for (std::uint32_t i = 0; i < a.cave_heaven.size(); ++i)
    {
        same = same && a.cave_heaven.at(i) == b.cave_heaven.at(i);
    }
    CHECK(same);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，报 `make_cave_heavens` 未声明。

- [ ] **Step 3: 把测试文件加进 CMakeLists**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 列表**最后一行之后**追加
一行（源文件 `src/world/heavens.cpp` 已在 Task 1 加进 `WORLDGEN_SOURCES`，
此处不动它）：

```cmake
    tests/test_world_heavens.cpp
```

- [ ] **Step 4: 写 `src/world/heavens.cpp`**

创建 `src/world/heavens.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 一座洞天摊到的福地面积，格。洞天数 = 福地面积 / 它。
constexpr std::uint32_t kCaveHeavenArea = 400;
/// 浓度换算成可锁住的灵气量。
constexpr std::uint32_t kCaveHeavenCapacity = 8;
/// 容量换算成年耗灵石。红线 9：开一座洞天要多花灵石，那灵石就少了。
constexpr std::uint32_t kCaveHeavenUpkeep = 2;
/// 两座洞天的最小中心距，格。洞天不是贴着开的。
constexpr std::uint32_t kCaveHeavenSpacing = 8;

/// 该格与已放置的洞天中心距是否小于给定间距。
[[nodiscard]] bool too_close_to(const std::vector<CaveHeaven>& placed,
                                std::uint32_t cell, std::uint32_t gap,
                                const WorldParams& p) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const CaveHeaven& cave : placed)
    {
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(cave.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(cave.cell / p.width);
        if (dx * dx + dy * dy
            < static_cast<std::int64_t>(gap) * static_cast<std::int64_t>(gap))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

void make_cave_heavens(World& world)
{
    const WorldParams& p = world.params();
    world.cave_heavens.clear();
    world.cave_heaven = Grid<bool>(p.width, p.height, false);

    // 候选：只有福地。设定 2.5：洞天是福地之上人工开辟的小空间。
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.land_type.at(i) == LandType::blessed)
            {
                candidates.push_back(i);
            }
        }
    }
    if (candidates.empty())
    {
        return;
    }

    // 按浓度降序排。同浓度时保留下标次序，排序结果因此稳定。
    std::ranges::stable_sort(candidates, [&world](std::uint32_t a,
                                                  std::uint32_t b) {
        return world.domain_concentration.at(a)
               > world.domain_concentration.at(b);
    });

    const auto count = static_cast<std::uint32_t>(candidates.size())
                       / kCaveHeavenArea;
    if (count == 0)
    {
        return;
    }

    // 按浓度降序逐个检视（设计文档 §3.7）：洞天要开在灵气最浓的地方。
    // 间距不够就跳过这个候选看下一个，而不是只在一小块池子里选址——
    // 池子化会让「洞天数 = 福地格数 / kCaveHeavenArea」这条公式开不满：
    // 同浓度时池子落在行主序的头几行，一行放不下几座洞天，洞天反而更
    // 集中（实测灵潮期末 6 个有福地的世界里，池化版 4 个开不满）。
    for (const std::uint32_t cell : candidates)
    {
        if (world.cave_heavens.size()
            >= static_cast<std::size_t>(count))
        {
            break;
        }
        if (too_close_to(world.cave_heavens, cell, kCaveHeavenSpacing, p))
        {
            continue;
        }
        // 用有效浓度而不是原始浓度：洞天是把外界灵气收进来锁住，
        // 被疤痕压过的地方当然锁不住那么多。福地格不会被死地类疤盖住
        // （地貌规则里死地压过福地），因此福地上的有效浓度就是原始浓度。
        const std::uint32_t concentration = effective_concentration(
            world.domain_concentration.at(cell),
            world.spirit_suppression.at(cell));
        const std::uint32_t capacity = concentration * kCaveHeavenCapacity;
        const std::uint32_t upkeep = capacity * kCaveHeavenUpkeep;
        world.cave_heavens.push_back(CaveHeaven{cell, capacity, upkeep});
        world.cave_heaven.at(cell) = true;
    }
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。若 `caves_only_sit_on_blessed_ground` 与
`the_cave_grid_matches_the_cave_list` 全都通过却一座洞天也没有，说明这一轮
的福地面积为 0——那是阶段 13/14 的标定问题，不是本阶段的问题；`cave_count
_follows_the_blessed_area` 用合成世界钉死了本阶段的公式。

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 报告本任务完成的内容与新增文件，等待用户决定
提交时机。供用户参考的暂存命令：

```powershell
git add src/world/heavens.cpp CMakeLists.txt
git add tests/test_world_heavens.cpp
```

---

---

### Task 9: 全流程接线校验与确定性测试扩展

**Files:**
- Modify: `src/world/generate.cpp`
- Modify: `include/gamev2/world/generate.hpp`
- Modify: `tests/test_world_determinism.cpp`
- Create: `tests/test_world_pipeline.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world_determinism.cpp`
- Test: `tests/test_world_pipeline.cpp`

**Interfaces:**
- Consumes: 阶段 11-17 的全部产出
- Produces: 无新接口。本任务只补齐「确定性」这条覆盖与「潮位是真实参数」
  这条断言。

- [ ] **Step 1: 校验编排顺序**

打开 `src/world/generate.cpp`，确认 `generate_world()` 的调用序列是：

```cpp
    World world(params);
    make_elevation(world);
    make_rainfall(world);
    make_temperature(world);
    make_hydrology(world);
    make_biomes(world);
    make_soils(world);
    make_strata(world);
    make_caverns(world);
    make_deposits(world);
    finalize_aquifers(world);
    make_spirit_tide(world);
    make_ley_network(world);
    make_spirit_veins(world);
    make_spirit_domains(world);
    make_historical_scars(world);
    make_land_types(world);
    make_cave_heavens(world);
    return world;
```

**顺序不可调换**。特别是：

- `make_spirit_tide` 必须在 `make_spirit_domains` 之前（浓度场要乘潮位）
- `make_ley_network` 必须在 `make_spirit_veins` 之前
- `make_spirit_domains` 必须在 `make_land_types` 之前（地貌要看分等）
- `make_historical_scars` 必须在 `make_land_types` 之前（死地由疤决定）
- `make_land_types` 必须在 `make_cave_heavens` 之前（洞天只落在福地上）

若某个阶段还没接线（Task 5-8 尚未完成），补上。

`include/gamev2/world/generate.hpp` 的文件注释还写着「世界生成的十个阶段」，
阶段数早已不是十个（本次接线之后是十七个）。把它改成「世界生成的十七个
阶段与编排入口」——那是读代码的人看到的第一句，数字错了会让人以为漏掉了
阶段。

- [ ] **Step 2: 给确定性测试加灵气层的比较函数**

在 `tests/test_world_determinism.cpp` 的 `using` 声明之后追加：

```cpp
using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::LeyChannel;
using gamev2::world::LeyNode;
using gamev2::world::Scar;
using gamev2::world::ScarSite;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritTide;
using gamev2::world::SpiritVein;
using gamev2::world::TidePhase;
using gamev2::world::VeinRank;
```

然后在 `first_diff` 的两个重载之后追加：

```cpp
/// 灵气层的非网格数据也要逐字段比。结构体没有 operator==，
/// 因此这里逐字段写——漏一个字段就会漏掉一类不确定性。
bool same_node(const LeyNode& a, const LeyNode& b)
{
    return a.cell == b.cell && a.depth == b.depth && a.flow == b.flow
           && a.next == b.next && a.merges == b.merges
           && a.element == b.element;
}

bool same_channel(const LeyChannel& a, const LeyChannel& b)
{
    return a.nodes == b.nodes;
}

bool same_vein(const SpiritVein& a, const SpiritVein& b)
{
    return a.id == b.id && a.rank == b.rank && a.channel == b.channel
           && a.first_node == b.first_node && a.last_node == b.last_node
           && a.element == b.element && a.reserve == b.reserve
           && a.stability == b.stability && a.eye == b.eye
           && a.overmined == b.overmined;
}

bool same_eye(const SpiritEye& a, const SpiritEye& b)
{
    return a.vein == b.vein && a.node == b.node && a.cell == b.cell
           && a.flow == b.flow && a.depleted == b.depleted;
}

bool same_scar(const ScarSite& a, const ScarSite& b)
{
    return a.kind == b.kind && a.cell == b.cell && a.radius == b.radius
           && a.target == b.target;
}

bool same_heaven(const CaveHeaven& a, const CaveHeaven& b)
{
    return a.cell == b.cell && a.capacity == b.capacity
           && a.upkeep == b.upkeep;
}

bool same_tide(const SpiritTide& a, const SpiritTide& b)
{
    return a.year == b.year && a.cycle == b.cycle && a.phase == b.phase
           && a.year_in_phase == b.year_in_phase
           && a.years_to_next_tide == b.years_to_next_tide
           && a.level == b.level && a.peak == b.peak;
}

/// 逐项比较两个向量。比较器由调用方给——灵气层的结构体没有
/// operator==，逐字段比较也更容易看出漏了哪个字段。
template<typename T, typename Eq>
std::string_view first_diff(std::string_view label,
                            const std::vector<T>& a,
                            const std::vector<T>& b, Eq eq,
                            std::string_view sofar)
{
    if (!sofar.empty())
    {
        return sofar;
    }
    if (a.size() != b.size())
    {
        return label;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!eq(a[i], b[i]))
        {
            return label;
        }
    }
    return sofar;
}
```

- [ ] **Step 3: 把灵气层字段接进 `world_diff`**

在 `world_diff()` 里，`d = first_diff("aquifer", a.aquifer, b.aquifer, d);`
之后、`sea_level` 那一段之前插入：

```cpp
    // 灵气层排在既有字段之后：字段的排列顺序就是阶段的先后顺序，
    // 报出来的名字因此直接指出最先跑偏的阶段。
    d = first_diff("ley_strength", a.ley_strength, b.ley_strength, d);
    d = first_diff("ley_depth", a.ley_depth, b.ley_depth, d);
    d = first_diff("ley_element", a.ley_element, b.ley_element, d);
    d = first_diff("vein_rank", a.vein_rank, b.vein_rank, d);
    d = first_diff("vein_id", a.vein_id, b.vein_id, d);
    d = first_diff("eye_flow", a.eye_flow, b.eye_flow, d);
    d = first_diff("domain_concentration", a.domain_concentration,
                   b.domain_concentration, d);
    d = first_diff("domain_grade", a.domain_grade, b.domain_grade, d);
    d = first_diff("scar_kind", a.scar_kind, b.scar_kind, d);
    d = first_diff("spirit_suppression", a.spirit_suppression,
                   b.spirit_suppression, d);
    d = first_diff("land_type", a.land_type, b.land_type, d);
    d = first_diff("eye_purity", a.eye_purity, b.eye_purity, d);
    d = first_diff("cave_heaven", a.cave_heaven, b.cave_heaven, d);
    d = first_diff("ley_channels", a.ley_channels, b.ley_channels,
                   same_channel, d);
    d = first_diff("ley_nodes", a.ley_nodes, b.ley_nodes, same_node, d);
    d = first_diff("veins", a.veins, b.veins, same_vein, d);
    d = first_diff("eyes", a.eyes, b.eyes, same_eye, d);
    d = first_diff("scars", a.scars, b.scars, same_scar, d);
    d = first_diff("cave_heavens", a.cave_heavens, b.cave_heavens,
                   same_heaven, d);
    if (d.empty() && !same_tide(a.spirit_tide, b.spirit_tide))
    {
        return "spirit_tide";
    }
```

- [ ] **Step 4: 扩展「每个可写字段都要被抓到」那条用例**

`the_field_comparison_catches_every_writable_field` 用例的名字声称覆盖了
**每一个**可写字段。新增了灵气层字段，它就得跟着覆盖，否则这条断言就是
假的。

把该用例的图幅改大——16×16 的陆地上凑不出山骨源点，灵气层会是空的：

```cpp
    // 灵气层需要一张足够大的图才有内容：16x16 的陆地凑不出山骨源点，
    // 地脉、灵脉、灵眼全会是空的。
    //
    // 还要取**灵潮期末**：谷底期（当代灵历 8914）没有福地，洞天是空的。
    // 那条 `if (!w.cave_heavens.empty())` 守卫会把扰动整段跳过，于是
    // 两个世界一模一样，断言「必须不相等」就成了假的。256x256 + 灵历
    // 9300（潮位 99）能产出洞天（种子 909 实测 1 座），扰动才真的落在
    // 非空向量上。
    WorldParams p = params_of(256, 256, 2, 909);
    p.spirit_year = 9300;
```

在该用例的最后一个 `}` 之前追加以下扰动块：

```cpp
    // 灵气层的网格：逐个扰动。
    {
        World w = *base;
        w.ley_strength.at(last) =
            static_cast<std::uint8_t>(w.ley_strength.at(last) ^ 0xFFu);
        check_differs(*base, w, "ley_strength");
    }
    {
        World w = *base;
        w.ley_depth.at(last) =
            static_cast<std::uint8_t>(w.ley_depth.at(last) ^ 0xFFu);
        check_differs(*base, w, "ley_depth");
    }
    {
        World w = *base;
        w.ley_element.at(last) = w.ley_element.at(last) == Element::metal
                                    ? Element::fire
                                    : Element::metal;
        check_differs(*base, w, "ley_element");
    }
    {
        World w = *base;
        w.vein_rank.at(last) = w.vein_rank.at(last) == VeinRank::none
                                   ? VeinRank::branch
                                   : VeinRank::none;
        check_differs(*base, w, "vein_rank");
    }
    {
        World w = *base;
        w.vein_id.at(last) = w.vein_id.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "vein_id");
    }
    {
        World w = *base;
        w.eye_flow.at(last) = w.eye_flow.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "eye_flow");
    }
    {
        World w = *base;
        w.domain_concentration.at(last) =
            static_cast<std::uint8_t>(w.domain_concentration.at(last) ^ 0xFFu);
        check_differs(*base, w, "domain_concentration");
    }
    {
        World w = *base;
        w.domain_grade.at(last) =
            w.domain_grade.at(last) == DomainGrade::barren
                ? DomainGrade::thick
                : DomainGrade::barren;
        check_differs(*base, w, "domain_grade");
    }
    {
        World w = *base;
        w.scar_kind.at(last) = w.scar_kind.at(last) == Scar::none
                                   ? Scar::sinkhole
                                   : Scar::none;
        check_differs(*base, w, "scar_kind");
    }
    {
        World w = *base;
        w.spirit_suppression.at(last) =
            static_cast<std::uint8_t>(w.spirit_suppression.at(last) ^ 0xFFu);
        check_differs(*base, w, "spirit_suppression");
    }
    {
        World w = *base;
        w.land_type.at(last) = w.land_type.at(last) == LandType::mundane
                                   ? LandType::dead
                                   : LandType::mundane;
        check_differs(*base, w, "land_type");
    }
    {
        World w = *base;
        w.eye_purity.at(last) =
            static_cast<std::uint8_t>(w.eye_purity.at(last) ^ 0xFFu);
        check_differs(*base, w, "eye_purity");
    }
    {
        World w = *base;
        w.cave_heaven.at(last) = !w.cave_heaven.at(last);
        check_differs(*base, w, "cave_heaven");
    }
    // 灵气层的非网格数据：逐个扰动。
    {
        World w = *base;
        w.spirit_tide.level =
            static_cast<std::uint8_t>(w.spirit_tide.level ^ 0xFFu);
        check_differs(*base, w, "spirit_tide");
    }
    {
        World w = *base;
        if (!w.ley_channels.empty())
        {
            w.ley_channels[0].nodes.push_back(12345u);
        }
        check_differs(*base, w, "ley_channels");
    }
    {
        World w = *base;
        if (!w.ley_nodes.empty())
        {
            w.ley_nodes[0].depth ^= 1u;
        }
        check_differs(*base, w, "ley_nodes");
    }
    {
        World w = *base;
        if (!w.veins.empty())
        {
            w.veins[0].reserve ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "veins");
    }
    {
        World w = *base;
        if (!w.eyes.empty())
        {
            w.eyes[0].flow ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "eyes");
    }
    {
        World w = *base;
        if (!w.scars.empty())
        {
            w.scars[0].radius ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "scars");
    }
    {
        World w = *base;
        if (!w.cave_heavens.empty())
        {
            w.cave_heavens[0].upkeep ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "cave_heavens");
    }
```

- [ ] **Step 5: 写「灵潮周期是真实参数」的用例**

在 `tests/test_world_determinism.cpp` 的匿名命名空间内追加：

```cpp
TEST(the_spirit_tide_is_a_real_parameter)
{
    // 同一个 seed，地脉网络必须完全相同——它是地质的，不随潮位变；
    // 但灵域分等必须变——设定 2.6 的「灵潮期：泉涌、福地扩张」与
    // 「谷底期：灵脉半死」就是这个区别。这条用例证明灵潮周期不是
    // 写在文档里的背景，而是一个真的参数。
    WorldParams trough = params_of(96, 96, 2, 606);
    trough.spirit_year = 8914;  // 谷底期
    WorldParams peak = trough;
    peak.spirit_year = 9300;    // 灵潮期最后一年，浓度峰值

    const auto a = generate_world(trough);
    const auto b = generate_world(peak);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }

    CHECK_EQ(a->ley_nodes.size(), b->ley_nodes.size());
    for (std::size_t i = 0; i < a->ley_nodes.size(); ++i)
    {
        CHECK_EQ(a->ley_nodes[i].cell, b->ley_nodes[i].cell);
        CHECK_EQ(a->ley_nodes[i].depth, b->ley_nodes[i].depth);
        CHECK_EQ(a->ley_nodes[i].flow, b->ley_nodes[i].flow);
    }
    for (std::uint32_t i = 0; i < a->ley_strength.size(); ++i)
    {
        CHECK_EQ(a->ley_strength.at(i), b->ley_strength.at(i));
    }

    CHECK(a->spirit_tide.phase == TidePhase::trough);
    CHECK(b->spirit_tide.phase == TidePhase::tide);
    CHECK(a->spirit_tide.level < b->spirit_tide.level);

    bool grade_changed = false;
    for (std::uint32_t i = 0; i < a->domain_grade.size(); ++i)
    {
        if (a->domain_grade.at(i) != b->domain_grade.at(i))
        {
            grade_changed = true;
            break;
        }
    }
    CHECK(grade_changed);
}
```

- [ ] **Step 6: 补接线顺序的护栏用例**

前面的用例**测阶段，不测接线**。`tests/test_world_heavens.cpp` 里的
`build_world` 自己手工编排阶段来造世界，它把顺序排对了，于是
`generate_world()` 里顺序写错也抓不到。阶段顺序错了，世界照样生成，
只是地貌与洞天全错——这类缺陷只有走 `generate_world()` 的用例拦得住。

新建 `tests/test_world_pipeline.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>

namespace {

using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::WorldParams;

/// 走整条流水线的世界参数。灵潮期末才有福地：谷底期没有福地，洞天随之
/// 不存在，用例就抓不到接线顺序。
WorldParams tide_end(std::uint64_t seed)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 2;
    p.seed = seed;
    p.spirit_year = 9300;
    return p;
}

/// 两个世界都既有洞天、又有死疤与浊疤（实测：909 有 1 座洞天，777 有
/// 9 座；死疤 6000 格上下，浊疤 1000 格上下）。
constexpr std::uint64_t kSeeds[] = {909, 777};

TEST(the_pipeline_lands_cave_heavens_only_on_blessed_ground)
{
    // 接线顺序的护栏：make_land_types 必须排在 make_cave_heavens 之前。
    // 反了的话洞天开在还没定型的地貌上，落点与数量一起错。
    //
    // 为什么单独立一个文件：tests/test_world_heavens.cpp 里的 build_world
    // 自己手工编排阶段，它把顺序排对了，于是 generate_world() 里顺序写
    // 错也抓不到——而顺序错了世界照样生成，只是内容全错。
    for (const std::uint64_t seed : kSeeds)
    {
        const auto w = generate_world(tide_end(seed));
        CHECK(w.has_value());
        if (!w)
        {
            continue;
        }
        CHECK(!w->cave_heavens.empty());
        for (const CaveHeaven& cave : w->cave_heavens)
        {
            CHECK(w->land_type.at(cave.cell) == LandType::blessed);
            // 福地只能来自规则 3（分等为福地）：死地与浊域优先于福地，
            // 所以「地貌是福地」蕴含「分等是福地」。
            CHECK(w->domain_grade.at(cave.cell) == DomainGrade::blessed);
        }
    }
}

TEST(the_pipeline_decides_the_land_types_after_the_scars)
{
    // 同样的护栏：make_historical_scars 必须排在 make_land_types 之前。
    // 反了的话地貌算出来时疤还没落下，死地与浊域就永远不存在。
    for (const std::uint64_t seed : kSeeds)
    {
        const auto w = generate_world(tide_end(seed));
        CHECK(w.has_value());
        if (!w)
        {
            continue;
        }
        std::uint32_t dead_cells = 0;
        std::uint32_t turbid_cells = 0;
        for (std::uint32_t i = 0; i < w->scar_kind.size(); ++i)
        {
            const Scar scar = w->scar_kind.at(i);
            if (scar == Scar::sinkhole || scar == Scar::tribulation
                || scar == Scar::depleted_eye)
            {
                ++dead_cells;
                CHECK(w->land_type.at(i) == LandType::dead);
            }
            else if (scar == Scar::turbid)
            {
                ++turbid_cells;
                CHECK(w->land_type.at(i) == LandType::turbid);
            }
        }
        // 数量必须非零：只断言蕴含关系的话，一个没有浊疤的世界照样通过，
        // 而「疤没落下」正是这条用例要抓的失效。
        CHECK(dead_cells > 0u);
        CHECK(turbid_cells > 0u);
    }
}

}  // namespace
```

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` 里，`tests/test_world_heavens.cpp`
之后加一行：

```cmake
    tests/test_world_pipeline.cpp
```

- [ ] **Step 7: 构建并运行测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。

**若 `the_field_comparison_catches_every_writable_field` 红了**，
说明某个新字段没被 `world_diff` 覆盖，或者某个扰动块写错了字段名。
对照 Step 3 的清单逐项核。

**若 `the_spirit_tide_is_a_real_parameter` 红了**，
先看是不是 `domain_concentration` 全为零（说明浓度刻度与灵眼流量没接上），
再核对 `spirit_tide.level` 是否真的不同。

- [ ] **Step 8: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/world/generate.cpp include/gamev2/world/generate.hpp CMakeLists.txt
git add tests/test_world_determinism.cpp tests/test_world_pipeline.cpp
```

---

### Task 10: 演示程序与阈值调参

**Files:**
- Modify: `src/demo/main.cpp`
- Test: 无新增测试文件。本任务靠 demo 的人工检视

**Interfaces:**
- Consumes: 灵气层的全部网格与列表
- Produces: 无新接口。产出 `agent-temp/worldgen/` 下的 PPM 与终端统计

- [ ] **Step 1: 给 demo 加灵气的名字表与配色**

在 `src/demo/main.cpp` 的 `using` 声明之后追加：

```cpp
using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritTide;
using gamev2::world::SpiritVein;
using gamev2::world::TidePhase;
using gamev2::world::VeinRank;
using gamev2::world::kLandTypeCount;
```

在 `kBiomeColor` 之后追加：

```cpp
/// 六类地貌的配色。与群系配色分开：两者正交，叠加显示会把地脉压掉。
constexpr std::array<std::array<std::uint8_t, 3>, kLandTypeCount> kLandTypeColor{{
    {240, 220, 120},  // blessed 福地
    {120, 180, 90},   // miasma 瘴域
    {90, 70, 120},    // yin 阴地
    {30, 30, 30},     // dead 死地
    {110, 150, 200},  // turbid 浊域
    {205, 205, 205},  // mundane 凡地
}};
static_assert(kLandTypeColor.size() == kLandTypeCount);

constexpr std::array<std::string_view, kLandTypeCount> kLandTypeNames{
    "blessed", "miasma", "yin", "dead", "turbid", "mundane",
};
static_assert(kLandTypeNames.size() == kLandTypeCount);
static_assert(kLandTypeNames[static_cast<std::size_t>(LandType::dead)] == "dead");
```

- [ ] **Step 2: 加地貌分类图导出与绝对刻度导出**

在 `export_biome` 之后追加两张导出。`export_land_type` 是分类配色；
`export_bytes` 是**绝对刻度**——灵气层那两张网格本来就是 0..255 的字节，
浓度的分档阈值（8/24/60/120）有意义，按最大值归一化会把潮位差与阈值
关系一起抹掉，那张图就再也分不出灵潮期与谷底期，调参回路也就断了。

```cpp
void export_land_type(const World& world, const std::filesystem::path& dir)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(world.land_type.size()) * 3);
    for (std::uint32_t i = 0; i < world.land_type.size(); ++i)
    {
        const auto& c =
            kLandTypeColor[static_cast<std::size_t>(world.land_type.at(i))];
        rgb.push_back(c[0]);
        rgb.push_back(c[1]);
        rgb.push_back(c[2]);
    }
    write_ppm(dir / "land_type.ppm", world.params().width,
              world.params().height, rgb);
}
```

紧接着追加绝对刻度导出：

```cpp
/// 绝对刻度导出。灵气层这两张网格本来就是 0..255 的字节，浓度的分档
/// 阈值（8/24/60/120）有意义；按最大值归一化会把潮位差与阈值关系一起
/// 抹掉，那张图就再也分不出灵潮期与谷底期。
void export_bytes(const World& world, const std::filesystem::path& dir,
                  std::string_view label, const Grid<std::uint8_t>& grid)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(grid.size()) * 3);
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        const Rgb px = ramp(grid.at(i));
        rgb.insert(rgb.end(), px.begin(), px.end());
    }
    write_ppm(dir / (std::string(label) + ".ppm"), world.params().width,
              world.params().height, rgb);
}
```

- [ ] **Step 3: 加灵气统计输出**

在 `print_underground_stats` 之后追加。`year_in_phase` 本身就是「与
相位起点的差」（灵历 8914 减谷底期起点 8801 等于 113，设定称「谷底期
第一百一十三年」），所以直接打印，**不要再加一**：

```cpp
std::string_view phase_name(TidePhase phase)
{
    switch (phase)
    {
    case TidePhase::tide:
        return "灵潮期";
    case TidePhase::decline:
        return "衰退期";
    case TidePhase::trough:
        return "谷底期";
    }
    return "未知";
}

void print_spirit_stats(const World& world)
{
    const SpiritTide& tide = world.spirit_tide;
    std::println("灵潮：灵历 {} 年，第 {} 轮，{}第 {} 年，距下轮 {} 年",
                 tide.year, tide.cycle + 1, phase_name(tide.phase),
                 tide.year_in_phase, tide.years_to_next_tide);
    std::println("  浓度 {} / 峰值 {}", tide.level, tide.peak);

    std::array<std::uint32_t, 5> ranks{};
    std::uint32_t eye_flow_total = 0;
    std::uint32_t depleted = 0;
    for (const SpiritVein& v : world.veins)
    {
        ++ranks[static_cast<std::size_t>(v.rank)];
    }
    for (const SpiritEye& e : world.eyes)
    {
        eye_flow_total += e.flow;
        if (e.depleted)
        {
            ++depleted;
        }
    }
    std::println("灵脉：龙 {}，主 {}，支 {}，荒 {}", ranks[4], ranks[3],
                 ranks[2], ranks[1]);
    std::println("灵眼：{} 个（枯眼 {}），总流量 {}", world.eyes.size(),
                 depleted, eye_flow_total);

    std::array<std::uint32_t, kLandTypeCount> lands{};
    for (std::uint32_t i = 0; i < world.land_type.size(); ++i)
    {
        ++lands[static_cast<std::size_t>(world.land_type.at(i))];
    }
    std::println("地貌面积占比：");
    for (std::size_t i = 0; i < kLandTypeCount; ++i)
    {
        if (lands[i] == 0)
        {
            continue;
        }
        std::println("  {:<10} {:>8} 格  {:>5.1f}%", kLandTypeNames[i], lands[i],
                     100.0 * lands[i] / world.land_type.size());
    }

    std::uint32_t upkeep_total = 0;
    for (const CaveHeaven& h : world.cave_heavens)
    {
        upkeep_total += h.upkeep;
    }
    std::println("洞天：{} 处，年耗灵石合计 {}", world.cave_heavens.size(),
                 upkeep_total);
}
```

- [ ] **Step 4: 把新导出与统计接进 `main`**

在 `main` 的参数解析里加 `--year`：灵历年直接决定潮位，而潮位决定福地
面积与洞天数量。当代 8914 是谷底期，没有福地也就没有洞天；要看福地与
洞天必须取灵潮期。demo 没有这个开关就调不了这条回路。

在 `--size` 分支之后、`else` 之前插入：

```cpp
            else if (arg == "--year" && i + 1 < argc)
            {
                params.spirit_year = std::stoul(argv[++i]);
            }
```

两处用法提示都改成同样三行，**但缩进不同**：`else` 分支在 `for` 循环里是
16 空格，`catch` 分支在函数体里是 8 空格。

`else` 分支：

```cpp
                std::println(stderr,
                             "用法: worldgen_demo [--seed N] [--size N] [--year N]");
```

`catch` 分支：

```cpp
        std::println(stderr,
                     "用法: worldgen_demo [--seed N] [--size N] [--year N]");
```

在 `main` 的 `print_underground_stats(world);` 之后追加：

```cpp
    print_spirit_stats(world);
```

在 `export_biome(world, dir);` 之后追加：

```cpp
    export_land_type(world, dir);
    export_bytes(world, dir, "ley_strength", world.ley_strength);
    export_bytes(world, dir, "domain_concentration",
                 world.domain_concentration);
```

- [ ] **Step 5: 构建并跑 demo，看图调参**

```powershell
cmake --build build
./build/worldgen_demo.exe --seed 2026 --size 256 --year 9300
```

**必须跑灵潮期末（--year 9300）。** 当代 8914 是谷底期，没有福地也就没有
洞天——那不是功能坏了，是设定里的「灵脉半死」。要看福地与洞天长什么样，
得看灵潮期。

Expected: 终端打印灵潮状态、灵脉品阶计数、灵眼数量、地貌面积占比、洞天与年耗；
`agent-temp/worldgen/` 下多出 `land_type.ppm`、`ley_strength.ppm`、
`domain_concentration.ppm`。

谷底期再跑一次做对照，把「0 座洞天」这条也钉住：

```powershell
./build/worldgen_demo.exe --seed 2026 --size 256
```

Expected: 同样打印地貌与洞天统计，但洞天为 0 处。

**种子 2026 在灵潮期末的洞天也是 0 座，这是设计如此。** 实测它的
`land_type == blessed` 只有 369 格，不到 `kCaveHeavenArea`（400），洞天
按福地面积算，所以开不出洞天（设计 §3.7）。想看洞天长什么样，换一个
福地面积够的种子，比如 `--seed 777`：灵潮期末福地 3745 格、洞天 9 座。

**这一步是本模块的调参回路。** 既有设计 §11.2 已经说明：浓度分档、域半径、
埋深系数、疤痕半径全是经验值，只能看着图调。逐项核对：

浓度图走**绝对刻度**（0..255 就是字节本身），所以图上能直接看出
有没有越过 8/24/60/120 四档阈值，也看得出潮位差。

| 现象 | 调哪个 |
|---|---|
| `domain_concentration.ppm` 几乎全黑 | `kReferenceFlow` 太大，浓度贡献被压扁 |
| 浓度图整片饱和成白 | `kReferenceFlow` 太小，或 `kDomainRadius` 太大 |
| 福地面积为 0（**且已用 `--year 9300`**） | `kBlessedThreshold` 太高，或浓度整体偏低 |
| 福地面积过大（几乎半张图） | `kBlessedThreshold` 太低 |
| 灵眼几乎没有 | 灵脉切不出段，调 `kVeinThreshold` / `kLeySourcePerCell` |
| 地脉看不见 | `kLeySourcePerCell` 太小，源点太少 |

**先确认潮位再动手。** 谷底期福地为 0 是**设计如此**，不是待修的问题——
潮位是纯乘子，谷底期字节是灵潮末的三分之一，字节上限 255 决定了
「谷底期有福地」与「灵潮期末福地 ≤ 6%」不可兼得（设计 §3.4 步骤 4、
§9.1 第 6 条）。在没有 `--year 9300` 对照的情况下调阈值，会把一个
正确的状态「修」坏。

调完再跑一次 demo，确认地貌分布看起来合理：**福地应当成片地落在山地**，
浊域与瘴域应当稀疏，死地应当是小块。

- [ ] **Step 6: 跑全量测试确认调参没有破坏不变量**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。**尤其注意** `vein_counts_match_the_setting_exactly`
（1 龙 / 9 主 / 40 支）与 `the_spirit_tide_is_a_real_parameter`
（地脉网络不随潮位变）。若调参把数量改了，说明阈值被调过了头。

- [ ] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/demo/main.cpp
git add include/gamev2/world/spirit_tables.hpp
```

调参若改了 `spirit_tables.hpp` 里的常量，一并暂存。

---

### Task 11: 阶段 18：渡劫场

2026-10-21 拍板追加（设计文档 §9.1 第 5 条）。规格见设计文档 §3.8。

**Files:**
- Create: `src/world/tribulation.cpp`
- Create: `tests/test_world_tribulation.cpp`
- Modify: `include/gamev2/world/spirit.hpp`（`TribulationSite` 类型）
- Modify: `include/gamev2/world/world.hpp`（`tribulation_site` 网格与 `tribulation_sites` 列表）
- Modify: `include/gamev2/world/generate.hpp`（阶段 18 声明）
- Modify: `include/gamev2/world/spirit_tables.hpp`（常量）
- Modify: `src/world/generate.cpp`（编排接线）
- Modify: `CMakeLists.txt`
- Modify: `src/demo/main.cpp`（导出与统计）
- Modify: `tests/test_world_determinism.cpp`（逐字段比较）

**Interfaces:**
- Consumes:
  - `World::land_type`（`Grid<LandType>`）——只读，阶段 16 的产出
  - `World::domain_grade`（`Grid<DomainGrade>`）——只读，阶段 14 的产出
  - `World::domain_concentration`（`Grid<std::uint8_t>`）——只读，阶段 14 的产出
  - `World::scar_kind`（`Grid<Scar>`）——只读，阶段 15 的产出
  - `World::params()`——读 `width`、`height`、`seed`
- Produces:
  - `void gamev2::world::make_tribulation_sites(World& world)`
  - `World::tribulation_site`（`Grid<bool>`）——阶段 18 的唯一写入者
  - `World::tribulation_sites`（`std::vector<TribulationSite>`）——阶段 18 的唯一
    写入者
  - 常量（`include/gamev2/world/spirit_tables.hpp`）：
    - `kTribulationRadius = 8`（劫云半径，格；= 百里）
    - `kTribulationSpacing = 16`（两个劫心的中心距下限，= 2 × 半径）
  - **没有浓度门槛常量**：福地（`domain_grade == blessed`）本身要求浓度
    ≥ `kBlessedThreshold`（120），再加一道阈值是死代码。浓度的差别由选点
    规则承担——按浓度降序取。
  - 不变式：`tribulation_site` 网格上为真的格数 == `tribulation_sites.size()`
  - 不变式：每个渡劫场的 `land_type == LandType::blessed`
  - 不变式：每个渡劫场的 `scar_kind == Scar::none`
  - 不变式：两个渡劫场的中心距 ≥ `kTribulationSpacing`（欧氏距离，与阶段 15
    的疤痕、阶段 17 的洞天同一口径）
  - 不变式：每个渡劫场的 `radius == kTribulationRadius`

**注意**：`kTribulationRadius` 与 `src/world/land.cpp` 里阶段 15 的同名常量是
**同一个物理量**（渡劫失败的死地半径 = 劫云半径 = 百里）。实现时应当把阶段 15
那个常量也改成从 `spirit_tables.hpp` 取，否则两处会各调各的。改的时候不要动
它的数值。

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_world_tribulation.cpp`：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::TribulationSite;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::make_tribulation_sites;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 18 的前置状态。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    gamev2::world::make_historical_scars(w);
    gamev2::world::make_land_types(w);
    make_tribulation_sites(w);
    return w;
}

/// 灵潮期末：福地最多，渡劫场候选最足。
constexpr std::uint32_t kTideEndYear = 9300;
/// 谷底期：当代。没有福地，渡劫场必然为空。
constexpr std::uint32_t kNadirYear = 8914;

TEST(tribulation_sites_only_sit_on_blessed_ground)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK(w.land_type.at(site.cell) == LandType::blessed);
    }
}

TEST(tribulation_sites_avoid_old_scars)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK(w.scar_kind.at(site.cell) == Scar::none);
    }
}

TEST(the_jiyun_reaches_one_hundred_li)
{
    const World w = build_world(3, kTideEndYear);
    for (const TribulationSite& site : w.tribulation_sites)
    {
        CHECK_EQ(site.radius, 8u);
    }
}

TEST(the_grid_and_the_list_agree)
{
    const World w = build_world(3, kTideEndYear);
    std::size_t marked = 0;
    for (std::size_t i = 0; i < w.tribulation_site.size(); ++i)
    {
        if (w.tribulation_site.at(i))
        {
            ++marked;
        }
    }
    CHECK_EQ(marked, w.tribulation_sites.size());
}

TEST(tribulation_sites_keep_their_distance)
{
    const World w = build_world(3, kTideEndYear);
    for (std::size_t i = 0; i < w.tribulation_sites.size(); ++i)
    {
        for (std::size_t j = i + 1; j < w.tribulation_sites.size(); ++j)
        {
            const double d = cell_distance(w, w.tribulation_sites[i].cell,
                                           w.tribulation_sites[j].cell);
            CHECK(d >= static_cast<double>(
                           gamev2::world::kTribulationSpacing));
        }
    }
}

TEST(the_tide_end_offers_tribulation_sites)
{
    // 灵潮期末福地成片，渡劫场应当真的排得出来——否则阶段 18 是个空壳。
    const World w = build_world(3, kTideEndYear);
    CHECK(!w.tribulation_sites.empty());
}

TEST(the_nadir_has_no_tribulation_site)
{
    // 谷底期没有福地，设定 2.6 说福地随灵潮出现而扩张。
    const World w = build_world(3, kNadirYear);
    CHECK(w.tribulation_sites.empty());
    CHECK_EQ(w.tribulation_site.size(), w.params().width * w.params().height);
}

TEST(the_sites_are_deterministic)
{
    const World a = build_world(7, kTideEndYear);
    const World b = build_world(7, kTideEndYear);
    CHECK_EQ(a.tribulation_sites.size(), b.tribulation_sites.size());
    for (std::size_t i = 0; i < a.tribulation_sites.size(); ++i)
    {
        CHECK_EQ(a.tribulation_sites[i].cell, b.tribulation_sites[i].cell);
        CHECK_EQ(a.tribulation_sites[i].radius, b.tribulation_sites[i].radius);
        CHECK_EQ(a.tribulation_sites[i].vein, b.tribulation_sites[i].vein);
    }
}

}  // namespace
```

**距离用欧氏距离**（`cell_distance` 辅助函数），口径与阶段 15 的疤痕、
阶段 17 的洞天一致——三处各用各的会让"间距"这个词失去意义。

其余用例应当直接通过。若不通过，先查选址三步（福地 → 无旧疤 → 浓度降序
取）是否按顺序执行。

- [ ] **Step 2: 加类型与容器**

在 `include/gamev2/world/spirit.hpp` 的 `CaveHeaven` 之后加：

```cpp
/// 渡劫场。设定 5.2：天地抽走方圆百里灵气形成劫云。
///
/// 劫云半径恒为百里，与阶段 15 的渡劫废墟半径同一把尺（见设计文档 3.8）。
struct TribulationSite
{
    std::uint32_t cell = 0;    ///< 劫心
    std::uint32_t radius = 0;  ///< 劫云半径，格（= 百里）
    std::uint32_t vein = 0;    ///< 关联的灵脉编号
};
```

在 `include/gamev2/world/world.hpp` 的 `cave_heaven` 网格之后加：

```cpp
    /// 是否为渡劫场的劫心。
    Grid<bool>          tribulation_site;
```

在非网格成员区加：

```cpp
    std::vector<TribulationSite> tribulation_sites;
```

- [ ] **Step 3: 加常量**

在 `include/gamev2/world/spirit_tables.hpp` 的常量区加：

```cpp
/// 劫云半径，格。= 百里。
///
/// 换算依据：`kSinkholeRadius = 24` 对应设定 4.2 的「三百里地陷」，
/// 于是 1 格 ≈ 12.5 里，百里 = 8 格。阶段 15 的渡劫废墟半径用同一个值。
inline constexpr std::uint32_t kTribulationRadius = 8;

/// 渡劫场的浓度门槛。低于它抽不动灵气，渡不了劫。调参项。
inline constexpr std::uint8_t kTribulationMinConcentration = 120;

/// 两个渡劫场的中心距下限。= 2 × 半径：劫云不重叠。
inline constexpr std::uint32_t kTribulationSpacing = 2 * kTribulationRadius;
```

- [ ] **Step 4: 实现阶段 18**

创建 `src/world/tribulation.cpp`。骨架：

```cpp
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 该格与已放置的渡劫场中心距是否小于给定间距。
///
/// 距离口径与阶段 17 的洞天、阶段 15 的疤痕一致：欧氏距离。
[[nodiscard]] bool too_close_to(const std::vector<TribulationSite>& placed,
                                std::uint32_t cell, std::uint32_t gap,
                                const WorldParams& p) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const TribulationSite& site : placed)
    {
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(site.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(site.cell / p.width);
        if (dx * dx + dy * dy
            < static_cast<std::int64_t>(gap) * static_cast<std::int64_t>(gap))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

void make_tribulation_sites(World& world)
{
    const WorldParams& p = world.params();
    world.tribulation_sites.clear();
    world.tribulation_site = Grid<bool>(p.width, p.height, false);

    // 候选：福地，且没有旧伤疤。
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.land_type.at(i) != LandType::blessed)
            {
                continue;
            }
            if (world.scar_kind.at(i) != Scar::none)
            {
                continue;
            }
            candidates.push_back(i);
        }
    }
    if (candidates.empty())
    {
        return;
    }

    // 按浓度降序排。同浓度时保留下标次序，排序结果因此稳定。
    std::ranges::stable_sort(candidates, [&world](std::uint32_t a,
                                                  std::uint32_t b) {
        return world.domain_concentration.at(a)
               > world.domain_concentration.at(b);
    });

    // 间距不够就跳过这个候选看下一个，而不是只在一小块池子里选址。
    for (const std::uint32_t cell : candidates)
    {
        if (too_close_to(world.tribulation_sites, cell, kTribulationSpacing, p))
        {
            continue;
        }
        world.tribulation_sites.push_back(TribulationSite{
            cell, kTribulationRadius, world.vein_id.at(cell)});
        world.tribulation_site.at(cell) = true;
    }
}

}  // namespace gamev2::world
```

`cell % p.width` 与 `cell / p.width` 是 `Grid` 的行主序寻址，与阶段 15、
阶段 17 的既有写法一致，不要另起一套换算。

- [ ] **Step 5: 接线 `generate_world()`**

在 `include/gamev2/world/generate.hpp` 的 `make_cave_heavens` 声明之后加：

```cpp
/// 阶段 18。读 land_type、domain_grade、domain_concentration、scar_kind，
/// 写 tribulation_site、tribulation_sites。渡劫场只落在无旧疤的福地上。
void make_tribulation_sites(World& world);
```

在 `src/world/generate.cpp` 的编排里，`make_cave_heavens(world);` 之后加：

```cpp
    make_tribulation_sites(world);
```

并把 `generate.hpp` 顶部的 doxygen「世界生成的十七个阶段」改成「十八个阶段」。

- [ ] **Step 6: demo 导出与统计**

在 `src/demo/main.cpp`：

- 新增 PPM 导出 `tribulation_site.ppm`（分类配色：劫心与普通格两色）
- 在 `print_spirit_stats` 里加一行：渡劫场数量
- 在导出的统计里写明**图幅尺寸**——2026-10-21 拍板第 7 条要求调用方知道自己
  拿的是哪一口径的统计量（设计文档 §9.2）

- [ ] **Step 7: 确定性测试扩展**

在 `tests/test_world_determinism.cpp` 的逐字段比较里加：

```cpp
    CHECK(a.tribulation_sites.size() == b.tribulation_sites.size());
    for (std::size_t i = 0; i < a.tribulation_sites.size(); ++i)
    {
        CHECK(a.tribulation_sites[i].cell == b.tribulation_sites[i].cell);
        CHECK(a.tribulation_sites[i].radius == b.tribulation_sites[i].radius);
        CHECK(a.tribulation_sites[i].vein == b.tribulation_sites[i].vein);
    }
```

并把 `tribulation_site` 网格纳入逐格比较。

- [ ] **Step 8: 跑全量测试**

```powershell
cmake --build build
./build/worldgen_tests.exe
```

Expected: 全部测试通过。**尤其注意** `vein_counts_match_the_setting_exactly`
（1 龙 / 9 主 / 40 支）——阶段 18 只读不写，不应影响灵脉数量。若它挂了，
说明阶段 18 写回了不该写的字段。

- [ ] **Step 9: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/world/tribulation.cpp
git add include/gamev2/world/spirit.hpp
git add include/gamev2/world/world.hpp
git add include/gamev2/world/generate.hpp
git add include/gamev2/world/spirit_tables.hpp
git add src/world/generate.cpp
git add src/world/land.cpp
git add src/demo/main.cpp
git add tests/test_world_tribulation.cpp
git add tests/test_world_determinism.cpp
```

`src/world/land.cpp` 在列，是因为阶段 15 的渡劫半径常量改从
`spirit_tables.hpp` 取（数值不变）。

---

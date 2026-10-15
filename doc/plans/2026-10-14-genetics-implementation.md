# 基因遗传系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 gamev2 实现一个真实遗传学内核：二倍体基因组、孟德尔显性与
多基因数量性状表达、环境修正、局部交配与重组突变、以及从采样机制中
涌现的漂变、近交衰退与瓶颈效应。

**Architecture:** `genetics/` 是纯函数内核，不依赖 ECS，全部以纯断言测试验证。
每物种用位点激活掩码区分，全局共享一张 300 位点的蓝图。繁殖季结算是串行
确定性的五个步骤，子代先入 staging 缓冲再原子提交。`ecs/adapt/` 是薄适配层，
只做「取组件 → 调纯函数 → 写回」。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-14-genetics-design.md`

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 错误处理用 `std::expected`，不手搓错误码 + 出参
- 输出用 `std::print` / `std::println`（`<print>`），不用 printf / cout
- 容器操作用 `std::ranges` 算法与视图
- 编译期约束用 `consteval` / `constexpr` / `if consteval`
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- 任何一行不得超过 90 个字符，超长行必须手动折行
- Windows 环境下通过 `pwsh` 调用终端
- 不得执行 `git commit`，提交时机由用户决定

**唯一允许修改的既有文件**（共四个，其余一律只读）：

| 文件 | 改动 | 见 |
|---|---|---|
| `CMakeLists.txt` | 追加源文件与目标 | Task 1 / Task 12 |
| `include/gamev2/ecs/command_buffer.hpp` | 增加 `create(EntityId&)` | Task 12 Step 3 |
| `src/ecs/command_buffer.cpp` | `flush` 跳过空 target 的失效判据 | Task 12 Step 3 |
| `include/gamev2/ecs/world.hpp` | 增加 `for_each_entity`（纯增量） | Task 12 Step 5 |

后两处是遗传学接入 ECS 的必要条件：`World::create()` 禁止在 System 执行期
调用，而 `CommandBuffer` 没有创建实体的命令；`View` 的回调只给组件引用、
不给实体句柄，而 `cmd.destroy` 需要句柄。设计依据见
[设计文档 §8.1](../2026-10-14-genetics-design.md)。

构建命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

运行测试：

```powershell
& .\build\genetics_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

已知工具链限制：`std::views::sort` 在该 STL 上不可用，需改用
`std::ranges::sort`。

## 文件结构

| 路径 | 职责 |
|---|---|
| `CMakeLists.txt` | 追加 genetics 库与 genetics_tests 目标 |
| `include/gamev2/genetics/random.hpp` | 确定性随机子流与无偏采样原语 |
| `include/gamev2/genetics/locus.hpp` | 位点表与配置常量 |
| `src/genetics/locus.cpp` | 配置校验 `LocusTable::validate` |
| `include/gamev2/genetics/genome.hpp` | AlleleCall / Haplotype / Genome |
| `include/gamev2/genetics/species.hpp` | SpeciesId / SpeciesMask / SpeciesRegistry |
| `src/genetics/species.cpp` | 物种注册表实现 |
| `include/gamev2/genetics/phenotype.hpp` | TraitKind / Phenotype |
| `include/gamev2/genetics/environment.hpp` | EnvironmentSnapshot |
| `include/gamev2/genetics/expression.hpp` | f(G) ⊕ g(E) |
| `src/genetics/expression.cpp` | 表达层实现 |
| `include/gamev2/genetics/breeding.hpp` | 重组 / 突变 / 亲代抽样 / 子代产生 |
| `src/genetics/breeding.cpp` | 繁殖层实现 |
| `include/gamev2/genetics/survival.hpp` | 生存率约束与适应力 |
| `src/genetics/survival.cpp` | 生理约束实现 |
| `include/gamev2/genetics/statistics.hpp` | 等位基因频率 / 杂合度 / F / Ne |
| `src/genetics/statistics.cpp` | 群体统计实现 |
| `include/gamev2/genetics/population.hpp` | 繁殖季结算与 staging 提交 |
| `src/genetics/population.cpp` | 结算实现 |
| `include/gamev2/ecs/adapt/genetics_systems.hpp` | ECS 组件与三个系统 |
| `src/ecs/adapt/genetics_systems.cpp` | 系统实现 |
| `tests/test_genetics_smoke.cpp` | 构建冒烟 |
| `tests/test_genetics_locus.cpp` | 位点表与校验 |
| `tests/test_genetics_species.cpp` | 物种注册表 |
| `tests/test_genetics_expression.cpp` | 显隐性 / 多基因 / 环境 |
| `tests/test_genetics_random.cpp` | 随机子流与无偏采样 |
| `tests/test_genetics_breeding.cpp` | 重组 / 突变 / 交配 |
| `tests/test_genetics_survival.cpp` | 生存约束 |
| `tests/test_genetics_statistics.cpp` | 频率 / 杂合度 / F / Ne |
| `tests/test_genetics_population.cpp` | 繁殖季结算 |
| `tests/test_genetics_determinism.cpp` | 确定性 / 近交 / 漂变 |
| `tests/test_genetics_performance.cpp` | 1 万个体单季结算预算 |
| `tests/test_genetics_systems.cpp` | ECS 适配层 |

**包含依赖方向**（无环）：

```
random.hpp         ← 无
phenotype.hpp      ← 无
environment.hpp    ← 无
genome.hpp         ← 无
locus.hpp          ← genome.hpp, phenotype.hpp
species.hpp        ← locus.hpp
breeding.hpp       ← genome.hpp, locus.hpp, random.hpp, species.hpp
survival.hpp       ← phenotype.hpp, environment.hpp, locus.hpp
expression.hpp     ← genome.hpp, locus.hpp, phenotype.hpp, environment.hpp,
                      species.hpp
statistics.hpp     ← genome.hpp, locus.hpp, species.hpp
population.hpp     ← breeding.hpp, expression.hpp, species.hpp,
                      statistics.hpp, survival.hpp
```

**包含依赖方向**（ecs/adapt，无环）：

```
ecs/adapt/genetics_systems.hpp
    ← command_buffer.hpp, component.hpp, entity.hpp, view.hpp, world.hpp,
      breeding.hpp, expression.hpp, phenotype.hpp, population.hpp,
      species.hpp, survival.hpp
```

---

### Task 1: 构建骨架与 genetics 目标

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/test_genetics_smoke.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - CMake 目标 `genetics`（静态库）
  - CMake 目标 `genetics_tests`（可执行文件，链接 `genetics`）
  - 复用既有 `tests/test_framework.hpp` 的 `TEST` / `CHECK` / `CHECK_EQ`

- [ ] **Step 1: 向 CMakeLists.txt 追加 genetics 库与测试目标**

在 `set(ECS_ASAN_SOURCES)` 之前的 `option(GAMEV2_ENABLE_ASAN ...)` 段之前，
紧接 `target_link_libraries(ecs_tests PRIVATE ecs)` 之后追加：

```cmake
set(GENETICS_SOURCES
    src/genetics/locus.cpp
    src/genetics/species.cpp
    src/genetics/expression.cpp
    src/genetics/breeding.cpp
    src/genetics/survival.cpp
    src/genetics/statistics.cpp
    src/genetics/population.cpp
)
add_library(genetics STATIC ${GENETICS_SOURCES})
target_include_directories(genetics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(genetics PUBLIC ecs)

set(GENETICS_TEST_SOURCES
    tests/test_genetics_smoke.cpp
)
add_executable(genetics_tests ${GENETICS_TEST_SOURCES})
target_link_libraries(genetics_tests PRIVATE genetics)
```

`target_link_libraries(genetics PUBLIC ecs)` 是为了让 genetics_tests 拿到 ECS
头文件与 `ecs` 库。genetics 库自身不引用任何 ECS 符号，这一条只是为了
让适配层的测试目标有完整链接环境。

注意：`GAMEV2_ENABLE_ASAN` 段里的 `foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES)`
需要同步加上 genetics 的两个列表，否则 ASan 构建不会覆盖新模块。把它改为：

```cmake
    foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES
                  GENETICS_SOURCES GENETICS_TEST_SOURCES)
```

- [ ] **Step 2: 创建 tests/test_genetics_smoke.cpp**

```cpp
#include "test_framework.hpp"

TEST(genetics_smoke_builds)
{
    CHECK(true);
}
```

- [ ] **Step 3: 补齐 genetics 源文件占位**

后续任务会逐个填充这些文件，但 CMake 在第一次配置时就会检查它们存在。
先各建一个只有许可证注释的空文件：

`src/genetics/locus.cpp`、`src/genetics/species.cpp`、
`src/genetics/expression.cpp`、`src/genetics/breeding.cpp`、
`src/genetics/survival.cpp`、`src/genetics/statistics.cpp`、
`src/genetics/population.cpp`

每个文件内容为：

```cpp
// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
```

- [ ] **Step 4: 构建并运行**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `1 test(s), 0 failed`，`exit=0`。

---

### Task 2: 位点表与配置校验

**Files:**
- Create: `include/gamev2/genetics/phenotype.hpp`
- Create: `include/gamev2/genetics/locus.hpp`
- Create: `src/genetics/locus.cpp`
- Create: `tests/test_genetics_locus.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 的 `genetics_tests` 目标
- Produces:
  - `gamev2::genetics::kLocusCount` / `kMaxAlleles` / `kTraitCount`
  - `AlleleId`（`std::uint8_t`）
  - `LocusKind`（`Trait` / `Structural`）
  - `AlleleDef { additive, state, dom_rank }`
  - `LocusDef { kind, allele_count, trait, scale, dominance, switch_rate, alleles }`
  - `LocusTable { std::array<LocusDef, kLocusCount> loci }`
  - `TraitKind` 枚举与 `kTraitCount`
  - `GeneticsConfig` 结构体
  - `LocusConfigError` 枚举
  - `std::expected<void, LocusConfigError> validate(const LocusTable&,
    const GeneticsConfig&)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_locus.cpp`：

```cpp
#include "gamev2/genetics/locus.hpp"

#include "test_framework.hpp"

#include <algorithm>

using namespace gamev2::genetics;

namespace {

/// 构造一个只有前 n 个位点被填好的表，其余位点 allele_count 为 0。
LocusTable blank_table()
{
    return LocusTable{};
}

void fill_trait_loci(LocusTable& table, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        auto& l = table.loci[i];
        l.kind = LocusKind::Trait;
        l.allele_count = 2;
        l.target = 0;
        l.scale = 1.0f;
        l.dominance = 0.0f;
        l.switch_rate = 0.01f;
        l.alleles[0].additive = 1.0f;
        l.alleles[0].dom_rank = 0;
        l.alleles[1].additive = -1.0f;
        l.alleles[1].dom_rank = 1;
    }
}

}  // namespace

TEST(locus_table_size_is_fixed)
{
    static_assert(kLocusCount == 300);
    static_assert(kMaxAlleles == 4);
    static_assert(sizeof(LocusTable) > 0);
    CHECK_EQ(blank_table().loci.size(), kLocusCount);
}

TEST(locus_rejects_allele_count_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].allele_count = 1;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::AlleleCountOutOfRange);
}

TEST(locus_rejects_allele_count_above_max)
{
    // 上界同样是硬约束：rank_is_unique 按 allele_count 索引 4 元素的
    // alleles 数组，越界的 allele_count 会让校验器自己越界读。
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].allele_count =
        static_cast<std::uint8_t>(kMaxAlleles + 1);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::AlleleCountOutOfRange);
}

TEST(locus_rejects_duplicate_rank_on_structural)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].kind = LocusKind::Structural;
    table.loci[0].alleles[0].dom_rank = 1;
    table.loci[0].alleles[1].dom_rank = 1;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::DuplicateRank);
}

TEST(locus_rejects_trait_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].target = static_cast<std::uint8_t>(kTraitCount);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::TraitOutOfRange);
}

TEST(locus_rejects_negative_dominance)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].dominance = -0.5f;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::NegativeDominance);
}

TEST(locus_rejects_too_many_overdominant)
{
    auto table = blank_table();
    // 必须填满全部位点：validate 对每个位点都判 allele_count < 2，
    // 留空位会让循环在到达末尾的 overdominant 计数前就提前返回
    // EmptyAlleleSet，测到的就不是本用例想测的分支。
    fill_trait_loci(table, kLocusCount);
    for (std::size_t i = 0; i < 3; ++i)
    {
        table.loci[i].dominance = 1.5f;
    }
    GeneticsConfig cfg{};
    cfg.max_overdominant_loci = 2;
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::TooManyOverdominant);
}

TEST(locus_rejects_bad_switch_rate)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].switch_rate = 1.5f;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::BadSwitchRate);
}

TEST(locus_rejects_slot_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].kind = LocusKind::Structural;
    table.loci[0].target =
        static_cast<std::uint8_t>(StructuralSlot::kSlotCount);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::SlotOutOfRange);
}

TEST(locus_accepts_a_well_formed_table)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    // 用满但不超过超显配额：同时验证「合法表」与「配额恰好压到边界」
    // 两条路径，而不是让阈值形同虚设。
    GeneticsConfig cfg{};
    cfg.max_overdominant_loci = 8;
    for (std::size_t i = 0; i < cfg.max_overdominant_loci; ++i)
    {
        table.loci[i].dominance = 1.5f;
    }
    const auto r = validate(table, cfg);
    CHECK(r.has_value());
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
& .\build\genetics_tests.exe
```

Expected: 编译失败，`gamev2/genetics/locus.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/phenotype.hpp**

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::genetics {

enum class TraitKind : std::uint8_t
{
    BodySize,
    Speed,
    Strength,
    Metabolism,
    Lifespan,
    SenseRange,
    Fecundity,
    Resilience,
    kTraitCount
};

inline constexpr std::size_t kTraitCount =
    static_cast<std::size_t>(TraitKind::kTraitCount);

[[nodiscard]] constexpr std::size_t trait_index(TraitKind t) noexcept
{
    return static_cast<std::size_t>(t);
}

/// 固定结构目录：字段集合编译期固定，才能定长进 ECS。
struct Phenotype
{
    std::array<float, kTraitCount> traits{};

    std::uint8_t body_segments = 1;
    std::uint8_t limb_pairs = 0;
    std::uint8_t eyes = 0;
    std::uint8_t flags = 0;  ///< bit 0 壳 / 1 翼 / 2 鳍 / 3 腺体
};

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 include/gamev2/genetics/locus.hpp**

```cpp
#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace gamev2::genetics {

inline constexpr std::size_t kLocusCount = 300;
inline constexpr std::size_t kMaxAlleles = 4;

using AlleleId = std::uint8_t;  ///< 位点内索引，不是全局编号

enum class LocusKind : std::uint8_t
{
    Trait,       ///< 数量性状位点，贡献到一个连续性状轴
    Structural,  ///< 结构位点，解析为离散形态状态
};

/// 固定结构目录的槽位。槽位集合编译期固定，才能定长进 ECS。
enum class StructuralSlot : std::uint8_t
{
    BodySegments,
    LimbPairs,
    Eyes,
    Flags,  ///< 壳 / 翼 / 鳍 / 腺体的位域
    kSlotCount
};

struct AlleleDef
{
    float additive = 0.0f;    ///< 加性取值 v_i
    std::uint8_t state = 0;   ///< 结构态编码，Trait 位点为 0
    std::uint8_t dom_rank = 0;///< 显性等级，越大越显
};

struct LocusDef
{
    LocusKind kind = LocusKind::Trait;
    std::uint8_t allele_count = 0;
    /// 目标索引，含义随 kind 而变：Trait 位点是 TraitKind 轴，
    /// Structural 位点是 StructuralSlot。两者分列两个字段会多出一个
    /// 必须保持同步的冗余状态,而这里一个字段就够。
    std::uint8_t target = 0;
    float scale = 0.0f;       ///< 该位点在该轴上的权重
    float dominance = 0.0f;   ///< 显性系数 h，须 >= 0
    /// 每代发生突变的概率。不是「替换等位基因的概率」——后者会让突变率恒为 1。
    float switch_rate = 0.0f;
    std::array<AlleleDef, kMaxAlleles> alleles{};
};

/// 表达层与突变中需要调平衡的标量，随配置加载一次。
struct GeneticsConfig
{
    std::array<float, kTraitCount> trait_base{};
    std::array<float, kTraitCount> trait_min{};
    std::array<float, kTraitCount> trait_max{};
    std::int8_t jitter_step = 1;
    /// 1 单位 jitter 对应的 additive 偏移。int8_t 与 float 之间必须写明
    /// 比例尺，否则同一 jitter 值在不同位点上表示量级完全不同的效应。
    float jitter_unit = 0.01f;
    float allele_switch_share = 0.05f;
    /// 以下三项是 g(E) 的系数：环境偏移映射到性状轴的强度。
    float temperature_metabolic_load = 0.1f;
    float fertility_fecundity_gain = 1.0f;
    float light_sense_gain = 1.0f;
    float thermal_base_width = 1.0f;
    float thermal_resilience_gain = 1.0f;
    float escape_responsiveness = 1.0f;
    std::uint8_t max_overdominant_loci = 8;
};

/// 所有物种共享同一张蓝图。物种差异体现为等位基因分布与激活掩码。
struct LocusTable
{
    std::array<LocusDef, kLocusCount> loci{};
};

enum class LocusConfigError
{
    /// allele_count 不在 [2, kMaxAlleles]。上界同样是硬约束：
    /// 校验器按 allele_count 索引定长的 alleles 数组，越界会让自己越界读。
    AlleleCountOutOfRange,
    DuplicateRank,
    SlotOutOfRange,  ///< Structural 位点的 target 越界
    TraitOutOfRange, ///< Trait 位点的 target 越界
    NegativeDominance,
    BadSwitchRate,
    TooManyOverdominant,
};

/// 只在加载配置时调用一次。热路径零校验：配置期不成立的条件是配置错误，
/// 应当在启动时失败，而不是在第 3 季第 4000 个个体身上失败。
[[nodiscard]] std::expected<void, LocusConfigError>
validate(const LocusTable& table, const GeneticsConfig& config);

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 创建 include/gamev2/genetics/genome.hpp**

`Genome` 住在 `genome.hpp` 而不是 `locus.hpp`：基因组结构不含任何位点表
语义，反过来让 `locus.hpp` 包含 `genome.hpp` 才对——`AlleleId` 与
`kLocusCount` 是 `LocusDef` 与 `AlleleCall` 共同的基础。

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::genetics {

inline constexpr std::size_t kLocusCount = 300;
inline constexpr std::size_t kMaxAlleles = 4;

using AlleleId = std::uint8_t;  ///< 位点内索引，不是全局编号

/// jitter 是等位基因取值上的有界微扰，使 Trait 位点的突变成为平滑步进
/// 而非在离散等位基因间跳变；Structural 位点忽略它。
struct AlleleCall
{
    AlleleId allele = 0;
    std::int8_t jitter = 0;
};

using Haplotype = std::array<AlleleCall, kLocusCount>;

/// 单倍型拆成两个数组而非交错存储，是为了让重组（按单倍型整体交叉）
/// 与统计（按单倍型遍历）都能顺序访问。
struct Genome
{
    Haplotype a{};
    Haplotype b{};
};

}  // namespace gamev2::genetics
```

本步同时回头修正 Step 4 的 `locus.hpp`：删掉它顶部重复定义的
`kLocusCount` / `kMaxAlleles` / `AlleleId`（它们现在住在
`genome.hpp`），并把首行包含改为：

```cpp
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"
```

不删则两个头文件各自定义同一常量，编译期即报重定义。

- [ ] **Step 6: 创建 src/genetics/locus.cpp**

```cpp
#include "gamev2/genetics/locus.hpp"

#include <algorithm>
#include <cstdint>

namespace gamev2::genetics {

namespace {

bool rank_is_unique(const LocusDef& locus)
{
    for (std::size_t i = 0; i < locus.allele_count; ++i)
    {
        for (std::size_t j = i + 1; j < locus.allele_count; ++j)
        {
            if (locus.alleles[i].dom_rank == locus.alleles[j].dom_rank)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

std::expected<void, LocusConfigError>
validate(const LocusTable& table, const GeneticsConfig& config)
{
    std::size_t overdominant = 0;

    for (const auto& locus : table.loci)
    {
        // 上下界一起判：上界漏掉时，rank_is_unique 会按 allele_count
        // 索引 4 元素的 alleles 数组，越界读发生在校验器自己身上。
        if (locus.allele_count < 2
            || locus.allele_count > static_cast<std::uint8_t>(kMaxAlleles))
        {
            return std::unexpected(LocusConfigError::AlleleCountOutOfRange);
        }
        if (locus.kind == LocusKind::Structural && !rank_is_unique(locus))
        {
            return std::unexpected(LocusConfigError::DuplicateRank);
        }
        if (locus.kind == LocusKind::Trait
            && locus.target >= static_cast<std::uint8_t>(kTraitCount))
        {
            return std::unexpected(LocusConfigError::TraitOutOfRange);
        }
        // 槽位合法性必须在这里挡住：表达层的 write_slot 对四个槽位是
        // 无分支穷举的，越界值到了那里只会静默丢弃。
        if (locus.kind == LocusKind::Structural
            && locus.target
                   >= static_cast<std::uint8_t>(StructuralSlot::kSlotCount))
        {
            return std::unexpected(LocusConfigError::SlotOutOfRange);
        }
        if (locus.dominance < 0.0f)
        {
            return std::unexpected(LocusConfigError::NegativeDominance);
        }
        if (locus.switch_rate < 0.0f || locus.switch_rate > 1.0f)
        {
            return std::unexpected(LocusConfigError::BadSwitchRate);
        }
        if (locus.dominance > 1.0f)
        {
            ++overdominant;
        }
    }

    if (overdominant > config.max_overdominant_loci)
    {
        return std::unexpected(LocusConfigError::TooManyOverdominant);
    }
    return {};
}

}  // namespace gamev2::genetics
```

- [ ] **Step 7: 注册测试文件并运行**

在 `CMakeLists.txt` 的 `GENETICS_TEST_SOURCES` 里
`tests/test_genetics_smoke.cpp` 之后追加：

```cmake
    tests/test_genetics_locus.cpp
```

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `14 test(s), 0 failed`。

---

### Task 3: 物种注册表

**Files:**
- Create: `include/gamev2/genetics/species.hpp`
- Create: `src/genetics/species.cpp`
- Create: `tests/test_genetics_species.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 的 `kLocusCount` / `LocusTable` / `AlleleDef`
- Produces:
  - `SpeciesId { std::uint32_t value = 0 }`
  - `SpeciesMask { std::bitset<kLocusCount> active }`
  - `SpeciesError { EmptyMask, TooManySpecies }`
  - `class SpeciesRegistry`：`register_species(SpeciesMask)` →
    `std::expected<SpeciesId, SpeciesError>`；`mask(SpeciesId)` →
    `const SpeciesMask&`；`species_count()` → `std::size_t`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_species.cpp`：

```cpp
#include "gamev2/genetics/species.hpp"

#include "test_framework.hpp"

using namespace gamev2::genetics;

namespace {

SpeciesMask mask_of(std::initializer_list<std::size_t> loci)
{
    SpeciesMask m{};
    for (auto l : loci)
    {
        m.active.set(l);
    }
    return m;
}

}  // namespace

TEST(species_rejects_empty_mask)
{
    SpeciesRegistry registry;
    const auto r = registry.register_species(SpeciesMask{});
    CHECK(!r.has_value());
    CHECK(r.error() == SpeciesError::EmptyMask);
}

TEST(species_register_returns_stable_ids)
{
    SpeciesRegistry registry;
    const auto a = registry.register_species(mask_of({0, 1, 2}));
    const auto b = registry.register_species(mask_of({3, 4}));
    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(a->value != b->value);
    CHECK_EQ(registry.species_count(), std::size_t{2});
}

TEST(species_mask_roundtrips)
{
    SpeciesRegistry registry;
    const auto id = registry.register_species(mask_of({7, 8, 9}));
    CHECK(id.has_value());
    const auto& m = registry.mask(*id);
    CHECK(m.active.test(7));
    CHECK(m.active.test(8));
    CHECK(m.active.test(9));
    CHECK(!m.active.test(10));
}

TEST(species_default_id_is_invalid)
{
    const SpeciesId id{};
    CHECK(!static_cast<bool>(id));
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/species.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/species.hpp**

```cpp
#pragma once

#include "gamev2/genetics/genome.hpp"

#include <bitset>
#include <cstdint>
#include <expected>
#include <vector>

namespace gamev2::genetics {

struct SpeciesId
{
    std::uint32_t value = 0;

    constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }

    friend constexpr bool operator==(SpeciesId, SpeciesId) noexcept = default;
};

/// 物种之间共享同一张位点蓝图，差异只体现在「启用哪些位点」。
struct SpeciesMask
{
    std::bitset<kLocusCount> active{};
};

enum class SpeciesError
{
    /// 没有激活任何位点的物种无法产生表型，也无法参与表达层
    EmptyMask,
    TooManySpecies,
};

/// 物种在本轮是外部给定的静态标签。形成新物种属于下一份 spec；
/// 这里只保证位点掩码的接口在那里。
class SpeciesRegistry
{
public:
    [[nodiscard]] std::expected<SpeciesId, SpeciesError>
    register_species(SpeciesMask mask);

    [[nodiscard]] const SpeciesMask& mask(SpeciesId id) const;

    [[nodiscard]] std::size_t species_count() const noexcept
    {
        return masks_.size();
    }

private:
    /// 下标 0 是保留的非法值，因此 id 从 1 起，mask 数组下标用 value - 1。
    std::vector<SpeciesMask> masks_;
};

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 src/genetics/species.cpp**

```cpp
#include "gamev2/genetics/species.hpp"

namespace gamev2::genetics {

namespace {
/// 上百个物种时全量逐位点统计不可行，注册数上限把这条路径挡住。
constexpr std::size_t kMaxSpecies = 4096;
}  // namespace

std::expected<SpeciesId, SpeciesError>
SpeciesRegistry::register_species(SpeciesMask mask)
{
    if (mask.active.none())
    {
        return std::unexpected(SpeciesError::EmptyMask);
    }
    if (masks_.size() >= kMaxSpecies)
    {
        return std::unexpected(SpeciesError::TooManySpecies);
    }
    masks_.push_back(mask);
    return SpeciesId{static_cast<std::uint32_t>(masks_.size())};
}

const SpeciesMask& SpeciesRegistry::mask(SpeciesId id) const
{
    return masks_[id.value - 1];
}

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_species.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `18 test(s), 0 failed`。

---

### Task 4: 表达层——Trait 位点显性取值

**Files:**
- Create: `include/gamev2/genetics/environment.hpp`
- Create: `include/gamev2/genetics/expression.hpp`
- Create: `src/genetics/expression.cpp`
- Create: `tests/test_genetics_expression.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 的 `LocusDef` / `AlleleDef` / `GeneticsConfig` / `kLocusCount`
- Produces:
  - `EnvironmentSnapshot { float temperature, fertility, light }`
  - `float trait_value(const LocusDef&, AlleleId x, std::int8_t jx,
    AlleleId y, std::int8_t jy, const GeneticsConfig&)`
  - `std::uint8_t structural_state(const LocusDef&, AlleleId x, AlleleId y)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_expression.cpp`，本任务只放 Trait 与 Structural
的取值函数测试：

```cpp
#include "gamev2/genetics/expression.hpp"

#include "test_framework.hpp"

using namespace gamev2::genetics;

namespace {

LocusDef make_trait_locus(float v0, float v1, std::uint8_t r0,
                          std::uint8_t r1, float h)
{
    LocusDef l{};
    l.kind = LocusKind::Trait;
    l.allele_count = 2;
    l.scale = 1.0f;
    l.dominance = h;
    l.switch_rate = 0.0f;
    l.alleles[0].additive = v0;
    l.alleles[0].dom_rank = r0;
    l.alleles[1].additive = v1;
    l.alleles[1].dom_rank = r1;
    return l;
}

LocusDef make_structural_locus(std::uint8_t s0, std::uint8_t r0,
                               std::uint8_t s1, std::uint8_t r1)
{
    LocusDef l{};
    l.kind = LocusKind::Structural;
    l.allele_count = 2;
    l.alleles[0].state = s0;
    l.alleles[0].dom_rank = r0;
    l.alleles[1].state = s1;
    l.alleles[1].dom_rank = r1;
    return l;
}

GeneticsConfig no_jitter()
{
    GeneticsConfig c{};
    c.jitter_unit = 0.0f;
    return c;
}

}  // namespace

TEST(trait_homozygote_is_allele_value)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 0.0f);
    const auto v = trait_value(l, 0, 0, 0, 0, no_jitter());
    CHECK(v == 1.0f);
}

TEST(trait_additive_heterozygote_is_midpoint)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 0.0f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == 0.0f);
}

TEST(trait_complete_dominance_matches_homozygote)
{
    // 高等级等位基因为 1（值 -1），低等级为 0（值 1）。
    // h = 1 时杂合子应与显性纯合子同值，即 -1。
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 1.0f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == -1.0f);
}

TEST(trait_dominance_symmetric_in_argument_order)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 1.0f);
    const auto v = trait_value(l, 1, 0, 0, 0, no_jitter());
    CHECK(v == -1.0f);
}

TEST(trait_overdominance_beats_both_homozygotes)
{
    // 超显要让杂合子越过显性纯合子，显性方就必须是取值更高的那一个。
    // rank 0 高于 rank 1 时：杂合子 = +1.5 > homo0 = +1.0 > homo1 = -1.0。
    const auto l = make_trait_locus(1.0f, -1.0f, 1, 0, 1.5f);
    const auto het = trait_value(l, 0, 0, 1, 0, no_jitter());
    const auto homo0 = trait_value(l, 0, 0, 0, 0, no_jitter());
    const auto homo1 = trait_value(l, 1, 0, 1, 0, no_jitter());
    CHECK(het > homo0);
    CHECK(het > homo1);
}

TEST(trait_equal_rank_is_pure_additive)
{
    // 等级相同意味着共显，杂合子必须回到中点，与 h 无关。
    const auto l = make_trait_locus(1.0f, -1.0f, 3, 3, 1.5f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == 0.0f);
}

TEST(trait_jitter_shifts_additive_value)
{
    // 完全显性下杂合子等于显性方的取值，jitter 因此整额传导；
    // 若用纯合子，中点只吸收一半位移，测的就不是「整额传导」。
    auto l = make_trait_locus(1.0f, -1.0f, 1, 0, 1.0f);
    GeneticsConfig c{};
    c.jitter_unit = 0.1f;
    const auto base = trait_value(l, 0, 0, 1, 0, c);
    const auto jittered = trait_value(l, 0, 3, 1, 0, c);
    CHECK(jittered == base + 0.3f);
}

TEST(structural_homozygote_uses_own_state)
{
    const auto l = make_structural_locus(4, 0, 2, 1);
    CHECK_EQ(structural_state(l, 0, 0), std::uint8_t{4});
    CHECK_EQ(structural_state(l, 1, 1), std::uint8_t{2});
}

TEST(structural_heterozygote_uses_dominant_state)
{
    const auto l = make_structural_locus(4, 1, 2, 0);
    CHECK_EQ(structural_state(l, 0, 1), std::uint8_t{4});
    CHECK_EQ(structural_state(l, 1, 0), std::uint8_t{4});
}

TEST(phenotype_environment_changes_trait_axes)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 0.0f;  // 纯环境项，隔离 g(E)
    }
    GeneticsConfig config{};
    config.trait_base.fill(1.0f);
    config.trait_min.fill(-100.0f);
    config.trait_max.fill(100.0f);

    const auto mild = EnvironmentSnapshot{0.0f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{5.0f, 0.2f, 0.1f};

    const auto a = phenotype_of(Genome{}, table, all_loci(), mild, config);
    const auto b = phenotype_of(Genome{}, table, all_loci(), harsh, config);

    CHECK(a.traits[trait_index(TraitKind::Metabolism)]
          < b.traits[trait_index(TraitKind::Metabolism)]);
    CHECK(a.traits[trait_index(TraitKind::Fecundity)]
          > b.traits[trait_index(TraitKind::Fecundity)]);
    CHECK(a.traits[trait_index(TraitKind::SenseRange)]
          > b.traits[trait_index(TraitKind::SenseRange)]);
}

TEST(express_accumulates_loci_into_their_own_axes)
{
    // 每个轴至少两个位点：每轴只放一个时，「累加」与「后者胜」
    // 在观测上不可分，这个用例就测不到 express 的核心职责。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 2;
        table.loci[i].dominance = 0.0f;
    }
    // loci[0..1] -> BodySize, loci[2..3] -> Speed，各带不同 scale
    const float kBodyBase[2] = {0.5f, -1.0f};
    const float kSpeedBase[2] = {2.0f, 0.25f};
    for (std::size_t i = 0; i < 4; ++i)
    {
        table.loci[i].target = (i < 2) ? 0 : 1;
        table.loci[i].scale = (i % 2 == 0) ? 1.0f : 3.0f;
        table.loci[i].alleles[0].additive = kBodyBase[i % 2];
        table.loci[i].alleles[1].additive = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.trait_base.fill(10.0f);
    config.trait_min.fill(-1000.0f);
    config.trait_max.fill(1000.0f);

    // 纯合子 0/0，h=0 时取值 = additive
    Genome g{};
    const auto p = phenotype_of(g, table, all_loci(),
                                 EnvironmentSnapshot{}, config);

    const float body = 10.0f + 1.0f * 0.5f + 3.0f * (-1.0f);
    const float speed = 10.0f + 1.0f * 2.0f + 3.0f * 0.25f;
    CHECK(p.traits[trait_index(TraitKind::BodySize)] == body);
    CHECK(p.traits[trait_index(TraitKind::Speed)] == speed);
}

TEST(phenotype_environment_never_touches_structure)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Structural;
        table.loci[i].allele_count = 2;
        table.loci[i].target = 0;
        table.loci[i].alleles[0].state = 5;
        table.loci[i].alleles[0].dom_rank = 1;
        table.loci[i].alleles[1].state = 2;
        table.loci[i].alleles[1].dom_rank = 0;
    }
    GeneticsConfig config{};
    config.trait_base.fill(1.0f);
    config.trait_min.fill(-100.0f);
    config.trait_max.fill(100.0f);

    const auto mild = EnvironmentSnapshot{0.0f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{9.0f, 0.0f, 0.0f};

    const auto a = phenotype_of(Genome{}, table, all_loci(), mild, config);
    const auto b = phenotype_of(Genome{}, table, all_loci(), harsh, config);

    CHECK_EQ(a.body_segments, b.body_segments);
    CHECK_EQ(a.limb_pairs, b.limb_pairs);
    CHECK_EQ(a.eyes, b.eyes);
    CHECK_EQ(a.flags, b.flags);
    CHECK_EQ(a.body_segments, std::uint8_t{5});
}
```

上面两个新测试需要文件顶部增加一个 helper：

```cpp
SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/expression.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/environment.hpp**

```cpp
#pragma once

namespace gamev2::genetics {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 include/gamev2/genetics/expression.hpp**

```cpp
#pragma once

#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/species.hpp"

namespace gamev2::genetics {

/// 基因型 (x, y) 在 Trait 位点上的取值。
///
/// 显性由等位基因的 dom_rank 推导而非由「谁是 A」决定——这正是「一个 A 对
/// 一个 a」的真实语义：显性是设计数据指定的等级关系。h 恒取非负，负值等价于
/// 把两个 dom_rank 对调，不构成额外自由度。
[[nodiscard]] float trait_value(const LocusDef& locus, AlleleId x,
                                 std::int8_t jx, AlleleId y,
                                 std::int8_t jy,
                                 const GeneticsConfig& config) noexcept;

/// 基因型 (x, y) 解析出的结构态编码。
///
/// 配置校验要求 Structural 位点内 dom_rank 两两相等，因此共显分支实际不会
/// 触发；写它只为让规则完备，不留未定义路径。
[[nodiscard]] std::uint8_t structural_state(const LocusDef& locus,
                                            AlleleId x,
                                            AlleleId y) noexcept;

/// f(G)：基因型到表型的映射，忽略环境。
///
/// 环境修正在此之后单独施加（见 apply_environment）：基因效应的夹取必须
/// 先于环境修正，否则一个极端基因型会被环境增量救回，掩盖掉本应淘汰的表型。
[[nodiscard]] Phenotype express(const Genome& genome,
                                const LocusTable& table,
                                const SpeciesMask& mask,
                                const GeneticsConfig& config);

/// g(E)：环境只修正性状轴，不触碰结构字段。
void apply_environment(Phenotype& phenotype,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config);

/// P = f(G) ⊕ g(E)
[[nodiscard]] Phenotype phenotype_of(const Genome& genome,
                                     const LocusTable& table,
                                     const SpeciesMask& mask,
                                     const EnvironmentSnapshot& environment,
                                     const GeneticsConfig& config);

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 创建 src/genetics/expression.cpp**

```cpp
#include "gamev2/genetics/expression.hpp"

#include <algorithm>
#include <cmath>

namespace gamev2::genetics {

namespace {

float allele_value(const AlleleDef& allele, std::int8_t jitter,
                   const GeneticsConfig& config) noexcept
{
    return allele.additive + static_cast<float>(jitter) * config.jitter_unit;
}

void write_slot(Phenotype& p, const LocusDef& locus,
                std::uint8_t state) noexcept{
    // 四个槽位穷举、无静默丢弃分支：target 的合法性由 validate 保证，
    // 越界的配置在启动时就该失败，而不是走到这里被吃掉。
    switch (static_cast<StructuralSlot>(locus.target))
    {
    case StructuralSlot::BodySegments:
        p.body_segments = state;
        break;
    case StructuralSlot::LimbPairs:
        p.limb_pairs = state;
        break;
    case StructuralSlot::Eyes:
        p.eyes = state;
        break;
    case StructuralSlot::Flags:
        p.flags = state;
        break;
    default:
        std::unreachable();
    }
}

}  // namespace

float trait_value(const LocusDef& locus, AlleleId x, std::int8_t jx,
                  AlleleId y, std::int8_t jy,
                  const GeneticsConfig& config) noexcept
{
    const AlleleDef& ax = locus.alleles[x];
    const AlleleDef& ay = locus.alleles[y];

    const float vx = allele_value(ax, jx, config);
    const float vy = allele_value(ay, jy, config);

    const float m = (vx + vy) * 0.5f;
    const float a = (vx - vy) * 0.5f;

    if (ax.dom_rank == ay.dom_rank)
    {
        return m;
    }
    const float h_eff = (ax.dom_rank > ay.dom_rank) ? locus.dominance
                                                     : -locus.dominance;
    return m + h_eff * a;
}

std::uint8_t structural_state(const LocusDef& locus, AlleleId x,
                              AlleleId y) noexcept
{
    const AlleleDef& ax = locus.alleles[x];
    const AlleleDef& ay = locus.alleles[y];

    if (ax.dom_rank == ay.dom_rank)
    {
        return std::max(ax.state, ay.state);
    }
    return (ax.dom_rank > ay.dom_rank) ? ax.state : ay.state;
}

Phenotype express(const Genome& genome, const LocusTable& table,
                  const SpeciesMask& mask, const GeneticsConfig& config)
{
    Phenotype p{};
    p.traits = config.trait_base;

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        if (!mask.active.test(i))
        {
            continue;
        }
        const LocusDef& locus = table.loci[i];

        if (locus.kind == LocusKind::Trait)
        {
            const float v = trait_value(locus, genome.a[i].allele,
                                        genome.a[i].jitter,
                                        genome.b[i].allele,
                                        genome.b[i].jitter, config);
            p.traits[trait_index(static_cast<TraitKind>(locus.target))] +=
                locus.scale * v;
        }
        else
        {
            write_slot(p, locus,
                       structural_state(locus, genome.a[i].allele,
                                        genome.b[i].allele));
        }
    }

    for (std::size_t t = 0; t < kTraitCount; ++t)
    {
        p.traits[t] = std::clamp(p.traits[t], config.trait_min[t],
                                 config.trait_max[t]);
    }
    return p;
}

void apply_environment(Phenotype& phenotype,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config)
{
    // 温度偏离适宜区抬高代谢：温度应激是真实的代谢负担，它经由
    // Metabolism 传导到能量收支约束，因此不必另设一项约束。
    phenotype.traits[trait_index(TraitKind::Metabolism)] +=
        std::fabs(environment.temperature)
        * config.temperature_metabolic_load;

    // 资源不足压低繁殖力。
    phenotype.traits[trait_index(TraitKind::Fecundity)] +=
        (environment.fertility - 1.0f) * config.fertility_fecundity_gain;

    // 光照不足削弱视觉。环境不触碰结构字段：形态有反应规范，
    // 但那会让结构位点的表达规则与遗传方差分解一起膨胀。
    phenotype.traits[trait_index(TraitKind::SenseRange)] +=
        (environment.light - 1.0f) * config.light_sense_gain;

    for (std::size_t t = 0; t < kTraitCount; ++t)
    {
        phenotype.traits[t] = std::clamp(phenotype.traits[t],
                                         config.trait_min[t],
                                         config.trait_max[t]);
    }
}

Phenotype phenotype_of(const Genome& genome, const LocusTable& table,
                       const SpeciesMask& mask,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config)
{
    Phenotype p = express(genome, table, mask, config);
    apply_environment(p, environment, config);
    return p;
}

}  // namespace gamev2::genetics
```

- [ ] **Step 6: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_expression.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `30 test(s), 0 failed`。

---

### Task 5: 确定性随机子流

**Files:**
- Create: `include/gamev2/genetics/random.hpp`
- Create: `tests/test_genetics_random.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无
- Produces:
  - `gamev2::genetics::mix_seed(std::uint64_t, std::uint64_t) -> std::uint64_t`
  - `enum class RandomPurpose` 用途标签
  - `class RandomStreams`：持有每条用途的独立 `std::mt19937_64`，
    构造参数为 `(std::uint64_t root_seed, std::uint64_t season_index)`
  - `std::mt19937_64& stream(RandomPurpose)`
  - `float uniform_unit(std::mt19937_64&) -> float`（[0,1)）
  - `std::size_t uniform_index(std::mt19937_64&, std::size_t n) -> std::size_t`
    （前置：`n > 0`）

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_random.cpp`：

```cpp
#include "gamev2/genetics/random.hpp"

#include "test_framework.hpp"

#include <array>
#include <set>

using namespace gamev2::genetics;

TEST(random_uniform_unit_stays_in_range)
{
    auto rng = std::mt19937_64{42};
    for (int i = 0; i < 10000; ++i)
    {
        const float u = uniform_unit(rng);
        CHECK(u >= 0.0f);
        CHECK(u < 1.0f);
    }
}

TEST(random_uniform_index_covers_full_range)
{
    // bound 刻意取非 2 的幂：2 的幂上取模是精确的，测不出非精确路径。
    auto rng = std::mt19937_64{7};
    std::set<std::size_t> seen;
    for (int i = 0; i < 5000; ++i)
    {
        seen.insert(uniform_index(rng, 5));
    }
    CHECK_EQ(seen.size(), std::size_t{5});
}

TEST(random_uniform_index_is_unbiased_enough)
{
    // 拒绝采样应给出大致均匀的分布，允许 8% 相对偏差作为容差。
    //
    // 但要清楚这条断言的真实边界：uniform_unit 只取 24 位有效位，
    // 对 bound 3–7 的相对偏差实测 < 1e-5，远低于本容差。换任何 bound
    // 都无法让它证伪「浮点偏差」这一类系统性偏差——它的实际价值是
    // 「没有索引不可达」与「分布没有肉眼可见的偏斜」。
    //
    // Lemire 的拒绝分支在这个量级上同样不可测：threshold = 2^64 mod bound，
    // bound=5 时为 1，拒绝概率 2^-64。
    auto rng = std::mt19937_64{1234};
    std::array<int, 5> counts{};
    constexpr int kDraws = 40000;
    for (int i = 0; i < kDraws; ++i)
    {
        ++counts[uniform_index(rng, 5)];
    }
    const double expected = static_cast<double>(kDraws) / 5.0;
    for (const auto c : counts)
    {
        const double ratio = static_cast<double>(c) / expected;
        CHECK(ratio > 0.92);
        CHECK(ratio < 1.08);
    }
}

TEST(uniform_index_rejection_branch_is_reachable)
{
    // bound 取 2^63+1 时 threshold = 2^63 - 1，拒绝率约 50%，
    // 循环体会被稳定走到一半。普通 bound 上拒绝概率是 2^-64，
    // 那条分支在任何有限抽样里都观测不到——这是它唯一可测的入口。
    constexpr std::uint64_t kBound = (std::uint64_t{1} << 63) + 1;
    auto rng = std::mt19937_64{4242};

    constexpr int kDraws = 200000;
    std::uint64_t lo = kBound;
    std::uint64_t hi = 0;
    std::uint64_t resamples = 0;

    for (int i = 0; i < kDraws; ++i)
    {
        // 复制 uniform_index 的接受条件只为统计重采样次数。
        // 接受条件若与被测函数漂移，这份副本会静默失配——
        // 范围断言跑的是真函数，它才是主要防线。
        const std::uint64_t threshold = (~kBound + 1u) % kBound;
        std::uint64_t x = 0;
        do
        {
            if (x < threshold)
            {
                ++resamples;
            }
            x = rng();
        } while (x < threshold);

        const auto v = x % kBound;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }

    CHECK(lo < kBound);
    CHECK(hi < kBound);
    // 重采样率应在 50% 上下，远高于普通 bound 上的 2^-64。
    const double rate = static_cast<double>(resamples)
                        / static_cast<double>(kDraws + resamples);
    CHECK(rate > 0.45);
    CHECK(rate < 0.55);
}

TEST(random_streams_are_independent_across_purposes)
{
    RandomStreams a{1, 0};
    RandomStreams b{1, 0};
    // 同一条用途在两次构造后必须给出完全相同的序列。
    for (int i = 0; i < 100; ++i)
    {
        CHECK(uniform_unit(a.stream(RandomPurpose::Crossover))
              == uniform_unit(b.stream(RandomPurpose::Crossover)));
    }

    RandomStreams c{2, 0};
    RandomStreams d{1, 0};
    CHECK(uniform_unit(c.stream(RandomPurpose::Crossover))
          != uniform_unit(d.stream(RandomPurpose::Crossover)));
}

TEST(random_streams_differ_across_seasons)
{
    RandomStreams a{1, 0};
    RandomStreams b{1, 1};
    CHECK(uniform_unit(a.stream(RandomPurpose::Mating))
          != uniform_unit(b.stream(RandomPurpose::Mating)));
}

TEST(random_streams_isolate_purposes)
{
    // 在一条用途上多抽一次，不得影响另一条用途的序列。
    RandomStreams a{9, 0};
    RandomStreams b{9, 0};
    for (int i = 0; i < 50; ++i)
    {
        (void)uniform_unit(a.stream(RandomPurpose::Jitter));
    }
    for (int i = 0; i < 50; ++i)
    {
        CHECK(uniform_unit(a.stream(RandomPurpose::Mating))
              == uniform_unit(b.stream(RandomPurpose::Mating)));
    }
}

TEST(mix_seed_is_stable)
{
    CHECK_EQ(mix_seed(0, 0), mix_seed(0, 0));
    CHECK(mix_seed(0, 0) != mix_seed(0, 1));
    CHECK(mix_seed(0, 0) != mix_seed(1, 0));
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/random.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/random.hpp**

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace gamev2::genetics {

/// splitmix64 finalizer。用它把根种子与用途标签混合成独立子流。
[[nodiscard]] constexpr std::uint64_t
mix_seed(std::uint64_t seed, std::uint64_t purpose) noexcept
{
    std::uint64_t z = seed + (purpose + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 每种用途一条独立子流。共用一条流会导致「某处多抽一次随机数，
/// 后续所有结果整体漂移」的脆弱性——一旦出现，无法定位是哪一处改动引起的。
enum class RandomPurpose : std::uint64_t
{
    Mating = 0,      ///< 亲代抽样
    Survival = 1,    ///< 存活重抽
    Crossover = 2,   ///< 交叉点
    Jitter = 3,      ///< jitter 步进
    AlleleSwitch = 4 ///< 等位基因替换
};

inline constexpr std::size_t kPurposeCount = 5;

/// 同一种子两次运行必须逐位相同，这是可测的硬要求。
///
/// 均匀分布不能用 std::*_distribution：其实现未由标准规定，不同标准库
/// 给出不同结果，会直接破坏上面这条要求。std::mt19937_64 的算法本身由
/// 标准规定，可以安全使用。
class RandomStreams
{
public:
    RandomStreams(std::uint64_t root_seed, std::uint64_t season_index)
    {
        for (std::size_t i = 0; i < kPurposeCount; ++i)
        {
            const std::uint64_t s =
                mix_seed(mix_seed(root_seed, season_index),
                         static_cast<std::uint64_t>(i));
            engines_[i].seed(s);
        }
    }

    [[nodiscard]] std::mt19937_64&
    stream(RandomPurpose purpose) noexcept
    {
        return engines_[static_cast<std::size_t>(purpose)];
    }

private:
    std::array<std::mt19937_64, kPurposeCount> engines_{};
};

/// 取高 24 位映射到 [0, 1)：24 位正好落在 float 尾数里，不引入舍入偏差。
[[nodiscard]] inline float uniform_unit(std::mt19937_64& rng) noexcept
{
    const auto bits = static_cast<std::uint32_t>(rng() >> 40);
    return static_cast<float>(bits) * 0x1.0p-24f;
}

/// Lemire 拒绝采样：对任意 bound 无偏。
///
/// 浮点乘法取整的做法在 bound 不是 2 的幂时会系统性地偏袒小索引，
/// 累积若干代后表现为等位基因频率的假性漂移。
[[nodiscard]] inline std::size_t
uniform_index(std::mt19937_64& rng, std::size_t n) noexcept
{
    const std::uint64_t bound = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = (~bound + 1u) % bound;
    for (;;)
    {
        const std::uint64_t x = rng();
        if (x >= threshold)
        {
            return static_cast<std::size_t>(x % bound);
        }
    }
}

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_random.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `38 test(s), 0 failed`。

---

### Task 6: 重组与突变

**Files:**
- Create: `include/gamev2/genetics/breeding.hpp`
- Create: `src/genetics/breeding.cpp`
- Create: `tests/test_genetics_breeding.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 的 `RandomStreams` / `RandomPurpose` / `uniform_unit` /
  `uniform_index`；Task 2 的 `LocusDef` / `GeneticsConfig` / `kLocusCount`
- Produces:
  - `inline constexpr std::size_t kMaxCrossovers = 8`
  - `Haplotype make_gamete(const Haplotype& x, const Haplotype& y,
    std::size_t crossover_count, std::mt19937_64& rng)`
  - `void mutate(Haplotype&, const LocusTable&, const GeneticsConfig&,
    RandomStreams&)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_breeding.cpp`，本任务放重组与突变测试：

```cpp
#include "gamev2/genetics/breeding.hpp"

#include "test_framework.hpp"

#include <array>

using namespace gamev2::genetics;

namespace {

/// 构造只有第 0、1、2 位点有意义的单倍型，其余位点统一置为 allele = 9，
/// 使「某位点来自哪一亲代」可被一眼辨认。
Haplotype tagged_haplotype(AlleleId tag)
{
    Haplotype h{};
    for (auto& call : h)
    {
        call.allele = tag;
        call.jitter = 0;
    }
    return h;
}

}  // namespace

TEST(gamete_without_crossover_takes_one_parent_whole)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{5};

    for (int i = 0; i < 200; ++i)
    {
        const auto g = make_gamete(x, y, 0, rng);
        const AlleleId src = g[100].allele;
        for (std::size_t k = 0; k < kLocusCount; ++k)
        {
            CHECK_EQ(g[k].allele, src);
        }
    }
}

TEST(gamete_alleles_always_come_from_a_parent)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{6};
    for (int i = 0; i < 500; ++i)
    {
        const auto g = make_gamete(x, y, 4, rng);
        for (std::size_t k = 0; k < kLocusCount; ++k)
        {
            CHECK(g[k].allele == 0 || g[k].allele == 1);
        }
    }
}

TEST(gamete_produces_alternating_segments)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{8};

    bool saw_transition = false;
    for (int trial = 0; trial < 200 && !saw_transition; ++trial)
    {
        const auto g = make_gamete(x, y, 6, rng);
        for (std::size_t k = 1; k < kLocusCount; ++k)
        {
            if (g[k].allele != g[k - 1].allele)
            {
                saw_transition = true;
                break;
            }
        }
    }
    CHECK(saw_transition);
}

TEST(gamete_source_switch_count_bounded_by_crossovers)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{9};

    // 切换次数至多为交叉点数。重复落在同一位点会浪费交叉点，
    // 因此这是上界而非等式。
    for (int trial = 0; trial < 200; ++trial)
    {
        const auto g = make_gamete(x, y, 3, rng);
        std::size_t switches = 0;
        for (std::size_t k = 1; k < kLocusCount; ++k)
        {
            if (g[k].allele != g[k - 1].allele)
            {
                ++switches;
            }
        }
        CHECK(switches <= 3);
    }
}

TEST(mutate_respects_zero_switch_rate)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    auto genome = Genome{};
    auto rng = RandomStreams{3, 0};
    mutate(genome.a, table, config, rng);

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        CHECK_EQ(genome.a[i].allele, AlleleId{0});
        CHECK_EQ(genome.a[i].jitter, std::int8_t{0});
    }
}

TEST(mutate_jitter_accumulates_and_clamps)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;  // 每代都突变
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 0.0f;    // 全部走 jitter 微调
    config.jitter_step = 2;

    auto genome = Genome{};
    for (std::size_t season = 0; season < 1000; ++season)
    {
        auto rng = RandomStreams{11, season};
        mutate(genome.a, table, config, rng);
        const std::int8_t j = genome.a[0].jitter;
        CHECK(j >= -2);
        CHECK(j <= 2);
    }
}

TEST(mutate_switches_alleles_at_configured_rate)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.5f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;  // 突变即替换
    config.jitter_step = 0;

    constexpr std::size_t kTrials = 20000;
    std::size_t switched = 0;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        auto genome = Genome{};
        auto rng = RandomStreams{13, season};
        mutate(genome.a, table, config, rng);
        if (genome.a[0].allele != 0)
        {
            ++switched;
        }
    }
    // 实测替换率应落在设定值 0.5 附近，容差 0.03。
    const double rate = static_cast<double>(switched) / kTrials;
    CHECK(rate > 0.47);
    CHECK(rate < 0.53);
}

TEST(mutate_never_keeps_the_same_allele_on_switch)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 3;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;

    auto genome = Genome{};
    for (auto& call : genome.a)
    {
        call.allele = 1;
    }
    for (std::size_t season = 0; season < 2000; ++season)
    {
        auto rng = RandomStreams{17, season};
        mutate(genome.a, table, config, rng);
        CHECK(genome.a[0].allele != 1);
    }
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/breeding.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/breeding.hpp**

```cpp
#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/random.hpp"

#include <cstddef>
#include <random>

namespace gamev2::genetics {

inline constexpr std::size_t kMaxCrossovers = 8;

/// 减数分裂产生一个配子：单倍型 x 与 y 在随机交叉点处交替取段。
///
/// 交叉点用位图而非排序数组记录——交叉点只作为「来源在此翻转」的集合有意义，
/// 顺序无关，位图省掉一次 O(k log k) 排序。
///
/// 交叉点数固定均值而非服从泊松分布：它对群体均值的影响远小于位点数，
/// 换来的是实现与调参的简单。
[[nodiscard]] Haplotype make_gamete(const Haplotype& x, const Haplotype& y,
                                    std::size_t crossover_count,
                                    std::mt19937_64& rng);

/// 对单条单倍型施加一次突变。
///
/// switch_rate 是该位点每代发生突变的概率。突变发生时，Trait 位点以
/// config.allele_switch_share 的概率替换等位基因、否则 jitter 微调；
/// Structural 位点一律均匀替换，jitter 对它无意义。
void mutate(Haplotype& haplotype, const LocusTable& table,
            const GeneticsConfig& config, RandomStreams& streams);

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 src/genetics/breeding.cpp**

```cpp
#include "gamev2/genetics/breeding.hpp"

#include <algorithm>
#include <bitset>

namespace gamev2::genetics {

Haplotype make_gamete(const Haplotype& x, const Haplotype& y,
                      std::size_t crossover_count, std::mt19937_64& rng)
{
    const std::size_t count = std::min(crossover_count, kMaxCrossovers);

    std::bitset<kLocusCount> cuts{};
    for (std::size_t i = 0; i < count; ++i)
    {
        cuts.set(uniform_index(rng, kLocusCount));
    }

    bool take_x = uniform_unit(rng) < 0.5f;
    Haplotype out{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        out[i] = take_x ? x[i] : y[i];
        if (cuts.test(i))
        {
            take_x = !take_x;
        }
    }
    return out;
}

namespace {

AlleleId pick_other_allele(AlleleId current, std::uint8_t allele_count,
                           std::mt19937_64& rng) noexcept
{
    // 在其余 allele_count - 1 个等位基因中均匀选一个。
    const auto span = static_cast<std::size_t>(allele_count) - 1;
    const std::size_t offset = uniform_index(rng, span);
    return static_cast<AlleleId>(
        offset < current ? offset : offset + 1);
}

}  // namespace

void mutate(Haplotype& haplotype, const LocusTable& table,
            const GeneticsConfig& config, RandomStreams& streams)
{
    auto& rng = streams.stream(RandomPurpose::Jitter);
    auto& allele_rng = streams.stream(RandomPurpose::AlleleSwitch);

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        const LocusDef& locus = table.loci[i];
        if (uniform_unit(rng) >= locus.switch_rate)
        {
            continue;
        }

        if (locus.kind == LocusKind::Structural)
        {
            haplotype[i].allele =
                pick_other_allele(haplotype[i].allele, locus.allele_count,
                                  allele_rng);
            continue;
        }

        if (uniform_unit(allele_rng) < config.allele_switch_share)
        {
            haplotype[i].allele =
                pick_other_allele(haplotype[i].allele, locus.allele_count,
                                  allele_rng);
            continue;
        }

        const auto step = static_cast<std::int8_t>(
            uniform_index(rng, static_cast<std::size_t>(
                2 * static_cast<int>(config.jitter_step) + 1))
            - static_cast<int>(config.jitter_step));

        int jitter = static_cast<int>(haplotype[i].jitter)
                     + static_cast<int>(step);
        // 必须有界：否则位点会沿某个方向无限漂移，等于给了它一条不消耗
        // 突变代价的适应趋势，加性微效的统计性质随之失效。
        jitter = std::clamp(jitter, -static_cast<int>(config.jitter_step),
                            static_cast<int>(config.jitter_step));
        haplotype[i].jitter = static_cast<std::int8_t>(jitter);
    }
}

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_breeding.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `50 test(s), 0 failed`。

---

### Task 7: 生存率与适应力

**Files:**
- Create: `include/gamev2/genetics/survival.hpp`
- Create: `src/genetics/survival.cpp`
- Create: `tests/test_genetics_survival.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 的 `Phenotype` / `TraitKind`；Task 2 的 `GeneticsConfig`
- Produces:
  - `struct SurvivalInput { const Phenotype& phenotype;
    const EnvironmentSnapshot& environment; float feeding; float threat_level; }`
  - `float survival_rate(const SurvivalInput&, const GeneticsConfig&)`
  - `float fitness(float survival, const Phenotype&, float max_fecundity)`
  - `bool outlived(const Phenotype&, std::uint32_t age)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_survival.cpp`：

```cpp
#include "gamev2/genetics/survival.hpp"

#include "test_framework.hpp"

using namespace gamev2::genetics;

namespace {

Phenotype make_phenotype(float size, float speed, float metabolism,
                         float sense, float fecundity, float resilience)
{
    Phenotype p{};
    p.traits[trait_index(TraitKind::BodySize)] = size;
    p.traits[trait_index(TraitKind::Speed)] = speed;
    p.traits[trait_index(TraitKind::Strength)] = 1.0f;
    p.traits[trait_index(TraitKind::Metabolism)] = metabolism;
    p.traits[trait_index(TraitKind::Lifespan)] = 10.0f;
    p.traits[trait_index(TraitKind::SenseRange)] = sense;
    p.traits[trait_index(TraitKind::Fecundity)] = fecundity;
    p.traits[trait_index(TraitKind::Resilience)] = resilience;
    return p;
}

}  // namespace

TEST(survival_is_a_probability)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    const auto env = EnvironmentSnapshot{};
    const auto r = survival_rate(SurvivalInput{p, env, 1.0f, 0.0f},
                                 GeneticsConfig{});
    CHECK(r >= 0.0f);
    CHECK(r <= 1.0f);
}

TEST(survival_drops_as_metabolism_rises)
{
    const auto env = EnvironmentSnapshot{};
    const auto lean = make_phenotype(1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f);
    const auto hungry = make_phenotype(1.0f, 1.0f, 4.0f, 1.0f, 1.0f, 1.0f);
    const auto config = GeneticsConfig{};
    CHECK(survival_rate(SurvivalInput{lean, env, 1.0f, 0.0f}, config)
          > survival_rate(SurvivalInput{hungry, env, 1.0f, 0.0f}, config));
}

TEST(survival_drops_as_temperature_moves_away)
{
    const auto config = GeneticsConfig{};
    config.thermal_base_width = 1.0f;
    config.thermal_resilience_gain = 1.0f;
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    const auto mild = EnvironmentSnapshot{0.5f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{20.0f, 1.0f, 1.0f};
    CHECK(survival_rate(SurvivalInput{p, mild, 1.0f, 0.0f}, config)
          > survival_rate(SurvivalInput{p, harsh, 1.0f, 0.0f}, config));
}

TEST(survival_resilience_widens_thermal_tolerance)
{
    const auto config = GeneticsConfig{};
    config.thermal_base_width = 1.0f;
    config.thermal_resilience_gain = 1.0f;
    const auto fragile = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 0.0f);
    const auto tough = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 5.0f);
    const auto env = EnvironmentSnapshot{4.0f, 1.0f, 1.0f};
    CHECK(survival_rate(SurvivalInput{fragile, env, 1.0f, 0.0f}, config)
          < survival_rate(SurvivalInput{tough, env, 1.0f, 0.0f}, config));
}

TEST(survival_drops_under_predation)
{
    const auto env = EnvironmentSnapshot{};
    const auto config = GeneticsConfig{};
    const auto slow = make_phenotype(1.0f, 0.2f, 0.2f, 2.0f, 2.0f, 5.0f);
    const auto fast = make_phenotype(1.0f, 4.0f, 0.2f, 2.0f, 2.0f, 5.0f);
    const float low = survival_rate(SurvivalInput{slow, env, 1.0f, 0.5f},
                                    config);
    const float high = survival_rate(SurvivalInput{fast, env, 1.0f, 0.5f},
                                     config);
    CHECK(high > low);
}

TEST(survival_is_monotone_in_feeding)
{
    const auto env = EnvironmentSnapshot{};
    const auto config = GeneticsConfig{};
    const auto p = make_phenotype(1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f);
    const auto starving = survival_rate(SurvivalInput{p, env, 0.2f, 0.0f},
                                        config);
    const auto fed = survival_rate(SurvivalInput{p, env, 1.0f, 0.0f}, config);
    CHECK(fed > starving);
}

TEST(fitness_normalizes_against_population_max)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 4.0f, 1.0f);
    CHECK(fitness(0.5f, p, 4.0f) == 0.5f);
    CHECK(fitness(1.0f, p, 4.0f) == 1.0f);
}

TEST(fitness_is_one_when_population_max_is_zero)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    CHECK(fitness(0.7f, p, 0.0f) == 0.7f);
}

TEST(outlived_is_a_hard_death_condition)
{
    const auto short_lived =
        make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    const auto young = short_lived;
    auto p = short_lived;
    p.traits[trait_index(TraitKind::Lifespan)] = 5.0f;
    CHECK(!outlived(p, 4));
    CHECK(outlived(p, 5));
    (void)young;
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/survival.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/survival.hpp**

```cpp
#pragma once

#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <cstdint>

namespace gamev2::genetics {

struct SurvivalInput
{
    const Phenotype& phenotype;
    const EnvironmentSnapshot& environment;
    /// 实际摄入的能量，取值 0..1 的资源满足度。行为 spec 接入前传理论值 1。
    float feeding = 1.0f;
    /// 捕食威胁等级，0..1。无天敌时传 0。
    float threat_level = 0.0f;
};

/// 各项生理约束存活率之积。
///
/// 这里刻意不是适应度函数：适应力全部来自基因与环境的交互，没有开发者
/// 私下加的权重。每项约束都可以单独展示、单独调平衡、单独向玩家解释。
///
/// 寿命不在这个乘积里——它是硬判定，见 outlived。
[[nodiscard]] float survival_rate(const SurvivalInput& input,
                                  const GeneticsConfig& config);

/// 相对繁殖贡献，用于亲代抽样权重。max_fecundity 是当前季种群的最大
/// Fecundity，除以它使本季最优者恰为 1。
[[nodiscard]] float fitness(float survival, const Phenotype& phenotype,
                            float max_fecundity) noexcept;

/// 寿命是硬判定而非概率约束，因此不参与 survival_rate 的乘积。
[[nodiscard]] bool outlived(const Phenotype& phenotype,
                            std::uint32_t age) noexcept;

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 src/genetics/survival.cpp**

```cpp
#include "gamev2/genetics/survival.hpp"

#include <algorithm>
#include <cmath>

namespace gamev2::genetics {

namespace {

float trait_of(const Phenotype& p, TraitKind t) noexcept
{
    return p.traits[trait_index(t)];
}

float energy_survival(const SurvivalInput& in) noexcept
{
    const float intake = trait_of(in.phenotype, TraitKind::SenseRange)
                         * trait_of(in.phenotype, TraitKind::Fecundity)
                         * in.feeding;
    const float burn = trait_of(in.phenotype, TraitKind::Metabolism)
                       * trait_of(in.phenotype, TraitKind::BodySize)
                       * (1.0f + trait_of(in.phenotype, TraitKind::Speed));
    if (burn <= 0.0f)
    {
        return 1.0f;
    }
    return std::clamp(intake / burn, 0.0f, 1.0f);
}

float thermal_survival(const SurvivalInput& in,
                       const GeneticsConfig& config) noexcept
{
    const float width = config.thermal_base_width
                        + config.thermal_resilience_gain
                              * trait_of(in.phenotype,
                                         TraitKind::Resilience);
    if (width <= 0.0f)
    {
        return 0.0f;
    }
    const float distance = std::fabs(in.environment.temperature);
    return std::clamp(1.0f - distance / width, 0.0f, 1.0f);
}

float escape_survival(const SurvivalInput& in,
                      const GeneticsConfig& config) noexcept
{
    // 大体型更难被抓住，与真实捕食关系一致；代价是更容易被察觉，
    // 该代价由 SenseRange 承担的察觉压力体现，不在此处重复扣分。
    const float demand = in.threat_level
                         * trait_of(in.phenotype, TraitKind::BodySize);
    const float headroom = trait_of(in.phenotype, TraitKind::Speed) - demand;
    return std::clamp(headroom * config.escape_responsiveness, 0.0f, 1.0f);
}

}  // namespace

float survival_rate(const SurvivalInput& input, const GeneticsConfig& config)
{
    return energy_survival(input) * thermal_survival(input, config)
           * escape_survival(input, config);
}

float fitness(float survival, const Phenotype& phenotype,
              float max_fecundity) noexcept
{
    if (max_fecundity <= 0.0f)
    {
        return survival;
    }
    return std::clamp(survival * trait_of(phenotype, TraitKind::Fecundity)
                          / max_fecundity,
                      0.0f, 1.0f);
}

bool outlived(const Phenotype& phenotype, std::uint32_t age) noexcept
{
    return static_cast<float>(age)
           >= trait_of(phenotype, TraitKind::Lifespan);
}

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_survival.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `65 test(s), 0 failed`。

---

### Task 8: 局部交配与子代产生

**Files:**
- Modify: `include/gamev2/genetics/breeding.hpp`
- Modify: `src/genetics/breeding.cpp`
- Modify: `tests/test_genetics_breeding.cpp`
- Modify: `CMakeLists.txt`（无需改动，复用已注册文件）

**Interfaces:**
- Consumes: Task 6 的 `make_gamete` / `mutate`；Task 7 的 `fitness`；
  Task 3 的 `SpeciesId` / `SpeciesMask`
- Produces:
  - `struct Parent { const Genome* genome = nullptr; float fitness = 0.0f; }`
  - `struct ParentPool { std::span<const Parent> parents; SpeciesId species; }`
  - `enum class BreedingError { EmptyPool, PoolTooSmall }`
  - `std::expected<Genome, BreedingError> make_offspring(const ParentPool&,
    const LocusTable&, const GeneticsConfig&, RandomStreams&)`

- [ ] **Step 1: 追加失败测试**

在 `tests/test_genetics_breeding.cpp` 末尾追加：

```cpp
#include "gamev2/genetics/breeding.hpp"

#include <vector>

namespace {

struct PoolBuilder
{
    std::vector<Parent> items;

    void add(const Genome& g, float f)
    {
        items.push_back(Parent{&g, f});
    }

    [[nodiscard]] ParentPool pool() const
    {
        return ParentPool{items, SpeciesId{1}};
    }
};

}  // namespace

TEST(offspring_rejects_empty_pool)
{
    PoolBuilder b;
    auto rng = RandomStreams{1, 0};
    const auto table = LocusTable{};
    const auto config = GeneticsConfig{};
    const auto r = make_offspring(b.pool(), table, config, rng);
    CHECK(!r.has_value());
    CHECK(r.error() == BreedingError::EmptyPool);
}

TEST(offspring_rejects_single_parent_pool)
{
    PoolBuilder b;
    const Genome g{};
    b.add(g, 1.0f);
    auto rng = RandomStreams{1, 0};
    const auto table = LocusTable{};
    const auto config = GeneticsConfig{};
    const auto r = make_offspring(b.pool(), table, config, rng);
    CHECK(!r.has_value());
    CHECK(r.error() == BreedingError::PoolTooSmall);
}

TEST(offspring_prefers_parents_with_higher_fitness)
{
    // 高适应亲代占绝对权重时，合格后代必须主要由它贡献。计数方式是
    // 整条单倍型相同——亲代为纯合且互不相同时，后代单倍型可唯一归因。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome star{};
    for (auto& c : star.a)
    {
        c.allele = 1;
    }
    for (auto& c : star.b)
    {
        c.allele = 1;
    }
    Genome weak{};
    for (auto& c : weak.a)
    {
        c.allele = 0;
    }
    for (auto& c : weak.b)
    {
        c.allele = 0;
    }

    PoolBuilder b;
    b.add(weak, 1.0f);
    b.add(star, 999.0f);

    std::size_t from_star = 0;
    constexpr std::size_t kTrials = 4000;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        auto rng = RandomStreams{23, season};
        const auto child = make_offspring(b.pool(), table, config, rng);
        CHECK(child.has_value());
        if (child->a[0].allele == 1 && child->b[0].allele == 1)
        {
            ++from_star;
        }
    }
    const double rate = static_cast<double>(from_star) / kTrials;
    CHECK(rate > 0.95);
}

TEST(offspring_haplotypes_are_distinct_parental_contributions)
{
    // 纯合双亲的后代必然是纯合，但两份单倍型必须分别来自一个亲代：
    // 只有一个亲代时不该出现「两路都来自它」之外的组合。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome p0{};
    Genome p1{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        p0.a[i].allele = 0;
        p0.b[i].allele = 0;
        p1.a[i].allele = 1;
        p1.b[i].allele = 1;
    }

    PoolBuilder b;
    b.add(p0, 1.0f);
    b.add(p1, 1.0f);

    for (std::size_t season = 0; season < 500; ++season)
    {
        auto rng = RandomStreams{29, season};
        const auto child = make_offspring(b.pool(), table, config, rng);
        CHECK(child.has_value());
        const bool a_from_p0 = child->a[0].allele == 0;
        const bool b_from_p1 = child->b[0].allele == 1;
        CHECK(a_from_p0 || b_from_p1);
    }
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`Parent` / `make_offspring` 未声明。

- [ ] **Step 3: 向 breeding.hpp 追加接口**

在 `#include <random>` 之后追加 `<span>`、`<vector>`、`<expected>`，并在
`mutate` 声明之后追加：

```cpp
/// 亲代索引与相对适应力。父池由地图 / 行为 spec 按物种与空间分桶完毕后传入，
/// 本模块不关心它从哪来。
struct Parent
{
    const Genome* genome = nullptr;
    float fitness = 0.0f;
};

struct ParentPool
{
    std::span<const Parent> parents;
    SpeciesId species{};
    /// 待繁殖位置的环境快照，由地图 spec 采样后传入。
    /// 未提供时 settle 退化为默认环境——生态 spec 交付前的占位路径。
    std::optional<EnvironmentSnapshot> environment{};
};

enum class BreedingError
{
    EmptyPool,
    /// 池内只有一个个体时无法完成有性生殖。这一状态在真实的
    /// 地理隔离末期是可达的，调用方必须决定是放弃繁殖还是允许自交。
    PoolTooSmall,
};

/// 抽取亲代、重组、突变，返回一个子代基因组。
///
/// 亲代按 fitness 加权，有放回抽取。同一个亲代可能被选中两次，
/// 此时子代为自交——这是近交衰退在个体层面的入口。
[[nodiscard]] std::expected<Genome, BreedingError>
make_offspring(const ParentPool& pool, const LocusTable& table,
               const GeneticsConfig& config, RandomStreams& streams);
```

`breeding.hpp` 需新增 `#include "gamev2/genetics/species.hpp"` 与
`#include "gamev2/genetics/environment.hpp"` 与 `<optional>`。

`ParentPool` 多出的 `environment` 是可选成员，因此已有的
`ParentPool{parents, SpeciesId{1}}` 两参数构造全部保持有效，
Task 8 的测试与 `make_offspring` 实现都不需要改。

- [ ] **Step 4: 向 breeding.cpp 追加实现**

```cpp
#include <expected>
#include <span>
#include <vector>

namespace gamev2::genetics {

namespace {

/// 在 CDF 上二分查找，把 fitness 权重转成一次抽样。
/// CDF 而非别名表：亲代池只有几十个量级，构建成本可忽略。
[[nodiscard]] std::size_t sample_by_fitness(const ParentPool& pool,
                                            const std::vector<float>& cdf,
                                            std::mt19937_64& rng) noexcept
{
    const float total = cdf.back();
    if (total <= 0.0f)
    {
        // 全零权重时退化为等概率，避免除零把整季繁殖变成 NaN 污染。
        return uniform_index(rng, pool.parents.size());
    }
    const float target = uniform_unit(rng) * total;
    const auto it = std::ranges::upper_bound(cdf, target);
    const auto idx = static_cast<std::size_t>(it - cdf.begin());
    return std::min(idx, pool.parents.size() - 1);
}

}  // namespace

std::expected<Genome, BreedingError>
make_offspring(const ParentPool& pool, const LocusTable& table,
               const GeneticsConfig& config, RandomStreams& streams)
{
    if (pool.parents.empty())
    {
        return std::unexpected(BreedingError::EmptyPool);
    }
    if (pool.parents.size() < 2)
    {
        return std::unexpected(BreedingError::PoolTooSmall);
    }

    std::vector<float> cdf;
    cdf.reserve(pool.parents.size());
    float running = 0.0f;
    for (const auto& p : pool.parents)
    {
        running += std::max(p.fitness, 0.0f);
        cdf.push_back(running);
    }

    auto& mating = streams.stream(RandomPurpose::Mating);
    const std::size_t i = sample_by_fitness(pool, cdf, mating);
    const std::size_t j = sample_by_fitness(pool, cdf, mating);

    const Genome& p0 = *pool.parents[i].genome;
    const Genome& p1 = *pool.parents[j].genome;

    auto& crossover = streams.stream(RandomPurpose::Crossover);
    const std::size_t cuts = uniform_index(crossover, kMaxCrossovers + 1);

    Genome child{};
    child.a = make_gamete(p0.a, p0.b, cuts, crossover);
    child.b = make_gamete(p1.a, p1.b, cuts, crossover);

    mutate(child.a, table, config, streams);
    mutate(child.b, table, config, streams);
    return child;
}

}  // namespace gamev2::genetics
```

把新加的三个 `#include` 移到文件顶部的 include 区，不要留在 namespace 之后。

- [ ] **Step 5: 运行测试**

```powershell
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `73 test(s), 0 failed`。

---

### Task 9: 等位基因统计

**Files:**
- Create: `include/gamev2/genetics/statistics.hpp`
- Create: `src/genetics/statistics.cpp`
- Create: `tests/test_genetics_statistics.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 的 `kLocusCount` / `kMaxAlleles` / `LocusTable`；
  Task 3 的 `SpeciesMask` / `SpeciesId`
- Produces:
  - `struct AlleleStats { std::array<float, kMaxAlleles> frequency;
    float h_observed; float h_expected; }`
  - `struct SpeciesStatistics { std::vector<AlleleStats> loci;
    float heterozygosity; float f_coefficient;
    float effective_population; std::uint32_t census; }`
  - `SpeciesStatistics summarize(std::span<const Genome>, const SpeciesMask&)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_statistics.cpp`：

```cpp
#include "gamev2/genetics/statistics.hpp"

#include "test_framework.hpp"

#include <cmath>
#include <vector>

using namespace gamev2::genetics;

namespace {

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

Genome make_genome(AlleleId a, AlleleId b)
{
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i].allele = a;
        g.b[i].allele = b;
    }
    return g;
}

}  // namespace

TEST(statistics_on_empty_population_is_zero)
{
    const std::vector<Genome> none;
    const auto s = summarize(none, all_loci());
    CHECK_EQ(s.census, std::uint32_t{0});
    CHECK(s.loci.empty());
}

TEST(statistics_counts_alleles)
{
    // 3 个 Aa + 1 个 aa，共 8 条等位基因：A = 3，a = 5。
    // p(A) = 3/8 = 0.375，p(a) = 5/8 = 0.625
    // H_obs = 3/4 = 0.75，H_exp = 2 × 0.375 × 0.625 = 0.46875
    const std::vector<Genome> pop{make_genome(0, 1), make_genome(0, 1),
                                  make_genome(0, 1), make_genome(1, 1)};
    const auto s = summarize(pop, all_loci());
    CHECK_EQ(s.census, std::uint32_t{4});
    const auto& l = s.loci[0];
    CHECK(l.frequency[0] == 0.375f);
    CHECK(l.frequency[1] == 0.625f);
    CHECK(l.h_observed == 0.75f);
    CHECK(l.h_expected == 0.46875f);
}

TEST(statistics_reports_negative_f_for_heterozygote_excess)
{
    // 同一个群体：H_obs 0.75 高于 H_exp 0.46875，
    // F = 1 - 0.75 / 0.46875 = -0.6。负值表示杂合过剩，不是近交。
    const std::vector<Genome> pop{make_genome(0, 1), make_genome(0, 1),
                                  make_genome(0, 1), make_genome(1, 1)};
    const auto s = summarize(pop, all_loci());
    CHECK(s.f_coefficient < 0.0f);
}

TEST(statistics_falls_back_to_zero_for_monomorphic_population)
{
    // 全员同型合子：每位点 p 均为 1 或 0，H_exp = 0，全部位点低于阈值。
    // 此时 F 的加权分母为 0，必须退回 0 而不是 NaN 或除零。
    // 注意种群必须真的单态——AA/AA/aa/AA 是两态，
    // p = (0.75, 0.25)、H_exp = 0.375，落在阈值之上，F 会是 1.0。
    const std::vector<Genome> pop{make_genome(0, 0), make_genome(0, 0),
                                  make_genome(0, 0), make_genome(0, 0)};
    const auto s = summarize(pop, all_loci());
    CHECK(!std::isnan(s.f_coefficient));
    CHECK(s.f_coefficient == 0.0f);
}

TEST(statistics_skips_monomorphic_loci_in_ne)
{
    // 同上：每位点 H_exp = 0，故 h_loss = 300，Ne = 1 / (2 × 300) = 1/600。
    const std::vector<Genome> pop{make_genome(0, 0), make_genome(0, 0),
                                  make_genome(0, 0), make_genome(0, 0)};
    const auto s = summarize(pop, all_loci());
    CHECK(!std::isnan(s.effective_population));
    CHECK(s.effective_population == 1.0f / 600.0f);
}

TEST(statistics_heterozygosity_is_mean_over_loci)
{
    const std::vector<Genome> pop{make_genome(0, 1)};
    const auto s = summarize(pop, all_loci());
    // 全 Aa：每位点 H_obs = 1
    CHECK(s.heterozygosity == 1.0f);
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/statistics.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/statistics.hpp**

```cpp
#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/species.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::genetics {

struct AlleleStats
{
    std::array<float, kMaxAlleles> frequency{};
    float h_observed = 0.0f;
    float h_expected = 0.0f;
};

/// 漂变、瓶颈、近交衰退共用这一条统计路径——三者不是三个算法，
/// 而是同一采样机制在三个尺度上的表现。
struct SpeciesStatistics
{
    /// 只包含激活位点，逐位点明细仅对玩家选中的物种计算
    std::vector<AlleleStats> loci;
    float heterozygosity = 0.0f;
    float f_coefficient = 0.0f;
    float effective_population = 0.0f;
    std::uint32_t census = 0;
};

/// 近交系数用杂合度估计，不追踪家谱——追踪家谱要在每代给每个个体
/// 存一份祖先集合，而当代等位基因频率已经含有同等信息。
[[nodiscard]] SpeciesStatistics summarize(std::span<const Genome> genomes,
                                          const SpeciesMask& mask);

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 src/genetics/statistics.cpp**

```cpp
#include "gamev2/genetics/statistics.hpp"

#include <algorithm>
#include <cstddef>

namespace gamev2::genetics {

namespace {

/// H_exp 低于此值的位点，其 H_obs / H_exp 比值几乎全是估计噪声。
/// 跳过它们，否则近交系数会被这些近乎单态的位点主导。
constexpr float kMinExpectedHeterozygosity = 1.0f / 1024.0f;

}  // namespace

SpeciesStatistics summarize(std::span<const Genome> genomes,
                            const SpeciesMask& mask)
{
    SpeciesStatistics out{};
    out.census = static_cast<std::uint32_t>(genomes.size());
    if (genomes.empty())
    {
        return out;
    }

    std::vector<std::size_t> active;
    active.reserve(mask.active.count());
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        if (mask.active.test(i))
        {
            active.push_back(i);
        }
    }
    out.loci.resize(active.size());

    float heterozygosity_sum = 0.0f;
    float f_weighted_sum = 0.0f;
    float f_weight_sum = 0.0f;
    float h_loss_sum = 0.0f;

    for (std::size_t slot = 0; slot < active.size(); ++slot)
    {
        const std::size_t locus = active[slot];
        AlleleStats& stats = out.loci[slot];

        std::array<std::uint32_t, kMaxAlleles> counts{};
        std::uint32_t heterozygous = 0;

        for (const auto& genome : genomes)
        {
            const AlleleId x = genome.a[locus].allele;
            const AlleleId y = genome.b[locus].allele;
            ++counts[x];
            ++counts[y];
            if (x != y)
            {
                ++heterozygous;
            }
        }

        const float n = static_cast<float>(genomes.size());
        float h_expected = 0.0f;
        for (std::size_t k = 0; k < kMaxAlleles; ++k)
        {
            const float p = static_cast<float>(counts[k]) / (2.0f * n);
            stats.frequency[k] = p;
            h_expected += p * (1.0f - p);
        }

        stats.h_observed = static_cast<float>(heterozygous) / n;
        stats.h_expected = h_expected;
        heterozygosity_sum += stats.h_observed;

        if (h_expected >= kMinExpectedHeterozygosity)
        {
            const float f = 1.0f - stats.h_observed / h_expected;
            // 按等位基因方差加权：多样性低的位点上 F 的估计噪声大，
            // 不加权会让近交系数被它们主导。
            f_weighted_sum += f * h_expected;
            f_weight_sum += h_expected;
        }
        h_loss_sum += 1.0f - h_expected;
    }

    if (!active.empty())
    {
        out.heterozygosity =
            heterozygosity_sum / static_cast<float>(active.size());
    }
    out.f_coefficient = (f_weight_sum > 0.0f) ? f_weighted_sum / f_weight_sum
                                              : 0.0f;
    out.effective_population =
        (h_loss_sum > 0.0f) ? 1.0f / (2.0f * h_loss_sum) : 0.0f;
    return out;
}

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_statistics.cpp`，然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `79 test(s), 0 failed`。

---

### Task 10: 繁殖季结算

**Files:**
- Create: `include/gamev2/genetics/population.hpp`
- Create: `src/genetics/population.cpp`
- Create: `tests/test_genetics_population.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 8 的 `ParentPool` / `make_offspring`；
  Task 9 的 `SpeciesStatistics`；Task 4 的 `phenotype_of`；
  Task 7 的 `survival_rate` / `fitness` / `outlived`
- Produces:
  - `struct SeasonInput { std::span<const Genome> genomes;
    std::span<const Phenotype> phenotypes;
    std::span<const EnvironmentSnapshot> environments;
    std::span<const float> threats; std::span<const ParentPool> sites; }`
  - `struct SeasonOutput { std::vector<Genome> offspring;
    std::vector<Phenotype> offspring_phenotypes;
    std::vector<EnvironmentSnapshot> offspring_environments;
    std::vector<std::uint8_t> survived; std::vector<SpeciesStatistics> stats; }`
  - `SeasonOutput settle(const SeasonInput&, const LocusTable&,
    const SpeciesRegistry&, const GeneticsConfig&, RandomStreams&)`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_population.cpp`：

```cpp
#include "gamev2/genetics/population.hpp"

#include "test_framework.hpp"

#include <numeric>
#include <vector>

using namespace gamev2::genetics;

namespace {

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

LocusTable neutral_table()
{
    LocusTable t{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        t.loci[i].kind = LocusKind::Trait;
        t.loci[i].allele_count = 2;
        t.loci[i].scale = 1.0f;
        t.loci[i].switch_rate = 0.0f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

Phenotype robust_phenotype()
{
    Phenotype p{};
    p.traits[trait_index(TraitKind::BodySize)] = 1.0f;
    p.traits[trait_index(TraitKind::Speed)] = 4.0f;
    p.traits[trait_index(TraitKind::Metabolism)] = 0.2f;
    p.traits[trait_index(TraitKind::Lifespan)] = 100.0f;
    p.traits[trait_index(TraitKind::SenseRange)] = 4.0f;
    p.traits[trait_index(TraitKind::Fecundity)] = 4.0f;
    p.traits[trait_index(TraitKind::Resilience)] = 5.0f;
    return p;
}

}  // namespace

TEST(settle_keeps_offspring_in_staging_until_returned)
{
    const std::vector<Genome> genomes(4, Genome{});
    const std::vector<Phenotype> phenotypes(4, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(4, EnvironmentSnapshot{});
    const std::vector<float> threats(4, 0.0f);

    std::vector<Parent> parents;
    for (const auto& g : genomes)
    {
        parents.push_back(Parent{&g, 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};
    const std::vector<std::uint32_t> species_ids(4, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{31, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.offspring.size(), std::size_t{1});
    CHECK_EQ(out.offspring_phenotypes.size(), std::size_t{1});
    CHECK_EQ(out.offspring_environments.size(), std::size_t{1});
    CHECK_EQ(out.survived.size(), std::size_t{4});
    CHECK_EQ(out.stats.size(), std::size_t{1});
}

TEST(settle_marks_everyone_dead_when_no_sites)
{
    const std::vector<Genome> genomes(6, Genome{});
    const std::vector<Phenotype> phenotypes(6, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(6, EnvironmentSnapshot{});
    const std::vector<float> threats(6, 0.0f);
    const std::vector<ParentPool> sites{};
    const std::vector<std::uint32_t> species_ids(6, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{31, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK(out.offspring.empty());
    for (const auto s : out.survived)
    {
        CHECK_EQ(s, std::uint8_t{0});
    }
}

TEST(settle_enriches_selected_trait_in_fitness_weighted_pool)
{
    // 高存活率亲代占绝对权重时，后代应当主要继承它的表型。
    // 用体型轴做可观测代理：选中的亲代 BodySize = 4，其后代更高。
    auto table = neutral_table();
    table.loci[0].scale = 1.0f;
    table.loci[0].alleles[0].additive = 0.0f;
    table.loci[0].alleles[1].additive = 1.0f;
    table.loci[0].dominance = 1.0f;

    Genome favored{};
    for (auto& c : favored.a)
    {
        c.allele = 1;
    }
    for (auto& c : favored.b)
    {
        c.allele = 1;
    }
    Genome ignored{};
    for (auto& c : ignored.a)
    {
        c.allele = 0;
    }
    for (auto& c : ignored.b)
    {
        c.allele = 0;
    }

    Phenotype strong = robust_phenotype();
    Phenotype weak = robust_phenotype();
    weak.traits[trait_index(TraitKind::Speed)] = 0.05f;
    weak.traits[trait_index(TraitKind::SenseRange)] = 0.05f;
    weak.traits[trait_index(TraitKind::Fecundity)] = 0.05f;
    weak.traits[trait_index(TraitKind::BodySize)] = 8.0f;

    const std::vector<Genome> genomes{favored, ignored};
    const std::vector<Phenotype> phenotypes{strong, weak};
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    const std::vector<float> threats(2, 0.0f);

    std::vector<Parent> parents{Parent{&genomes[0], 1.0f},
                               Parent{&genomes[1], 1.0f}};
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};
    const std::vector<std::uint32_t> species_ids(2, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    std::size_t favored_lineage = 0;
    constexpr std::size_t kSeasons = 60;
    for (std::size_t season = 0; season < kSeasons; ++season)
    {
        auto rng = RandomStreams{37, season};
        auto out = settle(SeasonInput{genomes, phenotypes, envs, threats,
                                      species_ids, sites},
                          table, registry, GeneticsConfig{}, rng);
        if (out.offspring.empty())
        {
            continue;
        }
        // 后代表型已由 settle 计算，其 Speed 必然继承自抽中的亲代。
        const auto& p = out.offspring_phenotypes[0];
        if (p.traits[trait_index(TraitKind::Speed)] > 1.0f)
        {
            ++favored_lineage;
        }
    }
    CHECK(favored_lineage > kSeasons / 2);
}

TEST(settle_is_deterministic_for_a_fixed_seed)
{
    const std::vector<Genome> genomes(8, Genome{});
    const std::vector<Phenotype> phenotypes(8, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(8, EnvironmentSnapshot{});
    const std::vector<float> threats(8, 0.0f);

    std::vector<Parent> parents;
    for (const auto& g : genomes)
    {
        parents.push_back(Parent{&g, 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};
    const std::vector<std::uint32_t> species_ids(8, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    const auto run = [&]
    {
        auto rng = RandomStreams{41, 3};
        return settle(
            SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites},
            neutral_table(), registry, GeneticsConfig{}, rng);
    };

    const auto a = run();
    const auto b = run();
    CHECK_EQ(a.offspring.size(), b.offspring.size());
    for (std::size_t i = 0; i < a.offspring.size(); ++i)
    {
        CHECK(a.offspring[i].a == b.offspring[i].a);
        CHECK(a.offspring[i].b == b.offspring[i].b);
    }
    CHECK(a.survived == b.survived);
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/genetics/population.hpp` 不存在。

- [ ] **Step 3: 创建 include/gamev2/genetics/population.hpp**

```cpp
#pragma once

#include "gamev2/genetics/breeding.hpp"
#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/species.hpp"
#include "gamev2/genetics/statistics.hpp"
#include "gamev2/genetics/survival.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::genetics {

struct SeasonInput
{
    std::span<const Genome> genomes;
    std::span<const Phenotype> phenotypes;
    std::span<const EnvironmentSnapshot> environments;
    std::span<const float> threats;
    /// 与 genomes 同序的物种标签，供第 5 步按物种分组统计
    std::span<const std::uint32_t> species_ids;
    /// 待繁殖位置及其已分桶的亲代池。空列表意味着本季无繁殖。
    std::span<const ParentPool> sites;
};

/// 子代先在 staging 里积累，整季结算完成后一次性返回。
/// 行为系统因此永远读不到半新半旧的种群。
struct SeasonOutput
{
    std::vector<Genome> offspring;
    std::vector<Phenotype> offspring_phenotypes;
    std::vector<EnvironmentSnapshot> offspring_environments;
    std::vector<std::uint8_t> survived;
    std::vector<SpeciesStatistics> stats;
};

[[nodiscard]] SeasonOutput settle(const SeasonInput& input,
                                 const LocusTable& table,
                                 const SpeciesRegistry& species,
                                 const GeneticsConfig& config,
                                 RandomStreams& streams);

}  // namespace gamev2::genetics
```

- [ ] **Step 4: 创建 src/genetics/population.cpp**

```cpp
#include "gamev2/genetics/population.hpp"

#include <algorithm>

namespace gamev2::genetics {

namespace {

/// 子代的环境快照应由地图 spec 在待繁殖位置上采样后随父池一并传入。
/// 本轮生态 spec 未交付，先继承首位亲代的环境作为降级路径——
/// 它保证「同基因型在不同环境表现不同」这条语义在接口层已经成立。
[[nodiscard]] EnvironmentSnapshot site_environment(const ParentPool& pool)
{
    return pool.environment.value_or(EnvironmentSnapshot{});
}

}  // namespace

SeasonOutput settle(const SeasonInput& input, const LocusTable& table,
                    const SpeciesRegistry& species,
                    const GeneticsConfig& config, RandomStreams& streams)
{
    SeasonOutput out{};
    out.survived.assign(input.genomes.size(), 0);

    if (input.genomes.empty())
    {
        return out;
    }

    // 第 1 步：存活筛选 + 适应力。
    std::vector<float> fitness(input.genomes.size(), 0.0f);
    float max_fecundity = 0.0f;
    for (std::size_t i = 0; i < input.genomes.size(); ++i)
    {
        max_fecundity = std::max(
            max_fecundity,
            input.phenotypes[i].traits[trait_index(TraitKind::Fecundity)]);
    }

    auto& survival_rng = streams.stream(RandomPurpose::Survival);
    for (std::size_t i = 0; i < input.genomes.size(); ++i)
    {
        if (outlived(input.phenotypes[i], 0))
        {
            continue;
        }
        const SurvivalInput s{input.phenotypes[i], input.environments[i],
                              1.0f, input.threats[i]};
        const float rate = survival_rate(s, config);
        if (uniform_unit(survival_rng) < rate)
        {
            out.survived[i] = 1;
        }
        fitness[i] = fitness(rate, input.phenotypes[i], max_fecundity);
    }

    // 第 2、3、4 步：建亲代池 → 遗传 → 表型。
    for (const auto& site : input.sites)
    {
        const auto child = make_offspring(site, table, config, streams);
        if (!child.has_value())
        {
            continue;
        }
        const EnvironmentSnapshot env = site_environment(site);
        out.offspring.push_back(*child);
        out.offspring_environments.push_back(env);
        out.offspring_phenotypes.push_back(
            phenotype_of(*child, table, species.mask(site.species), env,
                         config));
    }

    // 第 5 步：按物种分组统计。
    std::vector<SpeciesId> ids(species.species_count());
    for (std::size_t s = 0; s < ids.size(); ++s)
    {
        ids[s] = SpeciesId{static_cast<std::uint32_t>(s + 1)};
    }
    out.stats.resize(ids.size());
    for (std::size_t s = 0; s < ids.size(); ++s)
    {
        std::vector<Genome> members;
        for (std::size_t i = 0; i < input.genomes.size(); ++i)
        {
            if (input.species_ids[i] == ids[s].value)
            {
                members.push_back(input.genomes[i]);
            }
        }
        out.stats[s] = summarize(members, species.mask(ids[s]));
    }

    return out;
}

}  // namespace gamev2::genetics
```

- [ ] **Step 5: 运行测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `88 test(s), 0 failed`。

---

### Task 11: 漂变、近交衰退、确定性与性能回归

**Files:**
- Create: `tests/test_genetics_determinism.cpp`
- Create: `tests/test_genetics_performance.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 10 的 `settle` / `SeasonInput` / `SeasonOutput`
- Produces: 无新接口，只有回归护栏

- [ ] **Step 1: 写确定性与群体统计回归测试**

创建 `tests/test_genetics_determinism.cpp`：

```cpp
#include "gamev2/genetics/population.hpp"

#include "test_framework.hpp"

#include <chrono>
#include <numeric>
#include <vector>

using namespace gamev2::genetics;

namespace {

struct Fixture
{
    LocusTable table;
    GeneticsConfig config;
    SpeciesRegistry registry;
    SpeciesId species{};
    std::vector<Genome> genomes;
    std::vector<Phenotype> phenotypes;
    std::vector<EnvironmentSnapshot> environments;
    std::vector<float> threats;
    std::vector<std::uint32_t> species_ids;
};

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// n 个个体的确定性初始种群，亲代池按 radius 小隔离成若干局部桶。
Fixture make_fixture(std::size_t n, std::size_t buckets)
{
    Fixture f;
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        f.table.loci[i].kind = LocusKind::Trait;
        f.table.loci[i].allele_count = 2;
        f.table.loci[i].scale = 1.0f;
        f.table.loci[i].switch_rate = 0.0f;
        f.table.loci[i].alleles[1].dom_rank = 1;
    }
    f.config.trait_base.fill(1.0f);
    f.config.trait_min.fill(0.0f);
    f.config.trait_max.fill(10.0f);
    f.config.jitter_step = 0;
    f.species = *f.registry.register_species(all_loci());

    f.genomes.resize(n);
    f.phenotypes.resize(n);
    f.environments.resize(n);
    f.threats.assign(n, 0.0f);
    f.species_ids.assign(n, f.species.value);
    for (std::size_t i = 0; i < n; ++i)
    {
        for (auto& c : f.genomes[i].a)
        {
            c.allele = static_cast<AlleleId>(i % 2);
        }
        for (auto& c : f.genomes[i].b)
        {
            c.allele = static_cast<AlleleId>((i / 2) % 2);
        }
        f.phenotypes[i].traits.fill(1.0f);
        f.phenotypes[i].traits[trait_index(TraitKind::Lifespan)] = 1e9f;
    }
    (void)buckets;
    return f;
}

std::vector<ParentPool> bucketize(const Fixture& f, std::size_t buckets)
{
    std::vector<ParentPool> sites;
    sites.reserve(buckets);
    for (std::size_t b = 0; b < buckets; ++b)
    {
        std::vector<Parent> parents;
        for (std::size_t i = b; i < f.genomes.size(); i += buckets)
        {
            parents.push_back(Parent{&f.genomes[i], 1.0f});
        }
        sites.push_back(ParentPool{parents, f.species});
    }
    return sites;
}

}  // namespace

TEST(determinism_same_seed_gives_identical_seasons)
{
    auto f = make_fixture(64, 8);
    const auto sites = bucketize(f, 8);

    const auto run = [&](std::uint64_t seed)
    {
        std::vector<Genome> current = f.genomes;
        std::vector<Phenotype> pheno = f.phenotypes;
        std::vector<EnvironmentSnapshot> envs = f.environments;
        // 与 current 同步收缩。settle 目前只用 genomes.size() 约束下标，
        // 留长并不会越界；同步是为了让测试意图显式，也防止 settle 之后
        // 改用别的长度做索引时静默读到上一季的数据。
        std::vector<float> threats = f.threats;
        std::vector<std::uint32_t> ids = f.species_ids;
        std::vector<Genome> trace;

        for (std::uint64_t season = 0; season < 8; ++season)
        {
            std::vector<Parent> parents;
            parents.reserve(current.size());
            for (const auto& g : current)
            {
                parents.push_back(Parent{&g, 1.0f});
            }
            std::vector<ParentPool> s{ParentPool{parents, f.species}};

            RandomStreams rng{seed, season};
            const auto out = settle(
                SeasonInput{current, pheno, envs, threats, ids, s},
                f.table, f.registry, f.config, rng);

            trace.insert(trace.end(), out.offspring.begin(),
                         out.offspring.end());
            if (out.offspring.empty())
            {
                break;
            }
            current = out.offspring;
            pheno = out.offspring_phenotypes;
            envs = out.offspring_environments;
            threats.assign(current.size(), 0.0f);
            ids.assign(current.size(), f.species.value);
        }
        return trace;
    };

    const auto a = run(1234);
    const auto b = run(1234);
    const auto c = run(5678);

    CHECK_EQ(a.size(), b.size());
    CHECK(a.size() > 0);
    bool any_difference = false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!(a[i].a == b[i].a && a[i].b == b[i].b))
        {
            any_difference = true;
        }
    }
    CHECK(!any_difference);

    bool seed_matters = false;
    if (a.size() == c.size())
    {
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!(a[i].a == c[i].a && a[i].b == c[i].b))
            {
                seed_matters = true;
            }
        }
    }
    CHECK(seed_matters);
}

TEST(inbreeding_small_pools_have_higher_f)
{
    const auto measure = [](std::size_t bucket_size)
    {
        // 每个亲代池只有 bucket_size 个个体，是纯合的，因此该池内
        // H_obs 恒为 0 —— 这正是局部交配产生近交衰退的最简形态。
        auto f = make_fixture(400, 0);
        const auto sites = bucketize(f, 400 / bucket_size);
        RandomStreams rng{99, 0};
        const auto out = settle(
            SeasonInput{f.genomes, f.phenotypes, f.environments, f.threats,
                        f.species_ids, sites},
            f.table, f.registry, f.config, rng);
        return out.stats.empty() ? 0.0f : out.stats[0].f_coefficient;
    };

    const float small = measure(4);
    const float large = measure(100);
    CHECK(small > large);
}

TEST(drift_small_pools_lose_heterozygosity_faster)
{
    const auto run = [](std::size_t bucket_size, std::uint64_t seed)
    {
        auto f = make_fixture(400, 0);
        const auto sites = bucketize(f, 400 / bucket_size);
        std::vector<Genome> current = f.genomes;

        float last_h = 1.0f;
        for (std::uint64_t season = 0; season < 12; ++season)
        {
            std::vector<Parent> parents;
            parents.reserve(current.size());
            for (const auto& g : current)
            {
                parents.push_back(Parent{&g, 1.0f});
            }
            const std::vector<ParentPool> s{ParentPool{parents, f.species}};
            RandomStreams rng{seed, season};
            const auto out = settle(
                SeasonInput{current, f.phenotypes, f.environments, f.threats,
                            f.species_ids, s},
                f.table, f.registry, f.config, rng);
            if (out.offspring.empty())
            {
                break;
            }
            current = out.offspring;
            last_h = summarize(current, all_loci()).heterozygosity;
        }
        return last_h;
    };

    // 小池的末代杂合度应低于大池。阈值取 0.35/0.55，留足统计涨落余量。
    const float small = run(4, 777);
    const float large = run(100, 777);
    CHECK(small < large);
    CHECK(small < 0.55f);
}

TEST(performance_one_wan_individuals_per_season)
{
    constexpr std::size_t kN = 10000;
    auto f = make_fixture(kN, 0);

    std::vector<Parent> parents;
    parents.reserve(kN);
    for (const auto& g : f.genomes)
    {
        parents.push_back(Parent{&g, 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, f.species}};

    // Debug 构建下不放宽：这是回归护栏，用于捕捉意外的复杂度爆炸，
    // 不是对 Release 性能的承诺。超预算说明算法退化，不是机器慢。
    constexpr auto kBudget = std::chrono::milliseconds(3000);

    RandomStreams rng{5150, 0};
    const auto start = std::chrono::steady_clock::now();
    const auto out = settle(
        SeasonInput{f.genomes, f.phenotypes, f.environments, f.threats,
                    f.species_ids, sites},
        f.table, f.registry, f.config, rng);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK_EQ(out.offspring.size(), std::size_t{1});
    std::println("settle(10000) = {}", elapsed);
    CHECK(elapsed < kBudget);
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，测试引用的 `settle` 签名与 `SeasonInput` 不符，
或 `test_genetics_determinism.cpp` 未注册。

- [ ] **Step 3: 注册测试并运行**

在 `GENETICS_TEST_SOURCES` 追加 `tests/test_genetics_determinism.cpp`，
然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `93 test(s), 0 failed`。

若 `inbreeding_small_pools_have_higher_f` 或
`drift_small_pools_lose_heterozygosity_faster` 失败，**不要放宽阈值**——
先检查 `bucketize` 是否真的把小池分开了，以及 `mutate` 的 `switch_rate`
是否为 0（该 fixture 里应为 0，否则测的是突变不是漂变）。

- [ ] **Step 4: 把性能测试拆成独立文件**

把 `performance_one_wan_individuals_per_season` 从
`test_genetics_determinism.cpp` 移到新建的
`tests/test_genetics_performance.cpp`，该文件自带 `using` 与
`make_fixture` / `bucketize` 的重复定义（测试之间不共享 helper，
这是刻意的：共享 helper 会在其中一个测试改动时连带弄坏另一个）。

在 `CMakeLists.txt` 的 `GENETICS_TEST_SOURCES` 追加：

```cmake
    tests/test_genetics_performance.cpp
```

- [ ] **Step 5: 完整回归**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `93 test(s), 0 failed`。
`settle(10000) = ...` 一行，耗时在预算内。

- [ ] **Step 6: ASan 回归**

⚠️ **ASan 目标只有一个,且产物在构建树的子目录里。**
`cmake/asan` 目录产出 `game_tests_asan`,它把两个测试套件的全部源文件直接编译进去
（`GENETICS_SOURCES` / `GENETICS_TEST_SOURCES` 已在 Task 1 的 foreach 里并入），
所以一个可执行文件就覆盖 genetics 与 ecs 两套。

`build-asan` 顶层的 `genetics_tests.exe` / `ecs_tests.exe` 是**普通 Debug 目标，
没有被插桩**（用 `grep -a clang_rt.asan <exe>` 可核实）；顶层若存在
`game_tests_asan.exe` 则可能是陈旧产物，只跑 ecs 套件、早于遗传系统落地——
**跑它会得到假绿**。

```powershell
cmake -S . -B build-asan -G "Unix Makefiles" `
    -DCMAKE_CXX_COMPILER=clang++ -DGAMEV2_ENABLE_ASAN=ON
cmake --build build-asan
$env:PATH += ';C:\Program Files\LLVM\lib\clang\23\lib\windows'
Remove-Item build-asan\game_tests_asan.exe -ErrorAction SilentlyContinue
& .\build-asan\cmake\asan\game_tests_asan.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `210 test(s), 0 failed`，`exit=0`，无 sanitizer 诊断。

那行 `Remove-Item` 是防止误跑陈旧产物；若要让「按文档操作必然得到真结果」
成立，更彻底的做法是把 ASan 产物输出到构建树顶层。

---

### Task 12: ECS 适配层

**Files:**
- Modify: `include/gamev2/ecs/command_buffer.hpp`
- Create: `include/gamev2/ecs/adapt/genetics_systems.hpp`
- Create: `src/ecs/adapt/genetics_systems.cpp`
- Create: `tests/test_genetics_systems.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 10 的 `settle` / `SeasonInput` / `SeasonOutput`；
  既有 `ecs::World` / `ecs::System` / `ecs::CommandBuffer`
- Produces:
  - `ecs::CommandBuffer::create(EntityId& out)` — flush 期创建实体
  - `namespace gamev2::ecs::adapt` 下的组件
    `Genome` / `SpeciesTag` / `CreaturePhenotype` / `Environment` / `Vitality`
  - `class AgingSystem : public ecs::System`
  - `class BreedingSystem : public ecs::System`
  - `class PopulationStatisticsSystem : public ecs::System`

- [ ] **Step 1: 写失败测试**

创建 `tests/test_genetics_systems.cpp`：

```cpp
#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/world.hpp"

#include "test_framework.hpp"

#include <vector>

using namespace gamev2::ecs;
using namespace gamev2::ecs::adapt;

namespace {

genetics::SpeciesMask all_loci()
{
    genetics::SpeciesMask m{};
    for (std::size_t i = 0; i < genetics::kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

genetics::LocusTable neutral_table()
{
    genetics::LocusTable t{};
    for (std::size_t i = 0; i < genetics::kLocusCount; ++i)
    {
        t.loci[i].kind = genetics::LocusKind::Trait;
        t.loci[i].allele_count = 2;
        t.loci[i].scale = 1.0f;
        t.loci[i].switch_rate = 0.0f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

genetics::GeneticsConfig default_config()
{
    genetics::GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    return c;
}

struct Harness
{
    genetics::LocusTable table{neutral_table()};
    genetics::GeneticsConfig config{default_config()};
    genetics::SpeciesRegistry species;
    EntityId id{};
    genetics::SpeciesId sid{};

    Harness()
    {
        sid = *species.register_species(all_loci());
    }

    EntityId spawn(World& w)
    {
        const EntityId e = w.create();
        (void)w.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)w.add<CreaturePhenotype>(e, CreaturePhenotype{});
        (void)w.add<Environment>(e, Environment{});
        (void)w.add<Vitality>(e, Vitality{});
        (void)w.add<Genome>(e, Genome{});
        return e;
    }
};

}  // namespace

TEST(adapt_components_are_default_constructible)
{
    static_assert(std::is_nothrow_move_constructible_v<Genome>);
    static_assert(std::is_default_constructible_v<Genome>);
    static_assert(std::is_default_constructible_v<CreaturePhenotype>);
    static_assert(std::is_default_constructible_v<Vitality>);
    CHECK(true);
}

TEST(adapt_aging_kills_expired_creatures)
{
    World w;
    Harness h;
    const EntityId e = h.spawn(w);

    auto* v = w.get_if<Vitality>(e);
    CHECK(v != nullptr);
    v->age_seasons = 0;

    auto* p = w.get_if<CreaturePhenotype>(e);
    CHECK(p != nullptr);
    p->phenotype.traits[genetics::trait_index(genetics::TraitKind::Lifespan)]
        = 0.0f;

    CommandBuffer cmd;
    AgingSystem{h.table, h.config}.execute(w, cmd);
    cmd.flush(w);
    CHECK(!w.is_alive(e));
}

TEST(adapt_aging_keeps_living_creatures)
{
    World w;
    Harness h;
    const EntityId e = h.spawn(w);

    auto* p = w.get_if<CreaturePhenotype>(e);
    p->phenotype.traits[genetics::trait_index(genetics::TraitKind::Lifespan)]
        = 100.0f;
    auto* v = w.get_if<Vitality>(e);
    v->age_seasons = 3;

    CommandBuffer cmd;
    AgingSystem{h.table, h.config}.execute(w, cmd);
    cmd.flush(w);
    CHECK(w.is_alive(e));
    CHECK_EQ(w.get_if<Vitality>(e)->age_seasons, std::uint32_t{4});
}

TEST(adapt_breeding_creates_offspring_entities)
{
    World w;
    Harness h;
    (void)h.spawn(w);
    (void)h.spawn(w);
    CHECK_EQ(w.entity_count(), std::size_t{2});

    std::vector<EntityId> born;
    born.resize(1);
    CommandBuffer cmd;
    cmd.create(born[0]);
    cmd.flush(w);
    CHECK(w.is_alive(born[0]));
    CHECK(w.has<Genome>(born[0]));
}

TEST(adapt_command_buffer_create_reports_new_entity)
{
    World w;
    EntityId e{};
    CommandBuffer cmd;
    cmd.create(e);
    CHECK(!static_cast<bool>(e));
    cmd.flush(w);
    CHECK(static_cast<bool>(e));
    CHECK(w.is_alive(e));
}

TEST(adapt_population_statistics_reports_per_species)
{
    World w;
    Harness h;
    (void)h.spawn(w);
    (void)h.spawn(w);
    (void)h.spawn(w);

    PopulationStatisticsSystem sys{h.table, h.species};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    CHECK_EQ(sys.statistics().size(), std::size_t{1});
    CHECK_EQ(sys.statistics()[0].census, std::uint32_t{3});
    CHECK_EQ(sys.statistics()[0].loci.size(), genetics::kLocusCount);
}
```

- [ ] **Step 2: 运行确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/ecs/adapt/genetics_systems.hpp` 不存在。

- [ ] **Step 3: 给 CommandBuffer 增加 create**

在 `include/gamev2/ecs/command_buffer.hpp` 的 public 段，
`void destroy(EntityId e);` 之前插入：

```cpp
    /// 在 flush 期创建实体。
    ///
    /// World::create() 改写 slots_ / free_indices_ 且非原子，其文档明确
    /// 禁止在 System 执行期调用；而队列里没有任何创建实体的命令，
    /// BreedingSystem 就只能绕开队列直接改结构。返回值只能通过出参拿：
    /// 命令在帧末才执行，那时调用方的栈上对象可能已经失效。
    void create(EntityId& out);
```

在 `inline void CommandBuffer::destroy(EntityId e)` 之前插入实现：

```cpp
inline void CommandBuffer::create(EntityId& out)
{
    // out 传值进 lambda 再回写：捕获引用会让队列持有调用方栈上的地址，
    // flush 时那块内存可能早已复用。
    commands_.push_back(
        Command{EntityId{}, [&out](World& w) { out = w.create(); }});
}
```

`create` 不带 target，其 `EntityId{}` 会被 `flush` 里既有的「目标已失效则拒绝」
判据拦下（`slot_of` 比对版本号，实体版本从 1 起），因此必须跳过该判据。
把 `src/ecs/command_buffer.cpp` 的 `flush` 整体替换为：

```cpp
void CommandBuffer::flush(World& world)
{
    for (const auto& cmd : commands_)
    {
        // create 命令的 target 是空的：它的目标就是待创建的新实体本身，
        // 不能套用其余命令的「目标已失效则拒绝」检查。
        if (cmd.target && !world.is_alive(cmd.target))
        {
            world.report(Diagnostic{
                DiagnosticLevel::error,
                "ecs.entity_invalid",
                std::string{"命令目标实体已失效，命令被拒绝"}});
            continue;
        }
        cmd.apply(world);
    }
    commands_.clear();
}
```

判据内部逻辑与诊断字符串一字不改，只多一个 `cmd.target &&` 前置条件。

- [ ] **Step 4: 创建 include/gamev2/ecs/adapt/genetics_systems.hpp**

```cpp
#pragma once

#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/population.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::ecs::adapt {

using gamev2::genetics::Genome;

/// 名字与 genetics::SpeciesMask 的参数重名会让 using 声明歧义，
/// 因此组件层一律加 Creature / Environment 前缀。
struct SpeciesTag
{
    gamev2::genetics::SpeciesId species;
};

struct CreaturePhenotype
{
    gamev2::genetics::Phenotype phenotype;
};

struct Environment
{
    gamev2::genetics::EnvironmentSnapshot snapshot;
};

struct Vitality
{
    float survival_rate = 1.0f;
    float fitness = 1.0f;
    std::uint32_t age_seasons = 0;
};

/// 年龄递增 + 寿命硬判定。寿命是确定死亡，不是概率约束，
/// 因此不走 genetics::survival_rate。
class AgingSystem final : public System
{
public:
    AgingSystem(const gamev2::genetics::LocusTable& table,
                const gamev2::genetics::GeneticsConfig& config)
        : table_{table}, config_{config}
    {
    }

    using ReadSet = std::tuple<const CreaturePhenotype>;
    using WriteSet = std::tuple<Vitality>;

    void execute(World& world, CommandBuffer& cmd) override;

private:
    const gamev2::genetics::LocusTable& table_;
    const gamev2::genetics::GeneticsConfig& config_;
};

/// 繁殖季结算。子代实体经 CommandBuffer 在帧末创建，
/// 整季换代因此是一次原子提交。
class BreedingSystem final : public System
{
public:
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index}
    {
    }

    /// 只读并拷贝出组件，本体不写回任何组件；结构变更全走队列。
    using ReadSet = std::tuple<const Genome, const CreaturePhenotype,
                               const Environment, const SpeciesTag>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer& cmd) override;

private:
    const gamev2::genetics::LocusTable& table_;
    const gamev2::genetics::SpeciesRegistry& species_;
    const gamev2::genetics::GeneticsConfig& config_;
    std::uint64_t root_seed_;
    std::uint64_t season_index_;
};

/// 群体统计。结果由调用方取走——系统不往 World 之外写全局状态，
/// 统计结果的生命周期归调用方管。
class PopulationStatisticsSystem final : public System
{
public:
    PopulationStatisticsSystem(const gamev2::genetics::LocusTable& table,
                               const gamev2::genetics::SpeciesRegistry& species)
        : table_{table}, species_{species}
    {
    }

    using ReadSet = std::tuple<const Genome, const SpeciesTag>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer& cmd) override;

    [[nodiscard]] const std::vector<gamev2::genetics::SpeciesStatistics>&
    statistics() const noexcept
    {
        return stats_;
    }

private:
    const gamev2::genetics::LocusTable& table_;
    const gamev2::genetics::SpeciesRegistry& species_;
    std::vector<gamev2::genetics::SpeciesStatistics> stats_;
};

}  // namespace gamev2::ecs::adapt
```

- [ ] **Step 5: 创建 src/ecs/adapt/genetics_systems.cpp**

```cpp
#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/genetics/breeding.hpp"
#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/survival.hpp"

#include <algorithm>

namespace gamev2::ecs::adapt {

using namespace gamev2::genetics;

/// View 的回调只给组件引用、不给实体句柄，而 outlived 的判定需要一个句柄
/// 才能和 destroy 对接。因此两个系统都走 for_each_entity 而不是 View：
/// 需要句柄的系统本来就得随机访问，View 的价值（按 chunk 连续遍历）
/// 在这里换不来东西。
void AgingSystem::execute(World& world, CommandBuffer& cmd)
{
    (void)table_;
    (void)config_;

    std::vector<EntityId> expired;
    world.for_each_entity([&](EntityId e)
    {
        auto* v = world.get_if<Vitality>(e);
        auto* p = world.get_if<CreaturePhenotype>(e);
        if (v == nullptr || p == nullptr)
        {
            return;
        }
        ++v->age_seasons;
        if (outlived(p->phenotype, v->age_seasons))
        {
            expired.push_back(e);
        }
    });

    for (const auto& e : expired)
    {
        cmd.destroy(e);
    }
}
```

`World::for_each_entity` 目前不存在。它是让遗传学接入 ECS 的必要条件之一
（与 `CommandBuffer::create` 同批），需在
`include/gamev2/ecs/world.hpp` 的 public 段加入：

```cpp
    /// 遍历全部存活实体，回调收到真实句柄。
    ///
    /// 存活判据与 slot_of 一致：archetype 为 kInvalidArchetype 即已销毁。
    /// 版本号取自 slot 自身，因此造出的句柄一定能通过 slot_of 的校验。
    template<typename F>
    void for_each_entity(F&& f)
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].archetype == kInvalidArchetype)
            {
                continue;
            }
            f(EntityId::make(i, slots_[i].version));
        }
    }
```

作为模板成员函数定义在 `world.hpp` 内即可，不需要改 `world.cpp`。

`BreedingSystem::execute` 的实现：

```cpp
void BreedingSystem::execute(World& world, CommandBuffer& cmd)
{
    std::vector<Genome> genomes;
    std::vector<Phenotype> phenotypes;
    std::vector<EnvironmentSnapshot> environments;
    std::vector<float> threats;
    std::vector<std::uint32_t> species_ids;
    /// 与上面五个 vector 同序，用来把 settle 的 survived 下标对回实体
    std::vector<EntityId> handles;

    world.for_each_entity([&](EntityId e)
    {
        auto* g = world.get_if<Genome>(e);
        auto* p = world.get_if<CreaturePhenotype>(e);
        auto* env = world.get_if<Environment>(e);
        auto* tag = world.get_if<SpeciesTag>(e);
        if (g == nullptr || p == nullptr || env == nullptr || tag == nullptr)
        {
            return;
        }
        genomes.push_back(*g);
        phenotypes.push_back(p->phenotype);
        environments.push_back(env->snapshot);
        threats.push_back(0.0f);
        species_ids.push_back(tag->species.value);
        handles.push_back(e);
    });

    // 父池按物种整体分桶：邻域分桶属于生态 spec，本轮只保留物种隔离，
    // 它的直接后果是小物种的 F 值显著高于大物种。
    std::vector<Parent> parents;
    parents.reserve(genomes.size());
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{&genomes[i], 1.0f});
    }
    std::vector<ParentPool> sites;
    if (!species_ids.empty())
    {
        sites.push_back(ParentPool{parents,
                                   SpeciesId{species_ids.front()}});
    }

    RandomStreams streams{root_seed_, season_index_};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, environments, threats, species_ids,
                    sites},
        table_, species_, config_, streams);

    // 换代：未存活的老个体销毁，新个体经队列创建。两者都在帧末 flush
    // 时发生，行为系统不会观察到半新半旧的种群。
    //
    // handles 与 genomes 收集自同一次 for_each_entity、同序，因此
    // out.survived 的下标可直接对应回实体。
    std::vector<std::pair<EntityId, bool>> verdict;
    verdict.reserve(handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        verdict.push_back(
            {handles[i], i < out.survived.size() && out.survived[i] != 0});
    }
    // 销毁命令按 EntityId 升序提交：CommandBuffer 按入队顺序执行，
    // 固定顺序才让「同一季结算两次」得到同一份实体集。
    std::ranges::sort(verdict, {}, [](const auto& v)
                      { return v.first.value; });
    for (const auto& [e, alive] : verdict)
    {
        if (!alive)
        {
            cmd.destroy(e);
        }
    }

    for (std::size_t i = 0; i < out.offspring.size(); ++i)
    {
        EntityId e{};
        cmd.create(e);
        const EnvironmentSnapshot env =
            i < out.offspring_environments.size()
                ? out.offspring_environments[i]
                : EnvironmentSnapshot{};
        const SpeciesId sid =
            sites.empty() ? SpeciesId{} : sites.front().species;
        cmd.add<SpeciesTag>(e, SpeciesTag{sid});
        cmd.add<Environment>(e, Environment{env});
        cmd.add<Genome>(e, out.offspring[i]);
        if (i < out.offspring_phenotypes.size())
        {
            cmd.add<CreaturePhenotype>(
                e, CreaturePhenotype{out.offspring_phenotypes[i]});
        }
        else
        {
            cmd.add<CreaturePhenotype>(e, CreaturePhenotype{});
        }
        cmd.add<Vitality>(e, Vitality{});
    }
}

void PopulationStatisticsSystem::execute(World& world, CommandBuffer& cmd)
{
    (void)cmd;

    // 按 species_ids 的取值分桶，桶的插入顺序即 species_ 的注册顺序，
    // 因此 stats() 的下标稳定对应 SpeciesId::value - 1。
    std::vector<std::vector<Genome>> buckets(species_.species_count());
    world.for_each_entity([&](EntityId e)
    {
        auto* g = world.get_if<Genome>(e);
        auto* tag = world.get_if<SpeciesTag>(e);
        if (g == nullptr || tag == nullptr)
        {
            return;
        }
        const std::size_t slot = tag->species.value - 1;
        if (slot < buckets.size())
        {
            buckets[slot].push_back(*g);
        }
    });

    stats_.clear();
    stats_.reserve(buckets.size());
    for (std::size_t i = 0; i < buckets.size(); ++i)
    {
        const SpeciesId sid{static_cast<std::uint32_t>(i + 1)};
        stats_.push_back(summarize(buckets[i], species_.mask(sid)));
    }
}

}  // namespace gamev2::ecs::adapt
```

```

文件顶部的 include 需补 `#include "gamev2/genetics/statistics.hpp"` 与
`<utility>`。

- [ ] **Step 6: 注册源文件与测试**

在 `CMakeLists.txt` 的 `GENETICS_SOURCES` 追加：

```cmake
    src/ecs/adapt/genetics_systems.cpp
```

在 `GENETICS_TEST_SOURCES` 追加：

```cmake
    tests/test_genetics_systems.cpp
```

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\genetics_tests.exe
```

Expected: `99 test(s), 0 failed`。

- [ ] **Step 7: 既有 ECS 测试不得回归**

`genetics_tests` 链接 `ecs`，但既有 `ecs_tests` 的行为也必须不变：

```powershell
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `N test(s), 0 failed`，`exit=0`，其中 N 与改动前一致。
`CommandBuffer::create` 的加入不得影响既有 `destroy` / `add` / `remove`
的语义——若 `ecs_tests` 出现失败，说明 Step 3 里 `if (cmd.target)` 的
包裹改错了缩进或作用域，回退重做。

---

## 验收标准

全部任务完成后，以下每一条都必须成立：

1. `cmake --build build` 无警告无错误
2. `& .\build\genetics_tests.exe` 输出 `96 test(s), 0 failed`
3. `& .\build\ecs_tests.exe` 输出 `N test(s), 0 failed`，N 与改动前一致
4. ASan 构建下两个测试可执行文件均无 sanitizer 报告
5. `settle(10000)` 在 Debug 构建下 3 秒内完成
6. 同一种子两次结算逐位相同（`determinism_same_seed_gives_identical_seasons`）
7. 小亲代池的 `F` 显著高于大亲代池
8. 小亲代池的杂合度衰减快于大亲代池
9. `genetics/` 不 include 任何 `ecs/` 头文件
10. 全部代码无一行超过 90 字符

## 已知偏离与遗留

- `apply_environment` 当前是恒零占位，只保证结构字段未被环境触碰。
  生态 spec 交付后填充实际公式。
- `site_environment` 继承首位亲代的环境，是生态 spec 未交付时的降级路径。
- `BreedingSystem` 的父池只按物种分桶，未做空间邻域分桶。
  邻域半径属于生态 spec；在此之前所有个体同物种即可交配，
  近交衰退因此只由小种群驱动，不含地理隔离成分。
- `make_offspring` 允许同一亲代被抽中两次（自交）。真实地理隔离末期
  只有单个个体的物种需要这个行为，但本轮未区分「自交」与「近亲交配」，
  两者的近交系数估计共用同一条路径。

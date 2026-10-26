# 众生层刀 1 实施计划：骨架与开局

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给既有 worldgen / history / faction 三层之上搭出**众生层**
（`gamev2::life`）的骨架：个体组件、从当代格局（8914）初始化众生的
种子逻辑、玩家选身位用的开局名册，以及名册的文本导出。这是设计文档
§11 的刀 1——「玩家选任意职位或散修开局」的最小可验证基础。

**Architecture:** 双引擎架构（设计 §1.1）里角色层的第一块砖：
`life` 拥有自己的 `ecs::World`，读 `World`（取种子）与 `EraReport`
（取开局年、当代格局的有无），产出 `Roster`。本刀**不碰**派系层的
任何行为，只读它的产物；**没有**年循环（刀 2 才有时钟与 system）。
人口规模与金字塔是世界常数（设定 §8 量级反推），属性档位由
`(seed, Stream::life, 序号)` 无状态生成——同 seed 全字段逐位相同。

**只有一个公共入口**：`life::Life`（构造 + `seed()` + `roster()`）
与 `life::export_roster`。生成逻辑住私有头 `src/life/generate.hpp`，
测试不包含它。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、
零第三方依赖（含测试框架）。

**Spec:** `doc/2026-10-26-life-layer-design.md`（本文的全部语义依据；
冲突时以设计文档为准，并回报差异——见文末「与设计文档的差异与补齐」）

**既有文档:** `doc/2026-10-20-worldview-xianxia.md`（设定：九境寿元表
§5.1、灵石经济 §6、势力 §8）、`doc/2026-10-22-faction-evolution-design.md`
（派系层）、`doc/plans/2026-10-22-faction-evolution-implementation.md`
（本计划的格式与纪律来源）

---

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 编译器固定为 clang++（CMakeLists 已钉住，不得绕开）
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- 任何一行不得超过 90 个字符，超长行必须手动折行
- Windows 环境下通过 `pwsh` 调用终端
- **不得执行 `git commit` / `git add` / `git stash` / `git clean`**：
  每个任务末尾改为「向用户报告并等待提交指令」，改动留在工作树
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- 每个新随机流必须同时登记进 `Stream` 枚举与 `kStreamIds` 数组；
  `static_assert(stream_ids_are_distinct())` 会拦住重复流号
- **本层只读**：不写 `World` 的任何字段，不写 `Ledger` 的任何字段，
  不写 `faction` 的任何类型；依赖方向 `life → faction → history →
  worldgen`，外加 `ecs`
- ECS 组件必须满足 `register_component<T>()` 的 `static_assert`：
  默认可构造、`nothrow` 可移动构造、`alignof` 不超过 `max_align_t`
- **测试只走公共入口**：`life::Life`、`life::export_roster`、
  `life` 的公共类型与常数；私有头 `src/life/generate.hpp` 不被任何
  测试包含
- **每新建一个 `src/life/` 下的 .cpp，同步加进 CMakeLists 的
  `LIFE_SOURCES`**；漏加的后果是「新文件没被编译」，链接失败或行为
  诡异，不会给出「你忘了加源文件」的提示
- 测试里生成世界的成本要克制：用 128×128（history/faction 层已验证
  该尺寸可用），不用默认 256×256
- 每个字段只有一个写入者（既有不变式，本刀要守住）：

  | 字段 | 唯一写入者 |
  |---|---|
  | `World` 的全部字段 | worldgen 的 18 个阶段（本刀只读） |
  | `Ledger` 的全部字段 | `build_ledger`（本刀只读） |
  | `EraReport` 的全部字段 | `faction::simulate`（本刀只读） |
  | `SoulId`、`Cultivation`、`Aptitude`、`Temper`、`Mortal`、`Office` | 建实体那一步（`Life::seed`），此后只读（后续刀的 system 接管各自组件时再改写入者表） |

---

## 与设计文档的差异与补齐（实现前必须先解决的口径问题）

1. **`Stream::life` 是 20，不是 19**。设计 §4.1 写「life = 19」时
   假定 18 空闲，但 `tribulation` 已经占了 19（`enums.hpp` 注释：
   「派系层先发布，本层让号改占 19」）。本计划以 **life = 20、
   `kStreamIds` 扩到 21** 为准，并在 Task 1 Step 1 修正设计文档。
2. **设计 §10 的文件清单是终态草图**。刀 1 的测试文件叫
   `tests/test_life_setup.cpp`（初始化与名册的测试；§10 列的
   `test_life_determinism.cpp` 等随刀 2 落地）。`chronicle.hpp` 本刀
   **不建**：`export_roster` 与 `LifeDiagnostics` 声明在
   `interface.hpp`，实现在 `src/life/chronicle.cpp`；刀 7 的编年
   导出再立 `chronicle.hpp`。
3. **`Life` 构造签名是 `(world, era, diagnostics)`，不收 ledger**。
   本刀只消费「有没有当代」（`era.veins` 非空）与「开局年」
   （`era.now`），两者都在 `EraReport` 上。刀 3 经济需要逐宗脉账时
   从 `era.veins` 取，不提前收参数。
4. **组件按刀分批建**（设计 §2.1 是终态表）：刀 1 建 6 个——
   `SoulId`、`Cultivation`、`Aptitude`、`Temper`、`Mortal`、`Office`。
   `Purse` 随刀 3，`Standing`/`Bonds`/`Motive` 随刀 4。建了没人读写的
   组件是死代码。`Office` 本刀只有 `kind`（在位年数、上位途径随
   刀 4/刀 6）。

---

## 文件结构

```
新建:
  include/gamev2/life/soul.hpp        组件与枚举（Realm、OfficeKind、六组件）
  include/gamev2/life/constants.hpp   世界常数（人口表、金字塔、寿元、姓名表）
  include/gamev2/life/interface.hpp   Roster/RosterEntry、LifeDiagnostics、
                                      Life、export_roster
  src/life/generate.hpp               私有头：PersonInit、generate_population
  src/life/generate.cpp               人口生成与职位分配
  src/life/setup.cpp                  Life 的构造、seed、名册回读
  src/life/chronicle.cpp              export_roster（Task 2 建）
  tests/test_life_setup.cpp           刀 1 全部测试

修改:
  include/gamev2/world/enums.hpp      Stream::life = 20；kStreamIds 扩到 21
  CMakeLists.txt                      LIFE_SOURCES / life / LIFE_TEST_SOURCES
                                      / life_tests
  doc/2026-10-26-life-layer-design.md §4.1 Stream 号修正（Task 1 Step 1）
```

---

## 测试夹具（两条夹具守卫本身就是探针）

主夹具：**128×128、z_levels=3、seed=13**。history 层的
`ledger_never_double_counts_overmining` 已在 128²/seed 13 上验证过
台账与渡劫候选池非空，脉必然在。

差异夹具（「换 seed 必须不同」测试）：**seed=14**。它没有被独立
验证过，所以那条测试带 `CHECK(!b.era.veins.empty())` 守卫——若 seed 14
在 128² 上没有当代脉，测试直接红，换一个 history 层用过的 128² 种子
（11 / 26 / 42），把最终选中的种子写进夹具注释。**不用单独写探针**：
守卫红了就是探针读数。

退化夹具：**64×64、z_levels=3、seed=5、spirit_year=8299**——当代早于
纪元 8300，`build_ledger` 直接返回空台账，`faction::simulate` 返回空
格局。64² 足够快（约 21 ms），且这条路径不需要任何脉。

---

### Task 1: 骨架——流号、组件、常数、众生初始化与名册

**Files:**
- Modify: `doc/2026-10-26-life-layer-design.md`（§4.1 一行）
- Modify: `include/gamev2/world/enums.hpp`（Stream 与 kStreamIds）
- Create: `include/gamev2/life/soul.hpp`
- Create: `include/gamev2/life/constants.hpp`
- Create: `include/gamev2/life/interface.hpp`
- Create: `src/life/generate.hpp`
- Create: `src/life/generate.cpp`
- Create: `src/life/setup.cpp`
- Create: `tests/test_life_setup.cpp`
- Modify: `CMakeLists.txt`（life 目标与 life_tests 目标，插在
  faction 块之后、`add_executable(worldgen_demo ...)` 之前）

**Interfaces:**
- Consumes: `gamev2::world::World` / `WorldParams`（`params().seed`、
  `params().spirit_year`）、`gamev2::faction::EraReport`（`now`、
  `veins`）、`gamev2::world::stage_seed` / `Pcg32` / `Stream`、
  `gamev2::ecs::World::create/add/get_if/view`
- Produces（Task 2 与后续刀依赖的精确签名）:
  - `gamev2::life::Realm`、`OfficeKind` 枚举
  - `gamev2::life::SoulId`、`Cultivation`、`Aptitude`、`Temper`、
    `Mortal`、`Office` 六个组件
  - `gamev2::life::kPopulation`（8 元素，按 `Sect` 序）、
    `kRealmLifespan`（9 元素）、`kMinAge`、`kMinElders`、
    `kElderDivisor`、`kInnerNum`/`kInnerDen`、`kSectPyramid`、
    `kFreePyramid`、`kSurnames`、`kGivenNames`
  - `gamev2::life::RosterEntry`、`Roster`、`LifeDiagnostics`
  - `gamev2::life::Life`：`Life(const world::World&, const
    faction::EraReport&, LifeDiagnostics&)`、`void seed()`、
    `const Roster& roster() const noexcept`、
    `ecs::World& ecs_world() noexcept`、`std::uint32_t year() const noexcept`

- [ ] **Step 1: 修正设计文档的 Stream 号**

`doc/2026-10-26-life-layer-design.md` §4.1，把

```
遍历顺序无关**。新增 `Stream::life = 19`（`kStreamIds` 同步扩到 20，
`static_assert` 守重复）。
```

改为（若行已折成两行，按语义等价地改，保持每行 ≤ 90 字符）：

```
遍历顺序无关**。新增 `Stream::life = 20`（19 已被渡劫占用，
`kStreamIds` 同步扩到 21，`static_assert` 守重复）。
```

- [ ] **Step 2: 登记随机流**

`include/gamev2/world/enums.hpp`，在 `tribulation = 19,` 之后追加：

```cpp
    /// 众生层的个体生成抽样（属性、姓名、年龄）。
    ///
    /// 19 已被渡劫占用，本层让号改占 20。改号会换掉整份开局名册，
    /// 所以这条一旦定下就不要再动。
    life = 20,
```

同文件 `kStreamIds`：类型从 `std::array<std::uint32_t, 20>` 改为
`std::array<std::uint32_t, 21>`，数组末尾（`Stream::tribulation` 那项
之后）加一行：

```cpp
    static_cast<std::uint32_t>(Stream::life),
```

- [ ] **Step 3: 写失败的测试（七条）**

`tests/test_life_setup.cpp` 全文。此步写完后**还没有任何 life 的
头文件与源文件**，下一步的 CMake 与构建就是预期的红：

```cpp
#include "gamev2/ecs/component.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

/// 本刀的主夹具：128×128。history 层已在 128² 上验证过台账可用；
/// seed 13 与 14 的当代格局由测试里的 veins 守卫钉住（见夹具节）。
gamev2::world::WorldParams roster_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

/// world + 台账 + 当代格局的一整套。world 存 optional：EraReport 不
/// 拥有世界，而 Life 还要读它，整套必须活到测试结束。
struct Stack
{
    std::optional<gamev2::world::World> world;
    gamev2::faction::EraReport era;
};

Stack make_stack(const gamev2::world::WorldParams& p)
{
    Stack stack;
    auto generated = gamev2::world::generate_world(p);
    if (!generated)
    {
        return stack;
    }
    stack.world = std::move(*generated);
    gamev2::history::LedgerDiagnostics ldiag;
    const auto ledger =
        gamev2::history::build_ledger(*stack.world, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    stack.era = gamev2::faction::simulate(*stack.world, ledger, fdiag);
    return stack;
}

std::uint32_t total_population()
{
    std::uint32_t n = 0;
    for (const auto& c : gamev2::life::kPopulation)
    {
        n += c;
    }
    return n;
}

}  // namespace

TEST(the_life_components_satisfy_the_ecs_constraints)
{
    // register_component 的 static_assert 守着「默认可构造、nothrow
    // 可移动、对齐不超限」。这条测试再守一层：六件组件必须是六个
    // **互不相同**的组件——共用一个 id 会让两个字段挤进同一列，
    // 静默串数据。
    using gamev2::ecs::component_id_of;
    const std::array ids{
        component_id_of<gamev2::life::SoulId>(),
        component_id_of<gamev2::life::Cultivation>(),
        component_id_of<gamev2::life::Aptitude>(),
        component_id_of<gamev2::life::Temper>(),
        component_id_of<gamev2::life::Mortal>(),
        component_id_of<gamev2::life::Office>(),
    };
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ids.size(); ++j)
        {
            CHECK(!(ids[i] == ids[j]));
        }
    }
}

TEST(the_same_seed_gives_the_same_roster)
{
    // 设计 §4.1 的断言形态：同 seed 重跑，全部个体的全部字段逐位相同。
    const auto p = roster_world(13);
    const Stack a = make_stack(p);
    const Stack b = make_stack(p);
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!a.era.veins.empty());

    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();

    const auto& r1 = life1.roster();
    const auto& r2 = life2.roster();
    // 规模是常数表给的，不是碰巧相等。
    CHECK_EQ(r1.entries.size(), total_population());
    CHECK_EQ(r1.entries.size(), r2.entries.size());
    CHECK_EQ(r1.year, a.era.now);
    for (std::size_t i = 0; i < r1.entries.size(); ++i)
    {
        const auto& x = r1.entries[i];
        const auto& y = r2.entries[i];
        CHECK_EQ(x.soul, y.soul);
        CHECK(x.name == y.name);
        CHECK(x.sect == y.sect);
        CHECK(x.office == y.office);
        CHECK(x.realm == y.realm);
        CHECK_EQ(x.age, y.age);
        CHECK(x.temper.greed == y.temper.greed);
        CHECK(x.temper.valor == y.temper.valor);
        CHECK(x.temper.order == y.temper.order);
        CHECK(x.temper.mercy == y.temper.mercy);
        CHECK(x.temper.vigor == y.temper.vigor);
        CHECK_EQ(x.aptitude.comprehension, y.aptitude.comprehension);
        CHECK_EQ(x.aptitude.sense, y.aptitude.sense);
        CHECK_EQ(x.aptitude.body, y.aptitude.body);
        CHECK_EQ(x.aptitude.fortitude, y.aptitude.fortitude);
    }
    CHECK_EQ(d1.no_contemporary_state, 0u);
    CHECK_EQ(d2.no_contemporary_state, 0u);
}

TEST(the_different_seed_gives_a_different_roster)
{
    // 守「随机真的被用过」：把 Stream::life 换成常量，上面那条同 seed
    // 的测试照样绿——必须有一条测试红在「换 seed 名册不变」上。
    // 两条 veins 守卫是 seed 14 的探针：红了就换种子（见夹具节）。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(14));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!a.era.veins.empty());
    CHECK(!b.era.veins.empty());

    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();

    const auto& r1 = life1.roster();
    const auto& r2 = life2.roster();
    CHECK_EQ(r1.entries.size(), r2.entries.size());
    bool differs = false;
    for (std::size_t i = 0; i < r1.entries.size(); ++i)
    {
        if (r1.entries[i].name != r2.entries[i].name
            || r1.entries[i].age != r2.entries[i].age
            || r1.entries[i].temper.greed != r2.entries[i].temper.greed)
        {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST(the_population_pyramid_matches_the_constants)
{
    // 设计 §1.4：境界分布是消耗金字塔的反推——引气最多、逐境递减、
    // 元婴寥寥、化神以上一个都没有（谷底期养不起）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::OfficeKind;
    using gamev2::life::Realm;
    constexpr std::size_t kSectKinds = 7;  // 七宗；第 8 位是无主
    std::array<std::uint32_t, kSectKinds> sect_population{};
    std::uint32_t free_count = 0;

    for (const auto& e : life.roster().entries)
    {
        const auto r = static_cast<std::size_t>(e.realm);
        // 化神及以上：谷底期的当代一个都容不下。分布表写 0，这里从
        // 名册侧钉住它真的没被生成——七宗与散修一起查。
        CHECK(r <= static_cast<std::size_t>(Realm::yuanying));
        if (e.sect == gamev2::history::Sect::unowned)
        {
            CHECK(e.office == OfficeKind::none);
            ++free_count;
            continue;
        }
        ++sect_population[static_cast<std::size_t>(e.sect)];
    }

    // 七宗逐宗核对规模与逐境递减。
    for (std::size_t sct = 0; sct < kSectKinds; ++sct)
    {
        const std::uint32_t n = gamev2::life::kPopulation[sct];
        CHECK_EQ(sect_population[sct], n);

        std::array<std::uint32_t, static_cast<std::size_t>(Realm::count)>
            counts{};
        for (const auto& e : life.roster().entries)
        {
            if (static_cast<std::size_t>(e.sect) == sct)
            {
                ++counts[static_cast<std::size_t>(e.realm)];
            }
        }
        for (std::size_t r = 1; r < static_cast<std::size_t>(Realm::count);
             ++r)
        {
            CHECK(counts[r - 1] >= counts[r]);
        }
        // 引气至少三成，否则金字塔是倒着长的。
        CHECK(counts[0] >= n * 3u / 10u);
        // 元婴寥寥：一宗至多一个。
        CHECK(counts[static_cast<std::size_t>(Realm::yuanying)] <= 1u);
    }

    // 散修按常数表单列。
    CHECK_EQ(free_count,
             gamev2::life::kPopulation[static_cast<std::size_t>(
                 gamev2::history::Sect::unowned)]);
}

TEST(nobody_outlives_their_realm)
{
    // 九境寿元逐字来自设定 §5.1。年龄在生成时就折半封顶，这里再从
    // 名册侧独立复算一遍——生成器写错（越界下标、忘了封顶）时红的
    // 是这条，而不是一条莫名其妙的越界崩溃。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (const auto& e : life.roster().entries)
    {
        CHECK(e.age >= gamev2::life::kMinAge);
        const auto r = static_cast<std::size_t>(e.realm);
        CHECK(e.age < gamev2::life::kRealmLifespan[r]);
    }
}

TEST(sect_leaders_are_zhuji_or_above)
{
    // 职位是资源与权力的阶梯（设计 §2.3）：宗主长老必须从筑基以上挑，
    // 金丹才脱离得了宗门供给体系（设定 §5.1）。外门内门不设门槛。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::OfficeKind;
    using gamev2::life::Realm;
    std::array<std::uint32_t, 7> masters{};
    std::array<std::uint32_t, 7> elders{};
    for (const auto& e : life.roster().entries)
    {
        const bool leading = e.office == OfficeKind::sect_master
                             || e.office == OfficeKind::elder;
        if (leading)
        {
            CHECK(e.realm >= Realm::zhuji);
        }
        if (e.sect == gamev2::history::Sect::unowned)
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(e.sect);
        if (sect >= masters.size())
        {
            continue;
        }
        masters[sect] += (e.office == OfficeKind::sect_master) ? 1u : 0u;
        elders[sect] += (e.office == OfficeKind::elder) ? 1u : 0u;
    }
    for (std::size_t i = 0; i < masters.size(); ++i)
    {
        const std::uint32_t n = gamev2::life::kPopulation[i];
        const std::uint32_t by_divisor = n / gamev2::life::kElderDivisor;
        const std::uint32_t expect_elders =
            (by_divisor > gamev2::life::kMinElders)
                ? by_divisor
                : gamev2::life::kMinElders;
        CHECK_EQ(masters[i], 1u);
        CHECK_EQ(elders[i], expect_elders);
    }
}

TEST(a_world_before_the_epoch_has_no_populace)
{
    // 退化不是错误（设计 §7）：当代早于纪元 8300 时台账为空、格局为空，
    // 没有当代格局就没有众生可初始化，诊断报出，名册为空。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 5;
    p.spirit_year = 8299;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics ldiag;
    const auto ledger = gamev2::history::build_ledger(*generated, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto era = gamev2::faction::simulate(*generated, ledger, fdiag);
    CHECK(era.veins.empty());

    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*generated, era, diag);
    life.seed();
    CHECK(life.roster().entries.empty());
    CHECK_EQ(diag.no_contemporary_state, 1u);
}
```

- [ ] **Step 4: CMake 目标——预期的红**

`CMakeLists.txt`：在 `add_executable(worldgen_demo ...)` 之前插入
（`chronicle.cpp` 属于 Task 2，此时还不存在，不列）：

```cmake
set(LIFE_SOURCES
    src/life/generate.cpp
    src/life/setup.cpp
)
add_library(life STATIC ${LIFE_SOURCES})
target_include_directories(life PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(life PUBLIC faction ecs)

set(LIFE_TEST_SOURCES
    tests/main.cpp
    tests/test_life_setup.cpp
)
add_executable(life_tests ${LIFE_TEST_SOURCES})
target_link_libraries(life_tests PRIVATE life)
```

Run: `cmake --build build`
Expected: **红**——`Cannot find source file src/life/generate.cpp`
（或等价错误）。这是预期的失败：目标立起来了，实现还不存在。
（头文件此时也不存在，但 CMake 的缺源文件错误先到。）

- [ ] **Step 5: 写组件与常数头**

`include/gamev2/life/soul.hpp`：

```cpp
/// @file soul.hpp
/// @brief 众生层：个体的组件与身份枚举。
#pragma once

#include "gamev2/history/ledger.hpp"

#include <cstdint>
#include <string>

namespace gamev2::life {

/// 九境（设定 §5.1）。名字用拼音，与 history::Sect 的命名一致。
enum class Realm : std::uint8_t
{
    yinqi,     ///< 引气，寿 120
    lianqi,    ///< 炼气，寿 150
    zhuji,     ///< 筑基，寿 300
    jindan,    ///< 金丹，寿 600
    yuanying,  ///< 元婴，寿 1200
    huashen,   ///< 化神，寿 2400
    lianxu,    ///< 炼虚，寿 5000
    heti,      ///< 合体，寿 10000
    dacheng,   ///< 大乘，近乎无穷（给一个远超任何玩法跨度的数）
    count,     ///< 哨兵：境数，不是境界
};

/// 宗门职位（设计 §2.3）。散修与未任职者是 none。
enum class OfficeKind : std::uint8_t
{
    sect_master, ///< 宗主
    elder,       ///< 长老
    inner,       ///< 内门弟子
    outer,       ///< 外门弟子
    none,        ///< 散修或未任职
    count,       ///< 哨兵：职位数，不是职位
};

/// 身份。建实体时写一次，此后只读（写入者表见实施计划 Global
/// Constraints；后续刀的迁移事件是经批准的例外）。
struct SoulId
{
    std::uint32_t index = 0;  ///< 创建次序：身位的稳定标识
    std::string name;
    history::Sect sect = history::Sect::unowned;
};

/// 境界。境界内进度随刀 2 加入。
struct Cultivation
{
    Realm realm = Realm::yinqi;
};

/// 能力四轴（1..9 档）。天生的，出生后不变。
struct Aptitude
{
    std::uint8_t comprehension = 1;  ///< 悟性
    std::uint8_t sense = 1;          ///< 神识
    std::uint8_t body = 1;           ///< 体魄
    std::uint8_t fortitude = 1;      ///< 韧性
};

/// 性格五轴（1..5 档，3 为中值）。
///
/// 档位来自种子、行为来自规则（设计 §2.1 的数值纪律）：轴只做岔路
/// 选择器，不做连续效用参数。设计文本里的「悖逆轴高」就是 order 轴低。
struct Temper
{
    std::uint8_t greed = 3;  ///< 1 节制 … 5 贪婪
    std::uint8_t valor = 3;  ///< 1 怯懦 … 5 勇烈
    std::uint8_t order = 3;  ///< 1 悖逆 … 5 守序
    std::uint8_t mercy = 3;  ///< 1 酷烈 … 5 仁厚
    std::uint8_t vigor = 3;  ///< 1 慵懒 … 5 进取
};

/// 年岁。寿元上限不存——它由境界决定（kRealmLifespan），存两份会
/// 在突破的那一刻静默分岔。
struct Mortal
{
    std::uint32_t birth_year = 0;
    std::uint32_t age = 0;
};

/// 职位。在位年数与上位途径随刀 4 / 刀 6 加入。
struct Office
{
    OfficeKind kind = OfficeKind::none;
};

}  // namespace gamev2::life
```

`include/gamev2/life/constants.hpp`：

```cpp
/// @file constants.hpp
/// @brief 众生层的世界常数。设定给定的逐字采用，标定值注明锚点。
#pragma once

#include "gamev2/life/soul.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace gamev2::life {

/// 各宗与散修的开局人口，按 history::Sect 的序（含 unowned 末位）。
///
/// 设定 §8 的量级：七宗各百余、散修数百、魔修数十。标定值——换这组
/// 数只平移绝对规模，不改金字塔结构（设计 §6）。
inline constexpr std::array<std::uint32_t, 8> kPopulation{{
    140,  // 太玄宗
    120,  // 丹霞谷
     90,  // 寒江剑府
    100,  // 万兽山
     90,  // 青囊谷
     60,  // 天机阁
     70,  // 九幽宫
    300,  // 无主（散修）
}};

/// 九境寿元（设定 §5.1，逐字）。大乘「近乎无穷」：给一个远大于任何
/// 玩法跨度的数，让「死于寿元」在大乘身上不发生。
inline constexpr std::array<std::uint32_t, 9> kRealmLifespan{{
    120, 150, 300, 600, 1200, 2400, 5000, 10000, 1000000,
}};

/// 最小入册年龄：低于此岁不入名册（弟子是少年，不是婴儿）。
inline constexpr std::uint32_t kMinAge = 16;

/// 长老数 = max(kMinElders, N / kElderDivisor)。制度性常量（设计 §6）。
inline constexpr std::uint32_t kMinElders = 3;
inline constexpr std::uint32_t kElderDivisor = 20;

/// 内门占比 = kInnerNum / kInnerDen，其余是外门。
inline constexpr std::uint32_t kInnerNum = 2;
inline constexpr std::uint32_t kInnerDen = 5;

/// 人口金字塔（万分比，逐境求和 = 10000）。
///
/// 七宗：引气 45%、炼气 38%、筑基 12%、金丹 4%、元婴 1%。
/// 反推自设定 §6.1 的消耗金字塔——谷底期的供给撑不起高境，
/// 元婴一人一年吃掉天下年产八分之一。
inline constexpr std::array<std::uint32_t, 9> kSectPyramid{{
    4500, 3800, 1200, 400, 100, 0, 0, 0, 0,
}};

/// 散修：更扁（设定 §6.4 的三十三年一枚下品），筑基以上只有 10%。
inline constexpr std::array<std::uint32_t, 9> kFreePyramid{{
    5500, 3500, 900, 100, 0, 0, 0, 0, 0,
}};

/// 姓名表。设定没给姓名表——这是内容常数，不是数值旋钮；同名不冲突，
/// 身位的唯一标识是 SoulId::index。窄字面量就是 UTF-8 字节（本仓库
/// 源文件即 UTF-8），拼进 std::string 没有编码链问题。
inline constexpr std::array<std::string_view, 16> kSurnames{{
    "陈", "林", "沈", "陆", "苏", "叶", "顾", "谢",
    "裴", "宋", "白", "孟", "温", "韩", "江", "燕",
}};

inline constexpr std::array<std::string_view, 32> kGivenNames{{
    "云", "松", "竹", "兰", "石", "溪", "岩", "岚",
    "霜", "雪", "风", "雷", "山", "泽", "川", "渊",
    "霖", "尘", "霄", "鹤", "鸣", "珂", "瑜", "琰",
    "璟", "珩", "玄", "素", "微", "澜", "怀", "默",
}};

}  // namespace gamev2::life
```

- [ ] **Step 6: 写接口头**

`include/gamev2/life/interface.hpp`：

```cpp
/// @file interface.hpp
/// @brief 众生层的公共入口：初始化众生、导出名册。
#pragma once

#include "gamev2/ecs/world.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gamev2::life {

/// 名册条目：玩家选身位时看到的一行（设计 §5.1）。
struct RosterEntry
{
    std::uint32_t soul = 0;
    std::string name;
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t age = 0;
    Temper temper;
    Aptitude aptitude;
};

/// 开局名册。
struct Roster
{
    std::uint32_t year = 0;
    std::vector<RosterEntry> entries;
};

/// 诊断（设计 §7）。其余字段随各自的刀加入：本刀只消费
/// 「有没有当代」这一条。
struct LifeDiagnostics
{
    /// 当代早于纪元：没有当代格局，就没有众生可初始化。
    std::uint32_t no_contemporary_state = 0;
};

/// 众生层的容器。刀 1 只做种子与名册；年循环在刀 2。
class Life
{
public:
    Life(const gamev2::world::World& world,
         const gamev2::faction::EraReport& era,
         LifeDiagnostics& diagnostics);

    /// 从当代格局初始化众生，写实体与名册。重复调用是调用方的错，
    /// 不做幂等兜底。
    void seed();

    [[nodiscard]] const Roster& roster() const noexcept
    {
        return roster_;
    }
    [[nodiscard]] gamev2::ecs::World& ecs_world() noexcept
    {
        return ecs_world_;
    }
    [[nodiscard]] std::uint32_t year() const noexcept { return year_; }

private:
    gamev2::ecs::World ecs_world_;
    const gamev2::world::World& world_;
    const gamev2::faction::EraReport& era_;
    LifeDiagnostics& diagnostics_;
    std::uint32_t year_ = 0;
    Roster roster_;
    std::vector<gamev2::ecs::EntityId> souls_;
};

/// 导出人可读名册文本。返回 false 表示文件写不动。
[[nodiscard]] bool export_roster(const Roster& roster,
                                 const std::filesystem::path& path);

}  // namespace gamev2::life
```

- [ ] **Step 7: 写生成器与 Life 实现**

`src/life/generate.hpp`（私有头，测试不得包含）：

```cpp
/// @file generate.hpp
/// @brief 私有头：人口生成的中间形态。只被 setup.cpp 包含。
#pragma once

#include "gamev2/life/constants.hpp"
#include "gamev2/life/soul.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gamev2::life {

/// 一个尚未进 ECS 的人。
struct PersonInit
{
    std::string name;
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t age = 0;
    Temper temper;
    Aptitude aptitude;
};

/// 按 (seed, Stream::life, 序号) 无状态生成全体众生。
///
/// 返回序 = 创建序 = 名册序。职位在生成后按宗门就地指派。
[[nodiscard]] std::vector<PersonInit> generate_population(
    std::uint64_t seed);

}  // namespace gamev2::life
```

`src/life/generate.cpp`：

```cpp
/// @file generate.cpp
/// @brief 众生层：人口生成与职位指派。
#include "generate.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gamev2::life {
namespace {

/// 一个人的全部随机字段。无状态派生：只依赖 (seed, 流号, 序号)，
/// 与遍历顺序无关（设计 §4.1——调整循环次序不得换一个世界）。
PersonInit make_person(std::uint64_t seed, std::uint32_t index,
                       history::Sect sect, Realm realm)
{
    const auto stream = static_cast<std::uint32_t>(world::Stream::life);
    world::Pcg32 rng(
        world::stage_seed(world::stage_seed(seed, stream), index));

    PersonInit p;
    p.name = std::string(kSurnames[rng.below(
                 static_cast<std::uint32_t>(kSurnames.size()))])
             + std::string(kGivenNames[rng.below(
                 static_cast<std::uint32_t>(kGivenNames.size()))]);
    p.sect = sect;
    p.realm = realm;

    // 年龄折半封顶：名册里不该有「行将就木还只是引气」的人；寿元的
    // 硬上限由 kRealmLifespan 的测试从名册侧独立复算。
    const auto lifespan = kRealmLifespan[static_cast<std::size_t>(realm)];
    p.age = kMinAge + rng.below(lifespan / 2u - kMinAge);

    p.temper.greed = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.valor = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.order = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.mercy = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.vigor = static_cast<std::uint8_t>(1u + rng.below(5));
    p.aptitude.comprehension = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.sense = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.body = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.fortitude = static_cast<std::uint8_t>(1u + rng.below(9));
    p.office = OfficeKind::none;
    return p;
}

/// 给一个宗门指派职位。[begin, begin+n) 是该宗在表里的区间。
void assign_offices(std::vector<PersonInit>& people, std::size_t begin,
                    std::uint32_t n)
{
    // 宗主与长老从筑基以上挑：境界是资历的硬门槛——金丹才脱离得了
    // 宗门供给体系，掌门至少得是能自立的人（设定 §5.1）。
    std::vector<std::size_t> leaders;
    for (std::size_t i = begin; i < begin + n; ++i)
    {
        const auto realm = static_cast<std::size_t>(people[i].realm);
        if (realm >= static_cast<std::size_t>(Realm::zhuji))
        {
            leaders.push_back(i);
        }
    }
    // 境界高的在前；stable_sort 让同境界保持创建序（资历）。
    std::stable_sort(leaders.begin(), leaders.end(),
                     [&](std::size_t a, std::size_t b) {
                         return people[a].realm > people[b].realm;
                     });

    const std::uint32_t elders =
        std::max(kMinElders, n / kElderDivisor);
    // 金字塔保证筑基以上有 N×17% 人：最小的宗（60 人）也有 10 人。
    // 常数表若改到撑不起职位，先在这里红。
    assert(leaders.size() >= 1u + elders);

    people[leaders[0]].office = OfficeKind::sect_master;
    for (std::uint32_t i = 1; i <= elders; ++i)
    {
        people[leaders[i]].office = OfficeKind::elder;
    }

    // 其余人按创建序填：内门先来，余下的全是外门。
    const std::uint32_t inner_quota = n * kInnerNum / kInnerDen;
    std::uint32_t inner = 0;
    for (std::size_t i = begin; i < begin + n; ++i)
    {
        if (people[i].office != OfficeKind::none)
        {
            continue;
        }
        people[i].office =
            (inner < inner_quota) ? OfficeKind::inner : OfficeKind::outer;
        ++inner;
    }
}

}  // namespace

std::vector<PersonInit> generate_population(std::uint64_t seed)
{
    std::vector<PersonInit> out;
    out.reserve(970);  // kPopulation 之和；只是保留，改常数表不破

    for (std::size_t s = 0; s < kPopulation.size(); ++s)
    {
        const auto sect = static_cast<history::Sect>(s);
        const std::uint32_t n = kPopulation[s];
        const auto& pyramid = (sect == history::Sect::unowned)
                                  ? kFreePyramid
                                  : kSectPyramid;

        const std::size_t begin = out.size();

        // 金字塔按万分比截断：余数给引气，总数必须等于 n——截断偏差
        // 不许改变人口规模，否则名册规模会随常数表漂移出测试的预期。
        std::array<std::uint32_t, static_cast<std::size_t>(Realm::count)>
            realms{};
        std::uint32_t placed = 0;
        for (std::size_t r = 0; r < realms.size(); ++r)
        {
            realms[r] = n * pyramid[r] / 10000u;
            placed += realms[r];
        }
        realms[0] += n - placed;

        for (std::size_t r = 0; r < realms.size(); ++r)
        {
            for (std::uint32_t i = 0; i < realms[r]; ++i)
            {
                out.push_back(make_person(
                    seed, static_cast<std::uint32_t>(out.size()), sect,
                    static_cast<Realm>(r)));
            }
        }

        if (sect != history::Sect::unowned)
        {
            assign_offices(out, begin, n);
        }
    }
    return out;
}

}  // namespace gamev2::life
```

`src/life/setup.cpp`：

```cpp
/// @file setup.cpp
/// @brief 众生层：从当代格局建实体、回读名册。
#include "gamev2/life/interface.hpp"
// 私有头与本文件同目录，不在也不该在 life 目标的包含路径里。
#include "generate.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace gamev2::life {

Life::Life(const gamev2::world::World& world,
           const gamev2::faction::EraReport& era,
           LifeDiagnostics& diagnostics)
    : world_(world),
      era_(era),
      diagnostics_(diagnostics),
      year_(era.now)
{
}

void Life::seed()
{
    // 当代格局是众生的出身地：没有当代（台账为空）就没有可初始化的
    // 众生。这是退化不是错误，由诊断报出（设计 §7）。
    if (era_.veins.empty())
    {
        ++diagnostics_.no_contemporary_state;
        return;
    }

    const std::vector<PersonInit> people =
        generate_population(world_.params().seed);

    souls_.reserve(people.size());
    for (std::uint32_t i = 0; i < people.size(); ++i)
    {
        const PersonInit& p = people[i];
        const gamev2::ecs::EntityId e = ecs_world_.create();
        souls_.push_back(e);
        (void)ecs_world_.add<SoulId>(e, SoulId{i, p.name, p.sect});
        (void)ecs_world_.add<Cultivation>(e, Cultivation{p.realm});
        (void)ecs_world_.add<Aptitude>(e, p.aptitude);
        (void)ecs_world_.add<Temper>(e, p.temper);
        (void)ecs_world_.add<Mortal>(e, Mortal{year_ - p.age, p.age});
        (void)ecs_world_.add<Office>(e, Office{p.office});
    }

    // 名册从实体回读，不从 PersonInit 抄：这样「组件真的挂上了」是
    // 被名册测试顺手钉住的事实（照 faction collect() 的先例）。
    roster_.year = year_;
    roster_.entries.reserve(souls_.size());
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = ecs_world_.get_if<SoulId>(e);
        const auto* cult = ecs_world_.get_if<Cultivation>(e);
        const auto* apt = ecs_world_.get_if<Aptitude>(e);
        const auto* temper = ecs_world_.get_if<Temper>(e);
        const auto* mortal = ecs_world_.get_if<Mortal>(e);
        const auto* office = ecs_world_.get_if<Office>(e);
        // 这六件都是本函数亲手挂上的：缺任何一件是内部错误，不是要
        // 兜住的运行时情形。
        assert(id != nullptr && cult != nullptr && apt != nullptr
               && temper != nullptr && mortal != nullptr
               && office != nullptr);

        RosterEntry entry;
        entry.soul = id->index;
        entry.name = id->name;
        entry.sect = id->sect;
        entry.office = office->kind;
        entry.realm = cult->realm;
        entry.age = mortal->age;
        entry.temper = *temper;
        entry.aptitude = *apt;
        roster_.entries.push_back(entry);
    }
}

}  // namespace gamev2::life
```

- [ ] **Step 8: 构建并运行——期望变绿**

Run: `cmake --build build`
Expected: 全部目标编译通过。

Run: `& .\build\life_tests.exe`
Expected: 七条 TEST 全过、零 FAIL。两处夹具判定：

- `the_different_seed_gives_a_different_roster` 若红在
  `!b.era.veins.empty()`——seed 14 在 128² 上没有当代脉，换 seed
  （11 / 26 / 42，都是 history 层用过的 128² 种子），把最终种子写进
  `roster_world` 的注释；
- 若红在 `differs == false`——随机源没起作用，检查 `Stream::life`
  是否真的进了 `make_person` 的派生。

- [ ] **Step 9: 全量回归**

Run: `cmake --build build` 后依次
`& .\build\ecs_tests.exe`、`& .\build\worldgen_tests.exe`、
`& .\build\history_tests.exe`、`& .\build\faction_tests.exe`
Expected: 全绿。`Stream` 与 `kStreamIds` 的改动是纯追加，既有随机流
一位未动；若有既有测试红了，说明动了不该动的号——回滚核对。

- [ ] **Step 10: 向用户报告并等待提交指令**

报告内容：七条新测试的名字与结果、四个既有套件的回归结果、夹具
种子的最终选择。**不执行任何 git 命令。**

---

### Task 2: 名册导出

**Files:**
- Create: `src/life/chronicle.cpp`
- Modify: `CMakeLists.txt`（`LIFE_SOURCES` 加 `src/life/chronicle.cpp`）
- Modify: `tests/test_life_setup.cpp`（追加两条测试与所需 include）

**Interfaces:**
- Consumes: Task 1 的 `Roster` / `RosterEntry` / `Life` / 常数
- Produces: `bool export_roster(const Roster&, const std::filesystem::path&)`
  （声明已在 Task 1 的 `interface.hpp` 里，本任务只补实现）

- [ ] **Step 1: 写失败的测试**

`tests/test_life_setup.cpp`：顶部 include 区加三行——

```cpp
#include <filesystem>
#include <fstream>
#include <string>
```

（`<string>` 若已在 include 区则不重复。）文件末尾追加：

```cpp
TEST(the_roster_is_exportable_as_text)
{
    // 名册是玩家选身位的界面（设计 §5.1），导不出文本就选不了。
    // 行数必须精确：表头一行 + 逐人一行。上界式的行数断言在这里
    // 不够——本文件没有别的段落，行数就是内容的全集。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "life_roster_test.txt";
    CHECK(gamev2::life::export_roster(life.roster(), path));

    std::ifstream in(path);
    CHECK(in.good());
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        lines.push_back(line);
    }
    CHECK_EQ(lines.size(), life.roster().entries.size() + 1u);
    // 表头要能说出这是哪一年、共多少人。
    CHECK(lines[0].contains("灵历 8914"));
    CHECK(lines[0].contains("970 人"));
    // 名册里必须看得见宗主那一行——玩家开局选宗主要靠它。
    bool saw_master = false;
    for (const auto& line : lines)
    {
        if (line.contains("宗主"))
        {
            saw_master = true;
            break;
        }
    }
    CHECK(saw_master);
}

TEST(an_empty_roster_still_exports)
{
    // 退化路径（设计 §7）：没有众生的名册照样是一份可读的文件，
    // 只是没有人。写不出文件才是错误。
    const gamev2::life::Roster empty;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "life_roster_empty.txt";
    CHECK(gamev2::life::export_roster(empty, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string first;
    CHECK(static_cast<bool>(std::getline(in, first)));
    CHECK(first.contains("共 0 人"));
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build`
Expected: 链接失败——`undefined reference to
gamev2::life::export_roster(...)`（或 MSVC 等价的 LNK2019）。这是
预期的红：声明在 Task 1 已经进了头文件，实现还不存在。

- [ ] **Step 3: 实现导出**

`src/life/chronicle.cpp`：

```cpp
/// @file chronicle.cpp
/// @brief 众生层：名册的文本导出。
#include "gamev2/life/interface.hpp"

#include <fstream>
#include <string_view>

namespace gamev2::life {
namespace {

std::string_view sect_name(history::Sect sect)
{
    switch (sect)
    {
        case history::Sect::taixuan: return "太玄宗";
        case history::Sect::daxia: return "丹霞谷";
        case history::Sect::hanjiang: return "寒江剑府";
        case history::Sect::wanshou: return "万兽山";
        case history::Sect::qiangnang: return "青囊谷";
        case history::Sect::tianji: return "天机阁";
        case history::Sect::jiuyou: return "九幽宫";
        case history::Sect::unowned: return "散修";
        default: return "?";
    }
}

std::string_view office_name(OfficeKind office)
{
    switch (office)
    {
        case OfficeKind::sect_master: return "宗主";
        case OfficeKind::elder: return "长老";
        case OfficeKind::inner: return "内门";
        case OfficeKind::outer: return "外门";
        case OfficeKind::none: return "无职";
        default: return "?";
    }
}

std::string_view realm_name(Realm realm)
{
    switch (realm)
    {
        case Realm::yinqi: return "引气";
        case Realm::lianqi: return "炼气";
        case Realm::zhuji: return "筑基";
        case Realm::jindan: return "金丹";
        case Realm::yuanying: return "元婴";
        case Realm::huashen: return "化神";
        case Realm::lianxu: return "炼虚";
        case Realm::heti: return "合体";
        case Realm::dacheng: return "大乘";
        default: return "?";
    }
}

}  // namespace

bool export_roster(const Roster& roster, const std::filesystem::path& path)
{
    std::ofstream out(path);
    if (!out)
    {
        return false;
    }

    out << "众生名册 · 灵历 " << roster.year << " · 共 "
        << roster.entries.size() << " 人\n";
    for (const auto& e : roster.entries)
    {
        out << e.soul << ' ' << e.name << ' ' << sect_name(e.sect) << ' '
            << office_name(e.office) << ' ' << realm_name(e.realm) << ' '
            << e.age << ' ' << static_cast<unsigned>(e.temper.greed) << ' '
            << static_cast<unsigned>(e.temper.valor) << ' '
            << static_cast<unsigned>(e.temper.order) << ' '
            << static_cast<unsigned>(e.temper.mercy) << ' '
            << static_cast<unsigned>(e.temper.vigor) << ' '
            << static_cast<unsigned>(e.aptitude.comprehension) << ' '
            << static_cast<unsigned>(e.aptitude.sense) << ' '
            << static_cast<unsigned>(e.aptitude.body) << ' '
            << static_cast<unsigned>(e.aptitude.fortitude) << '\n';
    }
    return out.good();
}

}  // namespace gamev2::life
```

- [ ] **Step 4: CMake 挂源文件**

`CMakeLists.txt` 的 `LIFE_SOURCES` 加一行：

```cmake
    src/life/chronicle.cpp
```

- [ ] **Step 5: 构建并运行，确认变绿**

Run: `cmake --build build`，再 `& .\build\life_tests.exe`
Expected: 九条 TEST 全过、零 FAIL。

- [ ] **Step 6: 全量回归**

Run: 依次 `& .\build\ecs_tests.exe`、
`& .\build\worldgen_tests.exe`、`& .\build\history_tests.exe`、
`& .\build\faction_tests.exe`
Expected: 全绿（本任务只加了一个 cpp 与两条测试，不动任何既有行为）。

- [ ] **Step 7: 向用户报告并等待提交指令**

报告内容：两条新测试的结果、全量回归结果、导出文件的样例前五行。
**不执行任何 git 命令。**

---

## 完成判据（刀 1 验收，对照设计 §11）

1. `life` 与 `life_tests` 目标立起来，九条测试全绿，四个既有套件
   回归全绿。
2. 同 seed 两次初始化，名册全字段逐位相同（确定性初始化）。
3. 人口金字塔形状符合常数表：逐境递减、元婴每宗至多一个、化神以上
   为零、引气至少三成。
4. 退化路径（当代早于纪元）：空名册 + 诊断报出。
5. 名册可导出为文本，行数与内容可断言。

刀 2（修炼与寿元）、刀 3（经济）……按设计 §11 的切法各自另出计划，
不在本文范围。

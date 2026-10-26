# 众生层刀 2 实施计划：修炼与寿元

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让众生层第一次**活起来**：启动年循环（时钟、老化、补员、
修炼四个 system），众生逐年老去、死亡、出生、突破境界——刀 1 的
静态名册变成一段可复算的百年演化。

**Architecture:** `Life` 增加 `run_years(n)`：每一年按设计 §1.2 的前四步
（Clock → Aging → Birth → Cultivate）跑一遍 `ecs::Scheduler`。
四个 system 一律遍历 `Life` 维护的**实体序号表**（`get_if` 读组件、
空指针即已死——`View::each` 不传 `EntityId`，拿不到销毁所需的句柄）。
结构性变更照 ECS 既有惯例：出生走 `world.create()` + `cmd.add<T>()`，
死亡走 `cmd.destroy()`（`Scheduler::run` 末尾自动 flush）。
刀 2 **没有**玩家窗口与派系交接（步 5–9 随刀 4–7）。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**Spec:** `doc/2026-10-26-life-layer-design.md`（语义依据；§1.2 年循环、
§2 组件与行为、§2.4 突破与寿元、§8 测试、§11 刀 2 范围与验收）
**前置计划:** `doc/plans/2026-10-26-life-skeleton-implementation.md`（刀 1；
其「与设计文档的差异」裁定继续有效）

---

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23；编译器固定 clang++；禁止第三方库（含测试框架）；
  禁止 `shared_ptr`/`unique_ptr`；禁止头文件 `using namespace`
- 注释 doxygen `///` 只解释「为什么」；任何一行不超过 90 个字符
- Windows 下经 `pwsh` 调终端；**不得执行 `git add`/`git commit`/`git stash`/
  `git clean`**：每个任务末尾向用户报告并等待提交指令
- 随机性一律走 `Pcg32`；本刀不新增流号：姓名/属性/突破掷运全部走已有的
  `Stream::life`，按 `(seed, 流号, 年, 序号/实体槽号)` 无状态派生
- **本层只读**：不写 `World`/`Ledger`/faction 字段；依赖方向
  `life → faction → history → worldgen` + `ecs`
- ECS 惯例（`tests/test_integration.cpp` 的 Spawner 先例）：system 里
  `world.create()` 直建实体、组件经 `cmd.add<T>()` 落地、销毁走
  `cmd.destroy(e)`；`Scheduler::run` 自带 flush
- **system 一律遍历实体序号表**（`std::vector<EntityId>`）做需要句柄的
  事；`get_if` 返回空指针 = 实体已死，跳过
- **不使用 `View::each_parallel`**（行为依赖批内位置）
- **每新建 `src/life/` 下的 .cpp 必须同步加进 CMakeLists 的 `LIFE_SOURCES`**
- **测试只走公共入口**：`life::Life`、`ecs_world()` 上的视图、公共类型与
  常数；私有头 `src/life/systems.hpp` 与 `src/life/generate.hpp` 不被任何
  测试包含
- 本刀的字段写入者表（继承刀 1 并新增）：

  | 字段 | 唯一写入者 |
  |---|---|
  | `SoulId`、`Aptitude`、`Temper` | 建实体（`Life::seed` 或 `BirthSystem`），此后只读 |
  | `LifeClock` | `ClockSystem` |
  | `Mortal` | `AgingSystem`（`BirthSystem` 只在建实体时写新生儿） |
  | `Cultivation` | `CultivateSystem`（建实体时写初值） |
  | `Office` | 建实体（收徒写外门、散修写无职）；晋升不在本刀 |
  | `Roster` | `Life::seed` 一次性快照，此后不更新 |

- 刀 1 的七条测试（`tests/test_life_setup.cpp`）必须原样全绿

---

## 与设计文档的差异与补齐（实现前必须先解决的口径问题）

1. **时钟组件叫 `LifeClock`**，不叫设计 §2.2 表里的 `SimClock`：
   `faction::SimClock` 已存在（还带潮位），同名跨层会误导读者。
2. **`BirthSystem` 提前进刀 2**。设计 §11 没把「补员」分给任何刀，但
   不补员金字塔几十年内塌底（初始引气/炼气要么老死要么晋升），刀 3
   的经济与刀 4 的决策就没有对象。本刀做最小补员：宗门年收徒 +
   散修每十年一批（设定 §8.3 逐字）。凡人聚落数字仍不建模（设计 §0.3）。
3. **刀 2 的修炼假设「供给充足」**：突破只判年资 + 悟性概率，设定
   §5.1 的资源门槛不记账——记账是刀 3 的 `Purse`。本刀全部修炼相关
   常数是标定值，注明「刀 3 重标」。
4. **名册是开局快照，不随推演更新**（设计 §5.1：名册的语义是玩家
   开局选身位的那份名录）。当前活着的众生一律从 `ecs_world()` 视图读。
5. **`Cultivation` 增加 `progress` 字段**（刀 1 注释已预告）。刀 1 的
   聚合初始化 `Cultivation{p.realm}` 不受影响（progress 默认 0）。
6. **新生实体的 `SoulId::index` 从 970 起单调递增**（`Life` 持有计数器，
   `seed()` 初始化为初始人口数）。初始队列与新生队列按 index 可分。
7. **`Life::year()` 去掉 `const`**：它读的是可变推演状态（时钟），
   与 `ecs_world()` 非 const 同理由。刀 1 没有任何测试调用过 `year()`，
   改签名不破坏既有断言。
8. **刀 1 终审挂起 Minor 的归属**：`assert` 在 NDEBUG 下消失——保留
   现状（debug 下测试先红），本刀不动；内外门断言、「散修」措辞锁定、
   导出测试空守卫——三条归刀 6，本刀不做。

---

## 文件结构

```
修改:
  include/gamev2/life/soul.hpp        + LifeClock；Cultivation + progress
  include/gamev2/life/constants.hpp   + 年资/突破/收徒/散修批 四组常数
  include/gamev2/life/interface.hpp   + Life::run_years；year() 去 const；
                                      私有成员 +clock_ +souls_ 沿用 +next_index_
  src/life/generate.hpp               make_person 声明（出匿名空间）
  src/life/generate.cpp               make_person 移出匿名空间
  src/life/setup.cpp                  seed() 建时钟与 next_index_；run_years；
                                      year() 实现
  CMakeLists.txt                      LIFE_SOURCES + systems.cpp
新建:
  src/life/systems.hpp                私有头：四个 system 类
  src/life/systems.cpp                四个 system 的实现
  tests/test_life_cultivation.cpp     本刀六条测试 + 夹具
```

---

### Task 1: 时钟与年循环骨架

**Files:**
- Modify: `include/gamev2/life/soul.hpp`（+ `LifeClock`；`Cultivation` +
  `progress`）
- Modify: `include/gamev2/life/interface.hpp`（+ `run_years`；`year()` 去
  const；私有成员 +`clock_` +`next_index_`，删 `year_`）
- Modify: `src/life/setup.cpp`（seed 建时钟；`year()` 实现；`run_years`）
- Create: `src/life/systems.hpp`（四个 system 的**完整声明**，构造签名
  按 Interfaces 块——Task 2–4 只补实现体，不再改签名）
- Create: `src/life/systems.cpp`（ClockSystem 实现 + 另三个空实现，
  空实现里写明「Task N 落地」的 `///` 说明）
- Modify: `CMakeLists.txt`
- Create: `tests/test_life_cultivation.cpp`

**Interfaces:**
- Consumes: 刀 1 的 `Life`、`LifeDiagnostics`、`kPopulation`、
  `ecs::System`/`Scheduler`/`CommandBuffer`、`world::Stream::life`
- Produces:
  - `gamev2::life::LifeClock { std::uint32_t year = 0; }`（soul.hpp）
  - `Cultivation` 现为 `{ Realm realm; std::uint32_t progress = 0; }`
  - `void Life::run_years(std::uint32_t n);`
  - `std::uint32_t Life::year();`（非 const；`seed()` 后 == `era.now`）
  - 私有头 `src/life/systems.hpp` 的四个类（精确构造签名）：
    - `ClockSystem(gamev2::ecs::EntityId clock)`
    - `AgingSystem(const std::vector<gamev2::ecs::EntityId>& souls)`
    - `BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
      std::vector<gamev2::ecs::EntityId>& souls, std::uint32_t& next_index)`
    - `CultivateSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
      const std::vector<gamev2::ecs::EntityId>& souls)`
  - `[[nodiscard]] PersonInit make_person(std::uint64_t seed,
    std::uint32_t index, history::Sect sect, Realm realm);`
    （generate.hpp；Task 3 才用，声明在本任务不动——`make_person` 的
    移出匿名空间是 Task 3 的事）

- [ ] **Step 1: 写失败的测试**

`tests/test_life_cultivation.cpp` 全文（本任务只有第一条测试，后续任务
在同一文件追加；夹具与刀 1 同口径，名字不得改）：

```cpp
#include "gamev2/ecs/world.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

/// 刀 2 的夹具：与刀 1 同口径（128×128、z3、seed 13/14）。
gamev2::world::WorldParams roster_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

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

/// 数一数活着的、谓词命中的众生（视图里能读到 SoulId 的就是活的）。
template<typename F>
std::uint32_t count_alive(gamev2::life::Life& life, F&& pred)
{
    std::uint32_t n = 0;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            if (pred(id))
            {
                ++n;
            }
        });
    return n;
}

std::uint32_t initial_population()
{
    std::uint32_t n = 0;
    for (const auto& c : gamev2::life::kPopulation)
    {
        n += c;
    }
    return n;
}

}  // namespace

TEST(the_clock_starts_at_the_era_and_advances)
{
    // 时钟从当代起算（设计 §1.4：8914 开局），每 run_years(1) 推一格。
    // 名册的 year 是开局快照，不随推演变（差异与补齐 §4）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    CHECK(!s.era.veins.empty());
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    CHECK_EQ(life.year(), s.era.now);
    life.run_years(5);
    CHECK_EQ(life.year(), s.era.now + 5u);
    CHECK_EQ(life.roster().year, s.era.now);
}
```

- [ ] **Step 2: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 编译失败——`Life` 没有 `run_years`（或 `year()` const 不匹配）。
这是预期的红。

- [ ] **Step 3: 组件与接口**

`include/gamev2/life/soul.hpp`：`Cultivation` 改为（`///` 注释随字段）：

```cpp
/// 境界与境内年资。
struct Cultivation
{
    Realm realm = Realm::yinqi;
    /// 境内年资（刀 1 注释预告的进度轴）。CultivateSystem 逐年写。
    std::uint32_t progress = 0;
};
```

文件末尾（`Office` 之后）加：

```cpp
/// 年时钟。与 faction::SimClock 无关（那个还带潮位），故不同名。
struct LifeClock
{
    std::uint32_t year = 0;
};
```

`include/gamev2/life/interface.hpp`：`Life` 公有区加

```cpp
    /// 推进 n 年：设计 §1.2 年循环的前四步（Clock→Aging→Birth→
    /// Cultivate）。玩家窗口与派系交接随后续刀加入。
    void run_years(std::uint32_t n);

    /// 当前年（seed 后等于开局年，run_years 后逐年推进）。非 const：
    /// 读的是可变的推演状态，与 ecs_world() 同理由。
    [[nodiscard]] std::uint32_t year();
```

（删除刀 1 的 `[[nodiscard]] std::uint32_t year() const noexcept ...`
声明。）私有区把 `std::uint32_t year_ = 0;` 换成：

```cpp
    gamev2::ecs::EntityId clock_{};
    /// 新生实体的下一个序号。从初始人口总数起单调递增，让「初始队列
    /// vs 新生队列」可按 index 区分。
    std::uint32_t next_index_ = 0;
```

（刀 1 已有 `souls_` 成员，沿用。）

- [ ] **Step 4: 四个 system 的声明与 ClockSystem 实现**

`src/life/systems.hpp`（私有头；测试不得包含）：

```cpp
/// @file systems.hpp
/// @brief 众生层的私有头：年循环的四个 system。
///
/// 不放进公共头：这些类型是实现细节。调用方只需要 Life 与
/// interface.hpp；测试要读的东西必须从 ecs_world() 的视图上读到。
#pragma once

#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"
#include "gamev2/life/soul.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::life {

/// 时钟。每年把年推进一格（设计 §1.2 步 1）。
class ClockSystem final : public gamev2::ecs::System
{
public:
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<LifeClock>;

    explicit ClockSystem(gamev2::ecs::EntityId clock);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_;
};

/// 老化。年老一岁，寿尽者死（设计 §1.2 步 2、§2.4 寿元表）。
class AgingSystem final : public gamev2::ecs::System
{
public:
    /// `const LifeClock` 进读集不是数据依赖，是调度次序声明：不声明
    /// 它，Aging 会与写钟的 ClockSystem 落进同一批（照 faction
    /// AssessSystem 的先例）。
    ///
    /// 读 Cultivation：寿元上限由境界定（kRealmLifespan），组件里
    /// 刻意不存——两份上限会在突破那一刻静默分岔。
    using ReadSet = std::tuple<const LifeClock, const Mortal,
                               const Cultivation>;
    using WriteSet = std::tuple<Mortal>;

    explicit AgingSystem(const std::vector<gamev2::ecs::EntityId>& souls);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    const std::vector<gamev2::ecs::EntityId>& souls_;
};

/// 补员。宗门年收徒 + 散修十年一批（设计 §1.2 步 3、差异与补齐 §2）。
class BirthSystem final : public gamev2::ecs::System
{
public:
    using ReadSet = std::tuple<const LifeClock, const SoulId>;
    using WriteSet = std::tuple<>;

    BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                std::vector<gamev2::ecs::EntityId>& souls,
                std::uint32_t& next_index);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    std::vector<gamev2::ecs::EntityId>& souls_;
    std::uint32_t& next_index_;
};

/// 修炼。年资 +1，够门槛掷悟性，中则突破（设计 §1.2 步 4、§2.4）。
class CultivateSystem final : public gamev2::ecs::System
{
public:
    using ReadSet = std::tuple<const LifeClock, const Cultivation,
                               const Aptitude>;
    using WriteSet = std::tuple<Cultivation>;

    CultivateSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                    const std::vector<gamev2::ecs::EntityId>& souls);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
};

}  // namespace gamev2::life
```

`src/life/systems.cpp`：本任务只实现 `ClockSystem`；另三个给**空
execute 脚手架**（Task 2–4 会整体替换它们，这不是半成品，是让时钟
任务独立可测）：

```cpp
/// @file systems.cpp
/// @brief 众生层：年循环的四个 system。
#include "systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"

#include <cassert>

namespace gamev2::life {

ClockSystem::ClockSystem(gamev2::ecs::EntityId clock) : clock_(clock)
{
}

void ClockSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer&)
{
    auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // seed 亲手挂的：缺了是内部错误
    ++clock->year;
}

AgingSystem::AgingSystem(
    const std::vector<gamev2::ecs::EntityId>& souls)
    : souls_(souls)
{
}

void AgingSystem::execute(gamev2::ecs::World&,
                          gamev2::ecs::CommandBuffer&)
{
    // Task 2 落地：年老一岁，寿尽者死。
}

BirthSystem::BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                         std::vector<gamev2::ecs::EntityId>& souls,
                         std::uint32_t& next_index)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      next_index_(next_index)
{
}

void BirthSystem::execute(gamev2::ecs::World&,
                          gamev2::ecs::CommandBuffer&)
{
    // Task 3 落地：宗门年收徒、散修十年一批。
    (void)seed_;
    (void)clock_;
}

CultivateSystem::CultivateSystem(
    std::uint64_t seed, gamev2::ecs::EntityId clock,
    const std::vector<gamev2::ecs::EntityId>& souls)
    : seed_(seed), clock_(clock), souls_(souls)
{
}

void CultivateSystem::execute(gamev2::ecs::World&,
                              gamev2::ecs::CommandBuffer&)
{
    // Task 4 落地：年资 +1，够门槛掷悟性。
    (void)seed_;
    (void)clock_;
    (void)souls_;
}

}  // namespace gamev2::life
```

- [ ] **Step 5: setup.cpp**

`src/life/setup.cpp`：顶部加

```cpp
#include "systems.hpp"
#include "gamev2/ecs/scheduler.hpp"
```

`seed()` 在名册回读之后追加：

```cpp
    // 时钟从当代起算：开局即 8914，run_years 每年推一格。
    clock_ = ecs_world_.create();
    (void)ecs_world_.add<LifeClock>(clock_, LifeClock{era_.now});
    // 新生序号从初始人口总数起：初始队列的 index 全部小于它。
    next_index_ = static_cast<std::uint32_t>(roster_.entries.size());
```

`seed()` 里原有的 `roster_.year = year_;` 改为
`roster_.year = era_.now;`（`year_` 成员已删）。

文件末尾（namespace 内）加 `year()` 与 `run_years`：

```cpp
std::uint32_t Life::year()
{
    auto* clock = ecs_world_.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // 时钟是 seed 亲手挂的：缺了是内部错误
    return clock->year;
}

void Life::run_years(std::uint32_t n)
{
    // 注册顺序即执行顺序（Scheduler::plan 按注册顺序贪心分层，冲突者
    // 落后一批）：Clock 写年，另三个读它（各自读集里都有 const
    // LifeClock）；Aging 写 Mortal，另两个不碰。串行是刻意的：本层
    // 规模下并行零收益，each_parallel 的行为又依赖批内位置。
    ecs::Scheduler scheduler(1);
    ClockSystem clock(clock_);
    AgingSystem aging(souls_);
    BirthSystem birth(world_.params().seed, clock_, souls_, next_index_);
    CultivateSystem cultivate(world_.params().seed, clock_, souls_);
    scheduler.add(clock);
    scheduler.add(aging);
    scheduler.add(birth);
    scheduler.add(cultivate);
    scheduler.plan();

    ecs::CommandBuffer cmd;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        scheduler.run(ecs_world_, cmd);
    }
}
```

- [ ] **Step 6: CMake 与构建——期望变绿**

`CMakeLists.txt` 的 `LIFE_SOURCES` 在 `chronicle.cpp` 之后加：

```cmake
    src/life/systems.cpp
```

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 8 条 0 failed（刀 1 七条原样 + 时钟一条）。Step 2 的编译红
是本任务唯一预期的红；此处全绿才说明时钟独立于其余三步可测。

- [ ] **Step 7: 回归**

依次 `& .\build\ecs_tests.exe`、`& .\build\worldgen_tests.exe`、
`& .\build\history_tests.exe`、`& .\build\faction_tests.exe`
Expected: 111 / 181 / 48 / 37 全绿。

- [ ] **Step 8: 向用户报告并等待提交指令**

报告：测试清单与结果。**不执行任何 git 命令。**

---

### Task 2: 老化与寿元死亡

**Files:**
- Modify: `src/life/systems.cpp`（替换 AgingSystem 的空实现；补
  `<cstddef>` include）
- Modify: `tests/test_life_cultivation.cpp`（追加两条测试 + 一个探针）

**Interfaces:**
- Consumes: Task 1 的 `AgingSystem`（`souls_` 序号表）、`kRealmLifespan`、
  `ecs::CommandBuffer::destroy`、`count_alive`/`initial_population`
  夹具
- Produces: 寿元死亡行为（Task 3/4 的补员与修炼建立其上）

- [ ] **Step 1: 写失败的测试**

`tests/test_life_cultivation.cpp` 追加：

```cpp
TEST(nobody_ever_outlives_their_realm)
{
    // 寿元表逐字来自设定 §5.1（kRealmLifespan 是唯一权威，组件里不存
    // 上限）。逐年断言：活着的众生年龄严格小于该境寿元——死发生在
    // 「满寿元」那一年，所以年末在册者都还差一岁。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (std::uint32_t y = 0; y < 150; ++y)
    {
        life.run_years(1);
        std::uint32_t alive = 0;
        life.ecs_world().view<const gamev2::life::Mortal,
                              const gamev2::life::Cultivation>().each(
            [&](const gamev2::life::Mortal& m,
                const gamev2::life::Cultivation& c)
            {
                ++alive;
                const auto r = static_cast<std::size_t>(c.realm);
                CHECK(m.age < gamev2::life::kRealmLifespan[r]);
            });
        CHECK(alive > 0u);
    }
}

TEST(the_cohort_shrinks_as_the_years_pass)
{
    // 寿元死亡不是死代码：跑 200 年，初始队列（index < 初始人口数）
    // 必须有人死去，同时世界不能空。
    //
    // 上下界由探针实测钉死（Step 3）：初始 970 人里，悟性最低、终生
    // 未突破的引气/炼气死于寿元；筑基以上（初始 133 人）最大年龄
    // +200 仍小于寿元，全活——所以存活者下界取 100 有硬依据。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    life.run_years(200);

    const std::uint32_t survivors = count_alive(
        life, [&](const gamev2::life::SoulId& id)
        { return id.index < initial_population(); });
    const std::uint32_t total = count_alive(
        life, [](const gamev2::life::SoulId&) { return true; });
    CHECK(survivors < initial_population());
    CHECK(survivors > 100u);
    CHECK(total > 0u);
}
```

- [ ] **Step 2: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 红——`nobody_ever_outlives_their_realm`：空实现下 210 岁的
引气还在册（初始最大 60 + 150 年 > 120）；`the_cohort_shrinks...` 红在
`survivors == initial_population()`。

- [ ] **Step 3: 实现 AgingSystem + 探针钉界**

`src/life/systems.cpp`：替换 AgingSystem 空实现：

```cpp
void AgingSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    // 遍历序号表而非 view.each：销毁需要 EntityId，而 each 只给组件
    // 引用。空 get_if = 死者留下的空位，跳过。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* mortal = world.get_if<Mortal>(e);
        const auto* cult = world.get_if<Cultivation>(e);
        if (mortal == nullptr || cult == nullptr)
        {
            continue;
        }
        ++mortal->age;
        // 寿元上限只查表不存表：kRealmLifespan 是设定 §5.1 的逐字
        // 落地、唯一权威（刀 1 的 Mortal 注释）。
        const auto realm = static_cast<std::size_t>(cult->realm);
        if (mortal->age >= kRealmLifespan[realm])
        {
            cmd.destroy(e);
        }
    }
}
```

**探针**：临时加 `TEST(probe_death_counts)`——seed 13 跑 200 年，
`std::println("probe survivors={} total={}", survivors, total)`，
运行一次记下读数，删探针，把 `the_cohort_shrinks_as_the_years_pass`
的注释写上实测环境与读数（128×128 / seed 13 / 200 年 / 实测存活 X）。
若实测 `survivors == 970`（200 年无人死），这是**真信号**：把跨度改到
300 年再探；仍无人死则 BLOCKED 报告，不要硬调测试。

- [ ] **Step 4: 构建并运行——期望变绿**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 10 条全绿。

- [ ] **Step 5: 回归**

四套件全绿（同 Task 1 Step 8 命令）。

- [ ] **Step 6: 向用户报告并等待提交指令**

报告：探针读数与钉死的界、测试结果。**不执行任何 git 命令。**

---

### Task 3: 补员——收徒与散修批

**Files:**
- Modify: `src/life/generate.hpp`（+ `make_person` 声明；头注释改
  「被 setup.cpp 与 systems.cpp 包含」）
- Modify: `src/life/generate.cpp`（`make_person` 移出匿名空间，函数体
  一字不改）
- Modify: `include/gamev2/life/constants.hpp`（+ 收徒/散修批常数）
- Modify: `src/life/systems.cpp`（替换 BirthSystem 空实现；补
  `<array>`、`<cassert>` 已有、`<algorithm>` 不需要）
- Modify: `tests/test_life_cultivation.cpp`（追加两条测试 + 探针）

**Interfaces:**
- Consumes: Task 1 的 `BirthSystem`（seed_/clock_/souls_/next_index_）、
  `LifeClock`、`Office`、`SoulId`、`kMinAge`
- Produces: `make_person`（generate.hpp；与 `generate_population` 共用
  同一套姓名/属性派生）

- [ ] **Step 1: 常数与 make_person 暴露**

`include/gamev2/life/constants.hpp` 追加：

```cpp
/// 宗门年收徒 = max(1, 在册数 / kSectRecruitDivisor)。
///
/// 标定值（刀 3 重标）：锚点是「补员与寿元死亡同量级」——断供（刀 3）
/// 打破它之前，宗门规模大致稳态。除数 50 ≈ 百余人宗门年收二徒。
inline constexpr std::uint32_t kSectRecruitDivisor = 50;

/// 散修每十年多一批（设定 §8.3 逐字）。每批人数是标定值（刀 3 重标）：
/// 锚点是与七宗年收徒总量同量级，让散修圈不被宗门吸空。
inline constexpr std::uint32_t kFreeBatchYears = 10;
inline constexpr std::uint32_t kFreeBatch = 30;
```

`src/life/generate.hpp`：`PersonInit` 之后加声明，并把文件头注释的
「只被 setup.cpp 包含」改为「被 setup.cpp 与 systems.cpp 包含」：

```cpp
/// 一个人的全部随机字段。无状态派生：只依赖 (seed, 流号, 序号)。
///
/// 收徒与散修批（systems.cpp）走同一条姓名/属性派生——两套实现会
/// 静默分岔，名册与众生长得像两个世界的人。
[[nodiscard]] PersonInit make_person(std::uint64_t seed,
                                     std::uint32_t index,
                                     history::Sect sect, Realm realm);
```

`src/life/generate.cpp`：把匿名空间里的 `make_person` 定义整体移出
到 `gamev2::life` 命名空间（函数体一字不改），`generate_population`
内的调用不变。

- [ ] **Step 2: 写失败的测试**

`tests/test_life_cultivation.cpp` 追加：

```cpp
TEST(the_populace_refills_from_below)
{
    // 补员的两条底线：引气不被晋升抽干（新生儿全是引气）、新队列
    // 确实在长（index ≥ 初始人口数的在册者出现）。设计 §1.4 的
    // 「金字塔随年演化」就是这个：底部持续有水进来。
    //
    // 总量上界由探针钉死（Step 4）：不塌（>500，初始筑基以上 133 人
    // 全活）也不炸（< 3000，百年新登记 ≈ 千人）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    life.run_years(100);

    std::uint32_t yinqi_count = 0;
    life.ecs_world().view<const gamev2::life::Cultivation>().each(
        [&](const gamev2::life::Cultivation& c)
        {
            if (c.realm == gamev2::life::Realm::yinqi)
            {
                ++yinqi_count;
            }
        });
    // 引气层必须一直有人：晋升会把老人抽走，只有新生儿（全是引气）
    // 能补上。这条为 0 就是补员断了。
    CHECK(yinqi_count > 0u);

    const auto newcomers = count_alive(
        life, [&](const gamev2::life::SoulId& id)
        { return id.index >= initial_population(); });
    CHECK(newcomers > 0u);

    const std::uint32_t total = count_alive(
        life, [](const gamev2::life::SoulId&) { return true; });
    CHECK(total > 500u);
    CHECK(total < 3000u);
}

TEST(the_same_seed_gives_the_same_populace_over_time)
{
    // 设计 §8 确定性：同 seed 跑 100 年，活着的总人数与逐境直方图
    // 逐位相同。补员的随机（收徒姓名/属性）一并被钉住——它按
    // (seed, 流号, 序号) 派生，与遍历顺序无关。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();
    life1.run_years(100);
    life2.run_years(100);

    using gamev2::life::Cultivation;
    using gamev2::life::SoulId;
    std::uint32_t n1 = 0;
    std::uint32_t n2 = 0;
    std::array<std::uint32_t, 9> realms1{};
    std::array<std::uint32_t, 9> realms2{};
    life1.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId&, const Cultivation& c)
        {
            ++n1;
            ++realms1[static_cast<std::size_t>(c.realm)];
        });
    life2.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId&, const Cultivation& c)
        {
            ++n2;
            ++realms2[static_cast<std::size_t>(c.realm)];
        });
    CHECK_EQ(n1, n2);
    CHECK(realms1 == realms2);
}
```

- [ ] **Step 3: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 红——`the_populace_refills_from_below` 的 `newcomers > 0`
失败（BirthSystem 空实现，100 年后没有 index ≥ 970 的人）。

- [ ] **Step 4: 实现 BirthSystem + 探针钉界**

`src/life/systems.cpp`：替换 BirthSystem 空实现：

```cpp
void BirthSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);

    // 各宗在册数：序号表里活着且有宗籍的。空 get_if = 已死。
    std::array<std::uint32_t, 7> alive{};
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        if (id == nullptr)
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(id->sect);
        if (sect < alive.size())
        {
            ++alive[sect];
        }
    }

    auto recruit = [&](history::Sect sect, std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            // 序号从 next_index_ 起单调：初始队列的 index 全部小于它，
            // 新老按号可分（差异与补齐 §6）。姓名/属性与 make_person
            // 同一套派生。
            const std::uint32_t index = next_index_++;
            const PersonInit p =
                make_person(seed_, index, sect, Realm::yinqi);
            const gamev2::ecs::EntityId e = world.create();
            souls_.push_back(e);
            (void)cmd.add<SoulId>(e, SoulId{index, p.name, p.sect});
            (void)cmd.add<Cultivation>(e, Cultivation{p.realm, 0u});
            (void)cmd.add<Aptitude>(e, p.aptitude);
            (void)cmd.add<Temper>(e, p.temper);
            (void)cmd.add<Mortal>(
                e, Mortal{clock->year - kMinAge, kMinAge});
            // 新入门是外门：晋升是刀 4 的事。
            (void)cmd.add<Office>(
                e, Office{sect == history::Sect::unowned
                              ? OfficeKind::none
                          : OfficeKind::outer});
        }
    };

    // 宗门年收徒：max(1, 在册 / 除数)。标定值，锚点见常数注释。
    for (std::size_t s = 0; s < alive.size(); ++s)
    {
        const std::uint32_t n = alive[s] / kSectRecruitDivisor;
        recruit(static_cast<history::Sect>(s), n > 0u ? n : 1u);
    }

    // 散修每十年多一批（设定 §8.3）。年份取模而非计数器：重放即
    // 存档（设计 §4.4），口径不依赖「跑没跑过上一个十年」。
    if (clock->year % kFreeBatchYears == 0u)
    {
        recruit(history::Sect::unowned, kFreeBatch);
    }
}
```

**探针**：临时 TEST 跑 100 年打印 `total`/`newcomers`/`yinqi_count`；
据此把 `the_populace_refills_from_below` 的 `total < 3000u` 收紧为
「实测 + 50% 余量」，注释写实测环境（128×128 / seed 13 / 100 年）。
删探针。

- [ ] **Step 5: 构建并运行——期望变绿**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 12 条全绿。**刀 1 的七条必须原样全绿**——`make_person` 移出
匿名空间不得改变 `generate_population` 的输出（刀 1 的确定性测试
守着这一点）。

- [ ] **Step 6: 回归**

四套件全绿。

- [ ] **Step 7: 向用户报告并等待提交指令**

报告：探针三个读数与钉死的界、刀 1 名册是否逐位未变、测试结果。
**不执行任何 git 命令。**

---

### Task 4: 修炼——年资、突破与金字塔上移

**Files:**
- Modify: `include/gamev2/life/constants.hpp`（+ 年资与突破常数）
- Modify: `src/life/systems.cpp`（替换 CultivateSystem 空实现；补
  `<algorithm>` 与 world 头 `#include "gamev2/world/enums.hpp"`、
  `"gamev2/world/rng.hpp"`）
- Modify: `tests/test_life_cultivation.cpp`（追加两条测试）

**Interfaces:**
- Consumes: Task 1 的 `CultivateSystem`（seed_/clock_/souls_）、
  `Cultivation::progress`、`Aptitude::comprehension`、
  `ecs::EntityId::index()`（entity.hpp：`std::uint32_t index()` 方法）、
  `world::Pcg32`/`stage_seed`/`Stream::life`
- Produces: 晋升行为（金字塔随年上移）

- [ ] **Step 1: 常数**

`include/gamev2/life/constants.hpp` 追加：

```cpp
/// 境内年资门槛（年）：攒够这些年资才开始掷突破。
///
/// 元婴的一百年闭关逐字来自设定 §5.1；其余是标定值（刀 3 重标——
/// 资源门槛并入后整套要重估）。锚点：百年窗口内众生主要停在
/// 筑基—金丹；化神及以上在 100 年内结构性不可能（四境门槛之和
/// 120 > 100）。大乘的 0 是哨兵：大乘不再进。
inline constexpr std::array<std::uint32_t, 9> kRealmYears{{
    20, 20, 30, 50, 100, 200, 300, 500, 0,
}};

/// 突破成功率（千分制）：base + (悟性 − 5) × per，钳制在 [min, max]。
///
/// 标定值（刀 3 重标）。锚点：悟性中位者三成年成功率，最愚钝两成、
/// 最聪颖五成。
inline constexpr std::uint32_t kBreakthroughBasePermille = 100;
inline constexpr std::uint32_t kBreakthroughPerAptitudePermille = 20;
inline constexpr std::uint32_t kBreakthroughMinPermille = 20;
inline constexpr std::uint32_t kBreakthroughMaxPermille = 500;
```

- [ ] **Step 2: 写失败的测试**

`tests/test_life_cultivation.cpp` 追加：

```cpp
TEST(the_pyramid_rises_with_the_years)
{
    // 设计 §11 刀 2 验收：无决策注入跑百年，人口金字塔随年演化。
    // 演化的方向是「上移」：新生队列里有人凭年资与悟性爬上筑基以上、
    // 初始引气队列被晋升抽走——合起来才叫修炼在运转。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::Cultivation;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    const auto count_realm_plus = [&](gamev2::life::Life& l,
                                      Realm floor, bool newcomers_only)
    {
        std::uint32_t n = 0;
        l.ecs_world().view<const SoulId, const Cultivation>().each(
            [&](const SoulId& id, const Cultivation& c)
            {
                const bool over =
                    static_cast<std::size_t>(c.realm)
                    >= static_cast<std::size_t>(floor);
                if (over
                    && (!newcomers_only
                        || id.index >= initial_population()))
                {
                    ++n;
                }
            });
        return n;
    };

    const std::uint32_t zhuji_before =
        count_realm_plus(life, Realm::zhuji, false);
    life.run_years(100);

    // 晋升潮：筑基以上比开局多（初始 133 人全活，多出来的全是新晋）。
    CHECK(count_realm_plus(life, Realm::zhuji, false) > zhuji_before);
    // 新队列里有人爬上筑基：他们的门槛之和是 20 + 20 年，加上掷运。
    CHECK(count_realm_plus(life, Realm::zhuji, true) > 0u);
    // 结构性上限：100 年内到不了化神（前四境门槛之和 120 年）。
    CHECK(count_realm_plus(life, Realm::huashen, false) == 0u);
}

TEST(the_same_seed_gives_the_same_realm_histogram)
{
    // 突破掷运的无状态派生：(seed, 流号, 年, 实体槽号)。同 seed 两个
    // 独立世界跑 100 年，逐境直方图逐位相同；不同 seed 至少一境不同
    // ——把掷运换成常量，两条断言之一必红。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(14));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!b.era.veins.empty());
    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::LifeDiagnostics d3;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*a.world, a.era, d2);
    gamev2::life::Life life3(*b.world, b.era, d3);
    life1.seed();
    life2.seed();
    life3.seed();
    life1.run_years(100);
    life2.run_years(100);
    life3.run_years(100);

    using gamev2::life::Cultivation;
    const auto histogram = [](gamev2::life::Life& l)
    {
        std::array<std::uint32_t, 9> h{};
        l.ecs_world().view<const Cultivation>().each(
            [&](const Cultivation& c)
            {
                ++h[static_cast<std::size_t>(c.realm)];
            });
        return h;
    };
    const auto h1 = histogram(life1);
    const auto h2 = histogram(life2);  // 与 h1 同 seed：必须相等
    const auto h3 = histogram(life3);  // seed 14：掷运不同，必须不同
    CHECK(h1 == h2);
    CHECK(h1 != h3);
}
```

（若探针实测 seed 13/14 的直方图恰好相等——概率极低但可复算的世界
里没有「运气」——换一个 history 层用过的 128² 种子（11/26/42）重探，
把最终种子写进测试注释。）

- [ ] **Step 3: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 红——`the_pyramid_rises_with_the_years`：CultivateSystem
空实现，100 年无人晋升，`zhuji_before` 不变、新队列筑基为 0。

- [ ] **Step 4: 实现 CultivateSystem**

`src/life/systems.cpp`：替换 CultivateSystem 空实现：

```cpp
void CultivateSystem::execute(gamev2::ecs::World& world,
                              gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* cult = world.get_if<Cultivation>(e);
        const auto* apt = world.get_if<Aptitude>(e);
        if (cult == nullptr || apt == nullptr)
        {
            continue;  // 死者留下的空位
        }
        if (cult->realm == Realm::dacheng)
        {
            continue;  // 大乘不再进（kRealmYears 的哨兵 0）
        }
        ++cult->progress;
        const auto realm = static_cast<std::size_t>(cult->realm);
        if (cult->progress < kRealmYears[realm])
        {
            continue;
        }
        // 刀 2 假设供给充足：设定 §5.1 的资源门槛不记账，记账是刀 3
        // 的 Purse。这里只有年资 + 悟性两轴，常数是标定值。
        std::int32_t chance =
            static_cast<std::int32_t>(kBreakthroughBasePermille);
        chance += (static_cast<std::int32_t>(apt->comprehension) - 5)
                  * static_cast<std::int32_t>(
                        kBreakthroughPerAptitudePermille);
        chance = std::clamp(
            chance, static_cast<std::int32_t>(kBreakthroughMinPermille),
            static_cast<std::int32_t>(kBreakthroughMaxPermille));

        // 掷运按 (seed, 流号, 年, 实体槽号) 无状态派生：调整遍历次序
        // 不得换一个世界（设计 §4.1）。
        const auto stream =
            static_cast<std::uint32_t>(world::Stream::life);
        world::Pcg32 rng(world::stage_seed(
            world::stage_seed(world::stage_seed(seed_, stream),
                              clock->year),
            e.index()));
        if (static_cast<std::int32_t>(rng.below(1000u)) < chance)
        {
            cult->realm = static_cast<Realm>(realm + 1u);
            cult->progress = 0u;
        }
        // 失败：年资保留，下一年再试。
    }
}
```

- [ ] **Step 5: 构建并运行——期望变绿**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 14 条全绿。若 `the_pyramid_rises_with_the_years` 红在
「新队列筑基为 0」——探针查新生悟性分布与掷运；必要时微调
`kBreakthrough*`，**任何常数调整必须在报告里写明新值与锚点未变的
理由**。

- [ ] **Step 6: 刀 2 验收与回归**

1. 四套件全绿（111 / 181 / 48 / 37）。
2. 对照设计 §11 刀 2 验收清单逐条确认：
   - 「无决策注入跑百年」→ `the_pyramid_rises_with_the_years`；
   - 「人口金字塔随年演化」→ 同上（上移 + 底部补员续水，
     `the_populace_refills_from_below`）；
   - 「寿元表逐字对设定」→ `nobody_ever_outlives_their_realm` +
     `the_cohort_shrinks_as_the_years_pass`。

- [ ] **Step 7: 向用户报告并等待提交指令**

报告：14 条测试清单与结果、四套件回归、任何常数微调与理由。
**不执行任何 git 命令。**

---

## 完成判据（刀 2 验收，对照设计 §11）

1. `life_tests` 14 条全绿；四套件回归全绿。
2. 年循环启动：`run_years(n)` 后 `year() == 开局年 + n`。
3. 寿元：任何活体 `age < kRealmLifespan[realm]` 逐年成立；200 年后
   初始队列有人死去、世界不空（探针钉界）。
4. 补员：100 年后引气层仍有人、新队列（index ≥ 初始人口数）在长、
   总量不塌不炸（探针钉界）。
5. 修炼：100 年后筑基以上多于开局、新队列有人上筑基、无人达化神。
6. 确定性：同 seed 两个世界跑 100 年，总人数与逐境直方图逐位相同。

刀 3（经济：Purse、kVeinToStone、断供克扣）按设计 §11 另出计划。

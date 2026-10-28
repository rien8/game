# 众生层刀 3 实施计划：经济（灵石账）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让灵石成为活的账：每个修士一本私囊（`Purse`），宗门有金库，
脉产出铸成灵石、Office 份额分蛋糕、断供从底层克扣、修士逐年烧毁
灵石、散修靠营生攒银两——并用守恒恒等式把整本账钉死。

**Architecture:** `Life` 增加 `EconomyLedger`（逐宗金库 + 累计铸造/烧毁）
与年循环第 5 个 system（`EconomySystem`，接在 Cultivate 之后；设计 §1.2
的步 7 在本刀提前落地，步 5/6 决策与行为是刀 4）。收入铸币口径是
「当年该宗名下活脉的年产能 × kVeinToStone」——产能是秩级标定表，
本刀用当代格局快照代替派系层逐年产出（`FactionOutcome` 是刀 5 的
接口，刀 5 把铸币基数换成产出清单）。散修的银两与灵石是两本账：
营生攒银两（凡人货币，不进灵石守恒），银两按固定换价从无主脉
金库买入下品（灵石恒等式内只发生划转、不发生铸造）。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**Spec:** `doc/2026-10-26-life-layer-design.md`（语义依据；§3 经济层、
§2.1 组件表、§6 数值纪律、§7 诊断、§8 测试、§11 刀 3 范围与验收）
**前置计划:** `doc/plans/2026-10-26-life-cultivation-implementation.md`（刀 2；
其「差异与补齐」裁定继续有效，本刀对其中两条的更正见下）

---

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23；编译器固定 clang++；禁止第三方库（含测试框架）；
  禁止 `shared_ptr`/`unique_ptr`；禁止头文件 `using namespace`
- 注释 doxygen `///` 只解释「为什么」；任何一行不超过 90 个字符
- Windows 下经 `pwsh` 调终端；**不得执行 `git add`/`git commit`/`git stash`/
  `git clean`**：每个任务末尾向用户报告并等待提交指令
- 随机性一律走 `Pcg32`；本刀**不新增随机**——经济全部是确定性算术
  （铸币、分配、烧毁、营生都无掷运）；既有掷运仍走 `Stream::life`
- **本层只读**：不写 `World`/`Ledger`/faction 字段；依赖方向
  `life → faction → history → worldgen` + `ecs`
- system 一律遍历 `Life` 的实体序号表；`get_if` 空指针 = 已死，跳过
- **不使用 `View::each_parallel`**
- **每新建 `src/life/` 下的 .cpp 必须同步加进 CMakeLists 的 `LIFE_SOURCES`**；
  每新建 `tests/test_life_*.cpp` 必须同步加进 `LIFE_TEST_SOURCES`
- **测试只走公共入口**：`life::Life`、`ecs_world()` 上的视图、公共类型与
  常数；私有头不被任何测试包含
- 刀 1/刀 2 的全部测试（`test_life_setup.cpp` 11 条 +
  `test_life_cultivation.cpp` 7 条，含性别两条）必须原样全绿
- 编译与运行：`pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
  （全量回归再跑其余四个 `*_tests.exe`）
- 本刀的字段写入者表（继承刀 2 并新增）：

  | 字段 | 唯一写入者 |
  |---|---|
  | 刀 2 全部六组件 | 照旧（`Purse` 见下） |
  | `Purse::stone` | 建实体初值 0；`EconomySystem`（分配入账、烧毁扣账、买入划转） |
  | `Purse::silver` | `EconomySystem`（营生入账、买入扣银） |
  | `EconomyLedger`（金库、累计铸造/烧毁） | `EconomySystem` |

---

## 与设计文档的差异与补齐（实现前必须先解决的口径问题）

1. **铸币基数 = 当代格局快照的活脉产能，不是 `supply`**。探针实测
   128²/seed13 当年供给 Σ=1（灵潮谷底、无主脉全枯），直接拿 `supply`
   铸币会让天下一年只铸 1 个指数单位。产能改为按**秩级标定表**
   `kRankAnnualYield`（每脉每年的指数产能，活脉才有、枯脉为 0），
   经 `kVeinToStone` 折成下品。刀 5 的 `FactionOutcome` 落地后，铸币
   基数换成产出清单——本刀先在 `EconomySystem` 注释里写明这条接缝。
2. **经济测试夹具改用 256²/seed13**。128²/seed13 只有太玄有活脉，
   六宗零收入，分配与断供测不出贫富差；256²/seed13 实测太玄 6 脉、
   丹霞/寒江/万兽各 1 脉、青囊/天机/九幽 0 脉，恰好覆盖「富宗、
   小宗、断供宗」三态。既有刀 1/刀 2 测试夹具不动。
3. **`kVeinToStone` 的锚点改在 256²/seed13 上标定**：探针求出参考
   世界的 Σ活脉产能，令 `kVeinToStone = 420000 / Σ`（下品/指数单位，
   整除截断、余数写进注释）。420000 = 设定 §6.1.1 的天下年产
   4200 中品逐字折算。换值只平移绝对规模，不改贫富结构。
4. **金库不在组件表里**。设计 §2.1 的组件表没有金库行；金库是
   「宗门的账」不是「人的账」，`EconomyLedger`（`Life` 持有的普通
   结构体，逐宗 `std::array<std::uint64_t, 8>` 按 `Sect` 序）承载它。
   与 `Roster` 同一先例：不是所有状态都必须是组件。
5. **断供克扣 = 支付次序，不是欠账**。份额按宗主→长老→内门→外门
   次序支付，金库见底即止（外门先饿、内门次之、长老宗主不动，
   设计 §3.3 逐字）。被克扣者**不记欠账**（`Purse` 不设 debt 字段）：
   设计 §2.1 的「欠账」留给刀 4 的判据 2 行为，本刀先不做半套。
6. **烧毁 = min(私囊, 应烧)**。设定 §6.1 的吸纳表逐字折成下品/年
   （`kRealmBurn`：引气 37、炼气 100、筑基 1000、金丹/元婴 33333，
   见各常数注释的折算过程）；囊中不足就烧多少算多少，不足额只是
   「今年没吃够」，不记账不追缴——灵石恒等式因此永远成立。
7. **突破的资源门槛不在本刀**。刀 2 的常数注释写「刀 3 重标」，指
   「断供模型并入后重估标定值」；但设定 §5.1 的资源门槛（引气一枚
   下品、筑基结基灵物 20~30 中品、金丹厚域、元婴闭关）里，金丹的
   厚域位置约束需要个体位置、元婴闭关需要 `Motive`——两者都是刀 4
   的组件。本刀把 `kRealmYears`/突破概率常数注释里的「刀 3 重标」
   **改成「刀 4 重标」**，本刀不动 CultivateSystem 的判定逻辑。
8. **散修营生的三十三年数学落在银两账上**。设定 §6.4：年入约
   六十两、一枚下品折两千两。`Purse` 因此有两个子账：`stone`
   （下品，进守恒）与 `silver`（两，凡人货币，**不进**灵石守恒）。
   散修攒够 2000 两时从**无主脉金库**按固定换价买入一枚下品
   （划转，不铸币）。探针实测两个参考世界的无主脉**全部枯竭**，
   无主金库恒为 0 → 买入分支在参考夹具上不可达；「33 年一枚」
   的验收钉在攒银数学上（33 年 1980 两买不起、34 年 2040 两买得起），
   买入分支的行为测试用 256² 的枯竭世界钉「公账不足不成交、银两
   留到明年」这一侧。盗采把灵石送进无主金库是刀 4 的事。
9. **`EconomySystem` 插在 Cultivate 之后**（设计 §1.2 步 7 提前，
   步 5/6 刀 4 补）。年循环变为 Clock→Aging→Birth→Cultivate→
   Economy 五步；`EconomySystem` 的 ReadSet 声明 `const Cultivation`
   以落进独立批（照刀 2 Birth 借读集定批的先例）。
10. **刀 2 遗留注释的两处更正随本刀落地**：`constants.hpp` 里
    `kSectRecruitDivisor` 的「刀 3 并入断供后重标」按本刀的探针
    重标结果更新读数与归属；`kRealmYears`/突破常数的「刀 3 重标」
    改「刀 4 重标」（见差异 7）。
11. **突破掷运的变异钉住测试归本刀**（刀 2 计划的携带项 ②）：
    一条测试同时钉「掷运会成功」与「掷运会失败」两个方向——只许
    成功的变异与只许失败的变异各有一条断言打红。
12. **`kSectRecruitDivisor` 重标归本刀**（刀 2 计划的携带项 ①）：
    探针在「含经济的世界」上重跑 200 年人口曲线，按「补员与寿元
    死亡同量级」的锚重标除数并钉住上下界。断供本身**不**杀人在
    本刀成立（烧毁不足只是不吃，没有饥饿死亡）——人口调节仍只靠
    寿元，除数是唯一旋钮。
13. **死者账消：私囊随死烧毁入账**。设计 §5.4 的继承结算在刀 6，
    本刀死者账上的灵石直接计入累计烧毁——恒等式一侧少一个实体，
    另一侧必须同时少一份钱，否则死亡第一年守恒就破。刀 6 落地时
    把这一笔改成「按 Bonds 分给继承人」。

---

## 文件结构

```
include/gamev2/life/
    economy.hpp        // 新：kRankAnnualYield/kVeinToStone/kOfficeShare/
                       //     kRealmBurn/换价/营生常数 + EconomyLedger
    soul.hpp           // + Purse 组件
    constants.hpp      // + lifespan_reached 共享谓词；
                       // kSectRecruitDivisor 重标 + 两处「刀 3 重标」注释订正
    interface.hpp      // LifeDiagnostics + 两字段；Life + economy() 与账本成员
src/life/
    economy.cpp        // 新：EconomySystem 全部实现
    systems.hpp        // + EconomySystem 声明；BirthSystem 写集改 <Purse>
    systems.cpp        // BirthSystem 给新生儿挂 Purse；Aging 改用共享谓词
    setup.cpp          // seed() 挂 Purse 与铸币表；run_years 注册第五个 system
tests/
    test_life_economy.cpp     // 新：经济七条
    test_life_cultivation.cpp // + 掷运变异钉住（刀 2 携带项 ②）
CMakeLists.txt         // LIFE_SOURCES + economy.cpp；LIFE_TEST_SOURCES + 新测试
```

探针钉死的标定读数（256²/seed13，执行任务 1 时须复核，不符即停并报告）：

| 量 | 读数 |
|---|---|
| 活脉（balance>0 且未标枯） | 龙脉 1（太玄）、主脉 8（太玄 5、丹霞 1、寒江 1、万兽 1） |
| Σ活脉产能（dragon=100/main=40） | 100 + 8×40 = **420** |
| `kVeinToStone` = 420000/420 | **1000** 下品/指数单位 |
| 逐宗年铸币 | 太玄 300000、丹霞/寒江/万兽 各 40000、青囊/天机/九幽/无主 0 |
| 职位计数（常数表决定，与网格无关） | 太玄 1/7/56/76、丹霞 1/6/48/65、寒江 1/4/36/49、万兽 1/5/40/54、青囊 1/4/36/49、天机 1/3/24/32、九幽 1/3/28/38（宗主/长老/内门/外门） |

份额表 `{35000, 3000, 500, 150}` 下的逐宗年需求与首年金库终值：

| 宗 | 年需求 | 首年金库终值 |
|---|---|---|
| 太玄 | 95400 | 204600（盈余） |
| 丹霞 | 86750 | 2（宗主 35000 + 长老各 833，内外门断供） |
| 寒江 | 72350 | 0（长老各 1250，内外门断供） |
| 万兽 | 78100 | 0（长老各 1000，内外门断供） |
| 青囊/天机/九幽 | 72350/60800/63700 | 0（全员断供） |

---

### Task 1: Purse 组件、常数表与账本骨架

**Files:**
- Modify: `include/gamev2/life/soul.hpp`（+ `Purse`）
- Create: `include/gamev2/life/economy.hpp`
- Modify: `include/gamev2/life/interface.hpp`（+ `Life::economy()`、账本成员）
- Modify: `src/life/setup.cpp`（`seed()` 挂 `Purse`、填铸币表）
- Modify: `src/life/systems.cpp`（`BirthSystem` 给新生儿挂 `Purse`）
- Test: `tests/test_life_economy.cpp`（新建，+ CMake 注册）

**Interfaces:**
- Produces: `struct Purse { std::uint64_t stone; std::uint32_t silver; }`；
  `struct EconomyLedger { std::array<std::uint64_t,8> vault;
  std::array<std::uint64_t,8> mint_per_year; std::uint64_t minted_total;
  std::uint64_t burned_total; std::uint64_t vault_total() const; }`；
  `EconomyLedger& Life::economy()`；常数 `kRankAnnualYield`/
  `kVeinToStone`/`kOfficeShare`/`kRealmBurn`/`kLiangPerStone`/
  `kFreeCultivatorIncomeLiang`。

- [ ] **Step 1: 写失败的测试**

`tests/test_life_economy.cpp`（夹具与刀 2 同口径，仅网格改 256²）：

```cpp
#include "gamev2/faction/era.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/economy.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace {

/// 刀 3 夹具：256²。128² 只有太玄有活脉，分配与断供测不出贫富差；
/// 256²/seed13 实测覆盖富宗（太玄）、小宗（丹霞/寒江/万兽各有 1 条
/// 主脉）、断供宗（青囊/天机/九幽 0 脉）三态（计划探针读数）。
gamev2::world::WorldParams economy_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 256;
    p.height = 256;
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
    const auto ledger = gamev2::history::build_ledger(*stack.world, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    stack.era = gamev2::faction::simulate(*stack.world, ledger, fdiag);
    return stack;
}

}  // namespace

TEST(every_soul_carries_an_empty_purse)
{
    // 有供给就得有账本。私囊挂在每个活人身上——初始 970 人开局两手
    // 空空（设定没给开局身家，0 是「从零开始」的默认）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    CHECK(!s.era.veins.empty());
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint32_t purses = 0;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Purse>().each(
        [&](const gamev2::life::SoulId&,
            const gamev2::life::Purse& purse)
        {
            ++purses;
            CHECK_EQ(purse.stone, 0u);
            CHECK_EQ(purse.silver, 0u);
        });
    CHECK_EQ(purses, 970u);
}

TEST(the_mint_matches_the_vein_anchors)
{
    // 铸币表两头钉：逐宗读数与天下合计 == 420000 下品（设定 §6.1.1
    // 的天下年产 4200 中品折算）。铸币基数是当代活脉产能快照（口径
    // 裁定 1），刀 5 换 FactionOutcome 后「天下合计」断言不变。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const auto& mint = life.economy().mint_per_year;
    CHECK_EQ(mint[0], 300000u);  // 太玄：1 龙脉 + 5 主脉
    CHECK_EQ(mint[1], 40000u);   // 丹霞：1 主脉
    CHECK_EQ(mint[2], 40000u);   // 寒江：1 主脉
    CHECK_EQ(mint[3], 40000u);   // 万兽：1 主脉
    CHECK_EQ(mint[4], 0u);       // 青囊：失脉
    CHECK_EQ(mint[5], 0u);       // 天机阁：无地
    CHECK_EQ(mint[6], 0u);       // 九幽宫：失脉
    CHECK_EQ(mint[7], 0u);       // 无主：支脉全枯
    std::uint64_t total = 0;
    for (const auto m : mint)
    {
        total += m;
    }
    CHECK_EQ(total, 420000u);
}
```

CMakeLists.txt 的 `LIFE_TEST_SOURCES` 加 `tests/test_life_economy.cpp`。

- [ ] **Step 2: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 编译红——`Purse`/`economy()`/`economy.hpp` 均不存在。

- [ ] **Step 3: 实现组件、常数与账本**

`include/gamev2/life/soul.hpp`（`Office` 之后追加）：

```cpp
/// 灵石账（设计 §2.1）。stone 按品级折算成统一记账单位（下品），
/// silver 是凡人货币——两本账不混：守恒恒等式只数 stone。
struct Purse
{
    std::uint64_t stone = 0;   ///< 下品折算
    std::uint32_t silver = 0;  ///< 两（设定 §6.4：一枚下品折两千两）
};
```

`include/gamev2/life/economy.hpp`（新建）：

```cpp
/// @file economy.hpp
/// @brief 众生层经济：换算系数、份额表、烧毁表与逐宗金库账。
#pragma once

#include <array>
#include <cstdint>

namespace gamev2::life {

/// 每脉每年的指数产能，按 world::VeinRank 序（none/barren/branch/
/// main/dragon）。
///
/// 标定值：锚点是龙脉:主脉:支脉 = 10:4:1 的量级差（设定没给产能表；
/// 换这组比值平移逐宗铸币的绝对量、不改贫富序——富宗仍是龙脉持有者）。
/// 枯脉（balance 为 0 或已标枯年）不产。
inline constexpr std::array<std::uint32_t, 5> kRankAnnualYield{
    {0, 2, 10, 40, 100}};

/// 指数产能 → 下品灵石。全局唯一换算（设计 §3.2：系数只写一份）。
///
/// 标定值：锚点是设定 §6.1.1 天下年产 4200 中品 = 420000 下品。
/// 256²/seed13 活脉 Σ产能 = 420（探针复核），420000 / 420 = 1000。
inline constexpr std::uint64_t kVeinToStone = 1000;

/// 逐职位年份额（下品/年），按 OfficeKind 序（宗主/长老/内门/外门）。
///
/// 制度性常量（设计 §3.3、§6：份额从底层往上克扣，高层不动）。锚点：
/// 宗主覆盖金丹级吸纳（33333）留余；长老是筑基级（1000）的三倍；
/// 内门是炼气级（100）的五倍；外门是引气级（37）的四倍糊口。换值
/// 只改断供出现的早晚，不改克扣次序。
inline constexpr std::array<std::uint32_t, 4> kOfficeShare{
    {35000, 3000, 500, 150}};

/// 逐境年烧毁（下品/年），按 Realm 序。
///
/// 设定 §6.1 品级表逐字折算：引气「下品约十日」→ 365/10 ≈ 37；
/// 炼气「中品约一年」→ 100；筑基「上品约十年」→ 10000/10 = 1000；
/// 金丹/元婴「极品约三十年」→ 1000000/30 ≈ 33333（与 §6.1.2 的元婴
/// 「三百三十余中品/年」互证）。化神及以上设定未给——×10 外推的
/// 标定值，刀 4 重估；测试跨度内结构性到不了（门槛之和 120 年 +
/// 掷运，100 年内无人化神）。
inline constexpr std::array<std::uint64_t, 9> kRealmBurn{
    {37, 100, 1000, 33333, 33333, 333330, 3333300, 33333000, 333330000}};

/// 固定换价（设定 §6.4 逐字）：一枚下品折两千两。
inline constexpr std::uint32_t kLiangPerStone = 2000;

/// 散修营生年入（设定 §6.4 逐字：猎兽采药押镖，年入约六十两）。
inline constexpr std::uint32_t kFreeCultivatorIncomeLiang = 60;

/// 逐宗金库账。刀 3 的金库不是组件（设计 §2.1 组件表无此项）——
/// 它是「宗门的账」不是「人的账」，与 Roster 同一先例：普通结构体
/// 由 Life 持有。下标按 history::Sect 序，第 8 位是无主（散修公账）。
struct EconomyLedger
{
    std::array<std::uint64_t, 8> vault{};
    /// 逐宗年铸币。当代格局快照定死（口径裁定 1）；刀 5 换成
    /// FactionOutcome 的产出清单后本字段退役。
    std::array<std::uint64_t, 8> mint_per_year{};
    std::uint64_t minted_total = 0;
    std::uint64_t burned_total = 0;

    /// Σ金库（含无主公账）。守恒校验与测试共用这一份求和。
    [[nodiscard]] constexpr std::uint64_t vault_total() const noexcept
    {
        std::uint64_t sum = 0;
        for (const auto v : vault)
        {
            sum += v;
        }
        return sum;
    }
};

}  // namespace gamev2::life
```

`include/gamev2/life/interface.hpp`：include 列表加
`"gamev2/life/economy.hpp"`，`Life` 私有成员加
`gamev2::life::EconomyLedger economy_;`，公有加：

```cpp
    /// 逐宗金库账。非 const：读的是可变推演状态，与 ecs_world() 同理由。
    [[nodiscard]] gamev2::life::EconomyLedger& economy()
    {
        return economy_;
    }
    [[nodiscard]] const gamev2::life::EconomyLedger& economy() const
    {
        return economy_;
    }
```

`src/life/setup.cpp` 的 `seed()`（时钟创建之前）加铸币表——逐脉按
owner 归宗、活脉产能求和；同函数的逐人建实体循环里（六件组件之后）
加 `(void)ecs_world_.add<Purse>(e, Purse{});`：

```cpp
    // 铸币表：当代活脉的年产能 × kVeinToStone（口径裁定 1：快照代替
    // 派系层逐年产出，刀 5 换 FactionOutcome）。枯脉不产——balance
    // 为 0 或已标枯年就是死了，再铸就是给死人发饷。
    for (const auto& vein : era_.veins)
    {
        if (vein.balance == 0u || vein.depleted_year != 0u)
        {
            continue;
        }
        const auto rank = static_cast<std::size_t>(vein.rank);
        const auto owner = static_cast<std::size_t>(vein.owner);
        economy_.mint_per_year[owner] += kRankAnnualYield[rank]
                                         * kVeinToStone;
    }
```

`src/life/systems.cpp` 的 `BirthSystem::execute` 里，`cmd.add<Office>`
之后加 `(void)cmd.add<Purse>(e, Purse{});`。

- [ ] **Step 4: 确认通过并复核探针读数**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 两条新测试绿。若 `the_mint_matches_the_vein_anchors` 的逐宗
数字与探针表不符：**停下报告**——探针读数是本计划的标定依据，不符
说明夹具世界变了，常数要重新推导，不得自行改测试数字凑绿。

- [ ] **Step 5: 向用户报告**

报告：铸币表逐宗读数、`kVeinToStone` 锚点推导、测试结果。
**不执行任何 git 命令。**

---

### Task 2: 年循环接入 EconomySystem——铸币、烧毁、死者账消与守恒

**Files:**
- Modify: `include/gamev2/life/constants.hpp`（+ `lifespan_reached`）
- Modify: `include/gamev2/life/interface.hpp`（+ 两条诊断字段）
- Modify: `src/life/systems.hpp`（+ `EconomySystem` 声明；`BirthSystem`
  写集改 `<Purse>`）
- Modify: `src/life/systems.cpp`（`AgingSystem` 改用共享谓词）
- Create: `src/life/economy.cpp`（+ CMake `LIFE_SOURCES` 注册）
- Modify: `src/life/setup.cpp`（`run_years` 注册第五个 system）
- Test: `tests/test_life_economy.cpp`（追加两条）

**Interfaces:**
- Consumes: Task 1 的 `Purse`/`EconomyLedger`/常数
- Produces: `EconomySystem(EntityId clock, const vector<EntityId>& souls,
  EconomyLedger&, LifeDiagnostics&)`；
  `constexpr bool lifespan_reached(const Mortal&, const Cultivation&)`；
  `LifeDiagnostics::stone_conservation_broken` /
  `office_without_sect`；年循环五步 Clock→Aging→Birth→Cultivate→Economy

- [ ] **Step 1: 写失败的测试**

`test_life_economy.cpp` 追加：

```cpp
TEST(the_identity_holds_every_year)
{
    // 设计 §3.5 的角色层恒等式：Σ(所有金库 + 所有私囊) = 累计铸造 −
    // 累计烧毁。逐年结算后校验，破一次诊断 +1。跑 100 年——跨度盖过
    // 初始引气队列的寿元死亡（死者的私囊随死烧毁，口径裁定 13），
    // 死亡不许把恒等式砸出缺口。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (std::uint32_t y = 0; y < 100; ++y)
    {
        life.run_years(1);

        std::uint64_t accounts = life.economy().vault_total();
        life.ecs_world().view<const gamev2::life::SoulId,
                              const gamev2::life::Purse>().each(
            [&](const gamev2::life::SoulId&,
                const gamev2::life::Purse& purse)
            { accounts += purse.stone; });
        CHECK_EQ(accounts,
                 life.economy().minted_total
                     - life.economy().burned_total);
    }
    CHECK_EQ(diag.stone_conservation_broken, 0u);
    CHECK_EQ(diag.office_without_sect, 0u);
    // 烧毁不是死代码：100 年里必然有人烧过灵石。
    CHECK(life.economy().burned_total > 0u);
    // 铸币表逐年入账：420000 × 100，逐年精确。
    CHECK_EQ(life.economy().minted_total, 42000000u);
}

TEST(the_same_seed_gives_the_same_ledger)
{
    // 经济全是确定性算术（无掷运），同 seed 两个世界跑 100 年，账本
    // 逐字段逐位相同；私囊的「有钱人计数」作为私囊分布的代表比对。
    const Stack a = make_stack(economy_world(13));
    const Stack b = make_stack(economy_world(13));
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

    CHECK_EQ(life1.economy().minted_total, life2.economy().minted_total);
    CHECK_EQ(life1.economy().burned_total, life2.economy().burned_total);
    CHECK(life1.economy().vault == life2.economy().vault);

    using gamev2::life::Purse;
    std::array<std::uint64_t, 2> h1{};
    std::array<std::uint64_t, 2> h2{};
    life1.ecs_world().view<const Purse>().each(
        [&](const Purse& p)
        { ++h1[p.stone > 0u ? 1u : 0u]; });
    life2.ecs_world().view<const Purse>().each(
        [&](const Purse& p)
        { ++h2[p.stone > 0u ? 1u : 0u]; });
    CHECK(h1 == h2);
}
```

- [ ] **Step 2: 确认失败**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 编译红——`EconomySystem` 不存在；即便手工跳过也红在
`stone_conservation_broken` 与 `economy()` 诊断字段缺失。

- [ ] **Step 3: 实现共享谓词、EconomySystem 与年循环接入**

`include/gamev2/life/constants.hpp`（`kRealmLifespan` 之后）：

```cpp
/// 寿元已尽。Aging 据此销毁、Economy 据此跳过分配——同一谓词两处用：
/// 分叉会让「谁被跳过」与「谁被销毁」判在不同人身上，账就对不上。
[[nodiscard]] constexpr bool lifespan_reached(
    const Mortal& mortal, const Cultivation& cult) noexcept
{
    return mortal.age
           >= kRealmLifespan[static_cast<std::size_t>(cult.realm)];
}
```

`src/life/systems.cpp` 的 `AgingSystem::execute` 判定改为
`if (lifespan_reached(*mortal, *cult))`（原比较逻辑删除，注释保留
「上限只查表不存表」的理由并指向新谓词）。

`src/life/systems.hpp`：`BirthSystem` 的 `using WriteSet = std::tuple<>;`
改为：

```cpp
    /// Purse 进写集不是装饰：新生儿经 cmd.add 落私囊，而 EconomySystem
    /// 也写私囊——Scheduler 看不见 cmd 的延迟写入。不声明这一个冲突，
    /// Economy 会与 Birth 落进同一批：Birth 追加 souls_、Economy 遍历
    /// souls_，迭代器失效在调试构建直接 abort（照刀 2 借读写集定批
    /// 的先例）。
    using WriteSet = std::tuple<Purse>;
```

同文件末尾加（头部 include 列表加 `"gamev2/life/economy.hpp"` 与
`"gamev2/life/interface.hpp"`）：

```cpp
/// 经济。铸币入金库、Office 份额分配、散修营生、烧毁、死者账消、
/// 守恒校验（设计 §1.2 步 7 提前落地；步 5/6 决策与行为是刀 4）。
class EconomySystem final : public gamev2::ecs::System
{
public:
    /// const Cultivation 进读集是调度声明：不声明它，Economy 与 Birth
    /// 的读写集互不冲突、会落进同一批（理由见 BirthSystem::WriteSet）；
    /// 声明后与 Cultivate 的写集冲突，落进独立批、排在 Cultivate
    /// 之后——年循环次序 Clock→Aging→Birth→Cultivate→Economy 成立。
    using ReadSet = std::tuple<const LifeClock, const SoulId, const Mortal,
                               const Cultivation>;
    using WriteSet = std::tuple<Purse>;

    EconomySystem(gamev2::ecs::EntityId clock,
                  const std::vector<gamev2::ecs::EntityId>& souls,
                  EconomyLedger& ledger, LifeDiagnostics& diagnostics);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
    EconomyLedger& ledger_;
    LifeDiagnostics& diagnostics_;
};
```

`src/life/economy.cpp`（新建；本任务先落铸币、烧毁、死者账消、守恒，
分配与营生是 Task 3/4——每个阶段恒等式都必须成立）：

```cpp
/// @file economy.cpp
/// @brief 众生层：年循环的经济步——铸币、烧毁、死者账消、守恒校验。
#include "systems.hpp"
#include "gamev2/life/constants.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace gamev2::life {

EconomySystem::EconomySystem(gamev2::ecs::EntityId clock,
                             const std::vector<gamev2::ecs::EntityId>& souls,
                             EconomyLedger& ledger,
                             LifeDiagnostics& diagnostics)
    : clock_(clock),
      souls_(souls),
      ledger_(ledger),
      diagnostics_(diagnostics)
{
}

void EconomySystem::execute(gamev2::ecs::World& world,
                            gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // seed 亲手挂的：缺了是内部错误
    (void)clock;

    // 铸币：快照表逐年入账（口径裁定 1，刀 5 换产出清单）。
    for (std::size_t s = 0; s < ledger_.vault.size(); ++s)
    {
        ledger_.vault[s] += ledger_.mint_per_year[s];
        ledger_.minted_total += ledger_.mint_per_year[s];
    }

    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* cult = world.get_if<Cultivation>(e);
        const auto* mortal = world.get_if<Mortal>(e);
        auto* purse = world.get_if<Purse>(e);
        if (cult == nullptr || mortal == nullptr || purse == nullptr)
        {
            continue;  // 死者留下的空位
        }
        if (lifespan_reached(*mortal, *cult))
        {
            // 死者账消（口径裁定 13）：私囊随死烧毁入账。实体销毁会把
            // 私囊连同恒等式的一侧一起带走——先清账再等 Aging 的
            // destroy 落地，否则死亡当年守恒就破一块。刀 6 换成
            // 「按 Bonds 分给继承人」。
            ledger_.burned_total += purse->stone;
            purse->stone = 0;
            continue;
        }
        // 烧毁：设定 §6.1 的吸纳表逐字。囊中不足就烧多少算多少——
        // 不足额只是「今年没吃够」，不追缴不记账，恒等式因此恒成立。
        const auto realm = static_cast<std::size_t>(cult->realm);
        const std::uint64_t due = kRealmBurn[realm];
        const std::uint64_t take =
            purse->stone < due ? purse->stone : due;
        purse->stone -= take;
        ledger_.burned_total += take;
    }

    // 守恒校验（设计 §3.5）：逐年人工对账，破一次记一次。
    std::uint64_t accounts = ledger_.vault_total();
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* purse = world.get_if<Purse>(e);
        if (purse != nullptr)
        {
            accounts += purse->stone;
        }
    }
    if (accounts != ledger_.minted_total - ledger_.burned_total)
    {
        ++diagnostics_.stone_conservation_broken;
    }

    // 内部一致性（设计 §7）：有职位但无宗门归属。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        const auto* office = world.get_if<Office>(e);
        if (id == nullptr || office == nullptr)
        {
            continue;
        }
        if (office->kind != OfficeKind::none
            && id->sect == gamev2::history::Sect::unowned)
        {
            ++diagnostics_.office_without_sect;
        }
    }
}

}  // namespace gamev2::life
```

`include/gamev2/life/interface.hpp` 的 `LifeDiagnostics` 加两字段
（紧跟 `no_contemporary_state`，注释照设计 §7 原文）：

```cpp
    /// 灵石恒等式对账不通过的年份（设计 §7）。
    std::uint32_t stone_conservation_broken = 0;
    /// 有职位但无宗门归属（内部一致性，设计 §7）。
    std::uint32_t office_without_sect = 0;
```

`src/life/setup.cpp` 的 `run_years`（Cultivate 注册之后、`plan()`
之前）加：

```cpp
    EconomySystem economy(clock_, souls_, economy_, diagnostics_);
    scheduler.add(economy);
```

（`setup.cpp` 已 include `systems.hpp`；`run_years` 的注释里五步次序
一并更新为 Clock→Aging→Birth→Cultivate→Economy。）

CMakeLists 的 `LIFE_SOURCES` 在 `src/life/systems.cpp` 后加
`src/life/economy.cpp`。

- [ ] **Step 4: 确认通过**

Run: 同上。Expected: 新两条绿；刀 1/刀 2 全部测试仍绿（此阶段只有
铸币/烧毁/账消，无分配——恒等式按构造成立，分配接入是 Task 3）。
若刀 2 有测试因批序变化变红：停下报告批序实况，不得调整刀 2 断言。

- [ ] **Step 5: 向用户报告**

报告：100 年恒等式逐年成立、烧毁累计读数、年循环五步的批序论证。
**不执行任何 git 命令。**

---

### Task 3: 宗门分配——Office 切蛋糕与断供克扣

**Files:**
- Modify: `src/life/economy.cpp`（分配段）
- Test: `tests/test_life_economy.cpp`（追加两条）

**Interfaces:**
- Consumes: Task 1/2 的 `kOfficeShare`、`EconomyLedger::vault`
- Produces: 每年铸币后、烧毁前的分配段：逐宗按宗主→长老→内门→外门
  次序从 `vault[sect]` 支付份额；金库不足按类停摆（同类内按实体
  序号表顺序支付，付空即止）

- [ ] **Step 1: 写失败的测试**

`test_life_economy.cpp` 追加（数字全部来自计划探针表，先算后钉）：

```cpp
TEST(the_rich_sect_pays_and_accumulates)
{
    // 设计 §3.3：宗主最高优先、长老高份额。首年太玄铸 300000、需求
    // 95400——全员足额，金库结余 204600（口径裁定 1 的探针表读数）。
    // 私囊实得要与「份额 − 当年烧毁」对得上：宗主引气级不可能，
    // 拿外门引气弟子的账做精确断言（份额 150 − 烧毁 37 = 113）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    CHECK_EQ(life.economy().vault[0], 204600u);

    using gamev2::life::OfficeKind;
    using gamev2::life::Purse;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    std::uint32_t checked = 0;
    life.ecs_world().view<const SoulId, const Office, const Purse,
                          const gamev2::life::Cultivation>().each(
        [&](const SoulId& id, const Office& office, const Purse& purse,
            const gamev2::life::Cultivation& cult)
        {
            if (id.sect != gamev2::history::Sect::taixuan
                || office.kind != OfficeKind::outer
                || cult.realm != Realm::yinqi)
            {
                return;
            }
            // 外门引气：份额 150 − 烧毁 37。
            CHECK_EQ(purse.stone, 150u - 37u);
            ++checked;
        });
    CHECK(checked > 0u);
}

TEST(the_starving_sect_cuts_from_the_bottom)
{
    // 设计 §3.3 的克扣次序：外门先饿、内门次之、长老宗主不动。丹霞
    // 首年铸 40000、需求 86750——宗主足额 35000，剩 5000 给 6 长老
    // （各 833、余 2 入库），内门外门分文不得（探针表读数）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    CHECK_EQ(life.economy().vault[1], 2u);

    using gamev2::life::OfficeKind;
    using gamev2::life::Purse;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    std::uint32_t outer_with_stones = 0;
    std::uint32_t inner_with_stones = 0;
    std::uint32_t elders_seen = 0;
    life.ecs_world().view<const SoulId, const Office, const Purse,
                          const gamev2::life::Cultivation>().each(
        [&](const SoulId& id, const Office& office, const Purse& purse,
            const gamev2::life::Cultivation& cult)
        {
            if (id.sect != gamev2::history::Sect::daxia)
            {
                return;
            }
            if (office.kind == OfficeKind::outer)
            {
                // 外门被克扣到 0：烧毁只烧得动已入账的钱。
                outer_with_stones += purse.stone > 0u ? 1u : 0u;
                CHECK_EQ(purse.stone, 0u);
            }
            else if (office.kind == OfficeKind::inner)
            {
                inner_with_stones += purse.stone > 0u ? 1u : 0u;
            }
            else if (office.kind == OfficeKind::elder)
            {
                ++elders_seen;
                // 长老不动：份额 833 − 各自境界烧毁，非负且小于份额。
                CHECK(purse.stone <= 833u);
            }
        });
    CHECK_EQ(outer_with_stones, 0u);
    CHECK_EQ(inner_with_stones, 0u);
    CHECK(elders_seen > 0u);
}
```

- [ ] **Step 2: 确认失败**

Run: 同 Task 2。Expected: 红——金库只有铸币（无分配支出）：
`vault[0]` 是 300000 而非 204600；丹霞外门私囊……此时还没有收入，
两组断言都错在「没分配」。

- [ ] **Step 3: 实现分配段**

`src/life/economy.cpp`：铸币循环之后、`for (souls_)` 烧毁循环之前
插入分配段（含设计 §3.3 的次序语义）：

```cpp
    // 分配（设计 §3.3）：Office 切蛋糕，支付次序 = 宗主→长老→内门→
    // 外门。金库不足时按类停摆——外门先饿、内门次之、长老宗主不动。
    // 同类内按序号表顺序（= 创建序，确定性）逐人支付，付空即止；
    // 被克扣者不记欠账（口径裁定 5：欠账是刀 4 判据 2 的事）。
    for (std::size_t sect = 0; sect < ledger_.vault.size(); ++sect)
    {
        for (std::size_t cls = 0; cls < kOfficeShare.size(); ++cls)
        {
            const auto want =
                static_cast<std::uint64_t>(kOfficeShare[cls]);
            for (const gamev2::ecs::EntityId e : souls_)
            {
                if (ledger_.vault[sect] == 0u)
                {
                    break;  // 金库见底：本宗后续类分文不得
                }
                const auto* id = world.get_if<SoulId>(e);
                if (id == nullptr
                    || static_cast<std::size_t>(id->sect) != sect)
                {
                    continue;
                }
                const auto* office = world.get_if<Office>(e);
                if (office == nullptr
                    || static_cast<std::size_t>(office->kind) != cls)
                {
                    continue;
                }
                if (lifespan_reached(*world.get_if<Mortal>(e),
                                     *world.get_if<Cultivation>(e)))
                {
                    continue;  // 死者不领当年份额（账消在同一年发生）
                }
                auto* purse = world.get_if<Purse>(e);
                assert(purse != nullptr);
                const auto pay =
                    purse != nullptr && ledger_.vault[sect] >= want
                        ? want
                        : ledger_.vault[sect];
                if (purse != nullptr)
                {
                    purse->stone += pay;
                    ledger_.vault[sect] -= pay;
                }
                if (pay < want)
                {
                    break;  // 付空：同类后续人不再领（克扣）
                }
            }
        }
    }
```

**实现注意**：上面伪码里的空指针路径（`get_if<Mortal>` 对已死者）
在真实实现里必须先取 `mortal`/`cult` 再判空——遍历中凡三件组件
（Mortal/Cultivation/Purse）缺一即 `continue`，与烧毁循环同一套
存活判据。断供停摆的 `break` 只跳出最内层（当前类的当前宗），
外层类循环照走——这正是「次序」：宗主领完才轮长老，一类付空后
下一类从 0 开始（自然得 0）。

- [ ] **Step 4: 确认通过**

Run: 同上。Expected: 新两条绿；恒等式测试（Task 2）仍绿——分配只在
金库与私囊之间划转，Σ不变。探针表数字（204600/2/833）逐一核对；
对不上停下报告实况。

- [ ] **Step 5: 向用户报告**

报告：太玄/丹霞首年分配读数、克扣次序断言、恒等式回归。**不执行
任何 git 命令。**

---

### Task 4: 散修营生——银两账与三十三年的绝望数学

**Files:**
- Modify: `src/life/economy.cpp`（营生段 + 买入段）
- Test: `tests/test_life_economy.cpp`（追加两条）

**Interfaces:**
- Consumes: `Purse::silver`、`kFreeCultivatorIncomeLiang`、
  `kLiangPerStone`、`EconomyLedger::vault[7]`（无主金库）
- Produces: 每年无主身份者 `silver += 60`；攒够 2000 两从无主金库
  买入一枚下品（金库为 0 则不成交、银两留到明年）

- [ ] **Step 1: 写失败的测试**

`test_life_economy.cpp` 追加：

```cpp
TEST(the_free_cultivator_needs_thirty_three_years)
{
    // 设定 §6.4 逐字的绝望数学：年入约六十两、一枚下品折两千两——
    // 2000/60 = 33.33，第 33 年末 1980 两买不起，第 34 年末 2040 两
    // 才够一次（且公账无钱、买不进——见下一条）。三十三年一枚不调参。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::Purse;
    using gamev2::life::SoulId;

    // 散修人数逐年变（十年一批），银两总量不是 60×N 的定值——断言
    // 「最富散修」：33 年时最富的散修也买不起一枚（1980 < 2000），
    // 34 年 2040 两够了但公账为 0、买不进。
    life.run_years(33);
    std::uint32_t people = 0;
    std::uint64_t richest = 0;
    std::uint32_t over_stone = 0;
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect != gamev2::history::Sect::unowned)
            {
                return;
            }
            ++people;
            richest = richest > p.silver ? richest : p.silver;
            over_stone += p.stone > 0u ? 1u : 0u;
        });
    CHECK(people > 0u);
    // 营生只发银两：第 33 年没人有过一枚灵石（33×60 = 1980 < 2000），
    // 且营生不产 stone——铸币是灵石唯一来源（设计 §3.5「没有第二
    // 个来源」；无主金库为 0，买入永不成交）。
    CHECK_EQ(over_stone, 0u);
    CHECK_EQ(richest, 33u * gamev2::life::kFreeCultivatorIncomeLiang);

    life.run_years(1);  // 第 34 年
    std::uint64_t richest34 = 0;
    over_stone = 0;
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect != gamev2::history::Sect::unowned)
            {
                return;
            }
            richest34 = richest34 > p.silver ? richest34 : p.silver;
            over_stone += p.stone > 0u ? 1u : 0u;
        });
    // 34×60 = 2040 ≥ 2000：够了——但参考世界无主脉全枯、公账为 0，
    // 买不进，银两留着（口径裁定 8：买入从无主金库划转，不铸币）。
    CHECK_EQ(richest34, 34u * gamev2::life::kFreeCultivatorIncomeLiang);
    CHECK_EQ(over_stone, 0u);
    CHECK_EQ(life.economy().vault[7], 0u);
}

TEST(income_has_exactly_one_source)
{
    // 设计 §3.5：「当年 Σ收入 = 脉产出 × kVeinToStone（没有第二个
    // 来源）」。银两不进灵石守恒：营生发银两，灵石只能从铸币来。
    // 断言：跑 100 年，铸造 == 420000×100，且恒等式（Task 2）仍平——
    // 营生若把银两错记进 stone，恒等式与铸造额至少破一头。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(100);

    CHECK_EQ(life.economy().minted_total, 42000000u);
    CHECK_EQ(diag.stone_conservation_broken, 0u);

    // 银两确实发过：散修攒了 100 年营生（部分人已用 2000 两尝试
    // 买入失败，银两不会凭空消失——无主金库为 0 时买入不成交）。
    std::uint64_t silver = 0;
    using gamev2::life::Purse;
    using gamev2::life::SoulId;
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect == gamev2::history::Sect::unowned)
            {
                silver += p.silver;
            }
        });
    CHECK(silver > 100u * gamev2::life::kFreeCultivatorIncomeLiang);
}
```

- [ ] **Step 2: 确认失败**

Run: 同上。Expected: 红——`richest` 为 0（营生未实现）。

- [ ] **Step 3: 实现营生与买入段**

`src/life/economy.cpp`：分配段之后、烧毁循环之前插入（保持次序
铸币→分配→营生→买入→烧毁，设计 §3.1 的单向河：营生的钱赶得上
当年烧毁）：

```cpp
    // 散修营生（设计 §3.4，设定 §6.4 逐字）：年入约六十两入银两账。
    // 银两是凡人货币，不进灵石恒等式（口径裁定 8）。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        if (id == nullptr || purse == nullptr)
        {
            continue;
        }
        if (id->sect == gamev2::history::Sect::unowned)
        {
            purse->silver += kFreeCultivatorIncomeLiang;
        }
    }

    // 攒够一枚就从无主金库买进（固定换价，不是市场——设计 §3.1）。
    // 公账为 0 买不进：银两原样留到明年，不许凭空铸币。参考世界
    // 无主脉全枯，买入分支因此只走到「不成交」侧；把灵石送进公账
    // 是刀 4 盗采的事。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        if (id == nullptr || purse == nullptr)
        {
            continue;
        }
        if (id->sect != gamev2::history::Sect::unowned)
        {
            continue;
        }
        while (purse->silver >= kLiangPerStone
               && ledger_.vault[7] > 0u)
        {
            purse->silver -= kLiangPerStone;
            purse->stone += 1u;
            ledger_.vault[7] -= 1u;
        }
    }
```

（实现注意：营生与买入可以在同一个 `for` 里做完——分成两段是为
对照设计 §3.4 的两条收入线；合并不扣语义，实现者自行判断，但买入
必须在营生之后，同一年攒够同一年买得进。）

- [ ] **Step 4: 确认通过**

Run: 同上。Expected: 新两条绿；恒等式（Task 2）仍绿——买入是金库→
私囊的划转，Σ不变；营生只碰 silver。

- [ ] **Step 5: 向用户报告**

报告：33/34 年的银两读数、买入分支的不成交侧、恒等式回归。
**不执行任何 git 命令。**

---

### Task 5: 突破掷运的变异钉住（刀 2 携带项 ②）

**Files:**
- Test: `tests/test_life_cultivation.cpp`（追加一条）

**Interfaces:**
- Consumes: 刀 2 的 `CultivateSystem` 与既有夹具
- Produces: 一条测试同时钉「掷运会成功」与「掷运会失败」两个方向

**背景**：刀 2 预告的验收项——把掷运换成「恒成功」或「恒失败」的
变异，必须各有一条断言打红。恒失败变异让 100 年内无人突破到筑基；
恒成功变异让引气/炼气队列在门槛年（20+20=40 年）后逐级全升、
引气层只剩新生儿。两条断言各守一边。

- [ ] **Step 1: 写失败（变异下才红）的测试**

`tests/test_life_cultivation.cpp` 追加。断言选型说明：不能用「初始
队列滞留引气 > 0」钉恒失败——正常掷运 300‰ × 数十次尝试下，200 年
里人人都几乎必然突破过，那条断言在正常跑就红。改用两个可观测的
方向旗：**有人突破过**（新队列上筑基，恒失败时为 0）与**有人掷
输了**（快照时刻存在 `progress >= 门槛` 未晋级者——晋级当年进度清零，
进度达标即「今年掷失败、明年再试」，恒成功时恒为 0）。

```cpp
TEST(the_breakthrough_roll_can_pass_and_fail)
{
    // 掷运的变异钉住（刀 2 计划携带项 ②）：
    //   恒成功变异 → 晋级即清零，快照里无人进度达标 → 「有人掷输」
    //   断言红；
    //   恒失败变异 → 无人晋级，新队列永远到不了筑基 → 「有人突破」
    //   断言红。
    // 正常掷运（中位 300‰）两头都不触界。
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

    using gamev2::life::Cultivation;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    using gamev2::life::kRealmYears;
    std::uint32_t breakthroughs = 0;   // 新队列上筑基的人数
    std::uint32_t failed_this_year = 0;  // 进度达标而未晋级的人数
    life.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId& id, const Cultivation& c)
        {
            if (id.index >= initial_population()
                && static_cast<std::size_t>(c.realm)
                       >= static_cast<std::size_t>(Realm::zhuji))
            {
                ++breakthroughs;
            }
            if (c.realm != Realm::dacheng
                && c.progress
                       >= kRealmYears[static_cast<std::size_t>(
                           c.realm)])
            {
                ++failed_this_year;
            }
        });
    CHECK(breakthroughs > 0u);        // 掷运会成功（恒失败时红）
    CHECK(failed_this_year > 0u);     // 掷运会失败（恒成功时红）
}
```

- [ ] **Step 2: 确认通过（正常掷运两头都不触界）**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
Expected: 绿——200 年正常掷运下新队列有人上筑基、快照里有人进度
达标未晋级。若任一实测为 0：停下报告读数（探针环境变了，锚不成立）。

- [ ] **Step 3: 人工变异验证两条断言真的会红**

把 `src/life/systems.cpp` 的 `CultivateSystem` 掷运临时改为
`const bool success = true;`（或 `false;`），重编重跑本条：
恒成功 → `failed_this_year > 0` 红；恒失败 → `breakthroughs > 0` 红。
**验证后恢复原掷运代码**，重编重跑确认全绿。报告两次变异各自的
红灯位置。

- [ ] **Step 4: 向用户报告**

报告：200 年实测的两个方向旗读数、两次变异验证的红灯位置。
**不执行任何 git 命令。**

---

### Task 6: kSectRecruitDivisor 重标（刀 2 携带项 ①）+ 注释订正

**Files:**
- Modify: `include/gamev2/life/constants.hpp`（重标 + 注释订正）
- Test: `tests/test_life_cultivation.cpp`（如需，重钉边界读数）

**Interfaces:**
- Consumes: 含经济的完整年循环（Task 1-4）
- Produces: 重标后的 `kSectRecruitDivisor`；注释读数更新

**背景**：刀 2 实测除数 50 是正反馈（100 年 970→3554，×3.7）——
锚「补员与寿元死亡同量级」远未达到。本刀世界加了经济但断供不杀人
（口径裁定 12），人口调节仍只有寿元死亡与收徒节奏两个杠杆：
除数是唯一旋钮。断供宗（青囊/天机/九幽、以及丹霞等小宗的外门层）
在刀 4 会用「出走」偿还，本刀只调除数。

- [ ] **Step 1: 探针——找除数**

临时 `TEST(probe_recruit_divisor)`：128²/seed13（刀 2 口径），对
候选除数 `{50, 100, 200, 400}` 各跑一遍 200 年，打印每年的
「当年收徒数」与「当年死亡数」（差分总人口即可：新增 = 补员 +
新生计数差，死亡 = 老队列减少；直接逐年的总人口曲线也够用）与
200 年末总人口。锚：**稳态**——后 50 年总人口漂移 < 10%，且
100 年末总人口在 1000~3000（不塌不炸的量级，参照初始 970）。

- [ ] **Step 2: 定值并重钉边界**

按探针读数把 `kSectRecruitDivisor` 改为达标除数（预期在 200 附近，
以实测为准），注释更新为新的实测读数（除数、100 年末人口、
200 年末人口、稳态漂移）。检查刀 2 的两条探针边界测试——
`the_cohort_shrinks_as_the_years_pass`（`survivors > 80`）与
`the_populace_refills_from_below`（`total > 500`、`< 5400`）——
在新除数下重跑：**边界放宽方向**（人口变少）若触线，按下探针新
读数重钉，注释同步；不许为保绿改断言语义。

- [ ] **Step 3: 注释订正（口径裁定 10）**

`constants.hpp` 两处：`kSectRecruitDivisor` 的「刀 3 并入断供后
重标」改为实测读数与「断供出走是刀 4」的归属；`kRealmYears` 与
突破概率四常数的「刀 3 重标」改为「刀 4 重标」（资源门槛并入
`Purse` 的判定是刀 4 的决策面）。

- [ ] **Step 4: 全量回归**

Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe; & .\build\ecs_tests.exe; & .\build\worldgen_tests.exe; & .\build\history_tests.exe; & .\build\faction_tests.exe"`
Expected: 五套全绿。删除探针测试后重跑一遍。

- [ ] **Step 5: 向用户报告**

报告：除数探针四档读数表、选定值与理由、刀 2 边界重钉明细、
五套回归结果。**不执行任何 git 命令。**

---

## 完成判据（刀 3 验收，对照设计 §11）

1. `life_tests` 26 条全绿（setup 10 + cultivation 8 + economy 8）；
   四套件回归全绿。
2. 灵石恒等式：100 年逐年 `Σ(金库+私囊) == 累计铸造 − 累计烧毁`，
   `stone_conservation_broken == 0`。
3. 接口对账（本刀口径）：铸币表逐宗读数 == 探针表；天下年铸 ==
   420000 下品（设定 §6.1.1 的 4200 中品折算）。
4. 33 年一枚下品：第 33 年末无人有 stone、最富散修 1980 两；
   第 34 年 2040 两、公账为 0 不成交、银两留存。
5. 克扣次序：丹霞（断供宗）首年外门/内门实得 0、长老足额 833、
   金库余 2；太玄（盈余宗）外门引气实得 113（150−37）。
6. 变异钉住：恒成功/恒失败两种人工变异各红一条断言（Step 验证过）。
7. `kSectRecruitDivisor` 重标完成、注释读数与实测一致、刀 2 边界
   重钉有据。
8. 确定性：同 seed 两个 256² 世界跑 100 年，账本与私囊分布逐位相同。

刀 4（决策与行为：厚属性判定、Motive/Act、盗采横死、涌现验收）按
设计 §11 另出计划。携带项：① 突破资源门槛并入 `Purse`（`kRealmYears`
与突破概率重标）；② 死者账消换成继承结算的接口预留；③ 断供出走
（外门变散修）——`SoulId` 迁移写入者的经批准例外；④ 买入分支的
「成交」侧需要公账有钱（盗采入账后自然可达，届时补一条正向测试）。

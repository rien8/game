# 派系演化层实施计划：六百年的配额博弈

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给既有 worldgen 与时序层追加一个**派系演化层**——把台账的 8300
快照放进一个活的状态里，用 ECS 逐年推演六百年，产出**当代格局**：谁手里
还剩多少脉、谁被夺过、谁在越采、哪年枯的、格局在哪一年变过。

当代格局是设定 §10.1 四个冲突的物证。台账答不了「老配额怎么变成今天
这个样子的」（台账设计 §8.1：持有格局是设定给定的）；这一层让持有格局
**变成涌现的**。

**Architecture:** 本层**只读** worldgen 的终态与台账的 8300 快照，不回写
它们的任何一个字段。全部状态住在自己的 `ecs::World` 里：脉、派系、洞天、
时钟四族实体，六百年的推演跑在六个 system 上，由 `ecs::Scheduler` 按
读写集分批。

台账是**无派系的基准推演**，本层是**有派系的现实**：两者共用同一套原语
与**同一个标定系数**（`history/quota.hpp`），所以差值就是派系行为的代价。
这条对照是本层的核心产出之一。

**只有一个公共入口**：`faction::simulate(world, ledger, diagnostics)`。
它是值对象式的——读世界与台账，吐一份 `EraReport`。测试全部走它，
不碰私有头。

**随机性只有一处**：越采被发现的年份。其余全部确定。

**Tech Stack:** C++23、clang++ 23.1.1、MSVC STL 14.51、CMake 4.4、
Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-22-faction-evolution-design.md`
（本文的全部语义依据；冲突时以设计文档为准，并回报差异——
本文的「与设计文档的差异与补齐」一节列出了实现前必须先解决的口径问题）

**既有设计文档：** `doc/2026-10-21-history-layer-design.md`（时序层）、
`doc/2026-10-20-worldgen-spirit-layer-design.md`（灵气层）、
`doc/2026-10-20-worldview-xianxia.md`（设定）

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
- 任务步骤里列出的 `git add` 命令**是给用户参考的，不是给执行者跑的**：
  执行者一律不得执行 `git add`、`git commit`、`git stash`、`git clean`。
  改动全部留在工作树里，由用户决定何时、怎么提交
- 输出文件（格局文本、事件表）写入 `agent-temp/`，该目录已被 gitignore
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- 每个新随机流必须同时登记进 `Stream` 枚举与 `kStreamIds` 数组；
  `static_assert(stream_ids_are_distinct())` 会拦住重复流号
- 任何读 `std::uint8_t` 网格当数值的算术，一律显式转成整数再算，
  不得依赖隐式提升
- **派系演化层只读 worldgen 与台账**：不得写 `World` 的任何字段，不得写
  `Ledger` 的任何字段，不得让 `worldgen` 或 `history` 依赖 `faction`
  （依赖方向只能是 `faction → history → worldgen`，以及 `faction → ecs`）
- **本层内的全部历史量用整数或定点**：推演过程中不出现浮点累加。
  潮位折算、越采分摊、表决份额一律走定点或整数除法，否则小数部分会被
  截断，误差会顺着枯竭年传播到整份事件表
- ECS 组件必须满足 `register_component<T>()` 的 `static_assert`：
  默认可构造、`nothrow` 可移动构造、`alignof` 不超过 `max_align_t`
- **system 的执行顺序由 `System::order` 显式声明**：每个 system 写死自己
  在年内 7 步里的次序（设计 §3.13 的表号就是 `order` 值），不依赖注册
  顺序。`Scheduler::plan` 先按 order 稳定排序再贪心分层，冲突者自动落到
  后一批，读者自动落到写者之后。注册顺序只作同 order 时的次级键
  - 早先这里写的是「注册顺序即冲突系统的执行顺序」。那是把语义押在注册
    代码不动上：调度器一改分层规则，次序就静默漂移，而漂移不报错，只改
    数字。合并 main 的 ECS（`order` 权威）之后改成显式声明
- **不使用 `View::each_parallel`**：它的行为依赖 system 在批内的位置
  （批内多 system 时 `in_system_` 被置位，`parallelize_allowed()` 为假，
  它会退化为串行）。本层规模不需要并行，一律用 `each`
- **测试只走公共入口**：`faction::simulate` 与 `faction::export_era`。
  私有头 `src/faction/systems.hpp` 不被任何测试包含——测试要读的东西
  必须出现在 `EraReport` 上
- **每个任务新建 `src/faction/` 下的 .cpp 时，同步把它加进 CMakeLists 的
  `FACTION_SOURCES`**。漏加的后果是「新文件没被编译」——链接失败或
  行为诡异，不会给出「你忘了加源文件」的提示
- 每个字段只有一个写入者（既有不变式，本计划必须守住）：

  | 字段 | 唯一写入者 |
  |---|---|
  | `World` 的全部字段 | worldgen 的 18 个阶段（本计划只读） |
  | `Ledger` 的全部字段 | `build_ledger`（本计划只读） |
  | `FactionId`、`VeinId`、`VeinProfile`、`VeinRight`、`VeinRemainder`、`CaveHeavenId` | 建实体那一步（`setup`），此后只读 |
  | `VeinStock` | `ExtractSystem`（合法开采）、`OvertakeSystem`（越采）、`DepleteSystem`（枯竭年）、`ResolveSystem`（公有化年份） |
  | `VeinOwner` | `ResolveSystem`（公有化与转让） |
  | `FactionBalance` | `AssessSystem`（用度、供给、缺口）、`ExtractSystem`（累计合法开采）、`OvertakeSystem`（累计越采） |
  | `FactionIntent` | `ResolveSystem`（提案年） |
  | `SimClock` | `ClockSystem` |
  | `CaveHeavenState` | `DepleteSystem` |

构建与测试命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/history_tests.exe
./build/faction_tests.exe
```

只跑单个测试文件不需要额外手段：测试框架靠静态注册，删掉 CMakeLists 里的
其余条目即可。正常做法是全部构建。

---

## 文件总览

本计划创建下列文件。文件职责单一，遵循「一件事一个文件」。

| 文件 | 职责 |
|---|---|
| `include/gamev2/history/quota.hpp` | 台账与派系演化层共用的原语：标定系数、潮位积分、定点余数累加器、阴脉判定 |
| `src/history/quota.cpp` | 上述原语的实现 |
| `include/gamev2/faction/faction.hpp` | 组件、角色、立场、设定身份表、诊断结构 |
| `include/gamev2/faction/era.hpp` | 当代格局的输出类型、`simulate` 声明、文本导出 |
| `src/faction/systems.hpp` | 私有头：推演上下文、六个 system、推演器 |
| `src/faction/setup.cpp` | 从 world 与台账建实体；设定身份表的实现 |
| `src/faction/economy.cpp` | 有效流速、衰减、用度、供给、缺口 |
| `src/faction/systems.cpp` | 六个 system 的实现 |
| `src/faction/resolve.cpp` | 重勘：清单生成、表决、归属变更 |
| `src/faction/era.cpp` | 当代格局的汇总与文本导出 |
| `tests/test_faction_economy.cpp` | 经济规则、守恒、枯竭测试 |
| `tests/test_faction_era.cpp` | 格局、事件、重勘、导出测试 |
| `tests/test_faction_determinism.cpp` | 确定性、与台账的等价性 |

本计划修改下列文件：

| 文件 | 改动 |
|---|---|
| `src/history/ledger.cpp` | 改用 `history/quota.hpp` 的共享原语（**数值不变**） |
| `include/gamev2/world/enums.hpp` | `Stream` 扩容到 19 条（新增 `faction = 18`） |
| `include/gamev2/world/spirit.hpp` | 新增哨兵 `kNoCaveHeaven` |
| `CMakeLists.txt` | `history` 库加 `quota.cpp`；新增 `faction` 库与测试；demo 改链 `faction` |
| `src/demo/main.cpp` | 新增当代格局统计输出与 `era.txt` 文本导出 |
| `tests/test_history_ledger.cpp` | 追加共享原语的回归测试 |

**只读边界**：`faction` 依赖 `history` 与 `worldgen`（读它们的终态），
反向不依赖。本计划**不修改** `include/gamev2/world/` 下任何既有头文件的
**行为**，只做两件事：

1. 在 `enums.hpp` 的流号登记表里追加一条流——那是既有代码自己写明的规矩
   （`enums.hpp` 注释原文：「新增一条流必须同改」）。
2. 在 `spirit.hpp` 里加一个哨兵常量 `kNoCaveHeaven`——仿同一文件里
   `kNoVein` / `kNoEye` 的既有做法，不改任何既有值。

---

## 与设计文档的差异与补齐

设计文档给出的规则在本计划落地时，有六处必须先定死的口径。它们不改变
设计的结论，只是把「设计文档里的说法」翻译成「代码里怎么算」。

### 1. 表决权重是配额份额，不是用度份额

设计 §3.11.4 写的是「用度份额」。但魔修与散修的用度无上限（设计 §3.3），
拿它当权重算不出比例。**改为配额份额**：该宗在 8300 年名下脉的总量配额
之和。

这样有三个好处：配额是制度里的既成事实——《脉律》本就按配额分派；
散修不在盟约内，没有席位，权重自然为 0；天机阁无地，权重也是 0；而
魔修的用度虽然无上限，它的**配额**是有数的，所以它有票。

设计文档已同步修订（§3.11.3、§3.11.4、§3.11.5、§9 第 13 条）。

### 2. 越采的上限是余额，不是剩余配额

设计 §3.7 写 `over(v, y) = min(share(v), eff_rate × kOvertakeFactor,
stock(v))`——上限是 `stock`，正确。**但这一条必须写死在代码里**。

**控制方更正**（2026-10-23，实现者实测发现）：本层里 `stock` 与
`quota_total − extracted` **恒等**——建脉时两者相等，此后每次开采同减同增。
所以「余额」与「剩余配额」在本层是同一个数，**把它们写反并不会让越采
消失**（实测：把上限换成饱和的 `quota_total − extracted` 重建，整套测试
照样全绿）。此前写的「越采永不发生」是**台账口径**的推理，搬错了。

真正会崩的变体是**用「剩余合法配额」封顶**——即拿只扣了合法开采的
配额差去封越采：累计开采会超过总量配额，余额变负，守恒恒等式
`quota_total == stock + extracted` 崩掉。那条由守恒断言守着。

所以本条规则的**真实价值**是：越采量必须从余额里扣，余额不能变负。
新加的 `the_overmining_actually_happens` 守的是另一类回归：越采整体
消失（重写 system 时把越采路径改没了）。

守恒恒等式之所以成立，是因为越采也是从 `stock` 里扣的：越采量以余额
封顶，余额不会变负。

### 3. 抽取的整数部分与小数余数

有效流速是定点量。台账用「余数累加」把小数部分逐年结转；本层必须
用同一套机制，否则年度开采量会被截断成 0，分布上出现断崖。

**余数是脉的状态，不是年的状态**：每条脉一个余数器，跨年保留。

### 4. 衰减把定点标度抬高一层

阴脉的衰减率本身是 2^-16 的定点量（`decay_of` 返回 0..65536）。它乘进
分子之后，分母要跟着抬高一层：

```
分母 = 255 × 2^16 × 2^16 = 255 × 2^32
```

台账的余数结转用的是 `255 × 2^16`。本层用 `255 × 2^32`——两层的
余数标度不同，但**各自的分子分母自洽**，而等价的那些脉（见第 6 条）
算出来的整数开采量相同。

**两处必须用同一个分母**：`take_effective` 的取模与整除都要用
`kQuotaFixedDen * kQuotaFixedScale`，差一层就会让有效流速整体差
65536 倍。

### 5. 无上限用度在数据里的表示

设计 §2.3 的 `FactionBalance` 用 `unbounded_demand` 标志 + `demand = 0`
表示「用度无上限」：用度没有上限，就没有可比较的数，写成一个大数只会
在减法里溢出。

行为上的差别在 `OvertakeSystem` 里分支：魔修与散修的越采量直接取
`min(eff_rate × kOvertakeFactor, stock)`，不经过缺口分摊。

### 6. 等价性测试要排除三类脉

设计 §6.1 说等价性比较「台账里 `overmined_amount == 0` 且持有者不是
「无主」的脉」。落地时发现还要排除**阴脉**：

| 排除 | 理由 |
|---|---|
| 越采脉 | 台账的越采是一笔按年抽样的定额，本层的越采是缺口驱动的连续行为，两者本就不同 |
| 无主脉 | 台账里无主脉仍受它自己的总量配额约束；本层的散修用度无上限，会越过配额开采 |
| 阴脉 | 台账不建模「采出即衰」，本层建模。阴脉的合法开采在本层里逐年递减，两边必然不等 |

**比较的是累计合法开采**（不含越采），不是累计总开采。台账那侧的
合法开采从事件表里取（`over_quota == false` 的事件按脉汇总），
不需要给 `Ledger` 加新方法。

设计文档已同步修订（§6.1）。

---

## 测试图幅：为什么派系层的测试要用默认图幅

灵脉的数量按图幅面积缩放（`vein_targets`），所以**小图幅凑不齐设定里的
持有格局**。实测（seed 7）：

| 图幅 | 脉数 | 龙 | 主 | 支 | 七宗的持有格局 |
|---|---|---|---|---|---|
| 64×64 | 5 | 1 | 1 | 3 | 只有太玄宗与无主拿得到脉（主脉不足九条） |
| 128×128 | 13 | 1 | 2 | 10 | 同上，只是太玄多一条主脉 |
| 256×256 | 50 | 1 | 9 | 40 | 完整的设定格局 |

**后果**：格局变化与重勘的测试必须用**默认图幅**。在 64×64 上，除了
太玄宗与无主，谁都拿不到脉，脉权转让无处发生，验收测试会一直失败
——而它失败的原因与代码无关。

实测 256×256 / seed 7 的格局正好是设定要的样子：太玄宗握龙脉加三条
主脉，丹霞谷两条，寒江剑府握着**那条阴脉**，万兽山、青囊谷、九幽宫
各一条，无主池四十条支脉；九幽宫那条主脉在六百年里被抽干，无主池里
还留着活着的支脉（余额 2、132……）。这些正是本层要演出的那几件事。

代价可以接受：256×256 的世界生成约 0.35 秒（台账推演 0.002 秒）。
便宜的测试（守恒、快照、确定性）仍用 64×64。

---

## 模型的三个读数

推演跑完之后，有三样东西必须能被读出来，且都能被测试断言：

| 读数 | 在哪 | 测试怎么判 |
|---|---|---|
| 守恒 | 每条脉 `quota_total == stock + extracted` | 逐脉断言 |
| 格局变化 | 当代的「脉 — 持有者」映射 vs 8300 的映射 | 两张表比对，差异数 ≥ 1 |
| 与基准的差 | 本层的合法开采 vs 台账的合法开采 | 逐脉相等（排除三类脉） |

**格局变化必须能被自动判定**（设计 §6.4）：测试比对两张映射表，而不是
比对某个具体的「太玄宗失去了某条脉」。写死的对照会让这一条测试在
世界变了之后仍然「通过」，而它其实什么都没测。

---

### Task 1: 共享原语（history/quota.hpp），台账改用它

**Files:**
- Create: `include/gamev2/history/quota.hpp`
- Create: `src/history/quota.cpp`
- Modify: `src/history/ledger.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: `gamev2::world::World`、`gamev2::world::compute_spirit_tide`
- Produces:
  - `inline constexpr std::uint64_t gamev2::history::kQuotaFixedScale`（`1 << 16`）
  - `inline constexpr std::uint64_t gamev2::history::kQuotaFixedDen`
    —— `255 * kQuotaFixedScale`
  - `inline constexpr std::uint32_t gamev2::history::kMaxSpanYears`（100000）
    —— 推演跨度的上限（年）
  - `gamev2::history::tide_integral(std::uint32_t epoch, std::uint32_t now)`
    —— `[epoch, now)` 区间内 `compute_spirit_tide(year).level` 之和
  - `gamev2::history::quota_conversion(std::uint64_t sum_total,
    std::uint64_t sum_rate, std::uint64_t integral)` —— 标定系数 `kNum`
  - `class gamev2::history::QuotaRemainder`
    —— `std::uint64_t take(std::uint32_t rate, std::uint32_t level,
    std::uint64_t conversion)` 与 `void reset()`
  - `gamev2::history::is_yin_vein(const World&, std::uint32_t vein_id)`

- [x] **Step 1: 写失败的测试**

在 `tests/test_history_ledger.cpp` 的匿名 namespace 里追加（加在它的
`}  // namespace` 之前）：

```cpp
TEST(the_shared_conversion_matches_the_ledger_extraction)
{
    // 台账与派系演化层必须用同一套原语，否则两套推演的数字不可比，
    // 对照表会变成「说不清的差」。这一条用台账自己的推演结果反过来
    // 验共享原语：共享原语算出的逐年开采量，必须与台账记的账一致。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(!ledger.veins.empty());

    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral =
        gamev2::history::tide_integral(ledger.epoch, ledger.now);
    CHECK(integral > 0u);
    const std::uint64_t conversion =
        gamev2::history::quota_conversion(sum_total, sum_rate, integral);
    CHECK(conversion > 0u);

    // 用共享原语重算第一条脉的累计开采
    const auto& entry = ledger.veins.front();
    gamev2::history::QuotaRemainder remainder;
    std::uint64_t extracted = 0;
    for (std::uint32_t y = ledger.epoch; y < ledger.now; ++y)
    {
        const std::uint32_t level =
            gamev2::world::compute_spirit_tide(y).level;
        const std::uint64_t eff =
            remainder.take(entry.surveyed_rate, level, conversion);
        const std::uint32_t quota_left =
            entry.surveyed_total - static_cast<std::uint32_t>(extracted);
        const std::uint32_t take =
            (eff >= quota_left) ? quota_left : static_cast<std::uint32_t>(eff);
        extracted += take;
        if (eff >= quota_left)
        {
            remainder.reset();
        }
    }
    CHECK_EQ(extracted, entry.extracted);
}

TEST(the_shared_yin_vein_matches_the_ledger_assignment)
{
    // 阴脉判定必须只有一份实现：台账按「这条脉占的格子里有没有阴地」
    // 判，本层也要按同一条规则判。两边各判一次，寒江剑府拿到的
    // 可能就不是同一条脉。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t yin_count = 0;
    for (const auto& vein : w.veins)
    {
        if (gamev2::history::is_yin_vein(w, vein.id))
        {
            ++yin_count;
        }
    }
    CHECK_EQ(yin_count, 1u);
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: 编译失败，`gamev2::history::tide_integral` 未声明。

- [x] **Step 3: 写共享原语**

创建 `include/gamev2/history/quota.hpp`：

```cpp
/// @file quota.hpp
/// @brief 台账与派系演化层共用的配额原语。
///
/// 这几个原语必须只有一份实现。标定系数、阴脉判定、有效流速这三样
/// 若各写一份，两层算出的数字会静默错位——错位在结果里看不出来，
/// 只在对照表里表现为「说不清的差」。
#pragma once

#include "gamev2/world/world.hpp"

#include <cstdint>

namespace gamev2::history {

/// 定点标度：年度开采量保留 2^16 分之一。
///
/// 年度开采量是个小数（流速 × 潮位 × 换算系数，量级 0.05..3），
/// 直接截断成整数会把绝大多数年份变成 0，分布上出现断崖。
inline constexpr std::uint64_t kQuotaFixedScale = 1ull << 16;

/// 有效流速的分母：255 × 2^16。
inline constexpr std::uint64_t kQuotaFixedDen = 255ull * kQuotaFixedScale;

/// 推演跨度的上限（年）。
///
/// 台账与派系演化层都是逐年推演，跨度就是循环次数：年份是个 uint32，
/// 传 UINT32_MAX 就能要出四十亿次循环，外加一份几百 GB 的余额轨迹。
/// 上限把这条岔路堵在分配之前——退化由诊断量报出，不是一段崩掉的
/// 白屏。
inline constexpr std::uint32_t kMaxSpanYears = 100000;

/// 潮位积分：epoch 到 now 之间 level 的总和（level 是 0..255 的字节）。
///
/// 换算系数用它做锚点：若无配额封顶，全世界的存量恰好在当代耗尽；
/// 配额封顶把枯竭摊在六百年里，当代仍有余额。
[[nodiscard]] std::uint64_t tide_integral(std::uint32_t epoch,
                                           std::uint32_t now);

/// 标定系数 kNum。
///
/// 锚点：若无配额封顶，全世界的存量恰好在当代耗尽。它是**世界级**
/// 的量：两层必须用同一个值，否则两套推演不可比。
[[nodiscard]] std::uint64_t quota_conversion(std::uint64_t sum_total,
                                              std::uint64_t sum_rate,
                                              std::uint64_t integral);

/// 年度开采量的小数余数累加器。
///
/// 每条脉一个，跨年保留。抽干配额的那一年余数要清零：被配额卡住时
/// 未消费的小数没有意义。
class QuotaRemainder
{
public:
    /// 当年的有效流速（定点量的整数部分）。
    [[nodiscard]] std::uint64_t take(std::uint32_t rate, std::uint32_t level,
                                      std::uint64_t conversion);

    /// 清零余数。配额被卡住时调用。
    void reset() noexcept { remainder_ = 0; }

private:
    std::uint64_t remainder_ = 0;
};

/// 阴脉判定：这条脉占的格子里有没有阴地。
///
/// 地貌是网格属性，所以要按 vein_id 网格反查这条脉的占格。
[[nodiscard]] bool is_yin_vein(const gamev2::world::World& world,
                               std::uint32_t vein_id);

}  // namespace gamev2::history
```

创建 `src/history/quota.cpp`：

```cpp
/// @file quota.cpp
/// @brief 台账与派系演化层共用的配额原语的实现。
#include "gamev2/history/quota.hpp"
#include "gamev2/world/spirit_tables.hpp"

namespace gamev2::history {

std::uint64_t tide_integral(std::uint32_t epoch, std::uint32_t now)
{
    std::uint64_t sum = 0;
    for (std::uint32_t year = epoch; year < now; ++year)
    {
        sum += gamev2::world::compute_spirit_tide(year).level;
    }
    return sum;
}

std::uint64_t quota_conversion(std::uint64_t sum_total,
                               std::uint64_t sum_rate,
                               std::uint64_t integral)
{
    if (integral == 0u || sum_rate == 0u)
    {
        return 0u;
    }
    return sum_total * 255ull * kQuotaFixedScale / (integral * sum_rate);
}

std::uint64_t QuotaRemainder::take(std::uint32_t rate, std::uint32_t level,
                                    std::uint64_t conversion)
{
    const std::uint64_t num =
        static_cast<std::uint64_t>(rate) * level * conversion;
    const std::uint64_t total = num + remainder_;
    remainder_ = total % kQuotaFixedDen;
    return total / kQuotaFixedDen;
}

bool is_yin_vein(const gamev2::world::World& world, std::uint32_t vein_id)
{
    for (std::uint32_t cell = 0; cell < world.vein_id.size(); ++cell)
    {
        if (world.vein_id.at(cell) != vein_id)
        {
            continue;
        }
        if (world.land_type.at(cell) == gamev2::world::LandType::yin)
        {
            return true;
        }
    }
    return false;
}

}  // namespace gamev2::history
```

- [x] **Step 4: 台账改用共享原语**

修改 `src/history/ledger.cpp`：

1. 删掉本地的 `kFixedScale`、`kMaxSpanYears`、`tide_integral`、
   `is_yin_vein` 四个定义，改为 `#include "gamev2/history/quota.hpp"`。
2. 把 `kNum` 的计算换成
   `const std::uint64_t kNum = quota_conversion(sum_total, sum_rate, integral);`
   ——**除法与截断的顺序保持原样**（原式是
   `sum_total * 255ull * kFixedScale / (integral * sum_rate)`，共享函数里
   逐字相同）。
3. 把 `advance` lambda 里的 `std::vector<std::uint64_t> remainder` 换成
   `std::vector<QuotaRemainder> remainder`，并把余数运算换成
   `remainder[i].take(entry.surveyed_rate, level, kNum)`；配额被卡住时
   调 `remainder[i].reset()` 取代 `remainder[i] = 0`。
4. `den` 改为 `kQuotaFixedDen`。

**数值必须一字不变**：本步只是搬家，改完立刻跑全量台账测试，任何一条
挂掉就说明搬家搬错了。

- [x] **Step 5: 更新 CMakeLists**

在 `HISTORY_SOURCES` 里追加 `src/history/quota.cpp`：

```cmake
set(HISTORY_SOURCES
    src/history/ledger.cpp
    src/history/quota.cpp
)
```

- [x] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: 全部测试通过，且台账的既有 38 条测试一条不少地过。

- [x] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add include/gamev2/history/quota.hpp
git add src/history/quota.cpp
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
git add CMakeLists.txt
```

---

### Task 2: 类型骨架、设定身份表与 CMake 目标

**Files:**
- Create: `include/gamev2/faction/faction.hpp`
- Create: `include/gamev2/faction/era.hpp`
- Create: `src/faction/setup.cpp`
- Create: `tests/test_faction_economy.cpp`
- Modify: `include/gamev2/world/spirit.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `gamev2::history::Sect`、`gamev2::world::VeinRank`、
  `gamev2::world::Element`
- Produces:
  - `namespace gamev2::faction`
  - `enum class Role : std::uint8_t` —— `sect, demonic, free_cultivator, landless`
  - `enum class Stance : std::uint8_t` —— `contesting, neutral, isolationist`
  - `struct FactionIdentity` —— `sect, role, stance, may_propose`
  - `identity(gamev2::history::Sect)` —— 返回该派的设定身份
  - `gamev2::faction::kOvertakeFactor`（2）—— 越采能采到流速的几倍
  - `gamev2::faction::kRedraftPeriod`（200）—— 脉律重勘的间隔
  - `gamev2::faction::kEarlyRedraftInterval`（50）—— 两次重勘的最小间隔
  - `gamev2::faction::kVoteThresholdNum` / `kVoteThresholdDen`（1 / 2）
    —— 过半数门槛。写成两个整数是为了免掉除法：比较时两边同乘分母。
  - 组件 `FactionId`、`FactionBalance`、`FactionIntent`、`VeinId`、
    `VeinProfile`、`VeinRight`、`VeinStock`、`VeinOwner`、`VeinRemainder`、
    `CaveHeavenId`、`CaveHeavenState`、`SimClock`
  - `struct FactionDiagnostics`
  - `struct EraEvent`、`struct VeinReport`、`struct FactionReport`、
    `struct EraReport`
  - `simulate(const World&, const Ledger&, FactionDiagnostics&)`
  - `export_era(const EraReport&, const FactionDiagnostics&, const path&)`
  - `gamev2::world::kNoCaveHeaven`（`0xFFFFFFFFu`）

- [x] **Step 1: 写失败的测试**

创建 `tests/test_faction_economy.cpp`：

```cpp
#include "gamev2/faction/era.hpp"
#include "gamev2/history/quota.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

// 后三个头是后续任务要用的：quota.hpp 出共享原语，generate.hpp 出世界
// 生成入口，spirit_tables.hpp 出潮位函数。era.hpp 只带出 world.hpp，
// 不带这三样。

#include <cstddef>

namespace {

using gamev2::faction::FactionBalance;
using gamev2::faction::identity;
using gamev2::faction::Role;
using gamev2::faction::Stance;
using gamev2::history::Sect;

TEST(the_identity_table_covers_every_sect)
{
    // 七宗 + 无主，一个都不能少。少一个就意味着有派系在推演里无处安放，
    // 而它会在推演中途才崩。
    for (std::size_t i = 0; i < static_cast<std::size_t>(Sect::count); ++i)
    {
        const auto sect = static_cast<Sect>(i);
        const auto& id = identity(sect);
        CHECK(id.sect == sect);
    }
}

TEST(the_identity_table_follows_the_setting)
{
    // 这张表是设定给定的常量：太玄执掌盟约、天机无地、九幽是魔修。
    // 改它等于改设定，必须先改设定文档。
    CHECK(identity(Sect::taixuan).may_propose);
    CHECK(identity(Sect::taixuan).stance == Stance::contesting);
    CHECK(identity(Sect::tianji).role == Role::landless);
    CHECK(identity(Sect::jiuyou).role == Role::demonic);
    CHECK(identity(Sect::unowned).role == Role::free_cultivator);
    CHECK(identity(Sect::qiangnang).stance == Stance::neutral);
    CHECK(identity(Sect::wanshou).stance == Stance::isolationist);
}

TEST(the_faction_balance_defaults_to_a_bounded_demand)
{
    // 无上限用度必须由 unbounded_demand 明说，不能靠 demand 取一个大数
    // 蒙混：减法里会溢出。
    FactionBalance balance;
    CHECK(!balance.unbounded_demand);
    CHECK_EQ(balance.demand, 0u);
    CHECK_EQ(balance.vote_weight, 0u);
}

}  // namespace
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`gamev2/faction/era.hpp` 不存在。

- [x] **Step 3: 写组件与身份表**

创建 `include/gamev2/faction/faction.hpp`：

```cpp
/// @file faction.hpp
/// @brief 派系演化层：活的状态与设定身份。
///
/// 本层只读 worldgen 的终态与台账的 8300 快照，不回写它们的任何字段。
/// 它的全部状态住在自己的 ecs::World 里：六百年的推演是一个有状态的
/// 过程，不是一次性函数。
#pragma once

#include "gamev2/history/ledger.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>

namespace gamev2::faction {

using gamev2::history::Sect;

/// 越采能采到流速的几倍。
///
/// 越采是强行抽取：能采到比流速更多的量，代价是把这口井抽废。
/// 它是标定值：只影响枯竭的早晚，不影响格局的结构。
inline constexpr std::uint32_t kOvertakeFactor = 2;

/// 脉律重勘的间隔（年）。设定 §6.5：脉律每二百年重勘一次。
inline constexpr std::uint32_t kRedraftPeriod = 200;

/// 两次重勘的最小间隔（年）。
///
/// 提案与表决是大事，不能年年开。50 年是「一代人的功夫」这个量级的
/// 程序性间隔，不是经济参数。
inline constexpr std::uint32_t kEarlyRedraftInterval = 50;

/// 过半数门槛。写成整数对是为了免掉除法。
inline constexpr std::uint32_t kVoteThresholdNum = 1;
inline constexpr std::uint32_t kVoteThresholdDen = 2;

/// 派系的角色。决定用度规则与开采规则。
enum class Role : std::uint8_t
{
    /// 正派宗门：用度 = 8300 年实际可采量，采到配额上限就停。
    sect,
    /// 魔修：用度无上限，采尽为止（设定 §8.2「资源当取尽」）。
    demonic,
    /// 散修与无主：用度无上限，无约束开采。
    free_cultivator,
    /// 无地（天机阁）：用度为零，不开采，不参与争夺。
    landless,
};

/// 派系的立场。决定它参不参与争夺，以及能不能被夺。
enum class Stance : std::uint8_t
{
    /// 参与争夺：可以提案，也可以被夺。
    contesting,
    /// 中立：不提案，且不可被夺。
    neutral,
    /// 孤立：不提案，但可以被夺。
    isolationist,
};

/// 派系的设定身份。不可变，不进组件。
struct FactionIdentity
{
    Sect sect = Sect::unowned;
    Role role = Role::free_cultivator;
    Stance stance = Stance::neutral;
    /// 执掌盟约者有提案权。
    bool may_propose = false;
};

/// 派系的设定身份表：七宗 + 无主。
///
/// 这张表是**设定给定的常量**，不是调参项。角色与立场是定性的，
/// 数值差异一律来自状态——没有一个派系拥有自己的「贪婪度」参数。
[[nodiscard]] const FactionIdentity& identity(Sect sect);

/// 派系的身份。
struct FactionId
{
    Sect sect = Sect::unowned;
};

/// 派系的年度账。
struct FactionBalance
{
    /// 用度：门人每年要采出的量。制度量，固定不变。
    ///
    /// 无上限用度的派系（魔修、散修）这一项恒为 0，改由
    /// `unbounded_demand` 表达：用度没有上限，就没有可比较的数。
    std::uint32_t demand = 0;
    /// 用度无上限。
    bool unbounded_demand = false;
    /// 当年合法开采到的量。
    std::uint32_t supply = 0;
    /// 缺口 = demand − supply（饱和，不足为 0）。
    /// 用度无上限的派系（魔修、散修）这一项恒为 0。
    std::uint32_t deficit = 0;
    /// 累计开采（含越采）。
    std::uint32_t extracted = 0;
    /// 累计越采。
    std::uint32_t overmined = 0;
    /// 累计开采量中「未登记在册」的部分。
    std::uint32_t unregistered = 0;
    /// 表决权重：8300 年名下脉的总量配额之和。
    std::uint32_t vote_weight = 0;
};

/// 派系的提案权。
struct FactionIntent
{
    /// 执掌盟约者（太玄宗）可以提前发起脉律重勘。
    bool may_propose = false;
    /// 已经提出重勘的年份，0 表示还没提。
    std::uint32_t proposed_year = 0;
};

/// 脉的身份。生成后不变。
struct VeinId
{
    std::uint32_t id = 0;
};

/// 脉的固有属性。生成后不变。
struct VeinProfile
{
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;
    gamev2::world::Element element = gamev2::world::Element::none;
    /// 阴脉：采出即衰（设定 §8.1）。
    bool yin = false;
    /// 挂在这条脉上的洞天。
    std::uint32_t cave_heaven = gamev2::world::kNoCaveHeaven;
};

/// 脉的制度性权利。《脉律》核定的结果，本层不改。
struct VeinRight
{
    /// 8300 年核定的总量配额。
    std::uint32_t quota_total = 0;
    /// 8300 年核定的安全流速。
    std::uint32_t quota_rate = 0;
    /// 该脉的配额是否登记在册（九幽宫那条不登记）。
    bool registered = true;
    /// 8300 年的持有者。建实体时写下，此后不变。
    ///
    /// 它与 `VeinOwner::owner` 分开是有原因的：归属会变（公有化、转让），
    /// 而「格局变化了多少」要拿当代的归属与 8300 的归属比。若只留一个
    /// 字段，转让之后就分不出「当初是谁的」与「现在是谁的」。
    history::Sect initial_owner = history::Sect::unowned;
};

/// 脉的物理状态。唯一的「真账」。
struct VeinStock
{
    /// 当代余额。
    std::uint32_t stock = 0;
    /// 累计开采（含越采）。
    std::uint32_t extracted = 0;
    /// 累计合法开采（不含越采）。与台账的对照只能比这一项。
    std::uint32_t legal_extracted = 0;
    /// 当年开采量：ExtractSystem 赋值，OvertakeSystem 累加。
    ///
    /// 重勘的试算要拿「这条脉今年被抽了多少」与「转手之后会被抽多少」
    /// 比，所以当年开采量必须留在数据里：事后重算算不出原值，
    /// 余额已经扣过了。
    std::uint32_t taken = 0;
    /// 当年合法开采量。当年越采量 = taken − legal_taken。
    std::uint32_t legal_taken = 0;
    /// 枯竭年，0 表示未枯竭。
    std::uint32_t depleted_year = 0;
    /// 余额被公有化的年份，0 表示未公有化。
    std::uint32_t public_year = 0;
    /// 这条脉的越采是否已经被发现过。一条脉至多报一次。
    bool overmine_reported = false;
};

/// 脉的归属。只有 ResolveSystem 写它。
struct VeinOwner
{
    Sect owner = Sect::unowned;
};

/// 年度开采量的小数余数。跨年保留，所以它是状态。
///
/// 挂成独立组件而不是塞进 VeinStock：余数是「算术的余数」，
/// 不是「账上的量」，混在一起会让守恒恒等式的代码读起来含混。
struct VeinRemainder
{
    std::uint64_t value = 0;
};

/// 洞天。
struct CaveHeavenId
{
    std::uint32_t cell = 0;
    std::uint32_t vein = 0;
};

struct CaveHeavenState
{
    Sect owner = Sect::unowned;
    bool alive = true;
};

/// 时钟。
struct SimClock
{
    std::uint32_t year = 0;
    std::uint32_t level = 0;
};

/// 推演的诊断量。退化不是错误，由这里报出。
struct FactionDiagnostics
{
    /// 台账不可用（当代早于纪元、台账为空）。
    std::uint32_t no_baseline = 0;
    /// 换算系数整除成 0：推演不出任何开采量。
    std::uint32_t conversion_collapsed = 0;
    /// 跨度超限：按「没有推演」处理。
    std::uint32_t span_too_large = 0;
    /// 提案被驳回的次数。
    std::uint32_t redraft_rejected = 0;
    /// 提案通过的次数。
    std::uint32_t redraft_passed = 0;
    /// 公有化后仍挂在原持有者名下的脉数（内部一致性错误）。
    std::uint32_t public_release_mismatch = 0;
    /// 守恒对账不通过的脉数。
    std::uint32_t conservation_broken = 0;
};

}  // namespace gamev2::faction
```

在 `include/gamev2/world/spirit.hpp` 里，`kNoEye` 之后追加：

```cpp
/// 洞天编号的哨兵值。仿 kNoVein / kNoEye：不用 optional，
/// 因为它是与洞天表一一对应的稠密索引。
inline constexpr std::uint32_t kNoCaveHeaven = 0xFFFFFFFFu;
```

- [x] **Step 4: 写输出类型**

创建 `include/gamev2/faction/era.hpp`：

```cpp
/// @file era.hpp
/// @brief 派系演化层的产物：当代格局、事件表与文本导出。
#pragma once

#include "gamev2/faction/faction.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gamev2::faction {

/// 事件类型。
///
/// 只有这几种：多了就说明有行为没有被想清楚，它会变成事件表里的
/// 一行谁也读不懂的记录。
enum class EraEventKind : std::uint8_t
{
    redraft_proposed,    ///< 有人提出重勘
    redraft_passed,      ///< 表决通过
    redraft_rejected,    ///< 表决被否
    vein_transferred,    ///< 脉换了持有者
    vein_depleted,       ///< 脉枯竭
    vein_publicized,    ///< 枯竭的脉公有化
    overmine_discovered, ///< 越采被发现
    cave_heaven_lost,   ///< 洞天失效
};

/// 一条事件。
struct EraEvent
{
    EraEventKind kind = EraEventKind::redraft_proposed;
    std::uint32_t year = 0;
    /// 涉及的脉（提案与表决不用脉）。
    std::uint32_t vein = 0;
    /// 谁做的。
    Sect actor = Sect::unowned;
    /// 涉及谁（受损方）。
    Sect counterparty = Sect::unowned;
    /// 数额（开采量、转移的脉数等）。
    std::uint32_t amount = 0;
};

/// 一脉的当代状态。
struct VeinReport
{
    std::uint32_t vein = 0;
    Sect owner = Sect::unowned;
    /// 8300 年的持有者。与 owner 不同就是格局变过。
    Sect initial_owner = Sect::unowned;
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;
    std::uint32_t quota_total = 0;
    std::uint32_t extracted = 0;
    /// 累计合法开采（不含越采）。与台账的对照只比这一项。
    std::uint32_t legal_extracted = 0;
    std::uint32_t balance = 0;
    std::uint32_t depleted_year = 0;
    bool registered = true;
    bool yin = false;
};

/// 一宗的当代格局。
struct FactionReport
{
    Sect sect = Sect::unowned;
    std::uint32_t vein_count = 0;
    std::uint32_t initial_vein_count = 0;
    std::uint32_t quota_total = 0;
    std::uint32_t balance = 0;
    std::uint32_t demand = 0;
    std::uint32_t supply = 0;
    std::uint32_t deficit = 0;
    std::uint32_t extracted = 0;
    std::uint32_t overmined = 0;
    std::uint32_t unregistered = 0;
    std::uint32_t vote_weight = 0;
    std::uint32_t cave_heavens = 0;
};

/// 当代格局。
struct EraReport
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    /// 实际推演的年份数。
    std::uint32_t years_simulated = 0;
    /// 最后一个被推演的年份。
    std::uint32_t current_year = 0;
    /// 最后一年的潮位。
    std::uint32_t current_level = 0;
    std::vector<FactionReport> factions;
    std::vector<VeinReport> veins;
    std::vector<EraEvent> events;
    /// 当代与 8300 的持有格局不同的脉数。
    std::uint32_t owner_changes = 0;
    std::uint32_t redraft_passed = 0;
    std::uint32_t redraft_rejected = 0;
    std::uint64_t total_extracted = 0;
    std::uint64_t total_balance = 0;
};

/// 从 world 与台账推演 8300 → 当代，产出当代格局。
///
/// 退化不是错误：由 diagnostics 报出，由调用方决定要不要打印。
[[nodiscard]] EraReport simulate(const gamev2::world::World& world,
                                 const gamev2::history::Ledger& ledger,
                                 FactionDiagnostics& diagnostics);

/// 导出人可读格局文本。返回 false 表示文件写不动。
///
/// 诊断量一并写出：退化不是错误，但格局是给人读的文档，读者必须能
/// 在里面看见每一处对不上的地方。
[[nodiscard]] bool export_era(const EraReport& report,
                              const FactionDiagnostics& diagnostics,
                              const std::filesystem::path& path);

}  // namespace gamev2::faction
```

- [x] **Step 5: 实现身份表**

创建 `src/faction/setup.cpp`：

```cpp
/// @file setup.cpp
/// @brief 派系演化层：设定身份表与实体的搭建。
#include "gamev2/faction/faction.hpp"

#include <array>

namespace gamev2::faction {
namespace {

/// 设定给定的身份表。顺序与 Sect 枚举一致，索引即宗门编号。
///
/// 这张表是常量：角色与立场来自设定，不是调参项。
constexpr std::array<FactionIdentity, 8> kIdentities{{
    {Sect::taixuan, Role::sect, Stance::contesting, true},
    {Sect::daxia, Role::sect, Stance::contesting, false},
    {Sect::hanjiang, Role::sect, Stance::contesting, false},
    {Sect::wanshou, Role::sect, Stance::isolationist, false},
    {Sect::qiangnang, Role::sect, Stance::neutral, false},
    {Sect::tianji, Role::landless, Stance::neutral, false},
    {Sect::jiuyou, Role::demonic, Stance::contesting, false},
    {Sect::unowned, Role::free_cultivator, Stance::neutral, false},
}};

static_assert(kIdentities.size() == static_cast<std::size_t>(Sect::count));

}  // namespace

const FactionIdentity& identity(Sect sect)
{
    return kIdentities[static_cast<std::size_t>(sect)];
}

}  // namespace gamev2::faction
```

- [x] **Step 6: 更新 CMakeLists**

追加 `faction` 静态库与 `faction_tests` 可执行文件（放在 `history` 那段
之后）：

```cmake
set(FACTION_SOURCES
    src/faction/setup.cpp
)
add_library(faction STATIC ${FACTION_SOURCES})
target_include_directories(faction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(faction PUBLIC history ecs)

set(FACTION_TEST_SOURCES
    tests/main.cpp
    tests/test_faction_economy.cpp
)
add_executable(faction_tests ${FACTION_TEST_SOURCES})
target_link_libraries(faction_tests PRIVATE faction)
```

**本任务的 `faction` 库还没有 `simulate` 的实现**，所以 `era.cpp` 还不
存在。链接会缺符号——所以本任务**先不建 `era.cpp`**，把 `simulate`
留到 Task 3。若链接器报错「undefined reference to simulate」，说明
`era.hpp` 声明了它而没有实现：**Task 3 之前不要加任何调用它的测试**。

- [x] **Step 7: 构建并运行测试**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/faction_tests.exe
```

Expected: 3 条测试通过。

- [x] **Step 8: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add include/gamev2/faction/faction.hpp
git add include/gamev2/faction/era.hpp
git add src/faction/setup.cpp
git add include/gamev2/world/spirit.hpp
git add tests/test_faction_economy.cpp
git add CMakeLists.txt
```

---

### Task 3: 建实体与推演入口（simulate）

**Files:**
- Create: `src/faction/systems.hpp`
- Create: `src/faction/era.cpp`
- Modify: `src/faction/setup.cpp`
- Test: `tests/test_faction_economy.cpp`

**Interfaces:**
- Consumes: Task 2 的组件与身份表；`gamev2::history::Ledger`
- Produces:
  - `struct SimConfig` —— `epoch, now, conversion`
  - `class Simulation` —— 私有头里：`seed_entities()`、`run()`
  - `gamev2::faction::simulate(const World&, const Ledger&,
    FactionDiagnostics&)`（在 `era.cpp` 里实现）

- [x] **Step 1: 写失败的测试**

在 `tests/test_faction_economy.cpp` 的匿名 namespace 里追加。同时在文件
顶部补上 `#include "gamev2/world/generate.hpp"`——本任务起要自己生成
世界，`era.hpp` 只带出 `world.hpp`，不带生成入口。

```cpp
TEST(the_report_starts_from_the_8300_snapshot)
{
    // 建实体这一步决定了后续一切：脉的条数必须与台账一致，
    // 初始余额必须等于 8300 的总量配额，初始归属必须等于台账的
    // 持有格局。这三条错了，后面的推演算的是另一个世界的历史。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    CHECK(!ledger.veins.empty());

    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.veins.size(), ledger.veins.size());
    CHECK_EQ(report.factions.size(),
             static_cast<std::size_t>(gamev2::history::Sect::count));
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        CHECK_EQ(report.veins[i].vein, ledger.veins[i].vein);
        CHECK_EQ(report.veins[i].quota_total, ledger.veins[i].surveyed_total);
        CHECK(report.veins[i].initial_owner == ledger.veins[i].holder);
    }
}

TEST(a_world_before_the_epoch_yields_an_empty_era)
{
    // 当代早于纪元：没有「从 8300 推到当代」这件事，硬推出来的格局
    // 没有意义（与台账同规）。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    p.spirit_year = 8000;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK(report.veins.empty());
    CHECK_EQ(fdiag.no_baseline, 1u);
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 链接失败，`simulate` 未定义。

- [x] **Step 3: 写私有头**

创建 `src/faction/systems.hpp`：

```cpp
/// @file systems.hpp
/// @brief 派系演化层的私有头：推演上下文与推演器。
///
/// 不放进公共头：这些类型是实现细节，调用方只需要 faction.hpp 与
/// era.hpp。测试也不许包含本头——测试要读的东西必须出现在 EraReport 上。
#pragma once

#include "gamev2/faction/era.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::faction {

/// 推演上下文。
///
/// 标定系数在这里固化一次：两层推演必须用同一个值，否则对照表失效。
struct SimConfig
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    /// 标定系数（台账的同一个值）。
    std::uint64_t conversion = 0;
};

/// 推演器：持有实体世界、时钟与事件表。
class Simulation
{
public:
    Simulation(const gamev2::world::World& world,
               const gamev2::history::Ledger& ledger,
               SimConfig config,
               std::vector<EraEvent>& events);

    /// 建实体：脉、派系、洞天、时钟。
    void seed_entities();

    /// 推演 [epoch, now) 的每一年。
    void run();

    /// 把实体世界的终态读成报告。
    void collect(EraReport& report, FactionDiagnostics& diagnostics);

private:
    /// 输入。引用：生命周期由调用方保证（推演器不拥有世界）。
    const gamev2::world::World& world_;
    const gamev2::history::Ledger& ledger_;
    /// 实体世界。名字与上面那个 `world_` 故意分开：一个是只读的
    /// 地图，一个是本层自己的实体世界，两个都是「世界」，混成一个名字
    /// 迟早会写错其中一个。
    gamev2::ecs::World ecs_world_;
    gamev2::ecs::EntityId clock_;
    SimConfig config_;
    /// 事件表。引用：生命周期由调用方保证（推演器不拥有它）。
    std::vector<EraEvent>& events_;
    /// 脉实体的顺序。视图的遍历顺序不保证与台账一致，
    /// 所以要自己记一份，才能按脉编号取回。
    std::vector<gamev2::ecs::EntityId> veins_;
    std::vector<gamev2::ecs::EntityId> factions_;
};

}  // namespace gamev2::faction
```

- [x] **Step 4: 实现建实体**

在 `src/faction/setup.cpp` 里追加实现。要点：

1. **脉**：遍历 `ledger.veins`（台账已经把荒脉排除在外了），为每条建一个
   实体，挂上 `VeinId`（用 `entry.vein`）、`VeinProfile`（品阶与五行取自
   `world.veins[entry.vein]`，`yin` 用 `history::is_yin_vein`）、
   `VeinRight`（`entry.surveyed_total` / `entry.surveyed_rate` /
   `entry.registered` / `initial_owner = entry.holder`）、
   `VeinStock`（`stock = entry.surveyed_total`，
   其余为 0）、`VeinOwner`（`entry.holder`）、`VeinRemainder`。
2. **洞天**：`world.cave_heavens` 每一处建一个实体。它的归属取它所在
   格子对应的脉的持有者（`world.vein_id.at(cave.cell)` → 脉编号 →
   持有者）。它挂在哪条脉上，用同一个反查。
3. **派系**：八个实体，挂 `FactionId`、`FactionBalance`、
   `FactionIntent`（`may_propose` 取自身份表）。**用度与表决权重在这里
   一次算好**——它们是常量，不逐年变：

   - 用度 `demand(f) = Σ_{v ∈ f} min(eff_rate(v, 8300), quota_total(v))`，
     f 的脉取 **8300 的持有格局**。锚在 8300 年，是因为脉律是 8300 编的：
     它规定的年采额就是那年的实际开采量。
   - 用度无上限的角色（魔修、散修）置 `unbounded_demand = true`、
     `demand = 0`。
   - 表决权重 `vote_weight(f) = Σ_{v ∈ f} quota_total(v)`，同样取 8300
     的持有格局。**无主的权重恒为 0**：它不在盟约里，没有席位——
     这条要显式清零，不能靠「无主的脉都是支脉、权重小」蒙混：
     支脉有四十条，权重加起来比七宗加起来还大。
     天机阁无地，权重自然也是 0。

   `eff_rate(v, 8300)` 用 8300 年的状态算：余额 = 总量配额，累计开采 = 0，
   阴脉的衰减率因此是满值（尚未开采）。

4. **时钟**：一个实体，挂 `SimClock{year = epoch - 1, level = 0}`。

**时钟为什么从纪元前一年起算**：`ClockSystem` 每年把年推进一格，所以
第一年就是纪元年 8300，与台账的循环区间 `[epoch, now)` 一致。

**本任务的 `run()` 是空实现**：推演的六个 system 在 Task 5 才写。现在
`run()` 什么都不做，只留一句注释说明它将来会逐年跑推演——**不要提前
实现推演逻辑**（那是 Task 5 的活，提前写了 Task 5 就要推翻重来）。
同理 `collect` 把 `current_year` / `current_level` 填成时钟当前的值、
`years_simulated` 填 0（还没推演任何一年）。

**脉的顺序**：`veins_` 按台账的顺序记下实体句柄。ECS 的视图遍历顺序是
archetype/chunk 顺序，与台账的顺序没有对应关系；不自己记一份，就没法
按脉编号取回实体。

- [x] **Step 5: 实现 `simulate`**

创建 `src/faction/era.cpp`：

```cpp
/// @file era.cpp
/// @brief 派系演化层的入口与产物：当代格局的汇总与文本导出。
///
/// systems.hpp 是同目录的私有头，不能按 "gamev2/faction/..." 引——
/// src/ 不在 include 路径上（也不该在，那会让私有头对外可见）。
#include "gamev2/faction/era.hpp"
#include "systems.hpp"
#include "gamev2/history/quota.hpp"

namespace gamev2::faction {

EraReport simulate(const gamev2::world::World& world,
                   const gamev2::history::Ledger& ledger,
                   FactionDiagnostics& diagnostics)
{
    EraReport report;
    report.epoch = ledger.epoch;
    report.now = ledger.now;

    if (ledger.veins.empty() || ledger.now < ledger.epoch)
    {
        // 没有基线就没有推演。与台账同规：当代早于纪元时台账为空。
        diagnostics.no_baseline = 1u;
        return report;
    }

    // 跨度上限。必须在潮位积分之前：积分本身就是一次逐年循环，
    // 超大跨度会先在这里把机器拖死。
    if (ledger.now - ledger.epoch > gamev2::history::kMaxSpanYears)
    {
        diagnostics.span_too_large = 1u;
        return report;
    }

    // 标定系数：与台账逐字相同的算法，取自共享原语。
    //
    // 绝不在这里重写一遍公式——那正是 Task 1 存在的理由。
    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral =
        gamev2::history::tide_integral(ledger.epoch, ledger.now);
    const std::uint64_t conversion =
        gamev2::history::quota_conversion(sum_total, sum_rate, integral);
    if (conversion == 0u)
    {
        diagnostics.conversion_collapsed = 1u;
        return report;
    }

    SimConfig config;
    config.epoch = ledger.epoch;
    config.now = ledger.now;
    config.conversion = conversion;

    std::vector<EraEvent> events;
    Simulation sim(world, ledger, config, events);
    sim.seed_entities();
    sim.run();
    sim.collect(report, diagnostics);
    return report;
}

}  // namespace gamev2::faction
```

`Simulation::collect` 把实体世界的终态读成 `EraReport`：逐脉读
`VeinOwner` / `VeinStock` / `VeinRight` / `VeinProfile` / `VeinId`，
逐派读 `FactionBalance` / `FactionId`，事件表原样搬过来。

**报告里两个归属字段的来源不同**：`VeinReport::owner` 取自
`VeinOwner::owner`（当代的归属，转让后会变），`VeinReport::initial_owner`
取自 `VeinRight::initial_owner`（8300 年的归属，建实体时写下，此后不变）。
两者相等就是「这条脉没换过主人」。

**收尾时做两处对账**，结果写进诊断量：

```cpp
// 守恒：每条脉的余额与开采之和必须等于它的配额。
if (vein.stock + vein.extracted != vein.quota_total)
{
    ++diagnostics.conservation_broken;
}
// 公有化的一致性：抽干的脉不该还挂在原主人名下。
if (vein.stock == 0u && vein.owner != Sect::unowned)
{
    ++diagnostics.public_release_mismatch;
}
```

**这两处对账是本层的基本正确性判据**：它们不阻止推演跑完，只把
「哪里对不上」报出来。

- [x] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 6 条测试通过（Task 2 留下 4 条，本任务加 2 条）。

- [x] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/systems.hpp
git add src/faction/era.cpp
git add src/faction/setup.cpp
git add tests/test_faction_economy.cpp
```

---

### Task 4: 经济核心：有效流速、衰减、用度、供给、缺口

**Files:**
- Create: `src/faction/economy.cpp`
- Modify: `include/gamev2/faction/faction.hpp`（追加三个纯函数的声明）
- Test: `tests/test_faction_economy.cpp`

**Interfaces:**
- Consumes: `VeinProfile`、`VeinRight`、`VeinStock`、`VeinRemainder`、
  `FactionBalance`
- Produces:
  - `gamev2::faction::decay_of(const VeinProfile&, const VeinRight&,
    const VeinStock&)` —— 阴脉的衰减率（0..65536 的定点）
  - `gamev2::faction::take_effective(const VeinProfile&, const VeinRight&,
    const VeinStock&, std::uint32_t level, std::uint64_t conversion,
    VeinRemainder&)` —— 当年的有效流速（定点量的整数部分），并结转余数
  - `gamev2::faction::deficit_of(std::uint32_t demand,
    std::uint32_t supply)` —— 缺口
  - 三个函数的**声明放在公共头 `include/gamev2/faction/faction.hpp`**，
    不是私有头：测试要直接调它们，而测试不许包含私有头
    （见 Global Constraints「测试只走公共入口」）。它们是纯函数，
    声明在组件所在的公共头上不算扩散。

- [x] **Step 1: 写失败的测试**

在 `tests/test_faction_economy.cpp` 追加：

```cpp
TEST(a_plain_vein_does_not_decay)
{
    // 只有阴脉采出即衰。其余脉的衰减率恒为一。
    gamev2::faction::VeinProfile profile;
    profile.yin = false;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    gamev2::faction::VeinStock stock;
    stock.extracted = 500u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 65536u);
}

TEST(a_yin_vein_decays_with_the_extraction)
{
    // 阴脉的产出随累计开采线性衰减：采了一半，产出减半。
    // 这条是寒江剑府的结构性赤字的唯一来源：它没有专属参数，
    // 赤字是从地脉的性质里长出来的。
    gamev2::faction::VeinProfile profile;
    profile.yin = true;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    gamev2::faction::VeinStock stock;
    stock.extracted = 250u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 49152u);
    stock.extracted = 1000u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 0u);
}

TEST(the_effective_rate_equals_the_quota_rate_at_the_peak_year)
{
    // 8300 年是第九轮灵潮的峰值年，脉律的基数就编在这一年。
    // 把标定系数取成「使峰值年折出等于流速配额」的那个值，
    // 有效流速就应当等于流速配额。
    gamev2::faction::VeinProfile profile;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    right.quota_rate = 40u;
    gamev2::faction::VeinStock stock;
    stock.stock = 1000u;

    const std::uint32_t level_8300 =
        gamev2::world::compute_spirit_tide(8300).level;
    CHECK_EQ(level_8300, 110u);
    // 夹具的换算系数要向上取整：整除截断会把峰值年的有效流速算成
    // 39 而不是 40（39.9976 截断），断言就永远差一格。
    // 实现里的截断方向不能动——它要与台账的 take() 逐位相同。
    const std::uint64_t conversion =
        (gamev2::history::kQuotaFixedDen + 109u) / 110u;
    gamev2::faction::VeinRemainder remainder;
    const std::uint64_t eff = gamev2::faction::take_effective(
        profile, right, stock, level_8300, conversion, remainder);
    CHECK_EQ(eff, 40u);
}

TEST(the_deficit_is_the_gap_between_demand_and_supply)
{
    CHECK_EQ(gamev2::faction::deficit_of(100u, 30u), 70u);
    CHECK_EQ(gamev2::faction::deficit_of(30u, 100u), 0u);
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`decay_of` 未声明。

- [x] **Step 3: 写经济函数**

在 `include/gamev2/faction/faction.hpp` 里追加这三个函数的声明（**不是**
私有头 `src/faction/systems.hpp`：测试要直接调它们，而测试不许包含私有
头），然后创建 `src/faction/economy.cpp`：

```cpp
/// @file economy.cpp
/// @brief 派系演化层的经济核心：流速、衰减、用度、供给、缺口。
///
/// 三个纯函数声明在公共头 faction.hpp（测试要直接调它们，而测试
/// 不许包含私有头），所以这里包的是公共头，不是 systems.hpp。
#include "gamev2/faction/faction.hpp"
#include "gamev2/history/quota.hpp"

#include <cstdint>

namespace gamev2::faction {
namespace {

/// 衰减把定点标度抬高一层：分母 = 255 × 2^16 × 2^16。
///
/// 两处必须用同一个分母（取模与整除）。差一层就会让有效流速整体
/// 差 65536 倍。
constexpr std::uint64_t kDecayDen =
    gamev2::history::kQuotaFixedDen * gamev2::history::kQuotaFixedScale;

}  // namespace

std::uint32_t decay_of(const VeinProfile& profile, const VeinRight& right,
                       const VeinStock& stock)
{
    if (!profile.yin)
    {
        // 非阴脉的衰减率恒为一：只有阴脉采出即衰（设定 §8.1）。
        return static_cast<std::uint32_t>(gamev2::history::kQuotaFixedScale);
    }
    if (right.quota_total == 0u)
    {
        return 0u;
    }
    // 采出即衰：产出随累计开采线性衰减，采完为止。
    //
    // 衰减率 = 剩余存量 / 总量配额。累计开采可能超过配额（越采），
    // 所以剩余量要夹在 0：衰减率不能为负。
    //
    // 注意分子是**剩余量**不是总量配额——写成总量会让「采了一半」
    // 的脉衰减率为满值，衰减就永远不会发生。
    const std::uint32_t left =
        (stock.extracted < right.quota_total)
            ? (right.quota_total - stock.extracted)
            : 0u;
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(left) * gamev2::history::kQuotaFixedScale)
        / right.quota_total);
}

std::uint64_t take_effective(const VeinProfile& profile,
                             const VeinRight& right, const VeinStock& stock,
                             std::uint32_t level, std::uint64_t conversion,
                             VeinRemainder& remainder)
{
    const std::uint32_t decay = decay_of(profile, right, stock);
    // 乘法顺序要紧：rate × level 先做（≤ 1e6），再乘 conversion
    // （≈ 1.5e5），最后乘 decay（≤ 2^16）。每一步的中间值都在 1e17
    // 以内，不会溢出 uint64。
    const std::uint64_t num = static_cast<std::uint64_t>(right.quota_rate)
                              * level * conversion * decay;
    const std::uint64_t total = num + remainder.value;
    remainder.value = total % kDecayDen;
    return total / kDecayDen;
}

std::uint32_t deficit_of(std::uint32_t demand, std::uint32_t supply)
{
    return demand > supply ? demand - supply : 0u;
}

}  // namespace gamev2::faction
```

**为什么本层不复用 `history::QuotaRemainder::take`**：台账的余数标度是
`255 × 2^16`（没有衰减因子），本层是 `255 × 2^32`（有衰减因子）。两层的
余数不能混用，但**有效流速的整数结果必须相同**——那正是 Task 9 的
等价性测试要守住的。

- [x] **Step 4: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 9 条测试通过。

- [x] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/economy.cpp
git add src/faction/systems.hpp
git add tests/test_faction_economy.cpp
```

---

### Task 5: 逐年推进：时钟、评估、开采、越采

**Files:**
- Create: `src/faction/systems.cpp`
- Modify: `src/faction/systems.hpp`（追加四个 system 的声明）
- Modify: `src/faction/setup.cpp`（`Simulation::run` 装订 system）
- Modify: `include/gamev2/world/enums.hpp`（新增 `Stream::faction`）
- Test: `tests/test_faction_economy.cpp`

**Interfaces:**
- Consumes: Task 3 的组件与实体、Task 4 的经济函数
- Produces:
  - `class ClockSystem` —— 读写 `SimClock`
  - `class AssessSystem` —— 写 `FactionBalance`
  - `class ExtractSystem` —— 写 `VeinStock`、`VeinRemainder`
  - `class OvertakeSystem` —— 写 `VeinStock`、`FactionBalance`
  - `gamev2::world::Stream::faction`（值 18，`kStreamIds` 同步扩容到 19）

- [x] **Step 1: 写失败的测试**

在 `tests/test_faction_economy.cpp` 追加：

```cpp
TEST(the_simulation_covers_every_year)
{
    // 推演必须逐年跑完 [8300, 当代)。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.epoch, 8300u);
    CHECK_EQ(report.now, ledger.now);
    CHECK_EQ(report.years_simulated, ledger.now - ledger.epoch);
    CHECK_EQ(report.current_year, ledger.now - 1u);
    CHECK_EQ(report.current_level,
             gamev2::world::compute_spirit_tide(ledger.now - 1u).level);
}

TEST(the_extraction_never_breaks_the_conservation)
{
    // 守恒恒等式：quota_total == stock + extracted。
    // 越采也是从 stock 里扣的，所以它不会让这条恒等式崩。
    //
    // 这条守的是「越采把余额抽成负数」那类错——若越采用「剩余合法
    // 配额」封顶，累计开采会超过总量配额，余额变负，恒等式崩。
    // 另一类回归（越采整体消失）由 the_overmining_actually_happens 守。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(fdiag.conservation_broken, 0u);
    std::uint64_t quota = 0;
    std::uint64_t extracted = 0;
    std::uint64_t balance = 0;
    for (const auto& vein : report.veins)
    {
        quota += vein.quota_total;
        extracted += vein.extracted;
        balance += vein.balance;
    }
    CHECK_EQ(extracted + balance, quota);
    CHECK(extracted > 0u);
}

TEST(the_overmining_actually_happens)
{
    // 越采路径必须有一条自动化断言。
    //
    // 上面那条守恒测试抓不住「越采整体消失」这个错：越采一次都没发生
    // 时守恒照样成立，测试照样绿。而简报自己许诺过「另有一条专测越采
    // 的」——整个计划里却没有。越采一旦回归，全绿的测试会给出虚假的安心。
    //
    // （注：越采的「上限写成余额还是剩余配额」这个错，守恒测试已经守着
    // 它的后果——余额会变负、恒等式崩。见「与设计文档的差异与补齐」第 2 条
    // 的控制方更正。）
    //
    // 用**默认图幅**：64x64 上年流速截断为 0、越采根本不触发（小图幅
    // 退化），只有默认图幅才演得出越采。信号是公共入口能读到的：
    // 某条脉的 extracted > legal_extracted。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    std::uint32_t overmined = 0u;
    for (const auto& vein : report.veins)
    {
        if (vein.extracted > vein.legal_extracted)
        {
            ++overmined;
        }
    }
    CHECK(overmined > 0u);
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`years_simulated` 未声明。

- [x] **Step 3: 写四个 system**

在 `src/faction/systems.hpp` 追加声明，在 `src/faction/systems.cpp`
实现。形状如下（其余三个照此写）：

```cpp
/// 时钟。每年把年推进一格，并写入当年的潮位。
///
/// 它排在批首：当年的潮位是所有经济计算的输入，读者必须落在
/// 写者之后——而调度器靠读写集保证这一点（ClockSystem 写 SimClock，
/// 别的 system 读它，所以它们自动落到后面一批）。
class ClockSystem final : public gamev2::ecs::System
{
public:
    /// 读写集。`const SimClock` 出现在别的 system 的读集里，是为了让
    /// 调度器把「写钟的」与「读钟的」分到不同批次——同批次是并行跑的。
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<SimClock>;

    explicit ClockSystem(gamev2::ecs::EntityId clock) : clock_(clock) {}

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer&) override
    {
        auto* clock = world.get_if<SimClock>(clock_);
        if (clock == nullptr)
        {
            return;
        }
        ++clock->year;
        clock->level = gamev2::world::compute_spirit_tide(clock->year).level;
    }

private:
    gamev2::ecs::EntityId clock_;
};
```

**四个 system 的读写集**（顺序敏感，注释里写明）：

- `ClockSystem` —— 写 `SimClock`。
- `AssessSystem` —— 读 `const SimClock`、`const VeinOwner`、`const VeinRight`、
  `const VeinStock`、`const VeinProfile`、`const FactionId`；
  写 `FactionBalance`。
- `ExtractSystem` —— 读 `const SimClock`、`const VeinOwner`、`const VeinRight`、
  `const VeinProfile`；写 `VeinStock`、`VeinRemainder`、`FactionBalance`
  （归属派系的累计开采）。
- `OvertakeSystem` —— 读 `const SimClock`、`const VeinOwner`、`const VeinRight`、
  `const VeinProfile`、`const FactionId`；写 `VeinStock`、
  `FactionBalance`。

**`const SimClock` 必须在每个用到潮位的 system 的读集里**：调度器只靠
读写集判冲突，同一批里的 system 是并行跑的。潮位不声明成读，调度器就会
把 `AssessSystem` 与写 `SimClock` 的 `ClockSystem` 放进同一批——两个并行
跑，一个写一个读，读到的可能是旧值，也可能是撕裂的。数据竞争表现为
「同一个世界跑两次结果不同」，而它的根因在几十行之外。

（`DepleteSystem` 与 `ResolveSystem` 同样要读 `SimClock`，见 Task 6、7。）

**`AssessSystem`**：对每个派系，扫描它名下的脉，累加
`min(eff_rate, quota_total − extracted, stock)` 得供给；缺口 =
`deficit_of(demand, supply)`。用度在建实体时就算好（Task 3），这里不动。

**`ExtractSystem`**：对每条脉，
`take = min(eff_rate, quota_total − extracted, stock)`；
`extracted += take; legal_extracted += take; stock -= take;`
`taken = take; legal_taken = take;` 余数用 `take_effective` 结转；
配额被卡住时清零余数。

**`OvertakeSystem`**：对每个派系：

- 用度无上限的（魔修、散修）：越采量 = `min(eff_rate ×
  kOvertakeFactor, stock)`，不经过缺口分摊。
- 用度有上限的：缺口 > 0 时，缺口按各脉的 eff_rate 比例分摊，越采量 =
  `min(share, eff_rate × kOvertakeFactor, stock)`。
- 越采量加进 `extracted` 与 `stock`，并累加进派系的 `overmined` 与
  `extracted`；脉的 `registered` 为假时，累加进 `unregistered`。
- **越采的上限是 `stock`（余额）**：余额不能变负（见「与设计文档的差异与
  补齐」第 2 条的控制方更正——本层里余额与剩余配额恒等，写反不会让
  越采消失，但用「剩余合法配额」封顶会让余额变负、守恒崩）。

**发现事件**：登记在册的派系越采会被仲裁发现。每年 1/8 的概率，
**一条脉至多报一次**（`VeinStock::overmine_reported`）。发现的年份按
`(年, 派系, 脉)` 无状态派生（`Stream::faction`）。

- [x] **Step 4: 新增随机流**

在 `include/gamev2/world/enums.hpp` 的 `Stream` 枚举里追加：

```cpp
    /// 派系演化层的越采发现抽样。
    faction = 18,
```

并把 `kStreamIds` 扩容到 19，追加
`static_cast<std::uint32_t>(Stream::faction)`。**两处必须同改**——
枚举旁的注释写着「新增一条流必须同改」。

- [x] **Step 5: 装订推演器**

在 `src/faction/setup.cpp` 里实现 `Simulation::run`：

```cpp
void Simulation::run()
{
    // 执行顺序由每个 system 自己的 `order` 声明（设计 §3.13），
    // 下面这几行的注册顺序只作同 order 时的次级键。
    gamev2::ecs::Scheduler scheduler(1);
    ClockSystem clock(clock_);
    AssessSystem assess;
    ExtractSystem extract;
    OvertakeSystem overtake(events_);
    scheduler.add(clock);
    scheduler.add(assess);
    scheduler.add(extract);
    scheduler.add(overtake);
    scheduler.plan();

    gamev2::ecs::CommandBuffer cmd;
    for (std::uint32_t y = config_.epoch; y < config_.now; ++y)
    {
        scheduler.run(ecs_world_, cmd);
    }
}
```

**工作线程数给 1**：本层规模下并行是零收益，而多线程会让
`View::each_parallel` 的行为依赖批内位置（见 Global Constraints）。
串行是刻意的选择，不是省事。

- [x] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 11 条测试通过。

- [x] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/systems.cpp
git add src/faction/systems.hpp
git add src/faction/setup.cpp
git add include/gamev2/world/enums.hpp
git add tests/test_faction_economy.cpp
```

---

### Task 6: 枯竭、洞天失效与公有化

**Files:**
- Modify: `src/faction/systems.cpp`
- Modify: `src/faction/systems.hpp`
- Modify: `src/faction/setup.cpp`（注册 `DepleteSystem`）
- Test: `tests/test_faction_economy.cpp`

**Interfaces:**
- Produces:
  - `class DepleteSystem` —— 写 `VeinStock`、`CaveHeavenState`
  - `EraReport::veins[i].depleted_year`（已有）

- [x] **Step 1: 写失败的测试**

在 `tests/test_faction_economy.cpp` 追加：

```cpp
TEST(a_depleted_vein_is_marked)
{
    // 枯竭的脉要被认出来：余额归零的那一年记进 depleted_year。
    //
    // **公有化的断言不在这里**：公有化要改 VeinOwner，而 VeinOwner 的
    // 唯一写入者是 ResolveSystem——它到 Task 7 才存在。这条断言挪到
    // Task 7，那里才真的会发生。
    //
    // **用默认图幅**：默认图幅上有 45 条脉被抽干，信号够强；64x64 上
    // 只有 3 条（全是无主的支脉），断言虽然也过，覆盖的面窄得多。
    //
    // （实现者更正：控制方此前说「64x64 上没有一条脉被抽干」是错的——
    // 那次探针跑在 DepleteSystem 落地之前，数的是 depleted_year 而不是
    // 余额归零。台账与派系演化层在 64x64 上都是 3 条。）
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    std::size_t depleted = 0;
    for (const auto& vein : report.veins)
    {
        if (vein.depleted_year == 0u)
        {
            continue;
        }
        ++depleted;
        // 枯竭之后余额就是零。
        CHECK_EQ(vein.balance, 0u);
        // 枯竭年必须落在被推演的年份里。
        CHECK(vein.depleted_year >= report.epoch);
        CHECK(vein.depleted_year < report.now);
    }
    // 至少有一条脉该在这六百年里枯竭：潮位从 110 掉到 33。
    // 实测 64x64 / seed 7 的台账里 5 条脉有 3 条余额归零。
    CHECK(depleted > 0u);
}

TEST(a_cave_heaven_is_lost_with_its_vein)
{
    // 洞天挂在一条脉上，那条脉枯了，洞天就失效（事件，不改动力学）。
    //
    // 用默认图幅：小图幅上六百年里没有脉被抽干，洞天自然也不会失效，
    // 这条测试会退化成「什么都没验」。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    std::size_t lost = 0;
    for (const auto& event : report.events)
    {
        if (event.kind != gamev2::faction::EraEventKind::cave_heaven_lost)
        {
            continue;
        }
        ++lost;
        // 洞天失效必须挂在那条真的枯了的脉上：事件的脉编号要能在报告里
        // 找到，且那条脉的枯竭年不为零。
        bool found = false;
        for (const auto& vein : report.veins)
        {
            if (vein.vein == event.vein)
            {
                found = true;
                CHECK(vein.depleted_year != 0u);
            }
        }
        CHECK(found);
    }
    // 洞天失效数不超过洞天总数——上面已逐条验过脉的对应关系，这里再钉
    // 一次数量上界，防止同一个洞天被反复记失效。
    CHECK(lost <= report.veins.size());
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败或该测试挂（公有化还没实现）。

- [x] **Step 3: 写 `DepleteSystem` 与公有化**

先写一个文件内的辅助函数——洞天要知道它那条脉的状态：

```cpp
/// 按脉编号取脉的物理状态。找不到返回 nullptr。
///
/// 洞天挂在脉上，判它失效要读那条脉的枯竭年。视图的遍历顺序不保证
/// 与脉编号一致，所以只能按编号找。
const VeinStock* find_vein_stock(gamev2::ecs::World& world,
                                 std::uint32_t vein_id)
{
    const VeinStock* found = nullptr;
    world.view<VeinId, VeinStock>().each(
        [&](VeinId& id, VeinStock& stock) {
            if (id.id == vein_id)
            {
                found = &stock;
            }
        });
    return found;
}
```

（`find_vein_stock` 的指针指向列存储里的元素，视图的临时对象销毁后
仍然有效——列存储的生命周期跟着 World 走。但**不要**把它存到视图之外
去用；本函数只在一次 `each` 之内用。）

然后写 `DepleteSystem`：

```cpp
/// 枯竭与洞天失效。
///
/// 它排在开采与越采之后：枯竭年是余额归零的那一年，判定必须在
/// 开采之后。
class DepleteSystem final : public gamev2::ecs::System
{
public:
    using ReadSet = std::tuple<const SimClock, const VeinId,
                                const VeinStock, const CaveHeavenId>;
    using WriteSet = std::tuple<VeinStock, CaveHeavenState>;

    DepleteSystem(gamev2::ecs::EntityId clock,
                  std::vector<EraEvent>& events)
        : clock_(clock), events_(events)
    {
    }

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer&) override
    {
        const auto* clock = world.get_if<SimClock>(clock_);
        if (clock == nullptr)
        {
            return;
        }
        world.view<VeinId, VeinStock>().each(
            [&](VeinId& id, VeinStock& stock) {
                if (stock.depleted_year != 0u || stock.stock != 0u)
                {
                    return;
                }
                stock.depleted_year = clock->year;
                events_.push_back(
                    EraEvent{EraEventKind::vein_depleted, clock->year, id.id,
                             Sect::unowned, Sect::unowned, 0u});
            });
        // 洞天：它挂在一条脉上，那条脉枯了，洞天就失效。
        world.view<CaveHeavenId, CaveHeavenState>().each(
            [&](CaveHeavenId& cave, CaveHeavenState& state) {
                if (!state.alive)
                {
                    return;
                }
                const auto* vein = find_vein_stock(world, cave.vein);
                if (vein == nullptr || vein->depleted_year == 0u)
                {
                    return;
                }
                state.alive = false;
                events_.push_back(EraEvent{EraEventKind::cave_heaven_lost,
                                           clock->year, cave.vein, state.owner,
                                           Sect::unowned, 0u});
            });
    }
private:
    gamev2::ecs::EntityId clock_;
    std::vector<EraEvent>& events_;
};
```

**公有化要改 `VeinOwner`，而 `VeinOwner` 的唯一写入者是
`ResolveSystem`**（设计 §3.13）。所以本任务只把 `DepleteSystem` 做完，
**公有化留给 Task 7 的 `ResolveSystem`**。

**为了让 Task 6 的测试能过**，`Simulation::collect` 在本任务里把
`VeinReport::owner` 填成初始持有者；Task 7 完成公有化后，它填真实归属。

**这一处临时状态必须在 Task 7 结束时改掉**，否则公有化根本没发生，
而 Task 6 的测试照样绿（它不断言归属）。Task 7 的
`a_depleted_vein_is_publicized` 那条测试盯的就是它。

**洞天失效只记事件，不改动力学**（设计 §3.12）：洞天是用灵石供养的
蓄电池，而灵石账本层明确不做。

- [x] **Step 4: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 12 条测试通过。

- [x] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/systems.cpp
git add src/faction/systems.hpp
git add src/faction/setup.cpp
git add tests/test_faction_economy.cpp
```

---

### Task 7: 重勘：清单生成、表决与归属变更

**Files:**
- Create: `src/faction/resolve.cpp`
- Modify: `src/faction/systems.hpp`（追加 `ResolveSystem` 声明与常量）
- Modify: `src/faction/systems.cpp`
- Modify: `src/faction/setup.cpp`（注册 `ResolveSystem`；`collect` 填真实归属）
- Test: `tests/test_faction_era.cpp`（新建）

**Interfaces:**
- Produces:
  - `class ResolveSystem` —— 写 `VeinOwner`

- [x] **Step 1: 写失败的测试**

创建 `tests/test_faction_era.cpp`：

```cpp
#include "gamev2/faction/era.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <vector>

namespace {

using gamev2::faction::EraEvent;
using gamev2::faction::EraEventKind;

TEST(the_redraft_period_is_two_hundred_years)
{
    // 设定 §6.5：脉律每二百年重勘一次。
    CHECK_EQ(gamev2::faction::kRedraftPeriod, 200u);
}

TEST(the_early_redraft_interval_is_fifty_years)
{
    // 提案与表决是大事，不能年年开。
    CHECK_EQ(gamev2::faction::kEarlyRedraftInterval, 50u);
}

TEST(a_depleted_vein_is_publicized)
{
    // 枯竭的脉不可复生（设定 §2.3：枯眼不可复生），它随即公有化：
    // 一个抽干的脉对原持有者没有价值，也不该继续占着一个名字。
    // 公有化是物理程序，不需要表决。
    //
    // 这条断言属于 Task 7：公有化要改 VeinOwner，而 VeinOwner 的唯一
    // 写入者是 ResolveSystem——它在本任务里才存在。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    std::size_t depleted = 0;
    for (const auto& vein : report.veins)
    {
        if (vein.depleted_year == 0u)
        {
            continue;
        }
        ++depleted;
        CHECK(vein.owner == gamev2::history::Sect::unowned);
    }
    CHECK(depleted > 0u);
    // 内部一致性：抽干的脉不该还挂在原主人名下。
    CHECK_EQ(fdiag.public_release_mismatch, 0u);
}

TEST(a_vein_changes_hands_at_least_once)
{
    // 涌现性验收（设计 §6.4）：当代的「脉 — 持有者」映射必须与 8300
    // 的映射不同，且变化不是代码里写死的。
    //
    // 判据是「两张表不同」，不是「太玄宗失去了某条脉」：后者会在
    // 世界变了之后仍然通过，而它其实什么都没测。
    //
    // 用默认图幅：小图幅凑不齐七宗的持有格局（见「测试图幅」一节）。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK(report.owner_changes > 0u);
    // 变化必须真的落在事件表里，不能只有计数。
    std::size_t transferred = 0;
    for (const auto& event : report.events)
    {
        if (event.kind == EraEventKind::vein_transferred)
        {
            ++transferred;
        }
    }
    CHECK(transferred > 0u);
}

TEST(the_events_are_ordered_by_year)
{
    // 事件表是史官读的东西，乱序就没人读了。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    for (std::size_t i = 1; i < report.events.size(); ++i)
    {
        CHECK(report.events[i - 1].year <= report.events[i].year);
    }
}

}  // namespace
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`kRedraftPeriod` 未声明。

- [x] **Step 3: 写重勘的清单生成**

在 `src/faction/resolve.cpp` 里实现：

```cpp
/// 一条待转移的脉。
struct Transfer
{
    std::uint32_t vein = 0;
    Sect from = Sect::unowned;
    Sect to = Sect::unowned;
    /// 转移后这条脉当年的开采量（试算值）。
    std::uint32_t take_after = 0;
    /// 转移前这条脉当年的开采量。
    std::uint32_t take_before = 0;
};
```

清单的生成（对每条脉，按脉编号升序）：

1. 现任持有者 O 若是七宗且立场为 `neutral`（青囊谷三家共管，不可被夺），
   跳过。
2. 现任持有者 O 是**无主**（散修）→ **公有池，可以转手**。公有池不是
   「谁的私产」，它没有主人，也就没有人要同意；它也不在表决里
   （没有席位）。
3. 候选 C：在立场为 `contesting` 且 C ≠ O 的派系里，按**余力**降序、
   派系编号升序取第一个。余力 = 名下脉的存量之和 − 当年开采量之和。
   余力为 0 的派系跳过。
4. 试算 C 接手 v 之后的当年开采量：
   - 合法开采 `min(eff_rate(v), quota_total(v) − extracted(v), stock(v))`
   - C 若有缺口，会对 v 越采：C 的缺口减去它新获得的合法开采，
     按 C 名下各脉（含 v）的 eff_rate 比例分摊，
     越采量 `min(份额, eff_rate(v) × kOvertakeFactor, 剩余余额)`
5. 若试算值 **小于** v 当年的实际开采量（`VeinStock::taken`）→ 列入清单。

**试算只涉及两个派系与一条脉**，不重跑全年。

- [x] **Step 4: 写表决**

表决的规则（设计 §3.11.4）：

- 权重 = 该宗在 8300 年名下脉的**总量配额之和**（`vote_weight`）。
  散修不在盟约内，权重为 0。
- 受益方（清单里得到脉的）赞成；受损方（失去脉的）反对；
  无利害方赞成当且仅当**清单生效后当年的总开采量不增加**。
- 通过门槛：赞成的权重 > 总权重的一半。

表决用整数做，**两边同乘分母**，避免除法：

```cpp
// 赞成 × kVoteThresholdDen > 总权重 × kVoteThresholdNum
// ⟺ 赞成 > 总权重 / 2。
const bool passed = approved_weight * kVoteThresholdDen
                    > total_weight * kVoteThresholdNum;
```

- [x] **Step 5: 写 `ResolveSystem`**

`ResolveSystem` 每年的工作：

1. **公有化**：库存为零的脉，归属改为「无主」（除非已经是），
   并记下公有化的年份（`VeinStock::public_year`）。
   记 `vein_publicized` 事件。
2. **重勘判定**：
   - 例行年：`(year − epoch) % kRedraftPeriod == 0`。
   - 提前年：执掌盟约者（太玄）上次提案的年份距今 ≥
     `kEarlyRedraftInterval`，且它当年有缺口。
   两者都不满足 → 本年不重勘。
3. **生成清单** → **表决** → 通过则记 `redraft_passed`，
   否决记 `redraft_rejected`（附理由）。
4. **应用转移**：把清单里每条脉的归属改为新持有者，记
   `vein_transferred` 事件。**变更自次年生效**——本年的开采已经
   按旧归属算过了。

`VeinOwner` 只有 `ResolveSystem` 写（设计 §3.13）。

**`collect` 要跟着改两处**（Task 6 留的临时状态）：

- `VeinReport::owner` 填回 `VeinOwner::owner`（Task 6 暂填
  `initial_owner`，并注明「Task 7 必须改回」）。
- `EraReport::owner_changes` **填上**：当代归属 ≠ 初始归属的脉数。
  这个字段此前**全仓无人赋值**，恒为 0——而 Task 7 的验收标准正是
  `report.owner_changes > 0`。不填，验收测试必红。

- [x] **Step 6: 收紧正派的越采条件**

正派宗门的越采必须**先走完重勘程序**：只有当年是重勘年（或执掌盟约者
当年已提过案）而程序没能满足它的缺口，它才越线（设计 §3.7）。

Task 5 里这一条是放宽的（缺口 > 0 就允许），本任务把它收紧：

```cpp
// 正派：程序已用尽才越线。
const bool procedure_used = is_redraft_year || proposed_this_year;
if (!procedure_used)
{
    return;  // 不越采
}
```

**魔修与散修不受此限**（它们不在盟约里，程序管不着它们）。

- [x] **Step 7: 改 `collect`，填真实归属**

把 Task 6 留的临时状态改掉：`Simulation::collect` 的 `VeinReport::owner`
必须填**真实的** `VeinOwner::owner`，不是初始持有者。

**这一步不做，Task 6 的公有化测试就一直是假的**（它会绿着，而公有化
根本没发生）。

- [x] **Step 8: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 全部测试通过，且 `owner_changes > 0` 那条不挂。

- [x] **Step 9: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/resolve.cpp
git add src/faction/systems.cpp
git add src/faction/systems.hpp
git add src/faction/setup.cpp
git add tests/test_faction_era.cpp
```

---

### Task 8: 当代格局的汇总与文本导出

**Files:**
- Modify: `src/faction/era.cpp`
- Test: `tests/test_faction_era.cpp`

**Interfaces:**
- Produces: `gamev2::faction::export_era(const EraReport&,
  const FactionDiagnostics&, const std::filesystem::path&)`

- [x] **Step 1: 写失败的测试**

在 `tests/test_faction_era.cpp` 追加：

```cpp
TEST(the_report_covers_every_faction_and_vein)
{
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.factions.size(),
             static_cast<std::size_t>(gamev2::history::Sect::count));
    CHECK_EQ(report.veins.size(), ledger.veins.size());
    CHECK_EQ(report.epoch, 8300u);
    CHECK_EQ(report.now, ledger.now);
}

TEST(the_era_is_exportable_as_text)
{
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "faction_era_test.txt";
    CHECK(gamev2::faction::export_era(report, fdiag, path));
    std::ifstream in(path, std::ios::binary);
    CHECK(static_cast<bool>(in));
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("当代格局") != std::string::npos);
    CHECK(text.find("太玄宗") != std::string::npos);
    CHECK(text.find("诊断") != std::string::npos);
}
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 链接失败，`export_era` 未定义。

- [x] **Step 3: 实现 `export_era`**

在 `src/faction/era.cpp` 里实现，格式仿 `history::export_ledger`：

```
当代格局
纪元 8300 —— 当代 8914（614 年）

=== 各宗格局 ===
宗门\t持有脉\t初始脉\t配额\t余额\t用度\t供给\t缺口\t开采\t越采\t未登记\t表决权重\t洞天

=== 脉级格局 ===
脉\t初始持有者\t当代持有者\t品阶\t配额\t已采\t合法开采\t余额\t枯竭年\t登记

=== 与设定参照持有表的对照 ===
宗门\t本世界（龙/主/支）\t设定参照（龙/主/支）

=== 事件 ===
年份\t事件\t脉\t主体\t对方\t数额

=== 诊断 ===
项目\t计数
```

**导出的文本是给人读的**：诊断量要一并写出（沿用台账导出的规矩），
事件要能按年份读懂（每行写清「谁做了什么」）。

**「与设定参照持有表的对照」这一段是设计 §8.10 要求的**：推演出来的当代
格局与设定 §8.1 的参照持有表**不一致**（Ruling 10），差异必须摆在台面
上，由人裁决哪个算数。参照表（设定 §8.1）：

| 宗门 | 参照持有 |
|---|---|
| 太玄宗 | 龙 1 + 主 3 |
| 丹霞谷 | 主 2 |
| 寒江剑府 | 主 1（阴脉） |
| 万兽山 | 主 1 |
| 青囊谷 | 主 1 |
| 天机阁 | 无地 |
| 九幽宫 | 主 1 |
| 无主（散修） | 支（数目随图幅） |

按**品阶构成**比，不按绝对条数比——条数随图幅变（64×64 只有 3 条支脉、
1 条主脉），构成才是可比的。段首要写一句：此对照只在九条主脉齐备的
默认图幅（256×256）上有意义。

**本任务还要把还是 0 的字段填上**：`FactionReport` 的 `vein_count` /
`initial_vein_count` / `quota_total` / `balance` / `cave_heavens`，
以及 `EraReport` 的 `total_extracted` / `total_balance`——它们此前
**无人赋值**，导出会打成 0。

- [x] **Step 4: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 全部测试通过。

- [x] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/faction/era.cpp
git add tests/test_faction_era.cpp
```

---

### Task 9: demo 接入、确定性校验与全量回归

**Files:**
- Modify: `src/demo/main.cpp`
- Create: `tests/test_faction_determinism.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `tests/test_faction_determinism.cpp` 里的三条测试

- [x] **Step 1: 写失败的测试**

创建 `tests/test_faction_determinism.cpp`：

```cpp
#include "gamev2/faction/era.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <vector>

namespace {

gamev2::world::WorldParams small_world()
{
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    return p;
}

/// 等价性测试用默认图幅。
///
/// 无主的支脉占了大半（256x256 下是 50 条里的 40 条），它们被排除在
/// 比较之外，所以「比过的脉占多大比例」不是个有用的判据；判据是
/// 「比过的脉够不够多」。
gamev2::world::WorldParams full_world()
{
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    return p;
}

TEST(the_same_seed_gives_the_same_era)
{
    // 可复算是这一刀的第一条验收标准（设计 §6.4）。
    const auto p = small_world();
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics f1;
    gamev2::faction::FactionDiagnostics f2;
    const auto a = gamev2::faction::simulate(w, ledger, f1);
    const auto b = gamev2::faction::simulate(w, ledger, f2);
    CHECK_EQ(a.veins.size(), b.veins.size());
    for (std::size_t i = 0; i < a.veins.size(); ++i)
    {
        CHECK_EQ(a.veins[i].extracted, b.veins[i].extracted);
        CHECK(a.veins[i].owner == b.veins[i].owner);
        CHECK_EQ(a.veins[i].depleted_year, b.veins[i].depleted_year);
    }
    CHECK_EQ(a.events.size(), b.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i)
    {
        CHECK(a.events[i].kind == b.events[i].kind);
        CHECK_EQ(a.events[i].year, b.events[i].year);
        CHECK_EQ(a.events[i].vein, b.events[i].vein);
    }
    CHECK_EQ(a.owner_changes, b.owner_changes);
}

TEST(the_legal_extraction_matches_the_ledger)
{
    // 与台账的等价性（设计 §6.1）：本层的**合法开采**（不含越采）
    // 必须与台账的合法开采逐位相等。
    //
    // 比较范围是设计 §6.1 的三类排除（台账越采脉 / 无主脉 / 阴脉）——
    // 这三类恰好覆盖了两套模型**结构性**不同的地方。
    //
    // 台账那侧的合法开采从事件表里取：over_quota == false 的事件
    // 按脉汇总。
    const auto p = full_world();
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.veins.size(), ledger.veins.size());

    std::vector<std::uint32_t> legal(ledger.veins.size(), 0u);
    for (const auto& event : ledger.events)
    {
        if (event.over_quota)
        {
            continue;
        }
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            if (ledger.veins[i].vein == event.vein)
            {
                legal[i] += event.amount;
            }
        }
    }

    std::size_t compared = 0;
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        const auto& vein = report.veins[i];
        const auto& entry = ledger.veins[i];
        // 比较范围：设计 §6.1 的三类排除。
        //   台账越采脉：台账的越采是一笔按年抽样的定额，压低了它后续的
        //               合法开采；
        //   无主脉    ：本层的散修用度无上限，会越过配额开采，把余额抽干；
        //   阴脉      ：台账不建模「采出即衰」，本层建模。
        //
        // **不要**再加「本层越采过的脉」与「换过手的脉」两类排除：
        // 越采只有在吃穿余额或配额时才改变合法开采（否则分毫未变），
        // 而合法开采与归属无关——谁来采都是同一个数。加了反而把比较集
        // 清空（实测 50 条脉全被排除，compared = 0）。
        //
        // 已知的分岔模式：若两套模型在一条七宗名下的脉上分岔（本层的越采
        // 吃穿了那条脉的余额），这条测试会红——**那不是 bug，是「模型
        // 真的分岔了」的信号**。
        if (entry.overmined_amount != 0u
            || entry.holder == gamev2::history::Sect::unowned
            || vein.yin)
        {
            continue;
        }
        ++compared;
        CHECK_EQ(vein.legal_extracted, legal[i]);
    }
    // 排除之后至少还剩下一批脉，否则这条测试等于没测。
    // 不用「占比」当判据：无主的支脉占了 50 条里的 40 条。
    CHECK(compared >= 5u);
}

TEST(the_tide_drives_the_supply)
{
    // 潮位是这一刀全部戏剧性的来源：8300 年是峰值 110，谷底期 33。
    // 供给缩到三成而用度不变，缺口才会出现。
    CHECK_EQ(gamev2::world::compute_spirit_tide(8300).level, 110u);
    CHECK_EQ(gamev2::world::compute_spirit_tide(8801).level, 33u);
    CHECK_EQ(gamev2::world::compute_spirit_tide(8914).level, 33u);
}

}  // namespace
```

- [x] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
```

Expected: 编译失败，`legal_extracted` 未声明（`VeinReport` 里还没有它）。

- [x] **Step 3: 补上合法开采的累计**

`VeinReport` 里已经有 `legal_extracted` 字段（Task 2 写好了）。缺的是
`ExtractSystem` 对它的写入：合法开采时
`legal_extracted += take;`，越采时不碰它。

- [x] **Step 4: 构建并运行测试**

```powershell
cmake --build build
./build/faction_tests.exe
```

Expected: 全部测试通过。

**若等价性那条不通过**：说明两套推演的原语没有真正共用——回去查
Task 1 的搬家与 Task 4 的定点口径，**不要改测试**。三处最可能出错的地方：

1. 余数的分母（`255 × 2^16` 还是 `255 × 2^32`）；
2. `take_effective` 里衰减的乘法位置；
3. 台账改用共享原语时，截断的顺序变了。

- [x] **Step 5: 接入 demo**

在 `src/demo/main.cpp` 里：

1. `#include "gamev2/faction/era.hpp"`
2. 在 `print_ledger_stats` 之后加 `print_era_stats(const EraReport&,
   const FactionDiagnostics&)`：打印当代格局的规模、逐宗的持有脉数与
   余额、格局变化次数、重勘通过与驳回次数。
3. 导出 `dir / "era.txt"`：

```cpp
    gamev2::faction::FactionDiagnostics fdiag;
    const gamev2::faction::EraReport era =
        gamev2::faction::simulate(world, ledger, fdiag);
    print_era_stats(era, fdiag);
    if (!gamev2::faction::export_era(era, fdiag, dir / "era.txt"))
    {
        std::println(stderr, "写不了 {}", (dir / "era.txt").string());
        ++g_write_failures;
    }
```

4. `CMakeLists.txt` 里 `worldgen_demo` 改链 `faction`：

```cmake
target_link_libraries(worldgen_demo PRIVATE faction)
```

5. `CMakeLists.txt` 的 `FACTION_TEST_SOURCES` 追加
   `tests/test_faction_era.cpp` 与 `tests/test_faction_determinism.cpp`。

- [x] **Step 6: 跑全量测试**

```powershell
cmake --build build
./build/ecs_tests.exe
./build/worldgen_tests.exe
./build/history_tests.exe
./build/faction_tests.exe
```

Expected: 四个套件全绿。ecs 111、worldgen 180、history 38，加上
faction 的若干条。

- [x] **Step 7: 跑一次 demo 看输出**

```powershell
./build/worldgen_demo.exe
```

Expected: 退出码 0，`agent-temp/worldgen/era.txt` 被写出。打开它看
一眼：**当代格局那一段必须读得懂**。如果读不懂，是导出格式的问题，
不是推演的问题。

- [x] **Step 8: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add tests/test_faction_determinism.cpp
git add src/demo/main.cpp
git add CMakeLists.txt
```

---

## 已知限制与风险（实施后仍然成立）

设计文档 §8 列的限制本计划全部继承。以下四条是本计划**新增**的：

1. **等价性只能比「合法开采」**。本层的越采是缺口驱动的连续行为，
   台账的越采是按年抽样的定额，两者的累计开采不可能相等。所以
   对照表能比的是「合法开采」与「当代余额」，不是总开采。
2. **两层的余数标度不同**。台账是 `255 × 2^16`，本层因为多了一个
   衰减因子是 `255 × 2^32`。各自的分子分母自洽，等价的那些脉算出来
   的整数开采量相同——Task 9 的等价性测试守的就是这一条。
3. **阴脉在两层里不是同一条脉**。台账不建模「采出即衰」，本层建模，
   所以阴脉的合法开采两边必然不等。等价性测试排除它。
4. **提前重勘的 50 年间隔是程序性常数，不是经济参数**。它只影响
   事件表里提案的条数，不影响格局的结构（设计 §8.6 同样的道理）。

---

## 后续纵切（本轮不做）

- 灵石经济：洞天的供养、丹药的消耗、派系的实际战力
- 人口：弟子规模、散修的过剩人口（设计 §8.4）
- 派系覆灭、结盟、叛出：持有格局的终点不止是「换了主人」
- 兽潮、渡劫、鬼修、魔修的实体化（灵气层阶段 18 已给出渡劫选址）
- 王朝：《燧史》的记录者
- 灵脉恢复：枯竭之外的第二种稀缺（设计 §8.1）

---

**不得执行 `git commit`。**

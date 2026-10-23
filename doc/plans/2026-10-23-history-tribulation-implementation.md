# 渡劫结算与劫损 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给时序层追加第二刀——结算 8300→8914 的渡劫，把「天地抽走的
方图百里灵气」记成台账的**第三类偏差：劫损**。

**Architecture:** 仍然只读 worldgen：候选池由 `build_ledger` 内部从
`World` 的**拷贝**派生（拨回纪元潮位、重跑域/疤/地貌/渡劫四个阶段），
取走 `tribulation_sites` 后丢弃拷贝，输入 `World` 一个字节不动。
劫损走**独立的事件表**（不并进 `events`），因此守恒恒等式 1 与 3 各少/
多一项；年内次序写死为 **① 越采 → ② 劫损 → ③ 正常开采**。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**设计文档（本计划的全部语义依据）：**
`doc/2026-10-23-history-tribulation-design.md`
（冲突时以设计文档为准，并回报差异）

**既有文档：** `doc/2026-10-21-history-layer-design.md`（第一刀：台账）

---

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 编译器固定为 clang++（CMake 缓存已配 `-DCMAKE_CXX_COMPILER=clang++`）
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- **任何一行不得超过 90 个字符**（按字符数，不是字节数；表格行放不下就
  缩写单元格内容，不要靠软换行）
- Windows 环境下通过 `pwsh` 调用终端
- **不得执行 `git commit`**，提交时机由用户决定。每个任务末尾的提交步骤
  是「向用户报告并等待提交指令」
- 任务步骤里的 `git add` 命令**是给用户参考的，不是给执行者跑的**：
  执行者一律不得执行 `git add`、`git commit`、`git stash`、`git clean`
- 输出文件（探针、变异日志、demo 导出）写入 `agent-temp/`，该目录已 gitignore
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- **新增随机流必须同时登记进 `Stream` 枚举与 `kStreamIds` 数组**；
  `static_assert(stream_ids_are_distinct())` 会拦住重复流号，但**数组长度
  字面量（18 → 19）必须手改**，漏了会编译失败
- **时序层只读 worldgen**：不得写 `World` 的任何字段，不得改 `WorldParams`，
  依赖方向只能是 `history → world`
- 台账内的全部历史量用整数或定点，推演过程中不出现浮点累加
  （`kTribulationFailChance` 只用于成败抽样的一次比较，不进累加）

构建与测试命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/history_tests.exe
./build/worldgen_tests.exe
./build/ecs_tests.exe
```

---

## 已实测的前提（写计划前量过，执行时不必重新探）

**候选池在小图幅上是空的**（扫 40 个种子，`z_levels = 3`）：

| 图幅 | 纪元池非空的种子 | 单份世界生成耗时 |
|---|---|---|
| 64×64 | **0 / 40** | 21 ms |
| 128×128 | 22 / 40 | 130 ms |
| 256×256 | 6 ~ 16 处 | 230 ms ~ 2.4 s |

**派生快照的正确性与代价**：拷贝 + 重跑域/疤/地貌/渡劫，与「按
`spirit_year = 8300` 完整重生成」**逐处一致，8/8 种子**；代价 55 ms（256²）。

**本计划的唯一劫损 fixture：128×128 / seed 11**，实测：

| 项 | 值 |
|---|---|
| 纪元候选池 | 3 处 |
| 失败的渡劫 | 2 次，年份 **8733 与 8571** |
| 被圈到的台账脉 | 3 条 |
| **劫损下界** | **≥ 30**（余额单调不增，故按当代余额算出的 30 是下界，不是上界） |
| 越采脉 | 2 条（越采差 137） |
| 口径差 | 1924 |

对照的两个**旧 fixture 必须换**：

| 测试 | 旧 fixture | 为什么必须换 |
|---|---|---|
| `ledger_never_double_counts_overmining` | 128² / seed 13 | 池 1 处但**失败 0 次**，劫损恒 0 |
| `the_structural_deviation_dominates` | 128² / seed 26 | 失败 1 次，但劫损下界是 **0**，`> 0` 可能恒假 |

seed 11 在 128² 上同时满足「有越采脉」「有非零劫损」，所以**全部要验劫损的
测试统一用它**。

---

## 文件总览

本计划创建下列文件：无（全部是修改）。

本计划修改下列文件：

| 文件 | 改动 |
|---|---|
| `include/gamev2/history/ledger.hpp` | 新类型、新字段、失败率常量与候选池函数声明 |
| `src/history/ledger.cpp` | 候选池派生、排期、覆盖、年内次序接线、第三类偏差、逐宗汇总、导出、诊断 |
| `tests/test_history_ledger.cpp` | 新增 6 条、改 7 条（见任务） |
| `include/gamev2/world/enums.hpp` | `Stream::tribulation = 18`，`kStreamIds` 扩到 19 |
| `src/demo/main.cpp` | 三类偏差并排打印、渡劫统计 |

**只读边界**：`history` 依赖 `worldgen`，反向不依赖。本计划**不修改**
`src/world/` 下任何文件，`include/gamev2/world/` 下只改 `enums.hpp` 的
流号登记表（那是它自己写明的规矩：「新增一条流必须同改」）。

**字段唯一写入者**：`World` 全部字段归 worldgen 的 18 个阶段；
`Ledger` 全部字段归 `build_ledger`。

---

## 与设计文档的一致性

设计 §10 的任务切分与本计划逐条对应，四处细化：

1. **排期与损失合成一个任务**（本计划 Task 3）。只有排期、没有扣减时，
   台账上没有任何可观察输出，单独交付不了可测的东西。
2. **变异战役单独成任务**（Task 7），好让 Task 6 只做全量回归与
   「越采仍足额认领」的复验——设计 §8.3 把那条列为本刀唯一的既有行为风险。
3. **设计 §6.2 的 #2 与 #6 由同一条测试兑现**（`a_failed_..._covered_share`）：
   它的 `balance_before_toll` 把该年已经发生的越采也扣了进去，所以「损失 ==
   当年余额 × 覆盖比」与「基数含越采不含正常开采」是同一个等式的两面，
   拆成两条只是把同一个断言写两遍。
4. **多一条设计没列的测试**（`the_tribulation_toll_rolls_up_per_sect`）：
   设计 §2.2 加了 `SectLedger::tribulation_loss`，不测它就等于加了个没人
   看的字段。

---

## 恒等式与配额的三处改写（本计划的语义核心）

```
恒等式 1（逐脉终态）
  旧：balance == surveyed_total − extracted
  新：balance == surveyed_total − extracted − tribulation_loss

恒等式 3（reserve 与 balance 的差）
  旧：reserve − balance == normal
  新：reserve − balance == normal + tribulation_loss

配额余量（防无符号下溢，设计 §3.4）
  旧：quota_left = surveyed_total − extracted
  新：quota_left = surveyed_total − extracted − tribulation_loss
```

第三处不是口径调整：劫损把余额扣走之后，开采若仍按原配额扣满，
`balance` 会减成负数并回绕。`tribulation_loss = 0` 时三处都与旧式逐字相同。

---

### Task 1: 类型骨架、随机流与契约红态

**Files:**
- Modify: `include/gamev2/history/ledger.hpp`
- Modify: `include/gamev2/world/enums.hpp`
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: 既有的 `Ledger` / `QuotaEntry` / `SectLedger` / `LedgerDiagnostics`
- Produces（后续任务依赖，签名必须逐字一致）:
  - `struct TribulationEvent { std::uint32_t vein, year, amount, site; }`
  - `QuotaEntry::tribulation_loss` (`std::uint32_t`)
  - `SectLedger::tribulation_loss` (`std::int64_t`)
  - `Ledger::tribulation_events` (`std::vector<TribulationEvent>`)
  - `Ledger::tribulation_deviation` (`std::int64_t`)
  - `Ledger::tribulation_failed` (`std::uint32_t`)
  - `LedgerDiagnostics::tribulation_no_vein` (`std::uint32_t`)
  - `inline constexpr double kTribulationFailChance = 0.3;`
  - `gamev2::world::Stream::tribulation = 18`

- [ ] **Step 1: 改写受影响的既有测试（此刻编译不过，这就是红态）**

打开 `tests/test_history_ledger.cpp`，做下面六处改动。

**1a. 恒等式 1** —— `final_balance_matches_the_ledger`（约 910 行）。
把 fixture 的图幅从 64 改 128，并改写等式、加守卫：

```cpp
TEST(final_balance_matches_the_ledger)
{
    WorldParams p;
    p.width = 128;                 // 64² 的纪元渡劫场 40/40 全空，验不到劫损
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    // 守卫：这个 fixture 必须真的有劫损，否则新公式在它身上恒 0、空跑。
    CHECK(!ledger.tribulation_events.empty());
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance,
                 entry.surveyed_total - entry.extracted
                     - entry.tribulation_loss);
    }
}
```

**1b. 逐年守恒** —— `conservation_holds_at_every_year`（约 857 行）。
图幅 64 → 128（`seed = 11` 不变），并把劫损并入时间序。替换掉
`std::map<std::uint32_t, std::uint32_t> running;` 之后、
`CHECK(!ledger.events.empty());` 之前的整段回放逻辑：

```cpp
    // 守卫：这个 fixture 必须真的有劫损。
    CHECK(!ledger.tribulation_events.empty());

    // 逐年内次序与实现一致：① 越采 → ② 劫损 → ③ 正常开采；
    // 跨年按年份升序。三段过滤而不是按事件表下标交错：两张表各自按年份
    // 递增，但谁在前谁在后只有按年重排才看得出来。
    for (std::uint32_t year = ledger.epoch; year < ledger.now; ++year)
    {
        for (const auto& event : ledger.events)
        {
            if (event.year == year && event.over_quota)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
        for (const auto& event : ledger.tribulation_events)
        {
            if (event.year == year)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
        for (const auto& event : ledger.events)
        {
            if (event.year == year && !event.over_quota)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
    }
```

保留它原有的 `running` 初始化、`year_went_backwards` 检查与
`CHECK_EQ(running[entry.vein], entry.balance)` 收尾；`year_went_backwards`
改为遍历 `ledger.events` 单独判（劫损的年份升序由新测试 #4 负责）。

**1c. 恒等式 3** —— `ledger_never_double_counts_overmining`（约 1053 行）。
`p.seed = 13;` 改成 `p.seed = 11;`（13 那个池子失败 0 次），并在
`Ledger ledger = ...;` 之后加守卫：

```cpp
    // 守卫：seed 13 的纪元池 1 处但失败 0 次，换 seed 11 才有劫损可验。
    CHECK(!ledger.tribulation_events.empty());
```

并把等号右端改掉（`gap` 与 `normal` 声明保持原样）：

```cpp
        const std::int64_t gap =
            static_cast<std::int64_t>(vein.reserve) - entry.balance;
        CHECK(gap >= 0);
        // 等号那一半：越采脉的基数是反推出来的，reserve = 基数 − 越采量，
        // 所以 reserve − balance 恰好等于「不含越采的开采量 + 劫损」。
        CHECK_EQ(gap,
                 static_cast<std::int64_t>(normal)
                     + static_cast<std::int64_t>(entry.tribulation_loss));
```

**1d. 确定性** —— `the_ledger_is_deterministic`（约 976 行）。
`p.width/height = 64` 改 128、`p.seed = 2026` 改 11，加守卫，并补新字段比较：

```cpp
    const Ledger la = gamev2::history::build_ledger(*a);
    const Ledger lb = gamev2::history::build_ledger(*b);
    // 守卫：确定性比较要真的比较到渡劫事件，否则新字段没被测到。
    CHECK(!la.tribulation_events.empty());
    CHECK_EQ(la.tribulation_events.size(), lb.tribulation_events.size());
    for (std::size_t i = 0; i < la.tribulation_events.size(); ++i)
    {
        CHECK_EQ(la.tribulation_events[i].vein,
                 lb.tribulation_events[i].vein);
        CHECK_EQ(la.tribulation_events[i].year,
                 lb.tribulation_events[i].year);
        CHECK_EQ(la.tribulation_events[i].amount,
                 lb.tribulation_events[i].amount);
        CHECK_EQ(la.tribulation_events[i].site,
                 lb.tribulation_events[i].site);
    }
    CHECK_EQ(la.tribulation_failed, lb.tribulation_failed);
    CHECK_EQ(la.tribulation_deviation, lb.tribulation_deviation);
```

**1e. 超限路径** —— `a_span_beyond_the_limit_is_refused`（约 1499 行）。
图幅与种子不动，补两条（放在既有的 `CHECK_EQ(diagnostics.span_too_large, 1u);`
之后）：

```cpp
    // 没有推演就没有渡劫：与事件表、余额同一个口径。
    CHECK(ledger.tribulation_events.empty());
    CHECK_EQ(ledger.tribulation_deviation, 0);
    CHECK_EQ(ledger.tribulation_failed, 0u);
```

**1f. 导出行数** —— `the_ledger_is_exportable_as_text`（约 1359 行）。
图幅 64 → 128（`seed = 11` 不变），把 `rows` 的算法与上界改掉：

```cpp
    const std::size_t rows = ledger.veins.size() + ledger.events.size()
                           + ledger.tribulation_events.size()
                           + overmine_rows + depleted_rows + disputed_rows;
    CHECK(lines > rows);
    // 固定表头 = 原有的 24 行 + 越采案清单 3 行 + 枯竭脉清单 3 行
    // + 枯竭争议 3 行 + 渡劫事件清单 3 行 + 诊断 12 行（标记 + 表头 + 9 项）
    // + 偏差分解的劫损 1 行 = 49 行，余量 1 行留给导出格式的小变动。
    //
    // 越采案、枯竭脉、枯竭争议与渡劫清单是另外四段，它们把事件与脉各抄了
    // 一遍，所以行数里要把这几份重复算进去——只按「脉 + 事件」算，边界会
    // 差出几十行。
    CHECK(lines < rows + 50u);
```

- [ ] **Step 2: 构建，确认它按预期编译失败**

```powershell
cmake --build build
```

Expected: **编译失败**，报 `no member named 'tribulation_loss' in ...`
与 `'tribulation_events' is not a member of '...'`。
（这一步不是出错——测试先于类型，这正是 TDD 的红态。）

- [ ] **Step 3: 在 `ledger.hpp` 里加类型、字段、常量**

在 `struct QuotaEntry` 的 `balance` 字段**之后**插入：

```cpp
    /// 8300→当代被劫云抽走、没有回流的部分。渡劫成功不入账（净 0）。
    ///
    /// 它不是开采：没有配额、没有持有者。所以 balance = surveyed_total
    /// − extracted − 它，而 extracted 这一项不含它——两者分表正是为此。
    std::uint32_t tribulation_loss = 0;
```

在 `struct ExtractionEvent` 之后、`struct SectLedger` 之前插入：

```cpp
/// 一次入账的渡劫失败。成功不入账：劫云溃散回流当地，净 0。
///
/// 与 ExtractionEvent 分表：那是「人采走了」，这是「天地抽走了」，
/// 并成一张表会让「事件总和 == extracted」那条断言语义错乱。
struct TribulationEvent
{
    std::uint32_t vein = 0;
    std::uint32_t year = 0;
    std::uint32_t amount = 0;
    /// 劫心格。读者要在图上定位它，脉编号做不到这件事。
    std::uint32_t site = 0;
};
```

在 `struct SectLedger` 的 `overmined_amount` 之后插入：

```cpp
    /// 名下脉被劫云抽走的合计。
    std::int64_t tribulation_loss = 0;
```

在 `struct Ledger` 的 `events` 之后插入：

```cpp
    /// 失败并入账的渡劫。空圈失败只计数不入账，所以它比
    /// tribulation_failed 短或相等。
    std::vector<TribulationEvent> tribulation_events;
```

在 `overmining_deviation` 之后插入：

```cpp
    /// 第三类偏差：劫损合计（Σ tribulation_loss）。与越采差同形——
    /// 都是逐脉字段的 Σ，都是「没人该负责、但账上确实少了」的那一类。
    std::int64_t tribulation_deviation = 0;
    /// 失败的渡劫次数。空圈失败不入账，只看事件数会低估试了几次。
    std::uint32_t tribulation_failed = 0;
```

在 `struct LedgerDiagnostics` 的 `span_too_large` 之后插入：

```cpp
    /// 劫心圈内一条台账脉都没有：事件无效果，只计数。
    std::uint32_t tribulation_no_vein = 0;
```

在 `[[nodiscard]] Ledger build_ledger(...)` 的两个声明**之前**插入：

```cpp
/// 渡劫失败的概率。设定 §5.2 没给数，与 kOverminedChance 同样是经验值。
///
/// 取 0.3 而不是 0.15：候选池实测 6~25 处，0.15 时 6 处的池子有约 38%
/// 概率一次都不失败，第三类偏差常态为 0，招牌断言就写不出来。
inline constexpr double kTribulationFailChance = 0.3;
```

- [ ] **Step 4: 在 `enums.hpp` 登记新流**

把 `Stream::ledger = 17,` 之后补一行：

```cpp
    /// 时序层的渡劫年份与成败抽样。
    tribulation = 18,
```

把 `kStreamIds` 的类型从 `std::array<std::uint32_t, 18>` 改成
`std::array<std::uint32_t, 19>`，并在末尾 `Stream::ledger` 那一行后追加：

```cpp
    static_cast<std::uint32_t>(Stream::tribulation),
```

**数组长度的 18 必须手改**：`stream_ids_are_distinct()` 用的是
`kStreamIds.size()`，改了枚举不改长度会编译失败（少登记一条）。

- [ ] **Step 5: 构建并运行，确认是「那五条守卫红、其余全绿」**

```powershell
cmake --build build
./build/history_tests.exe
./build/worldgen_tests.exe
./build/ecs_tests.exe
```

Expected: `worldgen_tests` 181/0、`ecs_tests` 111/111 **全绿**；
`history_tests` 里 `final_balance_matches_the_ledger`、
`conservation_holds_at_every_year`、`ledger_never_double_counts_overmining`、
`the_ledger_is_deterministic`、`the_ledger_is_exportable_as_text`
**这五条因 `!ledger.tribulation_events.empty()` 守卫而失败**，其余全绿。

**这个红态是有意的**：字段立好了、契约改了，扣减要到 Task 3 才有内容。
**除这五条守卫以外还有任何红，就是出了问题，不要往下走。**

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add include/gamev2/history/ledger.hpp
git add include/gamev2/world/enums.hpp
git add tests/test_history_ledger.cpp
```

---

### Task 2: 纪元候选池派生

**Files:**
- Modify: `include/gamev2/history/ledger.hpp`
- Modify: `src/history/ledger.cpp`
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: Task 1 的 `kTribulationFailChance`（本任务不用，但同处一个头）
- Produces:
  - `std::vector<gamev2::world::TribulationSite>
    epoch_tribulation_sites(const gamev2::world::World& world);`
    （Task 3 在 `build_ledger` 里调它）

- [ ] **Step 1: 写失败的测试**

追加到 `tests/test_history_ledger.cpp` 文件末尾（`namespace` 收尾之前）：

```cpp
TEST(the_epoch_sites_are_derived_exactly)
{
    // 候选池不是「重新生成一份世界」，而是「拷贝 + 只重跑潮相关层」。
    // 两者的相等是唯一挡住「worldgen 在域/疤/地貌之间插了阶段」的东西：
    // 插了阶段，派生出来的池子就会静默过时，而台账不会报任何错。
    //
    // 64² 的池子 40/40 全空，两边都空的「逐处一致」是空比较，所以必须
    // 用 128²；seed 11 实测池子 3 处。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto derived = gamev2::history::epoch_tribulation_sites(
        *generate_world(p));
    CHECK(!derived.empty());

    WorldParams pe = p;
    pe.spirit_year = 8300;
    const auto full = generate_world(pe);
    CHECK(full.has_value());
    if (!full)
    {
        return;
    }
    CHECK_EQ(derived.size(), full->tribulation_sites.size());
    for (std::size_t i = 0; i < derived.size(); ++i)
    {
        CHECK_EQ(derived[i].cell, full->tribulation_sites[i].cell);
        CHECK_EQ(derived[i].radius, full->tribulation_sites[i].radius);
        CHECK_EQ(derived[i].vein, full->tribulation_sites[i].vein);
    }
}
```

**注意**：`*generate_world(p)` 会在 `generate_world` 返回 `nullopt` 时解引用
空的 `expected`。上一行紧接着 `CHECK(!derived.empty())`，若生成失败会先在这
里红。为把失败点说得更清楚，写成：

```cpp
    const auto host = generate_world(p);
    CHECK(host.has_value());
    if (!host)
    {
        return;
    }
    const auto derived =
        gamev2::history::epoch_tribulation_sites(*host);
    CHECK(!derived.empty());
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: **编译失败**，报 `'epoch_tribulation_sites' is not a member of
'gamev2::history'`。

- [ ] **Step 3: 声明并实现派生**

在 `ledger.hpp` 的 `kTribulationFailChance` 之后、`build_ledger` 声明之前
追加：

```cpp
/// 纪元年（8300）的渡劫场候选池：拷贝世界、把潮位拨回纪元、重跑潮相关层。
///
/// 灵脉不随潮位变（实测两份世界逐条相同），所以只重跑域、疤、地貌、渡劫
/// 四个阶段就够：55 ms，而完整重生成要 230 ms ~ 2.4 s，两者逐处一致。
///
/// 拷贝而不是改写输入：build_ledger 的只读边界是相对调用方那份世界说的。
[[nodiscard]] std::vector<gamev2::world::TribulationSite>
epoch_tribulation_sites(const gamev2::world::World& world);
```

在 `ledger.cpp` 里：

1. 顶部 include 区加一行（放在 `gamev2/world/rng.hpp` 之前，保持字母序）：

```cpp
#include "gamev2/world/generate.hpp"
```

2. 匿名命名空间里 `build_ledger` 的两个定义**之前**（即 `kQuotaEpoch`
   那段常量之后的空行处）加：

```cpp
/// 纪元年的候选池。推演用它当渡劫的候选，取完就丢，不留副本。
std::vector<gamev2::world::TribulationSite> derive_epoch_sites(
    const gamev2::world::World& world)
{
    // 拷贝整个世界：下面四个阶段要写 domain_grade、scars、land_type、
    // tribulation_sites，写在输入世界上就破了只读边界。
    gamev2::world::World derived = world;
    derived.spirit_tide = gamev2::world::compute_spirit_tide(kQuotaEpoch);
    gamev2::world::make_spirit_domains(derived);
    gamev2::world::make_historical_scars(derived);
    gamev2::world::make_land_types(derived);
    gamev2::world::make_tribulation_sites(derived);
    return derived.tribulation_sites;
}
```

3. 匿名命名空间**之外**（与 `build_ledger` 同级，即第一个
   `}  // namespace` 之后）加：

```cpp
std::vector<gamev2::world::TribulationSite>
epoch_tribulation_sites(const gamev2::world::World& world)
{
    return derive_epoch_sites(world);
}
```

- [ ] **Step 4: 运行测试确认通过**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: `the_epoch_sites_are_derived_exactly` **PASS**，其余与 Task 1
结束时一致（那五条守卫仍然红——本任务不碰扣减）。

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add include/gamev2/history/ledger.hpp
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 3: 排期、损失与年内次序

**Files:**
- Modify: `src/history/ledger.cpp`
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: Task 1 的字段与 `kTribulationFailChance`；Task 2 的
  `derive_epoch_sites`（文件内匿名命名空间函数）
- Produces（Task 4/5 依赖）:
  - `ledger.tribulation_events` 按 **(year, site)** 升序
  - `ledger.tribulation_failed` = 池中失败的候选数
  - `entry.tribulation_loss` 逐脉累计
  - 每笔 `TribulationEvent::amount == 上年末余额 − 该年越采) × covered / total`

- [ ] **Step 1: 写失败的测试**

追加到 `tests/test_history_ledger.cpp`（下面的 `coverage_of` 返回
`std::pair`，所以先把 include 补一行：在既有的 `#include <string>` 之后
加 `#include <utility>`）：

```cpp
namespace {

/// 某条脉在某处劫云圈内覆盖的节点数占它总节点数的量。照实现的公式
/// 从 World 反算——基数写在账里，但基数由谁提供要靠测试自己推，否则
/// 改了覆盖口径两边一起漂，断言永远绿。
std::pair<std::uint32_t, std::uint32_t> coverage_of(
    const World& w, std::uint32_t vein_index, std::uint32_t site_cell,
    std::uint32_t radius)
{
    const gamev2::world::SpiritVein& vein = w.veins[vein_index];
    const auto& nodes = w.ley_channels[vein.channel].nodes;
    const std::uint32_t width = w.params().width;
    const std::int64_t sx = static_cast<std::int64_t>(site_cell % width);
    const std::int64_t sy = static_cast<std::int64_t>(site_cell / width);
    const std::int64_t r2 = static_cast<std::int64_t>(radius)
                            * static_cast<std::int64_t>(radius);
    std::uint32_t inside = 0;
    for (std::uint32_t j = vein.first_node; j <= vein.last_node; ++j)
    {
        const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
        const std::int64_t dx = static_cast<std::int64_t>(cell % width) - sx;
        const std::int64_t dy = static_cast<std::int64_t>(cell / width) - sy;
        if (dx * dx + dy * dy <= r2)
        {
            ++inside;
        }
    }
    const std::uint32_t total = vein.last_node - vein.first_node + 1u;
    return {inside, total};
}

/// 找某年某脉在越采之前、上一年之后的余额——劫损的基数。
/// 只用两张事件表就能复算，不需要台账公开逐年余额。
std::uint32_t balance_before_toll(const Ledger& ledger, std::uint32_t vein,
                                  std::uint32_t year)
{
    std::uint32_t balance = 0;
    bool started = false;
    for (const auto& entry : ledger.veins)
    {
        if (entry.vein == vein)
        {
            balance = entry.surveyed_total;
            started = true;
            break;
        }
    }
    if (!started)
    {
        return 0;
    }
    for (std::uint32_t y = ledger.epoch; y < year; ++y)
    {
        for (const auto& e : ledger.events)
        {
            if (e.vein == vein && e.year == y)
            {
                balance -= e.amount;
            }
        }
        for (const auto& e : ledger.tribulation_events)
        {
            if (e.vein == vein && e.year == y)
            {
                balance -= e.amount;
            }
        }
    }
    for (const auto& e : ledger.events)
    {
        if (e.vein == vein && e.year == year && e.over_quota)
        {
            balance -= e.amount;
        }
    }
    return balance;
}

}  // namespace
```

```cpp
TEST(a_failed_tribulation_takes_the_covered_share)
{
    // 损失 = 当年余额 × 圈内节点占比，先乘后除。覆盖比 1.00 时整条脉
    // 被抽干，这正是设定 5.2「抽干方图百里」的几何读法。
    //
    // 这一条同时兑现设计 §6.2 的 #2 与 #6：`balance_before_toll` 把该年
    // **已经发生的越采**也扣掉，所以基数一旦不含越采（次序挪了）或含了
    // 当年正常开采，下面的等式立刻红。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());

    const auto pool =
        gamev2::history::epoch_tribulation_sites(w);
    std::size_t checked = 0;
    for (const auto& event : ledger.tribulation_events)
    {
        // 找到这笔事件对应的候选池条目，拿它的劫心与半径。
        std::uint32_t radius = 0;
        bool found = false;
        for (const auto& site : pool)
        {
            if (site.cell == event.site)
            {
                radius = site.radius;
                found = true;
                break;
            }
        }
        CHECK(found);
        if (!found)
        {
            continue;
        }
        const auto [covered, total] =
            coverage_of(w, event.vein, event.site, radius);
        CHECK(covered > 0u);
        // 基数：该年年初余额，扣掉该年已经在它之前发生的越采。
        const std::uint32_t base =
            balance_before_toll(ledger, event.vein, event.year);
        ++checked;
        CHECK_EQ(event.amount, base * covered / total);
    }
    CHECK(checked > 0);
}

TEST(tribulation_loss_never_exceeds_the_balance)
{
    // 无符号恒等式的前提：每一笔扣减都 ≤ 扣减时刻的余额。劫损取余额的
    // 覆盖比例，比例 ≤ 1 所以天然成立——这条断言守的是「天然」没被改坏。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance,
                 entry.surveyed_total - entry.extracted
                     - entry.tribulation_loss);
        CHECK(entry.tribulation_loss <= entry.surveyed_total);
    }
}

TEST(the_tribulation_events_land_inside_the_window)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());
    // 入账的是失败里落在有脉圈内的那部分，所以失败数只能多、不能少。
    CHECK(ledger.tribulation_failed >= ledger.tribulation_events.size());

    std::uint32_t last_year = ledger.epoch;
    bool first = true;
    for (const auto& event : ledger.tribulation_events)
    {
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        CHECK(event.amount > 0u);
        CHECK(event.vein < ledger.veins.size());
        if (!first)
        {
            CHECK(event.year >= last_year);
        }
        first = false;
        last_year = event.year;
    }
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: 编译通过（字段都在），但
`a_failed_tribulation_takes_the_covered_share`、
`tribulation_loss_never_exceeds_the_balance`、
`the_tribulation_events_land_inside_the_window`
与 Task 1 留下的五条守卫**全部 FAIL**（事件表还是空的）。

- [ ] **Step 3: 实现候选池、排期、覆盖与扣减**

在 `ledger.cpp` 匿名命名空间里（`derive_epoch_sites` 之后）加三个类型与
两个函数：

```cpp
/// 一处渡劫在一条台账脉上的覆盖：圈内节点数与该脉节点总数。
///
/// 先把覆盖算出来放着：它只依赖几何与脉的段范围，与推演的年份无关，
/// 两遍推演都要用同一份，算两遍是白费。
struct Coverage
{
    std::uint32_t site = 0;    ///< 候选池下标
    std::uint32_t vein = 0;    ///< 台账下标（与脉编号一一对应）
    std::uint32_t covered = 0;
    std::uint32_t total = 0;
};

/// 一次待结算的渡劫。amount 在推演里算，因为基数是执行那一刻的余额。
struct TollPlan
{
    std::uint32_t site = 0;    ///< 候选池下标
    std::uint32_t year = 0;
};

/// 逐候选的覆盖。圈内一条台账脉都没有时，候选不产生任何事件，
/// 但要计数——「抽了个空圈」是读者该看得见的退化。
void build_coverages(const gamev2::world::World& world,
                     const Ledger& ledger,
                     const std::vector<gamev2::world::TribulationSite>& pool,
                     std::vector<Coverage>& out,
                     LedgerDiagnostics& diagnostics)
{
    const std::uint32_t width = world.params().width;
    for (std::size_t s = 0; s < pool.size(); ++s)
    {
        const gamev2::world::TribulationSite& site = pool[s];
        const std::int64_t sx = static_cast<std::int64_t>(site.cell % width);
        const std::int64_t sy = static_cast<std::int64_t>(site.cell / width);
        const std::int64_t r2 = static_cast<std::int64_t>(site.radius)
                                * static_cast<std::int64_t>(site.radius);
        std::uint32_t touched = 0;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            const QuotaEntry& entry = ledger.veins[i];
            const auto& vein = world.veins[entry.vein];
            const auto& nodes = world.ley_channels[vein.channel].nodes;
            std::uint32_t inside = 0;
            for (std::uint32_t j = vein.first_node;
                 j <= vein.last_node; ++j)
            {
                const std::uint32_t cell = world.ley_nodes[nodes[j]].cell;
                const std::int64_t dx =
                    static_cast<std::int64_t>(cell % width) - sx;
                const std::int64_t dy =
                    static_cast<std::int64_t>(cell / width) - sy;
                if (dx * dx + dy * dy <= r2)
                {
                    ++inside;
                }
            }
            if (inside == 0u)
            {
                continue;
            }
            ++touched;
            Coverage c;
            c.site = static_cast<std::uint32_t>(s);
            c.vein = static_cast<std::uint32_t>(i);
            c.covered = inside;
            c.total = vein.last_node - vein.first_node + 1u;
            out.push_back(c);
        }
        if (touched == 0u)
        {
            ++diagnostics.tribulation_no_vein;
        }
    }
}

/// 排期：每处候选至多一次，年份在 [epoch, now) 内均匀抽，成败按
/// kTribulationFailChance 判。成功不入账（净 0），只计失败。
///
/// 随机流独立成一条（Stream::tribulation）：混进 Stream::ledger 的话，
/// 调渡劫参数会连带扰动越采年，把既有 fixture 全部推翻。
///
/// 排序必须拿到候选池：同年两笔的次序按劫心格定，而格号只有池子知道。
std::vector<TollPlan> sample_tolls(
    const std::vector<gamev2::world::TribulationSite>& pool,
    std::uint32_t years, std::uint64_t seed, Ledger& ledger)
{
    const std::uint64_t trib_seed = gamev2::world::stage_seed(
        seed,
        static_cast<std::uint32_t>(gamev2::world::Stream::tribulation));
    std::vector<TollPlan> plans;
    plans.reserve(pool.size());
    for (std::size_t s = 0; s < pool.size(); ++s)
    {
        gamev2::world::Pcg32 rng(
            gamev2::world::stage_seed(trib_seed,
                                      static_cast<std::uint32_t>(s)));
        const std::uint32_t year = kQuotaEpoch + rng.below(years);
        if (rng.unit() >= kTribulationFailChance)
        {
            continue;
        }
        TollPlan plan;
        plan.site = static_cast<std::uint32_t>(s);
        plan.year = year;
        plans.push_back(plan);
        ++ledger.tribulation_failed;
    }
    std::sort(plans.begin(), plans.end(),
              [&pool](const TollPlan& a, const TollPlan& b) {
                  if (a.year != b.year)
                  {
                      return a.year < b.year;
                  }
                  return pool[a.site].cell < pool[b.site].cell;
              });
    return plans;
}
```

**在 `build_ledger` 里接线。** 位置：`kNum == 0` 的 `flow_conversion_collapsed`
分支**之后**、`struct VeinState` **之前**——排期要在所有闸门之后（不推演就
不派生，白付 55 ms），且要在两遍推演之前（两遍必须用同一份排期）。

```cpp
    // 纪元候选池与排期。放在所有闸门之后：now <= epoch、跨度超限、换算
    // 塌缩都不推演，也就没有可结算的渡劫。
    const std::vector<gamev2::world::TribulationSite> pool =
        derive_epoch_sites(world);
    std::vector<Coverage> coverages;
    build_coverages(world, ledger, pool, coverages, diagnostics);
    const std::vector<TollPlan> tolls =
        sample_tolls(pool, years, world.params().seed, ledger);
```

**在 `advance` 内接三处改动**（这是本任务的核心）：

改动一 —— 初始化块里清两个新东西。把 `ledger.events.clear();` 换成：

```cpp
        ledger.events.clear();
        ledger.tribulation_events.clear();
```

并把同块的逐脉初始化加上劫损（保持其余两行原样）：

```cpp
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            ledger.veins[i].extracted = 0;
            ledger.veins[i].tribulation_loss = 0;
            ledger.veins[i].balance = ledger.veins[i].surveyed_total;
            states[i].drain_year = 0;
        }
```

改动二 —— 越采那一段保持**一字不动**，在它闭合之后、
`const std::uint64_t num =` **之前**插入 ② 劫损：

```cpp
                // ② 劫损：基数是越采之后、正常开采之前的余额。
                // 只动 balance 与 tribulation_loss，不动 extracted——
                // extracted 的语义是「人采走了多少」，天地抽走的不算。
                for (const TollPlan& plan : tolls)
                {
                    if (plan.year != year)
                    {
                        continue;
                    }
                    for (const Coverage& cov : coverages)
                    {
                        if (cov.site != plan.site || cov.vein != i)
                        {
                            continue;
                        }
                        const std::uint32_t loss =
                            entry.balance * cov.covered / cov.total;
                        if (loss == 0u)
                        {
                            continue;
                        }
                        entry.tribulation_loss += loss;
                        entry.balance = entry.surveyed_total
                                        - entry.extracted
                                        - entry.tribulation_loss;
                        TribulationEvent event;
                        event.vein = entry.vein;
                        event.year = plan.year;
                        event.amount = loss;
                        event.site = pool[plan.site].cell;
                        ledger.tribulation_events.push_back(event);
                    }
                }
```

**注意 `tolls` / `coverages` / `pool` 要能被 `advance` 这个 lambda 看到**
——它们声明在 lambda 之前，按值/引用捕获即可；`advance` 已经以
`[&]` 捕获，无需改动捕获列表。

改动三 —— 两处 `entry.balance = entry.surveyed_total - entry.extracted;`
改成：

```cpp
                        entry.balance = entry.surveyed_total
                                        - entry.extracted
                                        - entry.tribulation_loss;
```

（一处在越采块里，一处在正常开采 `entry.extracted += take;` 之后。）

改动四 —— 正常开采的配额余量减掉劫损。把：

```cpp
                const std::uint32_t quota_left =
                    entry.surveyed_total - entry.extracted;
```

改成：

```cpp
                // 配额余量要减掉劫损：天地抽走的那部分人也采不到了。
                // 不减的话，劫损之后那一年仍按原配额扣满，balance 会减成
                // 负数并回绕成巨大正数（无符号），整本账静默错乱。
                const std::uint32_t quota_left =
                    entry.surveyed_total - entry.extracted
                    - entry.tribulation_loss;
```

- [ ] **Step 4: 构建并运行测试确认通过**

```powershell
cmake --build build
./build/history_tests.exe
./build/worldgen_tests.exe
./build/ecs_tests.exe
```

Expected: `history_tests` **全绿**（Task 1 留下的五条守卫此刻应转绿），
`worldgen_tests` 181/0、`ecs_tests` 111/111 不变。

**如果 `ledger_never_double_counts_overmining` 的等号失败**，那是设计
§8.3 的风险提前发作（劫损把越采的可行窗口挤没了，越采没被足额认领）。
**不要改断言去迁就它**，也不要在本任务里调 fixture——记下来，留到 Task 6
按下面的决策处理，先报告。

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 4: 第三类偏差与逐宗汇总

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `include/gamev2/history/ledger.hpp`（只改 `deviation_total` 的
  `///` 注释——它解释的是 why，加上劫损之后不改就成了误导）
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: Task 3 的 `entry.tribulation_loss`
- Produces: `ledger.tribulation_deviation`、`sect.tribulation_loss`
  （Task 5 的导出与 demo 读它们）

- [ ] **Step 1: 写失败的测试**

在 `tests/test_history_ledger.cpp` 里把 `the_structural_deviation_dominates`
的 fixture 与断言改掉（`p.seed = 26;` → `p.seed = 11;`，并加第三类）：

```cpp
TEST(the_structural_deviation_dominates)
{
    // 招牌断言：8300 是峰值年、8914 是谷底年，口径差必然压过另两类。
    // 潮位公式若改了，这条会先变红——那正是它存在的意义。
    //
    // fixture 选 128x128/seed 11：实测纪元池 3 处、失败 2 次（8733、
    // 8571），劫损非零；seed 26 那个失败虽然存在，但按当代余额算出的
    // 劫损下界是 0，第三类会恒假。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.structural_deviation > 0);
    // 前提：这个 fixture 真的有越采、有劫损。少了任何一个，下面的比较
    // 就成了 0 与正数的比较，等于没测。
    CHECK(ledger.overmining_deviation > 0);
    CHECK(ledger.tribulation_deviation > 0);
    CHECK(ledger.structural_deviation >= ledger.overmining_deviation);
    CHECK(ledger.structural_deviation >= ledger.tribulation_deviation);
}
```

再追加一条逐宗汇总的测试：

```cpp
TEST(the_tribulation_toll_rolls_up_per_sect)
{
    // 劫损要能指名到宗门：「太玄宗的地被天地抽走了多少」才是配额之争里
    // 有人会拿来说话的数，只有一个全账总数就没法归因。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.tribulation_deviation > 0);

    std::int64_t sum = 0;
    for (const auto& sect : ledger.sects)
    {
        sum += sect.tribulation_loss;
        CHECK(sect.tribulation_loss >= 0);
    }
    // 宗级合计必须逐项加得回全账口径，否则归因就是假的。
    CHECK_EQ(sum, ledger.tribulation_deviation);
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: `the_tribulation_toll_rolls_up_per_sect` 编译失败
（`SectLedger` 有字段了但汇总没算，`sum` 恒 0 而 `tribulation_deviation`
也恒 0 → 断言 `> 0` 失败）；`the_structural_deviation_dominates` 的
`CHECK(ledger.tribulation_deviation > 0)` 失败（还没赋值）。

- [ ] **Step 3: 实现第三类偏差与逐宗汇总**

在 `compute_deviations` 里，把

```cpp
    ledger.structural_deviation = 0;
    ledger.overmining_deviation = 0;
```

改成

```cpp
    ledger.structural_deviation = 0;
    ledger.overmining_deviation = 0;
    ledger.tribulation_deviation = 0;
```

**并在 `if (level_epoch == 0u) { return; }` 的 `return` 之前**加上劫损——
口径差与流速差都要潮位基准，劫损不用，不能跟着它一起提前返回：

```cpp
    if (level_epoch == 0u)
    {
        // 潮位基准为 0：口径差与流速差无从折算，劫损不用潮位，仍要算。
        for (const auto& entry : ledger.veins)
        {
            ledger.tribulation_deviation += entry.tribulation_loss;
        }
        return;
    }
```

并在逐脉循环里 `ledger.overmining_deviation += entry.overmined_amount;`
之后追加一行：

```cpp
        ledger.tribulation_deviation += entry.tribulation_loss;
```

在 `aggregate_sects` 的逐脉循环里 `sect.overmined_amount += ...;`
之后追加：

```cpp
        sect.tribulation_loss += entry.tribulation_loss;
```

**并且把 `deviation_total` 的注释改准**（它现在是两部分之和）。
在 `sect.deviation_total = ...` 那一段的注释处写：

```cpp
        // deviation_total = quota_total − current_total，此刻等于
        // 「累计开采 + 累计劫损」——劫损也从余额里扣掉了。它恒 ≥ 0
        // 仍然成立：配额封顶让余额不会低于 0。
        sect.deviation_total =
            static_cast<std::int64_t>(sect.quota_total)
            - static_cast<std::int64_t>(sect.current_total);
```

同时把 `struct SectLedger` 里 `deviation_total` 字段的 `///` 注释从
「名下累计开采量」改成「名下累计开采量 + 累计劫损」，`header` 与
实现两侧的注释都要改（实现里的注释解释的是 why，不改就成了误导）。

- [ ] **Step 4: 构建并运行测试确认通过**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: **全绿**（此时 Task 1 的守卫也应全绿）。

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/history/ledger.cpp
git add include/gamev2/history/ledger.hpp
git add tests/test_history_ledger.cpp
```

---

### Task 5: 诊断、导出与 demo

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `src/demo/main.cpp`
- Test: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: Task 3 的 `tribulation_no_vein` 诊断、Task 4 的
  `tribulation_deviation` / `sect.tribulation_loss`
- Produces: 导出文本的两个新区段 + 一行诊断；demo 的第三类偏差与渡劫统计

- [ ] **Step 1: 写失败的测试**

追加到 `tests/test_history_ledger.cpp`：

```cpp
TEST(the_export_carries_the_tribulation_toll)
{
    // 台账是给人读的：劫损若只在内存里，配额之争的第三方成因读者看不见，
    // 那它跟没算过一样。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(!ledger.tribulation_events.empty());

    // 诊断量对着世界数一遍：它报的是「抽了个空圈」的候选数。只断言导出里
    // 有这一行的话，计数写成 0 或写成别的数都没人发现。
    const auto pool = gamev2::history::epoch_tribulation_sites(w);
    std::size_t empty_circles = 0;
    for (const auto& site : pool)
    {
        bool touched = false;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            const auto [covered, total] =
                coverage_of(w, static_cast<std::uint32_t>(i),
                            site.cell, site.radius);
            (void)total;
            if (covered > 0u)
            {
                touched = true;
                break;
            }
        }
        if (!touched)
        {
            ++empty_circles;
        }
    }
    CHECK_EQ(diagnostics.tribulation_no_vein, empty_circles);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("gamev2_ledger_toll_seed" + std::to_string(p.seed) + ".txt");
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    bool saw_toll_row = false;
    bool saw_toll_section = false;
    bool saw_no_vein = false;
    std::size_t toll_rows = 0;
    while (std::getline(in, line))
    {
        saw_toll_row |= line.starts_with("劫损（天地抽走）");
        saw_toll_section |= line.starts_with("=== 渡劫事件清单 ===");
        saw_no_vein |= line.find("空圈") != std::string::npos;
        if (saw_toll_section && !line.starts_with("=== ")
            && !line.starts_with("脉\t") && !line.empty()
            && line[0] >= '0' && line[0] <= '9')
        {
            ++toll_rows;
        }
    }
    std::filesystem::remove(path);
    CHECK(saw_toll_row);
    CHECK(saw_toll_section);
    CHECK(saw_no_vein);
    CHECK_EQ(toll_rows, ledger.tribulation_events.size());
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: `the_export_carries_the_tribulation_toll` **FAIL**
（三个 `saw_*` 全是 false，`toll_rows` 是 0）。

- [ ] **Step 3: 导出加三处、诊断计数**

**3a. 诊断计数**已经在 Task 3 的 `build_coverages` 里完成
（`++diagnostics.tribulation_no_vein;`），本步不重复。

**3b. 偏差分解加一行。** 在 `export_ledger` 里
`out << "越采差（人为多采）\t" << ledger.overmining_deviation << '\n';`
之后插入：

```cpp
    out << "劫损（天地抽走）\t" << ledger.tribulation_deviation << '\n';
```

**3c. 宗表加一列。** 把

```cpp
    out << "宗门\t登记\t8300配额\t8300流速\t流速偏差\t当代余额\t"
        << "配额与余额差\t越采\n";
```

改成

```cpp
    out << "宗门\t登记\t8300配额\t8300流速\t流速偏差\t当代余额\t"
        << "配额与余额差\t越采\t劫损\n";
```

并在循环里 `<< sect.overmined_amount << '\n';` 之前补一列：

```cpp
            << sect.deviation_total << '\t'
            << sect.overmined_amount << '\t'
            << sect.tribulation_loss << '\n';
```

**3d. 新段「渡劫事件清单」。** 插在 `=== 枯竭争议 ===` 段之后、
`=== 诊断 ===` 之前：

```cpp
    out << "\n=== 渡劫事件清单 ===\n";
    // 与越采案清单同形：越采是「人多采了」，劫损是「天地抽走了」，
    // 两者在政治上要分得开，账上就得分得开。
    out << "脉\t宗门\t年份\t数额\t劫心格\n";
    for (const auto& event : ledger.tribulation_events)
    {
        out << event.vein << '\t'
            << sect_name(holder_of(ledger, event.vein)) << '\t'
            << event.year << '\t'
            << event.amount << '\t'
            << event.site << '\n';
    }
```

**3e. 诊断段加一行。** 在
`out << "跨度超限\t" << diagnostics.span_too_large << '\n';` 之后插入：

```cpp
    out << "空圈\t" << diagnostics.tribulation_no_vein << '\n';
```

- [ ] **Step 4: demo 打印第三类偏差与渡劫统计**

`print_ledger_stats` 目前只收 `ledger` 与 `diagnostics`，而池数要现从世界
派生——先给它加一个 `world` 参数（同文件里的 `print_surface_stats`、
`print_spirit_stats` 都是这么收的）：

```cpp
void print_ledger_stats(const gamev2::history::Ledger& ledger,
                        const gamev2::history::LedgerDiagnostics& diagnostics,
                        const World& world)
```

调用点（约 692 行）同步改成：

```cpp
    print_ledger_stats(ledger, diagnostics, world);
```

再在 `print_ledger_stats` 函数体内，把

```cpp
    std::println("  口径差 {}，越采差 {}", ledger.structural_deviation,
                 ledger.overmining_deviation);
```

改成

```cpp
    // 三类并排：口径差压过另两类，这正是「老配额结构性不公」那句话的
    // 三个分量，缺一个就只剩两个可比。
    std::println("  口径差 {}，越采差 {}，劫损 {}",
                 ledger.structural_deviation,
                 ledger.overmining_deviation,
                 ledger.tribulation_deviation);
    // 失败次数与入账笔数不是一回事：空圈失败只计数。池数要现派生一次
    // （55 ms），demo 本来就要几秒，多这一次无所谓。
    const std::size_t pool =
        gamev2::history::epoch_tribulation_sites(world).size();
    std::println("  渡劫：纪元候选 {} 处，失败 {} 次，入账 {} 笔",
                 pool, ledger.tribulation_failed,
                 ledger.tribulation_events.size());
```

并把诊断打印的两处改掉——先在 `if` 条件里补一个：

```cpp
        || diagnostics.tribulation_no_vein > 0u
```

再在 `std::println` 的格式串与实参里各补一项（格式串较长，拆两行写）：

```cpp
        std::println("  诊断：枯眼无账 {}，枯竭未标 {}，缺阴脉 {}，主脉缺 {}，"
                     "越采早于纪元 {}，换算塌缩 {}，跨度超限 {}，空圈 {}",
                     diagnostics.depletion_claim_unbacked,
                     diagnostics.depletion_unmarked,
                     diagnostics.missing_yin_vein,
                     diagnostics.short_main_veins,
                     diagnostics.overmine_before_epoch,
                     diagnostics.flow_conversion_collapsed,
                     diagnostics.span_too_large,
                     diagnostics.tribulation_no_vein);
```

- [ ] **Step 5: 构建、跑测试与 demo**

```powershell
cmake --build build
./build/history_tests.exe
./build/worldgen_demo.exe --size 256 --seed 3
```

Expected: `history_tests` 全绿。demo 里应看到：

```
  口径差 13510，越采差 204，劫损 ...
  渡劫：纪元候选 16 处，失败 N 次，入账 M 笔
```

（256²/seed 3 的劫损是几十到几百量级；**注意 256² 用的是当代世界，纪元
候选池是另外派生出来的 16 处**，与当代 0 处不矛盾。）

导出的 `ledger.txt` 里，`=== 渡劫事件清单 ===` 与诊断段的「空圈」都要在。

- [ ] **Step 6: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/history/ledger.cpp
git add src/demo/main.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 6: 全量回归与「越采仍足额认领」复验

**Files:**
- Test: 无新增文件（复验可能改 fixture 或断言，**改动前先报告**）
- Modify（仅在复验失败时）: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: Task 1~5 的全部产出
- Produces: 一份可交给变异战役的绿色基线

- [ ] **Step 1: 跑三个测试目标**

```powershell
cmake --build build
./build/ecs_tests.exe
./build/worldgen_tests.exe
./build/history_tests.exe
```

Expected: 111 / 181 / 全绿，三个都 `0 failed`，退出码 0。

**尤其注意** `vein_counts_match_the_setting_exactly`（1 龙 / 9 主 / 40 支）
——本计划只读 worldgen，不应影响灵脉数量。若它挂了，说明改了不该改的字段。

- [ ] **Step 2: 显式复验「越采恒被足额认领」**

这是设计 §8.3 列的**本刀唯一既有行为风险**，必须单独验，不能靠
「全绿」顺带带过：

```powershell
./build/history_tests.exe 2>&1 | Select-String -Pattern "FAIL|overmin"
```

再跑一次 demo 并看诊断行里的 `越采早于纪元`：

```powershell
./build/worldgen_demo.exe --size 256 --seed 11
./build/worldgen_demo.exe --size 256 --seed 26
```

Expected: `ledger_never_double_counts_overmining` 通过；两个种子里
`越采早于纪元 0`（若非 0，说明劫损确实把可行窗口挤掉了一部分，往下走第 3 步）。

- [ ] **Step 3: 若等号失败或 `越采早于纪元 > 0`，按下面的顺序处理**

**不要为了让测试变绿而改断言。** 三个选项，按代价从小到大：

1. **换 fixture**（优先）：`ledger_never_double_counts_overmining` 改用
   另一个「有越采、有劫损、且越采年不早于劫损年」的种子。可选池是实测过的
   128² 种子：10、21、22、24、26、36、39（其中 22、24、39 的劫损下界
   非零：384 / 1393 / 216，且都带越采脉）。
2. **把恒等式 3 写成两支**：`gap == normal + loss` 当越采足额认领，
   否则 `gap == normal + loss + (target − claimed)`，并用诊断量标出
   「未足额认领」的脉数。
3. **两者都要**。

**选定前先报告给用户**，并把实测数字（哪个种子、越采差、劫损、
`overmine_before_epoch` 计数）一并给出。

- [ ] **Step 4: 跑 demo 核对量级**

```powershell
./build/worldgen_demo.exe --size 256 --seed 3
```

核对：脉 50、口径差约 13510、越采差约 204、劫损**非零且小于口径差**
（设计 §6.4 招牌断言的口径）。任何一项对不上，回对应任务自查。

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。**

---

### Task 7: 变异战役

**Files:**
- Create（临时，gitignore 内）: `agent-temp/sdd/mut/task_trib_mutations.ps1`
- 无仓库文件改动

**Interfaces:**
- Consumes: Task 6 的绿色基线
- Produces: 变异日志 `agent-temp/sdd/mut/task_trib_mutations.log`，以及
  「每条新断言都能被变异打红」的证据

- [ ] **Step 1: 写变异脚本**

写 `agent-temp/sdd/mut/task_trib_mutations.ps1`，对下面六个变异点逐个
改源码 → 重建 → 记录哪些测试变红 → 还原：

| # | 变异 | 应该变红 |
|---|---|---|
| M1 | `kTribulationFailChance` 0.3 → 0 | Task 1 五条守卫 + 新增 4 条 + 招牌断言的 `> 0` 前提 |
| M2 | 去掉覆盖比：loss 按整条余额算 | `a_failed_tribulation_takes_the_covered_share` |
| M3 | 劫损块从「越采之后」挪到「越采之前」 | `a_failed_tribulation_takes_the_covered_share`（基数变了） |
| M4 | 劫损块从「正常开采之前」挪到「之后」 | 同上（基数吃进了当年正常开采） |
| M5 | `derive_epoch_sites` 漏掉 `make_land_types` | `the_epoch_sites_are_derived_exactly` |
| M6 | `ledger.tribulation_events.clear();` 从 `advance` 里删掉 | 确定性与逐年守恒（第二遍叠账） |

每个变异点的**实际**变红集合要记进日志，与表里的期望**逐条对账**：
期望变红却没红的，是覆盖缺口，要报告，不要略过。

- [ ] **Step 2: 跑完六个变异并记录**

```powershell
pwsh -File agent-temp/sdd/mut/task_trib_mutations.ps1
```

Expected: 脚本跑完、日志里六个变异**全部**有「哪些测试红了」的记录。

**M3/M4 是条件性的**：若某年没有同时发生越采与劫损，基数对比不出差别，
这两条可能不红。日志里要写明是「没红（该年无越采）」还是「红了」——
两者不能混为一谈。

- [ ] **Step 3: 还原并确认基线全绿**

```powershell
cmake --build build
./build/history_tests.exe
./build/worldgen_tests.exe
./build/ecs_tests.exe
```

Expected: 全绿，且 `git status` 显示**只有本计划计划内的文件被改过**
（变异必须完全还原）。

```powershell
git status --short
```

- [ ] **Step 4: 把覆盖缺口写进 progress 并向用户报告**

报告内容：六个变异各自的实际变红集合、与期望不符的条目、以及你判断的
覆盖缺口。**不得执行 `git commit`。**

---

## 已知限制与风险（实施后仍然成立）

设计 §8 六条全部继承。实施中要盯的三条：

1. **越采的足额认领可能被劫损挤掉**（Task 6 第 2、3 步）。这是唯一
   可能逼着改既有断言的地方，改之前必须报告。
2. **M3/M4 变异的条件性**：没有同年既有越采又有劫损的年份时，次序变异
   打不红。日志必须区分「没红（条件不满足）」与「红了」。
3. **小图幅没有渡劫**：64² 的候选池 40/40 全空。任何新增的、要验劫损的
   测试**都不能**用 64²，否则断言整条空跑。

---

## 后续纵切（本轮不做）

- 劫溃后「百年内沦为死地」的冷却规则，以及同一候选的反复渡劫
- 把模拟出的失败写成 `Scar::tribulation`（要破只读边界，设计 §9.1 取了不留疤）
- 渡劫者是谁、属于哪一宗
- 候选池逐年化（要按年生成 N 份世界，设计 §8.1）

---

**不得执行 `git commit`。**

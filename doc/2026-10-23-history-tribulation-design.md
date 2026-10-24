# 时序层设计文档（第二刀）：渡劫结算与劫损

- 日期：2026-10-23
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23
- 基线分支：`worldgen`
- 前置文档：`doc/2026-10-21-history-layer-design.md`（第一刀：灵脉产权与开采台账）

---

## 0. 本文定位

### 0.1 这一刀是什么

**渡劫结算**：把设定 §5.2「天地抽走方图百里灵气」变成台账里的一笔账，
产出**第三类偏差——劫损**。

第一刀把「老配额不公」拆成口径差与越采差两个数；这一刀补上第三个来源：
**没人采、也不是配额口径的问题，是天地抽走的**。设定说渡劫是「修仙世界里
最惨烈的生态破坏行为」，但第一刀的账上完全没有它的位置——那三个数摆在一起
才是完整的稀缺链。

它仍然不是玩法：不产生玩家操作、不产生胜负、不产生画面。

### 0.2 为什么事件源必须是纪元快照（本文最硬的约束）

设定说渡劫的地理前提是**渡劫福地**（§5.2：「各大宗门争夺的'渡劫福地'，
本质是愿意被劫云抽干的地方」）。而 worldgen 在**当代年**给出的候选是空的。
256×256 实测（种子 1 / 3 / 7 / 42）：

| | 灵历 8914（台账当代，谷底期） | 灵历 8300（脉律纪元，峰值年） |
|---|---|---|
| 福地格数 | **0** | 789 ~ 2182 |
| `World::tribulation_sites` | **0 处** | 6 ~ 25 处 |
| `Scar::tribulation`（渡劫废墟） | 0 ~ 4 处 | 恒 6 处 |

灵气层设计 §3.8 已经写明本阶段「不产出渡劫的结果……这些都属于时序层」，
但它在当代这份世界里**一个候选都没给**。所以时序层要结算，就必须自己回答
「8300 年时哪里肯被抽」——**纪元年的候选池是一句历史陈述，不是空间陈述**。

### 0.3 本轮做与不做

| 本轮做 | 本轮不做 |
|---|---|
| 纪元渡劫场的派生与候选池 | 渡劫者是谁、属于哪一宗（越采的动机同理未建模） |
| 614 年里每个候选至多一次渡劫 | 同一处反复渡劫、以及「百年死地」的冷却规则 |
| 成功 / 失败的结算 | 劫云溃散后的浓度回流（成功净 0，不入账） |
| 逐脉劫损、逐宗劫损、第三类偏差 | 把劫损写回 `World`（仍然只读） |
| 守恒恒等式的改写 | 兽潮、七宗演化、私采点（仍是后续纵切） |

---

## 1. 与既有框架的关系

### 1.1 仍然只读 worldgen，不回写

第一刀的边界原样继承：`build_ledger` 不写 `World` 的任何字段，不改
`WorldParams`，依赖方向只能是 `history → world`。

**纪元快照的派生也不越界**：它在一份**拷贝**上重跑 worldgen 的潮相关阶段，
取走 `tribulation_sites` 之后立刻丢弃整份拷贝，输入 `World` 一个字节都没动。

### 1.2 纪元快照由历史层派生，不存进 World

候选池由三步得来：

```cpp
World derived = world;                        // 拷贝
derived.spirit_tide = compute_spirit_tide(8300);   // 潮位拨回纪元
make_spirit_domains(derived);                 // 阶段 14
make_historical_scars(derived);               // 阶段 15
make_land_types(derived);                     // 阶段 16
make_tribulation_sites(derived);              // 阶段 18
const std::vector<TribulationSite> pool = derived.tribulation_sites;
```

**为什么不存进 `World`**：那是历史陈述，不是空间陈述。设计 §1.2 已经定调
「历史是一次性推演的结果，与空间无关」；往 `World` 里加一个只服务下游模块的
字段，会让读代码的人以为 worldgen 后面还有阶段要用它。

**为什么能只重跑这四个阶段**：灵脉本身**不随潮位变**。实测两份世界
（8300 与 8914，同种子）的灵脉逐条完全相同——seed 3 是 52/52、seed 7 是
53/53，`reserve`、`channel`、段范围、越采标记全等。变的只有潮相关层：
域等级 → 疤 → 地貌 → 渡劫场。所以候选池只需要那四步。

**代价**：256×256 约 **55 ms**，而 `build_ledger` 本身 3.9 ms、完整重新生成
一份世界 230~2400 ms（种子间差异很大）。55 ms 落在推演侧，测试用的
64×64 / 128×128 fixture 只有几毫秒。

**这条路径必须被钉住**：它依赖 worldgen 的阶段顺序（14 → 15 → 16 → 18）。
worldgen 若在中间插入一个影响域等级 / 疤 / 地貌的阶段，派生会**静默过时**，
候选池与「按纪元年完整生成」的真值对不上。测试 §6 第 1 条就是这颗钉子。

### 1.3 不与既有渡劫废墟对应

`Scar::tribulation`（阶段 15 的废墟）是「**已经发生过的**失败」，可能发生在
纪元之前，也可能在当代那份世界里一处都没有（实测 0~4 处）；本层结算的是
`[8300, 8914)` 窗口内模拟出的失败。两者口径不同，**不交叉校验**。

实测还有一条佐证：废墟的 `ScarSite::target` 几乎全是 `kNoVein`——seed 3 是
6/6、seed 7 是 5/6——因为废墟的候选是厚域 / 福地，那里的格子通常没有所属脉。
**本层的归因只能按半径圈内的脉算，不能靠 `target`**（`TribulationSite::vein`
同理，同样可能是 `kNoVein`）。

---

## 2. 数据模型

### 2.1 劫损不是开采

第一刀的事件表记的是「**人**采走了多少」，`extracted` 是配额的消费。
劫损是天地抽走的：没有配额、没有持有者、没有越采——**并进事件表会让
`the_events_account_for_every_extraction`（事件总和 == `extracted`）语义错乱**。
所以事件**单独一张表**，代价是守恒恒等式 1 必须改写。

### 2.2 类型定义

```cpp
struct QuotaEntry
{
    ...
    /// 六百年里劫云抽走且没有回流的部分。成功不入账（净 0）。
    std::uint32_t tribulation_loss = 0;
};

/// 一次失败的渡劫。
struct TribulationEvent
{
    std::uint32_t vein = 0;
    std::uint32_t year = 0;
    std::uint32_t amount = 0;
    std::uint32_t site = 0;   ///< 劫心格，读者要能在图上找到它
};

struct SectLedger
{
    ...
    /// 该宗名下的劫损合计。
    std::int64_t tribulation_loss = 0;
};

struct Ledger
{
    ...
    std::vector<TribulationEvent> tribulation_events;
    /// 第三类偏差：劫损合计。与越采差同形——都是逐脉字段的 Σ。
    std::int64_t tribulation_deviation = 0;
    /// 失败的渡劫次数。入账的是它的一部分（空圈失败只计数、不入账），
    /// 所以 demo 要用它才能说出「试了几次」——只看事件数会误以为试得少。
    std::uint32_t tribulation_failed = 0;
};
```

字段唯一写入者不变式：`World` 全部字段归 worldgen 的 18 个阶段；
`Ledger` 全部字段归 `build_ledger`。

**注释同步**：`SectLedger::deviation_total` 现有注释写「名下累计开采量
（≡ 配额 − 当代余额）」。加上劫损后它等于**累计开采 + 累计劫损**——那条注释
解释的是「为什么恒 ≥ 0」，改后不更新就会误导后来人。

### 2.3 守恒恒等式的改写

```
恒等式 1（逐脉终态）
  旧：balance == surveyed_total − extracted
  新：balance == surveyed_total − extracted − tribulation_loss

恒等式 3（reserve 与 balance 的差）
  旧：reserve − balance == normal
  新：reserve − balance == normal + tribulation_loss
```

恒等式 2（宗级汇总 = 脉级之和）不改：`tribulation_deviation` 与
`tribulation_loss` 同样是逐脉 Σ，逐宗 Σ 同理。

**无符号不变式不加新条款**：`balance` 永不为负的前提仍然是「每一笔扣减都
≤ 扣减时刻的余额」。劫损取**当年余额**的覆盖比例（§3.4），天然满足。

### 2.4 受影响的既有测试

| 测试 | 改什么 | 为什么非改不可 |
|---|---|---|
| `final_balance_matches_the_ledger` | 公式补 `− tribulation_loss` | 恒等式 1 本体 |
| `conservation_holds_at_every_year` | 复算把劫损事件**按年份并入时间序** | 它只回放 `events`，漏掉劫损后末年余额对不上 |
| `ledger_never_double_counts_overmining` | 等号补 `+ tribulation_loss` | 恒等式 3 的等号那一半 |
| `the_ledger_is_exportable_as_text` | 行数上界 `rows + 45u` 加新增区段 | 多一段清单，边界会撞 |
| `the_ledger_is_deterministic` | 补比较 `tribulation_events` | 新字段不进比较 = 没测确定性 |
| `a_span_beyond_the_limit_is_refused` | 补 `tribulation_deviation == 0` | 超限路径的口径一致 |

**不受影响**：`the_events_account_for_every_extraction`（分表的直接收益）、
worldgen 的 181 条、ecs 的 111 条。

---

## 3. 结算规格

### 3.1 输入

| 输入 | 来源 |
|---|---|
| 候选池 | §1.2 派生的纪元渡劫场（`cell`、`radius`、`vein`） |
| 逐脉余额轨迹 | 第一遍推演（既有逻辑，含潮位接入） |
| 纪元 / 当代 | `epoch = 8300`（常量）、`now = params().spirit_year` |
| 灵脉节点 | `world.ley_channels[vein.channel].nodes`，配合 `ley_nodes[i].cell` |

### 3.2 候选池与排期

**每处候选至多一次渡劫**，年份在 `[epoch, now)` 内均匀抽；成败由
`kTribulationFailChance` 决定（§4）。

- 成功 → 劫云溃散回流当地 → **净 0，不入账**，只进 demo 的成功计数。
- 失败 → 逐脉计算劫损，写 `TribulationEvent` 与 `QuotaEntry::tribulation_loss`。

**为什么每处至多一次**：设定没给渡劫频率。反复抽同一处需要配套「抽干后百年
不可用」的冷却规则，那是世界状态的可变性（灵气层 §8.5 已经把这条划给模拟层），
本轮不引入。事件数 = 池子大小（实测 6~25），上界清楚。

**距离口径：欧氏**，与阶段 15 的疤痕、阶段 17 的洞天、阶段 18 的间距同一把尺
（`dx*dx + dy*dy < radius*radius`）。

### 3.3 年内次序

```
年初余额
  → ① 越采扣减（既有逻辑，含可行窗口判断）
  → ② 劫损
  → ③ 正常开采
```

**劫损排在越采之后是刻意的**：排在越采之前会压低第一遍的余额轨迹，缩窄越采
的可行窗口；一旦某条脉的越采**没被足额认领**，恒等式 3 的等号就塌，牵连的
测试会比现在多一倍。现有代码依赖「越采恒足额认领」（第一刀的提交说明原话：
*Overmining is therefore claimed in full and once*），这个前提必须继续成立。

次序写死在实现里，并由 §6.2 第 6 条直接钉住——那条测试按公式反算每一笔劫损
的基数，基数对不上就说明次序挪了。

### 3.4 损失量

对一次失败的渡劫，圈内（欧氏距离 ≤ `radius`）每条台账脉：

```
covered = 该脉 first_node..last_node 中 cell 落在圈内的节点数
total   = 该脉节点总数 = last_node − first_node + 1
loss_i  = balance × covered / total     // 先乘后除，整数截断
```

基数 `balance` 取 **② 执行时刻**该脉的余额（该年的越采已扣、正常开采未扣）。

**取整是截断、不用定点余数累加**：第一刀对逐年流量用定点余数，是因为同一条
脉要连续六百年累加，截断会把枯竭年挤成悬崖；劫损每一笔都独立，不跨年累加，
截断的偏差不会积累。

**正常开采的配额余量必须一并减掉劫损**：

```
旧：quota_left = surveyed_total − extracted
新：quota_left = surveyed_total − extracted − tribulation_loss
```

否则劫损那年之后的开采仍按原配额扣满：`surveyed = 100、已采 40、劫损 30`
时余额只剩 30，而 `quota_left = 60`，采满就把 `balance` 打成负数——无符号
回绕成一个巨大正数，整本账静默错乱。**这是防下溢的修正，不是口径调整**：
`tribulation_loss = 0` 时它与旧式逐字相同，既有行为一字未改。

- **覆盖比 1.00 时整条脉被抽干**——实测 seed 3 有这种脉，与设定「抽干方图
  百里」一致。
- **`loss_i ≤ balance_i` 恒成立**，因为比例 ≤ 1 且基数就是当前余额。§2.3 的
  无符号不变式因此不需要新条款。
- 圈内没有台账脉的候选 → 该次事件损失为 0，记诊断 `tribulation_no_vein`
  （实测 seed 3 / 42 各 3 处空圈）。

### 3.5 三类偏差

| 字段 | 含义 | 计算 |
|---|---|---|
| `structural_deviation` | 口径差：8300 峰值口径 vs 当代谷底口径 | 既有（潮位折算） |
| `overmining_deviation` | 越采差：人为多采 | `Σ overmined_amount` |
| `tribulation_deviation` | **劫损：天地抽走** | `Σ tribulation_loss` |

前两类是第一刀的招牌：口径差压过越采差，说明老配额**结构性**不公。补上第三类
之后招牌断言升级为三类都压住口径差。

**256×256 实测**（全部失败、即劫损的上界，用来看量级）：

| 种子 | 纪元渡劫场 | 空圈 | 最大覆盖比 | 全失败劫损 | 越采差 | 口径差 | 全账余额 |
|---|---|---|---|---|---|---|---|
| 3 | 16 | 3 | 1.00 | 2754（46%） | 204 | 13510 | 5949 |
| 7 | 6 | 0 | 0.83 | 683（25%） | 2094 | 12868 | 2782 |
| 42 | 15 | 3 | 1.00 | 309（14%） | 1319 | 8903 | 2135 |

按 `kTribulationFailChance = 0.3` 的期望值（0.3 × 上界）：seed 3 ≈ 826、
seed 7 ≈ 205、seed 42 ≈ 93。

**三类里口径差恒最大**（13510 / 12868 / 8903），招牌断言因此对三个种子都成立。
**劫损与越采差谁大则随种子变**：seed 3 是 826 > 204（天地抽走的比人偷采的多），
seed 7 是 205 < 2094、seed 42 是 93 < 1319（反过来了）。所以本设计**不声称**
劫损恒大于越采差——它只是稀缺链上第三个来源，位置由各自的发生频次决定。

### 3.6 退化路径

| 情形 | 处理 |
|---|---|
| `now < epoch` | 第一刀的闸门先跑，台账为空，渡劫自然无事件 |
| `now == epoch` | 跨度 0，`[epoch, now)` 为空，**零事件** |
| `span_too_large` | 与第一刀一致：台账按「没有推演」处理，`tribulation_deviation = 0` |
| `flow_conversion_collapsed` | 换算塌缩时余额停在基数上，劫损仍按 ② 的余额算，不额外处理 |
| 候选池为空 | 零事件、`tribulation_deviation = 0`，诊断计数不涨（空池不是退化） |

既有的 `the_epoch_year_itself_yields_a_populated_ledger` 不受零事件影响：
它断言的是脉与配额，不是事件。

---

## 4. 参数与随机流

### 4.1 参数

```cpp
/// 渡劫失败的概率。设定 §5.2 没给数，与 kOverminedChance、kDepletedRatio
/// 同样是经验值。取 0.3 而不是 0.15：候选池实测 6~25 处，0.15 时 6 处的
/// 池子有约 38% 的概率一次都不失败，第三类偏差常态为 0，招牌断言就写不出来。
inline constexpr double kTribulationFailChance = 0.3;
```

放在 `include/gamev2/history/ledger.hpp`：它只被时序层使用，不是 worldgen
的调参项（`kOverminePenalty` 要进 `spirit_tables.hpp` 是因为**两侧共用**，
这里只有一侧）。

失败率不影响损失的几何（覆盖比），只影响发生次数——所以它不参与任何守恒
计算，是纯粹的频次旋钮。

### 4.2 随机流

新增 `Stream::tribulation = 18`，`kStreamIds` 由 18 扩到 19；
`static_assert(stream_ids_are_distinct())` 会守住重复流号。

**不复用 `Stream::ledger`**：渡劫的年份与成败是另一路随机性。混进同一条流，
日后调 `kTribulationFailChance` 或候选池大小时会连带扰动越采年的抽样，
把既有的 fixture（seed 13 / 26 / 62 那几条）全部推翻。

确定性要求与第一刀一致：同 `(seed, params)` 必须产出逐字段相同的台账，
包括 `tribulation_events` 的顺序（按年份升序，同年按劫心升序）。

---

## 5. 错误处理（诊断）

退化不是错误，由 `LedgerDiagnostics` 报出，由调用方决定要不要打印。

| 字段 | 含义 |
|---|---|
| `tribulation_no_vein` | 劫心圈内没有台账脉，事件无效果的次数 |

新增这一项。**成功 / 失败的计数不进诊断**——它们是正常结果，进事件表与 demo
统计；诊断只放「对不上的地方」，与第一刀的八项口径一致。

导出的诊断段同步加一行「空圈」。

---

## 6. 测试策略

沿用 `tests/test_framework.hpp` 的 `TEST` / `CHECK_EQ`。

### 6.1 图幅与种子的硬约束（实测，先看这条）

候选池在小图幅上是**空的**。扫 40 个种子（z_levels=3）：

| 图幅 | 纪元池非空的种子 | 生成耗时 |
|---|---|---|
| 64×64 | **0 / 40** | 21 ms |
| 128×128 | **22 / 40**（seed 11 = 3 处，seed 9 = 2 处，其余多为 1 处） | 130 ms |
| 256×256 | 6~16 处 | 230 ms ~ 2.4 s |

**后果**：既有 12 条测试用 64×64，其中 4 条正是本刀要改的
（`conservation_holds_at_every_year`、`final_balance_matches_the_ledger`、
`the_ledger_is_deterministic`、`the_ledger_is_exportable_as_text`）。它们在
64² 下 `tribulation_loss` 恒 0，新公式里的 `− tribulation_loss` 是**空跑断言**
——改了等于没测。

**fixture 策略**：

- 要**验到**劫损的测试一律用 **128×128**：
  - §2.4 里用 64² 的那 4 条 —— `p.width` 从 64 改成 128，**种子不用换**
    （`seed 11` 在 128² 的池子是 3 处），成本每条约 +110 ms；
  - §6.2 新增的 #1 ~ #6；
  - `ledger_never_double_counts_overmining` 已经在 128²（seed 13，池 1 处），
    但**种子要按「真的有失败」重挑**，否则它的 `+ tribulation_loss` 恒加 0。
- **不验劫损**的测试（`a_year_before_…`、`the_surveyed_total_…`、
  `the_deviation_is_not_trivial` 等）留在 64²，不为它们付 130 ms。
- 凡验劫损的测试**必须带守卫** `CHECK(tribulation_loss > 0)`（或
  `CHECK(!tribulation_events.empty())`），否则种子或参数一变，断言整条空跑
  而没人察觉——这是本仓库的既定做法。
- **池子非空 ≠ 有失败**：128² seed 11 的 3 处候选在 0.3 下有约 66% 概率至少
  失败一次。实现后要**用探针实测**哪几个种子真的 `tribulation_loss > 0`，
  再定最终 fixture；这一步进实施计划，不靠推算。

### 6.2 新增

| # | 用例 | 断言 |
|---|---|---|
| 1 | `the_epoch_sites_are_derived_exactly` | 派生池 == 按 8300 完整生成的池，逐处一致（§1.2） |
| 2 | `a_failed_tribulation_takes_the_covered_share` | 损失 == 当年余额 × 覆盖比；比 1.00 整条抽干 |
| 3 | `tribulation_loss_never_exceeds_the_balance` | 损失不超过扣减时刻的余额 |
| 4 | `the_tribulation_events_land_inside_the_window` | 年份 ∈ `[epoch, now)`、金额 > 0、按年升序 |
| 5 | `the_export_carries_the_tribulation_toll` | 劫损行与渡劫事件清单真的进了导出文件 |
| 6 | `the_toll_takes_the_post_overmining_balance` | 每笔劫损 == `(上年末余额 − 该年越采) × 覆盖比` |

第 4 条断言的是「出现在事件表里的**不同劫心数** ≤ `tribulation_failed`」：
入账的是失败里落在**有脉圈内**的那部分，所以失败数只能多、不能少。
注意**不是** `tribulation_failed ≥ tribulation_events.size()`——一处劫心
圈住多条台账脉时逐脉各记一笔（§3.4），事件笔数可能超过失败次数
（实测 128²/seed 26：失败 1 处、事件 2 笔、不同劫心 1 个）。

第 6 条要在测试里从 `World` 反算覆盖比（§3.4 的公式），照的是
`overmined_vein_reserve_is_the_penalised_score_sum` 的先例：**基数写在账里，
但基数由谁提供要靠测试自己从世界反推**。它同时钉住次序的两端——基数里含
越采、不含该年的正常开采。正常开采几乎每年都有，所以这条断言**常年在跑**，
不会空跑。

**第 1 条与第 6 条都必须用 128×128**（见 §6.1）：64² 池子恒空，两边都是空池
的「逐处一致」是空比较。第 1 条生成两份 128² 世界约 **260 ms**，这是本刀在
测试套件上的全部新增成本。

### 6.3 改既有（见 §2.4）

六条：恒等式 1、逐年守恒、恒等式 3、导出行数上界、确定性比较、超限路径。

### 6.4 招牌断言升级

`the_structural_deviation_dominates` 从「口径差 ≥ 越采差」扩成三类都压住：

```cpp
CHECK(ledger.structural_deviation > 0);
CHECK(ledger.overmining_deviation > 0);
CHECK(ledger.tribulation_deviation > 0);
CHECK(ledger.structural_deviation >= ledger.overmining_deviation);
CHECK(ledger.structural_deviation >= ledger.tribulation_deviation);
```

`tribulation_deviation > 0` 这条前提同时是「第三类偏差不是空代码」的守卫。

### 6.5 变异要求

每条新断言都要能被变异打红：

| 变异 | 应该变红 |
|---|---|
| `kTribulationFailChance → 0` | #2、#4、#5、#6、招牌断言的 `> 0` 前提 |
| 损失公式去掉覆盖比例（改成全额扣） | #2、#6 |
| 劫损挪到正常开采之后 | #6（基数会吃进该年正常开采） |
| 劫损挪到越采之前 | #6（仅该年同时有越采时；否则靠恒等式 3 兜） |
| 派生漏跑阶段 14 / 15 / 16 / 18 中任意一个 | #1 |
| 劫损事件不写进 `tribulation_events` | #4、#5 |

---

## 7. 文件划分

| 文件 | 职责 |
|---|---|
| `include/gamev2/history/ledger.hpp` | 新类型、新字段（见下）、`kTribulationFailChance` |
| `src/history/ledger.cpp` | 池派生、排期与成败、损失与次序、第三类偏差、导出、诊断 |
| `tests/test_history_ledger.cpp` | §6.2 新增 6 条、§6.3 改 6 条、§6.4 招牌断言升级 |
| `include/gamev2/world/enums.hpp` | `Stream::tribulation = 18`，`kStreamIds` 扩到 19 |
| `src/demo/main.cpp` | 三类偏差并排打印、渡劫统计 |

`ledger.hpp` 新增的全部符号：`TribulationEvent`、`QuotaEntry::tribulation_loss`、
`SectLedger::tribulation_loss`、`Ledger::tribulation_events`、
`Ledger::tribulation_deviation`、`Ledger::tribulation_failed`、
`LedgerDiagnostics::tribulation_no_vein`、`kTribulationFailChance`、
`epoch_tribulation_sites`。

**不改**：`include/gamev2/world/` 下任何既有头文件的**行为**（`enums.hpp` 只
登记新流，那是它自己写明的规矩）；`src/world/` 全部不动。

---

## 8. 已知限制与风险

1. **候选池是纪元年的快照，不是逐年的**。设定说福地随灵潮扩张、随衰退收缩，
   所以 614 年里真实可用的候选是逐年的；本层用 8300 那一年的池子代表整段窗口。
   这是**读得到的近似**——要逐年化就得生成 N 份世界（每份 230~2400 ms）。
   影响方向：衰退后期（灵历 8801 之后）的渡劫被高估。

2. **损失量的几何是「覆盖节点比例」，不是浓度积分**。设定说抽走的是方图百里
   的**灵气**，而节点比例只近似了「这条脉有多少落在圈内」。更准的做法要按
   浓度场积分，但那要重算浓度（灵气层 §8.9：有效浓度是派生量），成本与
   复杂度都不成比例。**量级结论（夹在越采差与口径差之间）对这个近似不敏感。**

3. **劫损会缩窄越采的可行窗口**（§3.3）。实现时必须复验「越采仍足额认领」，
   否则 `ledger_never_double_counts_overmining` 的等号会塌，而 `overmine_before_epoch`
   诊断可能上升。**这条是本刀唯一的既有行为风险**，实施计划里要有专门一步。

4. **与既有 `Scar::tribulation` 不对应、不校验**（§1.3）。世界里同时存在两套
   「渡劫失败」的记录，口径不同：疤是已发生的、可能在纪元之前；本层是窗口内
   模拟的。读者若问「为什么疤的数量和台账对不上」，答案是设定里本来就没说
   一一对应。

5. **`kTribulationFailChance = 0.3` 是标定值，不是推导值**。换一个数，劫损的
   绝对量同比例平移，但**三类偏差的相对结构不变**（口径差压过另两类这一条
   对 0.15~0.5 全区间都成立）。

6. **候选池 6~25 处时事件很少**，小池子可能全程零失败 → 该种子的劫损为 0。
   0.3 把「6 处全成功」的概率压到 12%，但压不到 0。招牌断言的 fixture 要挑
   池子大、确定有失败的种子。

---

## 9. 未决问题

1. **失败的渡劫要不要在 `World` 上留疤？** 本层不写 `World`，所以模拟出的
   失败不会产生新的 `Scar::tribulation`——要留疤就得让模拟层回写，第一刀的
   只读边界和灵气层 §8.5「一写入者不变式会在这条链上被打破」都要让位。
   本设计取**不留疤**（沿用只读边界），把「疤与账本不对应」写进 §8.4。

2. **成功的渡劫要不要留任何痕迹？** 设定说「劫云溃散为灵气回流当地」，净 0，
   但过程里那口灵气是被抽走又还回来的，期间在该地是空的。本设计取**完全不入账**
   ——台账记的是净变化，不是流量。

3. **候选池里 `vein == kNoVein` 的场怎么办？** 那个字段只是「劫心格恰好落在
   哪条脉上」的附注；本设计的归因**一律按圈内脉算**，`vein` 字段只作参考、
   不参与计算。若日后需要「这条脉上渡过劫」的语义，那是另一件事。

4. **失败要不要区分境界（结丹 / 元婴 / 化神 / 炼虚）？** 设定 §5.2 列了四个
   关口，但没有给不同关口的抽走量差异。本设计取**不分**，因为分了就要凭空
   发明四个比例。

---

## 10. 实施计划预告

实现按既有格式另出：`doc/plans/2026-10-23-history-tribulation-implementation.md`。

任务切分：

1. **Task 1**：类型骨架、`Stream::tribulation` 登记、`kTribulationFailChance`、
   把要验劫损的既有测试换到 128² 并加守卫（守卫此刻应当红）
2. **Task 2**：纪元候选池派生 + 「派生 == 完整重生成」的钉子测试
3. **Task 3**：排期、损失计算与年内次序，改写恒等式 1 与 3（排期与损失合一个任务：只有排期没有可观察输出，单独成任务交付不了可测的东西）
4. **Task 4**：第三类偏差与逐宗汇总，招牌断言升级
5. **Task 5**：诊断、导出与 demo
6. **Task 6**：全量测试，**复验越采仍足额认领**
7. **Task 7**：变异战役（§6.5 那张表逐条跑）

任务 1..6 每个都遵循既有节奏：先写失败的测试，再写实现，最后向用户报告并
等待提交指令（**不执行 `git commit`**）。

**Task 6 是硬关卡**：§8.3 那条既有行为风险只有在那里能验出来；Task 7
接着在绿色基线上做变异战役。

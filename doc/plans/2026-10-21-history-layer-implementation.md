# 时序层实施计划：灵脉产权与开采台账

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给既有 worldgen 模块追加一个**时序层**——把已经生成好的世界往前推演
六百年，产出一本「灵脉产权与开采台账」，并算出老配额与当代承载力之间的偏差。
台账是设定 §10.1「配额之争」那场政治冲突的可复算数字。

**Architecture:** 时序层是一个**只读**模块：读 worldgen 的终态，不回写 `World`
的任何字段，也不挂在 `World` 上。它由 `build_ledger(const World&)` 独立构造，
产物是一个 `Ledger` 值对象（脉级账、事件账、宗级账、偏差分解）。

推演分两遍：第一遍只算正常开采，得到每条脉的余额轨迹；第二遍用第一遍的轨迹
定出「越采能落在哪一年」，再跑一遍完整的（正常 + 越采）推演。两遍各 614 年 ×
约 50 条脉，成本可忽略（设计 §8.6）。

**随机性只有一处**：越采年的采样。持有格局、配额、潮位全部确定。

**Tech Stack:** C++23、clang++ 23.1.1、MSVC STL 14.51、CMake 4.4、
Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-21-history-layer-design.md`
（本文的全部语义依据；冲突时以设计文档为准，并回报差异——
本文的「与设计文档的差异与补齐」一节列出了 8 处必须先解决的口径问题）

**既有设计文档：** `doc/2026-10-20-worldgen-spirit-layer-design.md`（灵气层）、
`doc/2026-10-19-worldgen-design.md`（既有十阶段）

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
  改动全部留在工作树里，由用户决定何时、怎么提交。
- 输出文件（台账文本、统计）写入 `agent-temp/`，该目录已被 gitignore
- 随机性一律走 `Pcg32`，禁止使用 `<random>` 的 distribution
- 每个新随机流必须同时登记进 `Stream` 枚举与 `kStreamIds` 数组；
  `static_assert(stream_ids_are_distinct())` 会拦住重复流号
- 任何读 `std::uint8_t` 网格当数值的算术，一律显式转成整数再算，
  不得依赖隐式提升
- **时序层只读 worldgen**：不得写 `World` 的任何字段，不得改 `WorldParams`，
  不得让 `world` 依赖 `history`（依赖方向只能是 `history → world`）
- **台账内的全部历史量用整数或定点**：推演过程中不出现浮点累加。
  潮位系数 `level / 255` 这类除法必须用定点余数累加，否则小数部分会被
  截断成 0（见「与设计文档的差异与补齐」第 2 条）

构建与测试命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/history_tests.exe
```

只跑单个测试文件不需要额外手段：测试框架靠静态注册，删掉 CMakeLists 里的
其余条目即可。正常做法是全部构建。

---

## 文件总览

本计划创建下列文件。文件职责单一，遵循「一件事一个文件」。

| 文件 | 职责 |
|---|---|
| `include/gamev2/history/ledger.hpp` | 台账的类型、`build_ledger` 声明、诊断结构 |
| `src/history/ledger.cpp` | 推演：推定、分配、两遍推演、认领、偏差表、文本导出 |
| `tests/test_history_ledger.cpp` | 全部台账测试（设计 §6 的 13 条 + 补齐项） |

本计划修改下列文件：

| 文件 | 改动 |
|---|---|
| `include/gamev2/world/enums.hpp` | `Stream` 扩容到 18 条（新增 `ledger = 17`） |
| `include/gamev2/world/spirit_tables.hpp` | 把 `kOverminePenalty` 从 `veins.cpp` 搬进来（见下） |
| `src/world/veins.cpp` | 改用共享的 `kOverminePenalty`（数值不变） |
| `CMakeLists.txt` | 追加 `history` 静态库、`history_tests` 可执行文件、demo 改链 `history` |
| `src/demo/main.cpp` | 新增台账统计输出与 `ledger.txt` 文本导出 |

**只读边界**：`history` 依赖 `worldgen`（读它的终态），反向不依赖。
本计划**不修改** `include/gamev2/world/` 下任何既有头文件的**行为**，
只做两件事：

1. 在 `enums.hpp` 的流号登记表里追加一条流——那是既有代码自己写明的规矩
   （`enums.hpp` 注释原文：「新增一条流必须同改」）。
2. 把 `kOverminePenalty` 从 `src/world/veins.cpp` 的匿名命名空间搬到
   `include/gamev2/world/spirit_tables.hpp`。它本来是越采削减比例，
   而**时序层要靠它反推 8300 年的承载上限**——两侧各写一份，worldgen
   改了这个数，台账的反推区间会静默错位。`spirit_tables.hpp` 已经是
   这类共享常量的家（`kOverminedChance`、`kDepletedRatio`、
   `kStabilityMined` 都在那儿），搬过去是顺理成章的。
   **数值不变**，worldgen 的行为一字未改。

**每个字段只有一个写入者**（既有不变式，本计划必须守住）：

| 字段 | 唯一写入者 |
|---|---|
| `World` 的全部字段 | worldgen 的 18 个阶段（本计划只读） |
| `Ledger` 的全部字段 | `build_ledger`（时序层不挂在 `World` 上，见设计 §1.2） |

---

## 与设计文档的差异与补齐

设计文档 §3.4 的推演公式在**字面实现**下会退化：实测 256×256 世界里，龙脉的
8300 年有效流速是 905/年，而它的全部承载上限只有 737——**当年就采空**。
全部 50 条脉平均在 1 年内采空。届时六百年的推演、脉律配额、枯竭认领、
偏差表全部塌成同一句话：所有脉余额为 0。

下面 9 条是实现前必须先解决的口径问题。**第 1 条与第 2 条是硬阻塞**，
不改就写不出能通过的测试。

### 1. 流速与储量不是同一个量纲（硬阻塞）

设计 §3.2 规定 `surveyed_total` 从 `vein.reserve` 反推，而 `surveyed_rate`
取 `ley_nodes[eye.node].flow`。但这两个数在 worldgen 里根本不是一回事：

- `reserve` 是**段上归一化得分的积分**。`veins.cpp` 把每个节点的原始得分
  按全图最大得分归一化到 0..255，再沿段求和。所以它是**无量纲的世界相对
  指数**，量级几十到几百。
- `node.flow` 是地脉的**累计流量**，有量纲，量级几百到几千。

直接相除比较，等于拿「里程表读数」和「油箱容积」当同一个单位。实测
（256×256，seed 1）：

| 脉 | reserve | node.flow | 8300 有效流速 | 采空所需 |
|---|---|---|---|---|
| 龙脉 | 737 | 2100 | 905/年 | 1 年 |
| 主脉 #4 | 422 | 4900 | 2115/年 | 1 年 |
| 支脉 #49 | 18 | 500 | 216/年 | 1 年 |

**补齐**：引入一个显式的换算系数 `kFlowToStock`，把流速折成储量单位。
系数的锚点取「世界总量」而非「单条脉」，否则所有脉会在同一年枯竭，
偏差表就没有层次了。

```
kFlowToStock = Σ surveyed_total / (I × Σ surveyed_rate)
```

其中 `I = Σ_{y=epoch}^{now-1} level_y / 255` 是六百年的潮位积分。
含义一句话说得清：**按实际潮位曲线，全世界的存量恰好在当代耗尽。**

这条锚点没有自由参数：改了 `now`（参数允许把世界推到别的年份），
潮位积分跟着变，系数跟着变，账仍然自洽。实测 256×256 时
`I = 39552`（平均潮位 64.4），`kFlowToStock ≈ 1.068e-3`。

换算之后的结果（256×256，seed 1）：**50 条脉里 38 条在六百年内枯竭，
枯竭年分布在 8345..8912**；10 条脉（大多是富脉）到当代仍有余量。
这正是设定 §8.1「谁被亏空」的分层。

### 2. 年度开采量必须定点累加（硬阻塞）

`kFlowToStock ≈ 1e-3`，于是单年的开采量是个小数：`rate × level/255 × k`
量级在 0.05..3 之间。**直接截断成整数会把绝大多数年份变成 0。**
实测（原型未加定点时）：锚点 H=60 时 39/50 条脉枯竭，H=80 突降到 6/50，
H≥100 全部为 0——一个由截断造成的断崖，不是真实的分布。

**补齐**：每个脉维护一个定点余数 `remainder`（单位 1/(255 × 2^16)），
逐年累加：

```
num     = rate × level × kNum
total   = num + remainder
remainder = total % (255 × 2^16)
eff     = total / (255 × 2^16)        // 储量单位，精度 1/65536
```

被配额卡住时把余数清零（未消费的小数没有意义）。改用定点后，
50 条脉里 35 条自然枯竭、枯竭年铺开在 8370..8883，是平滑分布。

### 3. 反推区间没有落点

设计 §3.2 与 §8.2 反复强调「保留 ±3 的反推区间，不取中值」，
但 `QuotaEntry::surveyed_total` 只有一个标量。

**补齐**：`surveyed_total` 取区间的**下界**（`ceil(reserve × 100/40)`），
另加 `surveyed_total_hi` 存**开区间上界**。非越采脉两者相等
（`surveyed_total_hi == surveyed_total`）。偏差表因此自带 ± 噪声底，
对应设计 §8.2 的「脉级偏差小于 3 时不可断言」。

### 4. 强行抽干会打破守恒恒等式

设计 §3.5 要求「余额在某一年归零，归零年记入 `depleted_year`」，
而 §3.7 第 3 条要求 `reserve − balance = 正常开采量 ≥ 0`。
若在认领年把余额一次性抽干，这笔抽干既不是正常开采也不是越采，
恒等式立刻不成立（原型实测 6 条脉失败）。

**补齐**：不强行抽干。台账的枯竭年**由推演得出**（余额归零的那一年），
worldgen 的 `eye.depleted` 标记改为**交叉校验**：

- worldgen 标了枯眼、台账也归零 → 一致
- worldgen 标了枯眼、台账没归零 → 诊断 `depletion_claim_unbacked`，
  按设计 §5 的规定把 `depleted_year` 设为 `now`
- 台账归零、worldgen 没标 → 诊断 `depletion_unmarked`，按枯竭处理

实测 256×256 会出现 4 条这样的冲突（worldgen 按身份哈希标枯眼，
与经济性无关），这是**如实报告**，不是缺陷。

### 5. 越采年需要可行窗口

设计 §4 规定越采年在 `[8301, 8914]` 里均匀采样，但越采是一笔整量
（`surveyed_total − reserve`，约为存量的 60%），必须**当年余额装得下**。
均匀采样的话，多数越采案会落在脉已经被采空之后。

**补齐**：两遍推演。第一遍只算正常开采，得到每条脉每年的余额轨迹；
越采年从「起点余额仍装得下这笔越采」的年份里采样；
第二遍才是完整推演。

**越采的那一笔在该年的正常开采之前扣除**：这样第一遍算出的
「该年起点的余额」在第二遍里原样成立（第一遍到该年为止没有越采），
可行窗口因此是严格成立的，不是近似。

若 `[8301, now]` 内没有可行年份，越采案落在 8300（脉律核定的当年，
余额必装得下），并报诊断 `overmine_before_epoch`。

### 6. `QuotaEntry::quota_total` 这个字段不存在

设计 §3.3 写「`QuotaEntry::quota_total` 就是 `surveyed_total`」，
但 §2.2 的结构体里只有 `surveyed_total`。§3.3 的 `quota_total(脉)`
是**记号**，不是字段名。

**补齐**：一律用 `surveyed_total`。`SectLedger::quota_total` 是真的字段
（宗级汇总），保持不变。

### 7. `build_ledger` 的签名

设计 §1.2 写 `Ledger build_ledger(const World&)`，§5 又要求诊断量
「由调用方决定要不要打印」——单参版本无处安放诊断。

**补齐**：两个重载。

```cpp
[[nodiscard]] Ledger build_ledger(const World& world);
[[nodiscard]] Ledger build_ledger(const World& world,
                                   LedgerDiagnostics& diagnostics);
```

单参版本内部构造一个丢弃的诊断结构。

### 8. 九幽宫那条脉的登记状态

设计 §2.2 的 `QuotaEntry` 有 `registered` 字段，但 `SectLedger` 没有，
而测试「九幽宫那条脉的开采不计入任何登记宗」需要宗级的登记状态。

**补齐**：`SectLedger` 增加 `bool registered = true;`。

### 9. `deviation_rate` 的公式（设计没给）

设计 §2.2 给了 `std::int64_t deviation_rate;` 并注释「流速偏差，同号
约定」，但**没有给公式**。字面最容易想到的写法是
`quota_rate − current_total`，那是拿**流量**减**存量**，量纲不搭；而且
这个字段在导出的对照表里没有对应列，等于算出来没人看。

**补齐**：定成**流量侧的口径差**，与 `structural_deviation` 同构（同一个
潮位比），作用在流速上：

```
deviation_rate = Σ_{v ∈ 名下} surveyed_rate(v) × (level_8300 − level_当代) / level_8300
```

读作「8300 年按峰值潮位核定的流速，按当代谷底潮位折下来差了多少」。
正 = 核定流速高于当代潮位能支撑的流速。这样 `quota_rate` 也有了唯一的
用处，不会是一列悬空的数字。

---

## 模型要点

以下是把设计文档的规则收敛成可实现规格后的完整模型。实现按此执行。

### 纪元与跨度

| 名称 | 值 | 来源 |
|---|---|---|
| `epoch` | 8300 | 设定 §6.5：上次重勘在灵历 8300 年前后 |
| `now` | `world.params().spirit_year` | 默认 8914 |

推演年份区间取 `[epoch, now)`，即 8300..8913 共 **614 年**。
8300 是快照瞬间（不发生开采），8914 是观测瞬间。
`now < epoch` 时台账为空（设计 §5）。

### 输入

只读下列字段（设计 §3.1）：

| 来源 | 字段 | 用途 |
|---|---|---|
| `world.veins` | `id` `rank` `reserve` `overmined` `eye` | 身份、品阶、承载上限、越采痕迹 |
| `world.eyes` | `vein` `node` `flow` `depleted` | 灵眼的流速与枯竭痕迹 |
| `world.ley_nodes` | `flow` | 重建枯眼**枯竭前**的流速 |
| `world.vein_id` / `world.land_type` | 网格 | 判定寒江剑府的阴脉 |
| `world.params()` | `spirit_year` `seed` | 当代纪元与抽样种子 |

**不用** `land_type` 之外的地貌字段、`scar_kind`、`domain_grade`：
第一刀不判地貌，不认疤痕（设计 §3.1）。

### 8300 快照推定

```
非越采脉：  surveyed_total    = reserve
            surveyed_total_hi = surveyed_total        // 无反推区间
越采脉：    surveyed_total    = ceil(reserve × 100 / 40)
            surveyed_total_hi = ceil((reserve + 1) × 100 / 40)
            overmine_total    = surveyed_total − reserve

surveyed_rate = eyes[vein.eye].flow > 0
                    ? eyes[vein.eye].flow
                    : ley_nodes[eyes[vein.eye].node].flow
```

**`surveyed_total_hi` 是精确的开区间上界**，不是「固定 +3」。
`reserve = floor(original × 40/100)` 意味着
`2.5·reserve ≤ original < 2.5·reserve + 2.5`，
所以候选原值落在 `[ceil(2.5·reserve), ceil(2.5·reserve + 2.5))`，
开区间上界就是 `ceil(2.5·(reserve + 1))`。
区间的**宽度是 2 或 3**（取决于 `reserve` 的奇偶），3 是它的上确界，
不是一个固定值——固定 +3 会在某些 `reserve` 上多报一格，
让一个自称「上界」的字段说谎。

设计文档 §3.2 写的是「`+3`」并断言「原值有三个候选」，
那是对区间宽度的粗略表述；本实现取精确上界，宽度仍然 ≤ 3，
不与设计的意图冲突。

**台账的一切算术以 `surveyed_total`（保守下界）为基数**，
`surveyed_total_hi` 只用来报出反推区间，**不参与任何算术**。
越采脉的真实 8300 配额落在 `(surveyed_total, surveyed_total_hi)` 之间，
严格大于 `surveyed_total`；若哪一步改用区间内的真值去做余额扣减，
`extracted` 就会超过 `surveyed_total`，无符号的 `balance` 立刻回绕。
余额、越采量、封顶，全部以 `surveyed_total` 为准。

枯眼的 `eye.flow` 被 `mark_depleted_eyes()` 置零，原始吞吐仍在
`ley_nodes[eye.node].flow` 里——这正是要重建的东西（设计 §3.2）。
实测非枯眼脉的 `eye.flow == ley_nodes[eye.node].flow`，两条路径同值。

### 持有格局（设计 §3.3）

持有格局是**设定给定的**，不是推演出来的。规则是确定性的：

1. 龙脉（`rank == dragon`，恒 1 条）→ 太玄宗。
2. 主脉按 `reserve` 降序排（同值按 `id` 升序），取前九条：
   第 1、2、3 条 → 太玄宗；第 4、5 条 → 丹霞谷；第 6 条 → 寒江剑府；
   第 7 条 → 万兽山；第 8 条 → 青囊谷；第 9 条 → 九幽宫（`registered = false`）。
3. **寒江剑府的阴脉**：判定口径是「该脉占的格子里有没有
   `land_type == LandType::yin` 的」。若第 6 条不是阴地，在主脉里另找
   一条阴地脉给寒江；此时**与那条脉的原持有者对调**（不是留一个空缺——
   测试「主脉无遗漏无重复」不允许空缺）。找不到阴地脉则按序取第 6 条，
   并报诊断 `missing_yin_vein`。
4. 支脉（`rank == branch`）→ `unowned`（散修与凡人共享池）。
5. 荒脉（`rank == barren`）无灵眼、不可采 → 不进台账，只计数。

实测 256×256：主脉恰好 9 条，分配为太玄 3、丹霞 2、寒江 1、万兽 1、
青囊 1、九幽 1，与设定 §8.1 精确吻合。

### 逐年推进

```
kNum = Σ surveyed_total × 2^16 × 255 / (I × Σ surveyed_rate)
I    = Σ_{y=epoch}^{now-1} compute_spirit_tide(y).level

for y in [epoch, now):
    level = compute_spirit_tide(y).level
    for each vein v (未枯竭者):
        num      = v.rate × level × kNum
        total    = num + v.remainder
        v.remainder = total % (255 × 2^16)
        eff      = total / (255 × 2^16)
        quota_left = v.surveyed_total − v.extracted
        take = (eff >= quota_left) ? quota_left : eff
        if (eff >= quota_left) { v.remainder = 0; }   // 被配额卡住
        v.extracted += take
        v.balance = v.surveyed_total − v.extracted
        记一笔事件 (vein, y, take, over_quota = false)
```

**配额的作用**：正常开采被 `surveyed_total − extracted` 卡住——采满了就停
（设定 §6.5「采至配额上限须停采」）。

**潮位的作用**：8300 年恰好是上一轮灵潮的峰值年（`level = 110`，
`peak = 110`），8914 年是谷底期第 113 年（`level = 33`）。
**脉律的基数是在天下灵气最充沛的一年编的**——这个结构性偏差在没有人
偷采的情况下就已经存在，是设定 §8.2「真问题」的物理来源。

### 越采与枯竭的认领

**第一遍**（只算正常开采）产出每条脉每年的余额轨迹 `balance[t]`
（t = 0..613，年末余额；`balance[−1] := surveyed_total`）。

**越采年采样**（仅越采脉）：

```
起点余额装得下的年份 t：balance[t−1] ≥ overmine_total
可行窗口 = [1, last_ok]，其中 last_ok 是满足条件的最大 t ≥ 1
o = epoch + 1 + Pcg32(stage_seed(stage_seed(seed, Stream::ledger), vein_id))
                 .below(last_ok)
若窗口为空 → o = epoch（8300），报诊断 overmine_before_epoch
```

`vein_id` 作 salt 的理由见设计 §4：灵脉的身份是 `id` 与名次无关，
同一条脉在任何世界里都得到同一个抽取结果。

**第二遍**（完整推演）在第一遍的基础上插入越采的那一笔：

```
for y in [epoch, now):
    for each vein v:
        if y == v.overmine_year:
            over = min(v.balance, v.overmine_total)   // 该年起点的余额
            v.extracted += over
            记一笔事件 (vein, y, over, over_quota = true)
        正常开采（同一套定点累加）
```

越采**在该年的正常开采之前**扣除，保证第一遍的可行窗口在第二遍里原样成立。

**枯竭认领**：台账的枯竭年是推演得出（`balance` 归零那年）。
worldgen 的 `eye.depleted` 作交叉校验，处理方式见「与设计文档的差异与补齐」
第 4 条。

### 偏差表（设计 §3.6）

三层：

```
第一层 · 脉级   deviation(v) = surveyed_total(v) − balance(v)
第二层 · 宗级   deviation_total = Σ_{v ∈ 名下} deviation(v)
第三层 · 口径差
    结构性偏差 = Σ surveyed_total(v) × (1 − level_now / level_epoch)
    越采偏差   = Σ overmined_amount(v)
```

第一层是**口径差**——8300 按峰值口径编、8914 按谷底口径看，与谁都没偷采无关。
第二层是**人为差**——谁真的多采了。

**注意**：结构性偏差与越采偏差**不是**实际偏差的加法分解。
实际偏差 `Σ (surveyed_total − balance) = 正常开采总量 + 越采总量`，
与前两者是不同口径的三个数。台账三个都报，不把口径差当成"亏空"。

第三层区分了三种立场（设计 §3.6 的表）：

| 立场 | 口径差 | 越采差 | 诉求 |
|---|---|---|---|
| 正派守律派 | 承认 | 追究 | 维持脉律，严惩越采 |
| 太玄宗（重分派） | 主张按当代承载力重分 | 自己是否越采由账说话 | 重新分配 |
| 魔修（取尽派） | 无所谓 | 无所谓 | 抽干炼血 |

实测 256×256：口径差 8853、越采差 2842，**口径差占 75%**。
这正是「老配额不公是结构性的」这条政治事实的可复现数字。

### 未登记开采

九幽宫那条脉 `registered = false`：它的开采量进
`Ledger::unregistered_extracted`，**不进任何登记宗的汇总**。
散修盗采（私采点）属后续纵切，第一刀不建模（设计 §8.5）。

### 守恒对账（设计 §3.7）

三条恒等式，全部可断言：

1. **逐年**：任何时刻 `balance ≥ 0`，且
   `balance(v, y) = balance(v, y−1) − 当年开采`。
2. **跨层**：`balance(v, now) = surveyed_total(v) − extracted(v)`。
3. **与 worldgen 对账**：`reserve(v) − balance(v, now) = 正常开采量 ≥ 0`。
   越采脉的 `reserve` 已被 worldgen 削过 40%，所以这条恒等式对越采脉与
   正常脉都成立——它验证的是**台账没有把越采重复计一次**。

第 3 条恒等式**恒真**：因为 `overmine_total = surveyed_total − reserve`
是恒等式，于是

```
balance = surveyed_total − extracted
        = surveyed_total − (正常开采 + 越采)
        = reserve − 正常开采
```

越采被足额认领是关键前提：任何一笔越采没认领全，这条就不成立，
所以它是「越采有没有被记漏」的那道哨针。

---

### Task 1: 类型骨架、诊断结构与 CMake 目标

**Files:**
- Create: `include/gamev2/history/ledger.hpp`
- Create: `src/history/ledger.cpp`
- Create: `tests/test_history_ledger.cpp`
- Modify: `include/gamev2/world/enums.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `gamev2::world::World`、`VeinRank`、`WorldParams`、`Stream`
- Produces:
  - `enum class gamev2::history::Sect : std::uint8_t`
    —— 取值 `taixuan, daxia, hanjiang, wanshou, qiangnang, tianji, jiuyou,
    unowned, count`（顺序与设计 §2.2 一致，索引即宗门编号）
  - `struct gamev2::history::QuotaEntry`
    —— 成员 `vein, holder, rank, surveyed_total, surveyed_total_hi,
    surveyed_rate, extracted, overmined_amount, balance, depleted_year,
    registered`
  - `struct gamev2::history::ExtractionEvent`
    —— 成员 `vein, year, amount, over_quota`
  - `struct gamev2::history::SectLedger`
    —— 成员 `sect, quota_total, quota_rate, current_total, deviation_total,
    deviation_rate, overmined_amount, registered`
  - `struct gamev2::history::Ledger`
    —— 成员 `epoch, now, veins, events, sects, unregistered_extracted,
    structural_deviation, overmining_deviation`
  - `struct gamev2::history::LedgerDiagnostics`
    —— 成员 `missing_yin_vein, short_main_veins, depletion_claim_unbacked,
    depletion_unmarked, overmine_before_epoch, barren_veins`
  - `gamev2::history::build_ledger(const World&)` 与
    `build_ledger(const World&, LedgerDiagnostics&)`（均 `[[nodiscard]]`）
  - `Stream::ledger = 17`（`kStreamIds` 同步扩容到 18）

- [ ] **Step 1: 写失败的测试**

创建 `tests/test_history_ledger.cpp`：

```cpp
#include "gamev2/history/ledger.hpp"
#include "test_framework.hpp"

#include <cstddef>

namespace {

using gamev2::history::Ledger;
using gamev2::history::LedgerDiagnostics;
using gamev2::history::Sect;
using gamev2::world::World;
using gamev2::world::WorldParams;

TEST(sect_count_matches_the_setting)
{
    // 七宗 + 无主池。少一个就意味着有一宗在台账里无处安放。
    CHECK_EQ(static_cast<std::size_t>(Sect::count), 8u);
    CHECK(static_cast<std::size_t>(Sect::unowned) == 7u);
}

TEST(an_empty_world_yields_an_empty_ledger)
{
    // 没有灵脉的世界不是错误：返回空台账，跨度照记。
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 2;
    const World w(p);
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.veins.empty());
    CHECK(ledger.events.empty());
    CHECK(ledger.sects.empty());
    CHECK_EQ(ledger.epoch, 8300u);
    CHECK_EQ(ledger.now, p.spirit_year);
    CHECK_EQ(ledger.unregistered_extracted, 0u);
}

TEST(a_year_before_the_epoch_yields_an_empty_ledger)
{
    // now < epoch：跨度为负，台账为空（设计 §5）。
    //
    // fixture 必须是一个**有灵脉**的世界：拿零灵脉的世界来测，
    // 这条断言就与年份无关地恒真，等于没测。所以用 64x64。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    p.spirit_year = 8000;
    // 必须走 generate_world：World 构造器只分配网格，脉是 generate_world
    // 生成的。拿构造器造世界，ledger.veins.empty() 恒真，这条又白测。
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.veins.empty());
    CHECK(ledger.events.empty());
    CHECK_EQ(ledger.epoch, 8300u);
    CHECK_EQ(ledger.now, 8000u);
}

TEST(a_year_after_the_epoch_yields_a_populated_ledger)
{
    // 上一条的反面：同一个有灵脉的世界，年份正常时台账必须有内容。
    // 两条放在一起，「now < epoch 让台账变空」才是被测到的行为，
    // 而不是因为世界本身没有灵脉。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    // 同上：World 构造器不产脉，必须走 generate_world。
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.veins.empty());
    CHECK_EQ(ledger.now, 8914u);
}

TEST(diagnostics_are_optional)
{
    // 单参版本必须存在且不崩；双参版本把诊断填出来。
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 2;
    const World w(p);
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(ledger.veins.empty());
    CHECK_EQ(diagnostics.barren_veins, 0u);
}

}  // namespace
```

- [ ] **Step 2: 运行测试确认失败**

在 `CMakeLists.txt` 的 `WORLDGEN_TEST_SOURCES` **之后**追加：

```cmake
set(HISTORY_SOURCES
    src/history/ledger.cpp
)
add_library(history STATIC ${HISTORY_SOURCES})
target_include_directories(history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(history PUBLIC worldgen)

set(HISTORY_TEST_SOURCES
    tests/main.cpp
    tests/test_history_ledger.cpp
)
add_executable(history_tests ${HISTORY_TEST_SOURCES})
target_link_libraries(history_tests PRIVATE history)
```

然后：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Expected: 编译失败，报 `gamev2/history/ledger.hpp` 不存在。

- [ ] **Step 3: 写 `ledger.hpp`**

创建 `include/gamev2/history/ledger.hpp`：

```cpp
/// @file ledger.hpp
/// @brief 时序层：灵脉产权与开采台账。
///
/// 台账是「一串有署名的增减」而不是「某一时刻的状态」：老配额为什么
/// 不公，答案不在终态里，在六百年里一笔笔累积出来的差。
///
/// 本模块只读 worldgen 的终态，不回写 `World` 的任何字段，也不挂在
/// `World` 上——历史是一次性推演的结果，与空间无关。
#pragma once

#include "gamev2/world/spirit.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::history {

/// 七宗。顺序固定：与设定 8.1 的表格一致，索引即宗门编号。
enum class Sect : std::uint8_t
{
    taixuan,    ///< 太玄宗：龙脉 + 三条主脉，执掌盟约
    daxia,      ///< 丹霞谷：两条主脉
    hanjiang,   ///< 寒江剑府：一条阴脉
    wanshou,    ///< 万兽山：一条主脉
    qiangnang,  ///< 青囊谷：一条主脉
    tianji,     ///< 天机阁：无地，执历算
    jiuyou,     ///< 九幽宫：一条主脉，不登记
    unowned,    ///< 无主：散修与凡人共享的支脉
    count,
};

/// 一条脉的账。
struct QuotaEntry
{
    std::uint32_t vein = 0;
    Sect holder = Sect::unowned;
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;

    /// 8300 年《脉律》核定的总量配额。越采脉取反推区间的下界。
    std::uint32_t surveyed_total = 0;
    /// 反推区间的开区间上界。非越采脉等于 surveyed_total。
    std::uint32_t surveyed_total_hi = 0;
    /// 8300 年核定的安全流速。
    std::uint32_t surveyed_rate = 0;

    /// 8300→当代累计开采量。
    std::uint32_t extracted = 0;
    /// 其中越采的部分。
    std::uint32_t overmined_amount = 0;
    /// 当代余额 = surveyed_total − extracted。
    ///
    /// 无符号成立的前提是 extracted ≤ surveyed_total：越采那一笔按当年
    /// 余额封顶，所以余额永远不会为负。真正的负偏差在宗级
    /// SectLedger::deviation_total 上（int64）。
    std::uint32_t balance = 0;

    /// 归零年。0 表示未枯竭。
    std::uint32_t depleted_year = 0;
    /// 该脉的脉律配额是否登记在册。九幽宫那条不登记。
    bool registered = true;
};

/// 一笔开采。
struct ExtractionEvent
{
    std::uint32_t vein = 0;
    std::uint32_t year = 0;
    std::uint32_t amount = 0;
    bool over_quota = false;
};

/// 一宗的汇总账。
struct SectLedger
{
    Sect sect = Sect::unowned;
    /// 名下脉的 8300 年配额总和。
    std::uint32_t quota_total = 0;
    /// 名下脉的 8300 年流速配额总和。
    std::uint32_t quota_rate = 0;
    /// 名下脉的当代余额总和。
    std::uint32_t current_total = 0;
    /// 配额 − 当代余额。正 = 还有余量，负 = 已被采穿。
    std::int64_t deviation_total = 0;
    /// 流速偏差，同号约定。
    std::int64_t deviation_rate = 0;
    /// 累计越采量。
    std::uint32_t overmined_amount = 0;
    /// 该宗的脉律配额是否登记在册。九幽宫为 false。
    bool registered = true;
};

/// 整本台账。
struct Ledger
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    std::vector<QuotaEntry> veins;
    std::vector<ExtractionEvent> events;
    std::vector<SectLedger> sects;
    /// 未登记开采的总量（九幽宫那条脉）。
    std::uint32_t unregistered_extracted = 0;
    /// 口径差：按 8300 峰值口径核定、按当代谷底口径衡量的差额。
    std::int64_t structural_deviation = 0;
    /// 人为差：越采总量。
    std::int64_t overmining_deviation = 0;
};

/// 台账推演的诊断量。退化不是错误，由这里报出。
struct LedgerDiagnostics
{
    /// 寒江剑府没拿到阴脉：主脉够六条（寒江能分到一条主脉），
    /// 但里面没有阴地。主脉不足六条时寒江连脉都分不到，
    /// 那时由 short_main_veins 报。
    std::uint32_t missing_yin_vein = 0;
    /// 主脉不足九条，差几条报几条。
    std::uint32_t short_main_veins = 0;
    /// worldgen 标了枯眼但推演中余额没归零。
    std::uint32_t depletion_claim_unbacked = 0;
    /// 推演中余额归零但 worldgen 没标枯竭。
    std::uint32_t depletion_unmarked = 0;
    /// 越采案早于脉律核定，只能落在 8300。
    std::uint32_t overmine_before_epoch = 0;
    /// 荒脉数量：不进台账，只计数。
    std::uint32_t barren_veins = 0;
};

/// 由 worldgen 的终态推演出灵脉台账。
///
/// 只读 world，不回写它的任何字段。退化不是错误：退化量由
/// LedgerDiagnostics 报出，由调用方决定要不要打印。
[[nodiscard]] Ledger build_ledger(const gamev2::world::World& world);

/// 同上，但把诊断量一并填出。
[[nodiscard]] Ledger build_ledger(const gamev2::world::World& world,
                                   LedgerDiagnostics& diagnostics);

}  // namespace gamev2::history
```

**不要写 `#include "gamev2/world/spirit.hpp"` 之外的东西**：
`VeinRank` 在 `spirit.hpp` 里，`World` 在 `world.hpp` 里。
`spirit.hpp` 已经 include 了 `enums.hpp`，不要重复引。
`<filesystem>` 也不要在本任务引——它只服务于 `export_ledger`，那是 Task 7 的事。

**`export_ledger` 的声明不在本任务**：声明与定义必须同批次落地，
否则公共头文件里会留下一个只有签名、没有实体的符号——静态库不引用
它就不报错，问题推迟到第一个调用方身上，链接错误离根因很远。
声明与定义一起在 Task 7 落地。

- [ ] **Step 4: 在 `enums.hpp` 登记新流**

在 `Stream` 枚举末尾追加：

```cpp
    /// 地貌判定阶段不取随机数，编号先占住。
    land = 16,
    /// 时序层的越采年抽样。
    ledger = 17,
```

并把 `kStreamIds` 的数组长度从 17 改成 18，末尾追加：

```cpp
    static_cast<std::uint32_t>(Stream::ledger),
```

`static_assert(stream_ids_are_distinct())` 会守住唯一性。

- [ ] **Step 5: 写 `src/history/ledger.cpp` 的空壳**

```cpp
/// @file ledger.cpp
/// @brief 时序层的推演实现。
#include "gamev2/history/ledger.hpp"

namespace gamev2::history {
namespace {

/// 《脉律》核定的纪元：灵历 8300 年。脉律只有这一版是既定的。
///
/// 不放进公共头文件：它是本模块的内部纪元常量，调用方只该从
/// Ledger::epoch 读它。
constexpr std::uint32_t kQuotaEpoch = 8300;

}  // namespace

Ledger build_ledger(const gamev2::world::World& world)
{
    LedgerDiagnostics ignored;
    return build_ledger(world, ignored);
}

Ledger build_ledger(const gamev2::world::World& world,
                    LedgerDiagnostics& diagnostics)
{
    (void)diagnostics;
    Ledger ledger;
    ledger.epoch = kQuotaEpoch;
    ledger.now = world.params().spirit_year;
    return ledger;
}

}  // namespace gamev2::history
```

`kQuotaEpoch` 定义在 `.cpp` 的匿名命名空间里（`8300`），
不要放进公共头文件——它是本模块的内部纪元常量。

- [ ] **Step 6: 构建并运行测试**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: 全部测试通过。

- [ ] **Step 7: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add include/gamev2/history/ledger.hpp
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
git add include/gamev2/world/enums.hpp
git add CMakeLists.txt
```

---

### Task 2: 8300 快照的推定

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `tests/test_history_ledger.cpp`
- Modify: `include/gamev2/world/spirit_tables.hpp`
- Modify: `src/world/veins.cpp`

**Interfaces:**
- Consumes: `World::veins`、`World::eyes`、`World::ley_nodes`、
  `gamev2::world::kOverminePenalty`（本任务搬到 `spirit_tables.hpp`）
- Produces: `QuotaEntry::surveyed_total`、`surveyed_total_hi`、`surveyed_rate`
  （本任务在 `build_ledger` 内部填好这三项，Task 3 起才开始对外可见）

- [ ] **Step 1: 写失败的测试**

测试文件顶部追加：

```cpp
#include "gamev2/world/generate.hpp"
#include <cstdint>
```

`generate_world` 在 `generate.hpp` 里，`ledger.hpp` 不引它。
`<cstdint>` 从本任务起才用得上（Task 1 只用到 `std::size_t`）。

在 `tests/test_history_ledger.cpp` 的匿名命名空间里追加：

```cpp
using gamev2::world::generate_world;
using gamev2::world::VeinRank;

TEST(the_surveyed_total_is_the_reserve_for_an_unmined_vein)
{
    // 64x64 足够快，且必然产出支脉（灵气层按面积缩放支脉数量）。
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
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            ++checked;
            CHECK_EQ(entry.surveyed_total, vein.reserve);
            // 非越采脉没有反推区间。
            CHECK_EQ(entry.surveyed_total_hi, entry.surveyed_total);
        }
    }
    // 守卫：fixture 一旦不再产出未越采的脉，上面两条断言就整条空跑。
    CHECK(checked > 0);
}

TEST(an_overmined_vein_reverses_the_forty_percent_penalty)
{
    // fixture 选 128x128/seed 26：实测台账里有 4 条越采脉，其中 2 条的
    // reserve 是奇数。奇数 reserve 才是「固定 +3」与精确上界分道扬镳的
    // 地方（区间宽度是 2 还是 3 取决于奇偶），选偶数 reserve 的种子
    // 这条断言就白写了。（世界里有 8 条越采脉，另 4 条是荒脉，不进台账。）
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            continue;
        }
        ++checked;
        // 反推区间的下界：reserve = floor(original * 40/100)
        // ⟹ original >= ceil(reserve * 100/40)。
        const std::uint32_t lo = (vein.reserve * 100u + 39u) / 40u;
        CHECK_EQ(entry.surveyed_total, lo);
        // 开区间上界：候选原值落在
        // [ceil(2.5·reserve), ceil(2.5·(reserve+1)))，上界取后者。
        const std::uint32_t hi = ((vein.reserve + 1u) * 100u + 39u) / 40u;
        CHECK_EQ(entry.surveyed_total_hi, hi);
        // 上界是「紧」的：hi−1 仍是合法原值，hi 已经越界。
        // 断言作用在实现字段上，不是局部算出来的 hi——否则改了公式
        // 这两条会跟着一起漂移，失去独立性。
        CHECK_EQ((entry.surveyed_total_hi - 1u) * 40u / 100u, vein.reserve);
        CHECK((entry.surveyed_total_hi * 40u / 100u) != vein.reserve);
        // 区间的宽度是 2 或 3（取决于 reserve 的奇偶），3 是上确界。
        CHECK(entry.surveyed_total_hi > entry.surveyed_total);
        CHECK(entry.surveyed_total_hi - entry.surveyed_total <= 3u);
        // 越采量 = 8300 承载上限 − 当代物理储量。
        CHECK_EQ(entry.overmined_amount,
                 entry.surveyed_total - vein.reserve);
    }
    // 守卫：这条断言一旦整条空跑，说明 fixture 不再产出越采脉。
    CHECK(checked > 0);
}

TEST(barren_veins_stay_out_of_the_ledger)
{
    // 荒脉没有灵眼、不可采，不进台账，只计数。
    // fixture 选 128x128/seed 26：实测世界里有 4 条荒脉。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK(diagnostics.barren_veins > 0u);
    // 台账里的脉数 = 世界里的脉数 − 荒脉数。一条不多，一条不少。
    CHECK_EQ(ledger.veins.size(),
             w.veins.size() - diagnostics.barren_veins);
    for (const auto& entry : ledger.veins)
    {
        CHECK(w.veins[entry.vein].rank != VeinRank::barren);
    }
}

TEST(a_depleted_eye_still_reports_its_original_throughput)
{
    // 枯眼的 eye.flow 被置零，但原始吞吐还在 ley_nodes 里。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 21;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    std::size_t alive = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        const auto& eye = w.eyes[vein.eye];
        if (!eye.depleted)
        {
            // 未枯竭的眼：流速就是灵眼自己的 flow，不用回查节点。
            ++alive;
            CHECK_EQ(entry.surveyed_rate, eye.flow);
            continue;
        }
        ++checked;
        CHECK_EQ(eye.flow, 0u);
        CHECK_EQ(entry.surveyed_rate, w.ley_nodes[eye.node].flow);
    }
    CHECK(checked > 0);
    // 未枯竭的那一支也要真的跑到，否则上面那条断言是空的。
    CHECK(alive > 0);
}

TEST(the_balance_starts_at_the_surveyed_total)
{
    // Task 4 的守恒恒等式从这条起算：余额的初始值就是封顶基数，
    // 而且是保守下界 surveyed_total，不是区间上界。
    //
    // fixture 必须含越采脉才有意义：非越采脉两者相等，断言恒真。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t overmined = 0;
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance, entry.surveyed_total);
        if (w.veins[entry.vein].overmined)
        {
            ++overmined;
            // 越采脉的基数是反推出来的，余额必须跟着基数走，
            // 不能是当代的物理储量——否则越采的痕迹就凭空消失了。
            CHECK(entry.balance > w.veins[entry.vein].reserve);
        }
    }
    CHECK(overmined > 0);
}

TEST(the_diagnostics_are_reset_between_runs)
{
    // 诊断量是「本次推演的结果」，不是累计值：调用方复用同一个结构时，
    // 不该看到上一次的计数叠上来。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;

    LedgerDiagnostics diagnostics;
    const Ledger first = gamev2::history::build_ledger(w, diagnostics);
    const std::uint32_t barren = diagnostics.barren_veins;
    CHECK(barren > 0u);

    const Ledger second = gamev2::history::build_ledger(w, diagnostics);
    // 同一个结构跑两遍，计数不能翻倍。
    CHECK_EQ(diagnostics.barren_veins, barren);
    CHECK_EQ(first.veins.size(), second.veins.size());
}

TEST(the_epoch_year_itself_yields_a_populated_ledger)
{
    // 闸门判的是 now < epoch，不是 now <= epoch。差一格是另一回事：
    // 8300 年是快照瞬间，台账正是从这一年起算，8300 本身必须有内容。
    //
    // 每个年份各生成一次世界：spirit_year 会改潮位，但潮位只被随后的
    // 浓度场阶段读取，灵脉生成不读它——两年的脉集合完全一致，
    // 「台账有没有内容」的差异只能来自年份本身。
    auto world_for_year = [](std::uint32_t year)
    {
        WorldParams p;
        p.width = 64;
        p.height = 64;
        p.z_levels = 3;
        p.seed = 7;
        p.spirit_year = year;
        return generate_world(p);
    };

    const auto before = world_for_year(8299);
    CHECK(before.has_value());
    if (before)
    {
        const Ledger ledger = gamev2::history::build_ledger(*before);
        CHECK(ledger.veins.empty());
        CHECK_EQ(ledger.now, 8299u);
    }

    const auto at_epoch = world_for_year(8300);
    CHECK(at_epoch.has_value());
    if (at_epoch)
    {
        const Ledger ledger = gamev2::history::build_ledger(*at_epoch);
        CHECK(!ledger.veins.empty());
        CHECK_EQ(ledger.now, 8300u);
    }
}
```

- [ ] **Step 2: 运行测试确认失败**

```powershell
cmake --build build
./build/history_tests.exe
```

Expected: 全部失败——`ledger.veins` 还是空的。

- [ ] **Step 3: 把 `kOverminePenalty` 搬到共享头文件**

这个常量现在是 `src/world/veins.cpp` 匿名命名空间里的私有副本，
而时序层要靠它反推 8300 年的承载上限。两侧各写一份，worldgen 改了
这个数，台账的反推区间会**静默错位**——没有编译错误，没有测试失败，
只有一个偏了几点的台账。

`include/gamev2/world/spirit_tables.hpp` 已经是这类共享常量的家
（`kOverminedChance`、`kDepletedRatio`、`kStabilityMined` 都在那儿），
所以搬过去是顺理成章的，**数值不变**。

在 `spirit_tables.hpp` 里 `kStabilityMined` 之后追加：

```cpp
/// 被越采后储量的削减比例（百分比）。
///
/// 搬到这里是因为时序层要靠它反推 8300 年的承载上限：越采脉的
/// reserve = floor(original * (100 - 它) / 100)，反推区间的宽度由它决定。
/// 两侧各写一份的话，worldgen 改了这个数，台账会静默错位。
inline constexpr std::uint32_t kOverminePenalty = 60;
```

然后在 `src/world/veins.cpp` 里删掉匿名命名空间里的那份定义，
改用共享的那一份（`veins.cpp` 已经 include 了 `spirit_tables.hpp`）：

```cpp
// 删掉这一行：
// constexpr std::uint32_t kOverminePenalty = 60;
```

`veins.cpp` 里用到它的地方是 `s.reserve = s.reserve * (100u - kOverminePenalty) / 100u;`
——**那行不用改**，共享常量同名同义。

- [ ] **Step 4: 实现推定**

在 `src/history/ledger.cpp` 的匿名命名空间里加：

```cpp
namespace {

/// 《脉律》核定的纪元：灵历 8300 年。脉律只有这一版是既定的。
constexpr std::uint32_t kQuotaEpoch = 8300;

}  // namespace
```

**没有 `kSurveySlack` 这个常量**：精确上界落地之后，它不再参与任何计算，
留着就是靠 `[[maybe_unused]]` 压警告的死代码。「区间宽度 ≤ 3」这件事
由测试的 `surveyed_total_hi - surveyed_total <= 3u` 断言守住。

**`kOverminePenalty` 不在这里定义了**——它现在在
`include/gamev2/world/spirit_tables.hpp` 里，`ledger.cpp` 引它即可。
两侧共用一个定义，改一处两处都跟着变。

`ledger.cpp` 顶部追加：

```cpp
#include "gamev2/world/spirit_tables.hpp"
```

本模块的命名空间是 `gamev2::history`，非限定名找不到
`gamev2::world` 里的常量，所以要用 using 声明引入：

```cpp
/// 越采削减比例。反推公式的除数由它推导，不写死百分比。
using gamev2::world::kOverminePenalty;
```

然后在 `build_ledger` 里，把可入账的脉（`rank != barren`）收进来：

```cpp
    // 诊断量是「本次推演的结果」，不是累计值：调用方复用同一个结构时
    // 不该看到上一次的计数叠上来。
    diagnostics = LedgerDiagnostics{};

    Ledger ledger;
    ledger.epoch = kQuotaEpoch;
    ledger.now = world.params().spirit_year;

    // 当代早于脉律核定年时台账为空（设计 §5）：没有「从 8300 推到当代」
    // 这件事，硬推出来的账没有意义。
    //
    // 这道闸门必须在入账之前，不能等推演那一步再判——那时脉已经进账了。
    //
    // 副作用：闸门在计数之前返回，所以 now < epoch 时 barren_veins 恒为 0。
    // 这是有意的：台账本身为空，「这个世界的荒脉有多少」不在台账要回答的
    // 问题范围内，诊断量跟着台账一起为空。
    if (ledger.now < ledger.epoch)
    {
        return ledger;
    }

    for (const auto& vein : world.veins)
    {
        if (vein.rank == gamev2::world::VeinRank::barren)
        {
            ++diagnostics.barren_veins;
            continue;
        }
        const auto& eye = world.eyes[vein.eye];

        QuotaEntry entry;
        entry.vein = vein.id;
        entry.rank = vein.rank;
        if (vein.overmined)
        {
            // 反推 8300 年的承载上限。越采脉的 reserve 被削到
            // floor(original * (100 - kOverminePenalty) / 100)，
            // 候选原值落在 [ceil(2.5·reserve), ceil(2.5·(reserve+1)))，
            // 两端都要，除数由 kOverminePenalty 推导而不写死百分比。
            entry.surveyed_total =
                (vein.reserve * 100u + (100u - kOverminePenalty) - 1u)
                / (100u - kOverminePenalty);
            entry.surveyed_total_hi =
                ((vein.reserve + 1u) * 100u + (100u - kOverminePenalty) - 1u)
                / (100u - kOverminePenalty);
            entry.overmined_amount = entry.surveyed_total - vein.reserve;
        }
        else
        {
            entry.surveyed_total = vein.reserve;
            entry.surveyed_total_hi = vein.reserve;
        }
        // 枯眼的 flow 被置零，原始吞吐在 ley_nodes 里。
        entry.surveyed_rate =
            eye.flow > 0 ? eye.flow : world.ley_nodes[eye.node].flow;
        entry.balance = entry.surveyed_total;
        ledger.veins.push_back(entry);
    }
```

**先不要**做后续任务的内容（持有格局、推演、偏差）。本任务只让
`surveyed_total` / `surveyed_total_hi` / `surveyed_rate` / `overmined_amount`
对得上。

**算术基数只有一个**：`surveyed_total` 是保守下界，余额与越采量全部
以它为准。`surveyed_total_hi` 只报区间，不参与算术——越采脉的真实配额
落在 `(surveyed_total, surveyed_total_hi)` 之间，若用真值扣余额，
无符号的 `balance` 会回绕。

**把 Task 1 留下的 `(void)diagnostics;` 删掉**：本任务开始写诊断量了，
那个 cast 变成一句没人看的废话。

**`entry.balance = entry.surveyed_total;` 要留**：余额在推演开始前等于
承载上限，这是 Task 4 的守恒恒等式（`balance == surveyed_total − extracted`）
在 `extracted == 0` 时的取值。

- [ ] **Step 5: 构建并运行测试**

Expected: 新测试通过，`an_empty_world_yields_an_empty_ledger` 等旧测试
仍然通过。

- [ ] **Step 6: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
git add include/gamev2/world/spirit_tables.hpp
git add src/world/veins.cpp
```

---

### Task 3: 持有格局的分配与配额快照

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: `QuotaEntry`、`World::vein_id`、`World::land_type`、
  `World::veins`
- Produces: `QuotaEntry::holder`、`QuotaEntry::registered`、
  `LedgerDiagnostics::missing_yin_vein`、
  `LedgerDiagnostics::short_main_veins`
- **不产生** `Ledger::sects`：宗级汇总要到 Task 6 才聚合（那时余额与
  偏差才算得出来），Task 3 只把持有者标到每条脉上。

- [ ] **Step 1: 写失败的测试**

```cpp
TEST(the_nine_main_veins_are_all_assigned)
{
    // 256x256 才能凑齐九条主脉：灵气层按面积缩放，小图幅凑不齐。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::vector<std::uint32_t> mains;
    std::vector<Sect> holders;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        mains.push_back(entry.vein);
        holders.push_back(entry.holder);
    }
    CHECK_EQ(mains.size(), 9u);

    // 无遗漏无重复：九条主脉归到六宗手里。设定 8.1 里主脉只归六宗
    // （太玄三主、丹霞两主、其余各一主），所以「九条脉九个持有者」
    // 不成立——成立的是「九条脉，六宗持有，且一宗不缺」。
    // Sect 是强类型枚举，没有内建的 operator<，排序得自己给比较器。
    std::sort(holders.begin(), holders.end(),
              [](Sect a, Sect b) {
                  return static_cast<std::uint8_t>(a)
                         < static_cast<std::uint8_t>(b);
              });
    std::vector<Sect> distinct;
    for (const auto& holder : holders)
    {
        if (distinct.empty() || distinct.back() != holder)
        {
            distinct.push_back(holder);
        }
    }
    CHECK_EQ(distinct.size(), 6u);
    // 天机阁无地：无主脉。
    CHECK(std::find(distinct.begin(), distinct.end(), Sect::tianji)
          == distinct.end());
    // 也没有主脉落进无主池：九条脉都有主。
    CHECK(std::find(distinct.begin(), distinct.end(), Sect::unowned)
          == distinct.end());
}

TEST(the_sects_hold_the_setting_six_veins)
{
    // 设定 8.1：太玄 1 龙 + 3 主，丹霞 2 主，寒江/万兽/青囊/九幽各 1 主。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t taixuan_main = 0;
    std::size_t taixuan_dragon = 0;
    std::size_t daxia = 0;
    std::size_t hanjiang = 0;
    std::size_t wanshou = 0;
    std::size_t qiangnang = 0;
    std::size_t jiuyou = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::dragon)
        {
            CHECK(entry.holder == Sect::taixuan);
            ++taixuan_dragon;
            continue;
        }
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        switch (entry.holder)
        {
        case Sect::taixuan: ++taixuan_main; break;
        case Sect::daxia: ++daxia; break;
        case Sect::hanjiang: ++hanjiang; break;
        case Sect::wanshou: ++wanshou; break;
        case Sect::qiangnang: ++qiangnang; break;
        case Sect::jiuyou: ++jiuyou; break;
        default: break;
        }
    }
    CHECK_EQ(taixuan_dragon, 1u);
    CHECK_EQ(taixuan_main, 3u);
    CHECK_EQ(daxia, 2u);
    CHECK_EQ(hanjiang, 1u);
    CHECK_EQ(wanshou, 1u);
    CHECK_EQ(qiangnang, 1u);
    CHECK_EQ(jiuyou, 1u);
}

TEST(branch_veins_belong_to_the_unowned_pool)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 5;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    std::size_t branches = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::branch)
        {
            continue;
        }
        ++branches;
        CHECK(entry.holder == Sect::unowned);
    }
    CHECK(branches > 0);
}

// 这条脉的占格里有没有阴地。
//
// 测试自己算一遍，不复用实现的判据：复用的话实现判错时测试也跟着错。
bool vein_has_yin_cell(const World& w, std::uint32_t vein)
{
    for (std::uint32_t cell = 0; cell < w.vein_id.size(); ++cell)
    {
        if (w.vein_id.at(cell) != vein)
        {
            continue;
        }
        if (w.land_type.at(cell) == gamev2::world::LandType::yin)
        {
            return true;
        }
    }
    return false;
}

TEST(the_hanjiang_vein_is_the_only_yin_main_vein)
{
    // 设定 8.1：寒江剑府握的是「一条阴脉（采出即衰）」。
    //
    // fixture 选 256x256/seed 7：实测九条主脉里只有一条占着阴地，
    // 而它排在第 0 槽（太玄的），寒江原本的槽位不是阴脉——所以
    // 「对调」这条分支必然被走到。
    //
    // 断言不写死脉编号，只断言「九条主脉里唯一的阴脉归寒江」。这样写
    // 既能抓住对调没发生，也能抓住整条 QuotaEntry 被换掉——那会让
    // 寒江拿着一条不阴的脉，而太玄拿着阴脉。
    WorldParams p;
    p.width = 256;
    p.height = 256;
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

    std::size_t yin_mains = 0;
    std::size_t hanjiang_mains = 0;
    std::size_t hanjiang_yin = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        const bool yin = vein_has_yin_cell(w, entry.vein);
        if (yin)
        {
            ++yin_mains;
        }
        if (entry.holder == Sect::hanjiang)
        {
            ++hanjiang_mains;
            if (yin)
            {
                ++hanjiang_yin;
            }
        }
    }
    // fixture 的前提：九条主脉里恰好一条阴脉。
    CHECK_EQ(yin_mains, 1u);
    CHECK_EQ(hanjiang_mains, 1u);
    CHECK_EQ(hanjiang_yin, 1u);
    // 拿到了阴脉，不该再报「缺阴脉」。
    CHECK_EQ(diagnostics.missing_yin_vein, 0u);
    // 对调不改变各宗的脉数：换成谁，那条脉就是谁的。
    std::size_t taixuan_main = 0;
    std::size_t daxia = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        if (entry.holder == Sect::taixuan)
        {
            ++taixuan_main;
        }
        else if (entry.holder == Sect::daxia)
        {
            ++daxia;
        }
    }
    CHECK_EQ(taixuan_main, 3u);
    CHECK_EQ(daxia, 2u);
}

TEST(a_world_without_a_yin_vein_reports_the_missing_yin_vein)
{
    // 256x256/seed 1：九条主脉里一条阴地都没有，寒江拿不到阴脉。
    // 退化不是错误（设计 5），由诊断量报出。
    //
    // 但也不能留空缺：寒江仍然按序分到一条主脉。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 1;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK_EQ(diagnostics.missing_yin_vein, 1u);
    std::size_t hanjiang_mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main && entry.holder == Sect::hanjiang)
        {
            ++hanjiang_mains;
        }
    }
    CHECK_EQ(hanjiang_mains, 1u);
}

TEST(a_small_world_reports_short_main_veins)
{
    // 128x128/seed 5：实测只有两条主脉。分派照做（两条都归太玄），
    // 但差了几条要报出来——持有格局是设定给定的，凑不齐就是退化。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 5;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    // 九条主脉只有两条，缺七条。
    CHECK_EQ(diagnostics.short_main_veins, 7u);
    // 现有的主脉照样分派。
    std::size_t mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main)
        {
            ++mains;
            CHECK(entry.holder != Sect::unowned);
        }
    }
    CHECK_EQ(mains, 2u);
}

TEST(a_reserve_tie_is_broken_by_the_vein_id)
{
    // 同 reserve 的两条脉换个顺序就换一个持有者，所以排序必须全序：
    // reserve 降序，同值按脉编号升序。
    //
    // fixture 选 256x256/seed 32：实测 vein 3 与 vein 4 的 reserve 同为
    // 652，排在 slot 2 与 slot 3——正是太玄与丹霞的分界，所以同值比较
    // 的方向一变，两个持有者就互换。这条 fixture 没有阴脉（实测
    // yin_slots=0），所以阴脉对调不会掩盖 tie-break 的效果。
    //
    // 别的 fixture 测不到这条：绝大多数世界里主脉的 reserve 两两不等，
    // 同值比较那个分支从来没被走到。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 32;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    Sect holder_of_three = Sect::unowned;
    Sect holder_of_four = Sect::unowned;
    std::size_t mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main)
        {
            ++mains;
        }
        if (entry.vein == 3u)
        {
            holder_of_three = entry.holder;
        }
        if (entry.vein == 4u)
        {
            holder_of_four = entry.holder;
        }
    }
    CHECK_EQ(mains, 9u);
    // 编号小的占靠前的槽位：vein 3 在 slot 2（太玄），vein 4 在 slot 3
    // （丹霞）。同值比较反过来写，这两条就互换。
    CHECK(holder_of_three == Sect::taixuan);
    CHECK(holder_of_four == Sect::daxia);
}

TEST(the_jiuyou_vein_is_not_registered)
{
    // 《脉律》册上无名：九幽宫那条脉不登记在册（设定 8.1）。
    //
    // 这条只守「谁不登记」；「登记跟最终持有者走、而不是跟槽位走」由
    // 下面 seed 62 那条守——阴脉不落在第 8 槽时两种写法结果一样。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t jiuyou = 0;
    std::size_t registered = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.holder == Sect::jiuyou)
        {
            ++jiuyou;
            CHECK(!entry.registered);
            continue;
        }
        ++registered;
        CHECK(entry.registered);
    }
    // 守卫：fixture 一旦不再分到九幽宫，上面两条断言就整条空跑。
    CHECK_EQ(jiuyou, 1u);
    CHECK(registered > 0u);
}

TEST(the_unregistered_vein_follows_the_final_holder)
{
    // 登记状态跟最终持有者走，不是跟排序槽位走。
    //
    // fixture 选 256x256/seed 62：实测九条主脉里唯一那条阴脉正好落在
    // 第 8 槽——九幽宫的槽位。这是唯一能区分两种写法的情形：阴脉对调
    // 把第 8 槽的阴脉换给寒江，寒江原来那条落到九幽宫手里。
    //
    // 「按槽位登记」的错误写法会让阴脉那条（现在是寒江的）不登记、
    // 而九幽宫那条（现在是寒江原来的）登记——正好颠倒。
    //
    // 其余 fixture 测不到这条：阴脉不落在第 8 槽时，九幽宫拿的仍然是
    // 排序槽位 8 那条，两种写法结果一样。实测 256x256 扫 seed 1..80，
    // 只有 seed 62 的阴脉落在第 8 槽。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 62;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    std::size_t jiuyou = 0;
    std::size_t hanjiang = 0;
    std::size_t jiuyou_yin = 0;
    std::size_t hanjiang_yin = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        const bool yin = vein_has_yin_cell(w, entry.vein);
        if (entry.holder == Sect::jiuyou)
        {
            ++jiuyou;
            CHECK(!entry.registered);
            if (yin)
            {
                ++jiuyou_yin;
            }
        }
        if (entry.holder == Sect::hanjiang)
        {
            ++hanjiang;
            CHECK(entry.registered);
            if (yin)
            {
                ++hanjiang_yin;
            }
        }
    }
    CHECK_EQ(jiuyou, 1u);
    CHECK_EQ(hanjiang, 1u);
    // 阴脉在寒江手里，不在九幽宫手里。
    CHECK_EQ(jiuyou_yin, 0u);
    CHECK_EQ(hanjiang_yin, 1u);
    // 拿到了阴脉，就不该再报「缺阴脉」。
    CHECK_EQ(diagnostics.missing_yin_vein, 0u);
}
```

**别忘了加 `#include <algorithm>`**（`std::sort` / `std::find`）与
`#include <vector>`。

- [ ] **Step 2: 运行测试确认失败**

Expected: `the_sects_hold_the_setting_six_veins` 与
`the_nine_main_veins_are_all_assigned` 都失败——所有脉的持有者都还是
`Sect::unowned`。三条诊断量与阴脉的测试也失败：持有者没分下去，
`missing_yin_vein` / `short_main_veins` 还是 0。
`branch_veins_belong_to_the_unowned_pool` 应当直接通过：支脉本来就归
无主池，实现前也已经是 `Sect::unowned`（默认值）。

**这七条测试都不是恒真的**——每条都要靠变异测试证明能变红：

| 变异 | 必须变红的测试 |
|---|---|
| `case 3/4` 从 `daxia` 改成 `taixuan` | `the_nine_main_veins_are_all_assigned`、`the_sects_hold_the_setting_six_veins`、`the_hanjiang_vein_is_the_only_yin_main_vein` |
| 阴脉对调改成整条 `QuotaEntry` 交换 | `the_hanjiang_vein_is_the_only_yin_main_vein` |
| 同值比较从 `<` 改成 `>` | `a_reserve_tie_is_broken_by_the_vein_id`（**只有 seed 32 抓得到**） |
| `missing_yin_vein = 1u` 改成 `0u` | `a_world_without_a_yin_vein_reports_the_missing_yin_vein` |
| 去掉 `short_main_veins` 的赋值 | `a_small_world_reports_short_main_veins` |
| 去掉 `entry.registered = false` | `the_jiuyou_vein_is_not_registered` |
| 把登记循环挪到阴脉对调**之前**（等于按槽位登记） | `the_unregistered_vein_follows_the_final_holder`（**只有 seed 62 抓得到**） |

- [ ] **Step 3: 实现分配**

在 `src/history/ledger.cpp` 的匿名命名空间里加：

```cpp
/// 阴脉判定：这条脉占的格子里有没有阴地。
///
/// 设定 8.1 说寒江剑府握的是「一条阴脉（采出即衰）」，即地貌为阴地的
/// 那条。地貌是网格属性，所以要按 vein_id 网格反查这条脉的占格。
bool is_yin_vein(const gamev2::world::World& world,
                 std::uint32_t vein_id)
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

/// 主脉下标按 reserve 降序排。同值按脉编号升序——排序必须全序，否则
/// 同 reserve 的两条脉换个顺序就会换一个持有者。
void sort_main_indices(std::vector<std::size_t>& mains,
                       const std::vector<QuotaEntry>& veins,
                       const gamev2::world::World& world)
{
    std::sort(mains.begin(), mains.end(),
              [&veins, &world](std::size_t a, std::size_t b) {
                  const auto& va = world.veins[veins[a].vein];
                  const auto& vb = world.veins[veins[b].vein];
                  if (va.reserve != vb.reserve)
                  {
                      return va.reserve > vb.reserve;
                  }
                  return veins[a].vein < veins[b].vein;
              });
}
```

**别忘了 `#include <algorithm>`**（`std::sort` / `std::swap`）。

然后在 `build_ledger` 里（推定之后）插入分配逻辑。**主脉存下标而不是
副本**：副本交换完还得写回，而写回时容易连脉编号一起换掉。

```cpp
    // 持有格局是设定给定的，不是推演出来的（设计 8.1）。
    // 太玄宗拿龙脉 + 前三条主脉，其余按序分派。
    std::vector<std::size_t> mains;
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        if (ledger.veins[i].rank == gamev2::world::VeinRank::main)
        {
            mains.push_back(i);
        }
    }
    sort_main_indices(mains, ledger.veins, world);
    if (mains.size() < 9u)
    {
        diagnostics.short_main_veins =
            static_cast<std::uint32_t>(9u - mains.size());
    }

    for (auto& entry : ledger.veins)
    {
        if (entry.rank == gamev2::world::VeinRank::dragon)
        {
            entry.holder = Sect::taixuan;
        }
        else if (entry.rank == gamev2::world::VeinRank::branch)
        {
            entry.holder = Sect::unowned;
        }
    }
    for (std::size_t i = 0; i < mains.size() && i < 9u; ++i)
    {
        // 九个槽位对上六宗：0-2 太玄、3-4 丹霞、5 寒江、6 万兽、
        // 7 青囊、8 九幽（天机阁无地，不分脉）。循环上限是 i < 9u，
        // 所以下面的 case 覆盖了全部可能取值，不需要 default。
        switch (i)
        {
        case 0: case 1: case 2:
            ledger.veins[mains[i]].holder = Sect::taixuan; break;
        case 3: case 4:
            ledger.veins[mains[i]].holder = Sect::daxia; break;
        case 5:
            ledger.veins[mains[i]].holder = Sect::hanjiang; break;
        case 6:
            ledger.veins[mains[i]].holder = Sect::wanshou; break;
        case 7:
            ledger.veins[mains[i]].holder = Sect::qiangnang; break;
        case 8:
            ledger.veins[mains[i]].holder = Sect::jiuyou; break;
        }
    }
```

**阴脉的交换规则**（补齐第 3 条）在分派之后处理：

```cpp
    // 寒江剑府要的是阴脉。第 6 条不是阴地就在主脉里另找一条，
    // 找到则与原持有者对调——对调而不是留空缺：九条主脉必须无遗漏
    // 无重复。
    if (mains.size() >= 6u)
    {
        const std::size_t hanjiang_slot = 5;
        if (!is_yin_vein(world, ledger.veins[mains[hanjiang_slot]].vein))
        {
            std::size_t yin_slot = mains.size();
            for (std::size_t i = 0; i < mains.size(); ++i)
            {
                // 跳过寒江自己的槽位：外层已经证明那条不是阴脉，这里
                // 再挡一次是为了让「找阴脉」这一段自己站得住。
                if (i != hanjiang_slot
                    && is_yin_vein(world, ledger.veins[mains[i]].vein))
                {
                    yin_slot = i;
                    break;
                }
            }
            if (yin_slot < mains.size())
            {
                std::swap(ledger.veins[mains[hanjiang_slot]].holder,
                          ledger.veins[mains[yin_slot]].holder);
            }
            else
            {
                diagnostics.missing_yin_vein = 1u;
            }
        }
    }
```

**只换 `holder`，不换整条 `QuotaEntry`**：整条交换会把脉编号也换掉，
台账的脉编号必须与 worldgen 一一对应。

**登记状态在交换之后定**：九幽宫那条脉不登记（《脉律》册上无名），
而阴脉对调会改变谁持有哪条脉，所以「不登记」要跟着**最终持有者**走，
不能绑在排序的槽位上。

```cpp
    // 九幽宫那条脉不登记在册。放在阴脉对调之后：登记状态跟着最终
    // 持有者走，而不是跟着排序槽位走。
    for (auto& entry : ledger.veins)
    {
        if (entry.holder == Sect::jiuyou)
        {
            entry.registered = false;
        }
    }
```

- [ ] **Step 4: 构建并运行测试**

Expected: 全部通过。

- [ ] **Step 5: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 4: 逐年开采推进与潮位接入

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `tests/test_history_ledger.cpp`
- Modify: `include/gamev2/history/ledger.hpp`（加一个诊断量字段）

**Interfaces:**
- Consumes: `QuotaEntry::surveyed_total`、`surveyed_rate`、
  `gamev2::world::compute_spirit_tide`
- Produces: `QuotaEntry::extracted`、`balance`、`Ledger::events`、
  `LedgerDiagnostics::flow_conversion_collapsed`

**本任务只做正常开采**。越采在 Task 5 接入。

**必须同时改掉 Task 2 的一条测试**：`the_balance_starts_at_the_surveyed_total`
断言 `balance == surveyed_total`，那是**推演之前**的初值。本任务一旦开始
逐年扣减，这条断言就会红。它的本意（余额初值等于封顶基数）要保留，
办法是换一个**不推演**的 fixture：把 `p.spirit_year` 设成 8300。
`now == epoch` 时没有年份可推演，余额停在初值，断言继续成立。
**不要删这条测试，也不要把断言改成 `balance == surveyed_total - extracted`
——那与本任务另一条测试重复，等于丢掉初值这个语义。**

- [ ] **Step 1: 写失败的测试**

```cpp
TEST(balances_never_go_negative)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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
    CHECK(!ledger.veins.empty());
    for (const auto& entry : ledger.veins)
    {
        // balance 是无符号的，「不为负」是类型保证，写出来只是恒真。
        // 真正要守的是两条边界：采量不超基数，余额不超基数。
        CHECK(entry.extracted <= entry.surveyed_total);
        CHECK(entry.balance <= entry.surveyed_total);
    }
}

TEST(final_balance_matches_the_ledger)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance, entry.surveyed_total - entry.extracted);
    }
}

TEST(the_events_account_for_every_extraction)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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

    // 逐脉核对：事件总和 == extracted。
    std::vector<std::uint32_t> sums(ledger.veins.size(), 0u);
    for (const auto& event : ledger.events)
    {
        CHECK(event.amount > 0u);
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        bool found = false;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            if (ledger.veins[i].vein == event.vein)
            {
                sums[i] += event.amount;
                found = true;
                break;
            }
        }
        CHECK(found);
    }
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        CHECK_EQ(sums[i], ledger.veins[i].extracted);
    }
}

TEST(the_ledger_is_deterministic)
{
    // 同 (seed, params) 两次构建，账本逐字段相同。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 2026;
    const auto a = generate_world(p);
    const auto b = generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    const Ledger la = gamev2::history::build_ledger(*a);
    const Ledger lb = gamev2::history::build_ledger(*b);
    CHECK_EQ(la.veins.size(), lb.veins.size());
    for (std::size_t i = 0; i < la.veins.size(); ++i)
    {
        CHECK_EQ(la.veins[i].vein, lb.veins[i].vein);
        CHECK_EQ(la.veins[i].holder, lb.veins[i].holder);
        CHECK_EQ(la.veins[i].surveyed_total, lb.veins[i].surveyed_total);
        CHECK_EQ(la.veins[i].surveyed_rate, lb.veins[i].surveyed_rate);
        CHECK_EQ(la.veins[i].extracted, lb.veins[i].extracted);
        CHECK_EQ(la.veins[i].balance, lb.veins[i].balance);
    }
    CHECK_EQ(la.events.size(), lb.events.size());
    for (std::size_t i = 0; i < la.events.size(); ++i)
    {
        CHECK_EQ(la.events[i].vein, lb.events[i].vein);
        CHECK_EQ(la.events[i].year, lb.events[i].year);
        CHECK_EQ(la.events[i].amount, lb.events[i].amount);
    }
}
```

- [ ] **Step 2: 运行测试确认失败**

Expected: `the_events_account_for_every_extraction` 失败——事件表还是空的。

- [ ] **Step 3: 实现换算系数与定点推演**

`src/history/ledger.cpp` 顶部追加：

```cpp
#include "gamev2/world/spirit_tables.hpp"
```

`compute_spirit_tide` 在 `spirit_tables.hpp` 里。

在 `src/history/ledger.cpp` 的匿名命名空间里加：

```cpp
/// 定点标度：年度开采量保留 2^16 分之一。
///
/// 年度开采量是个小数（rate × level/255 × kFlowToStock，量级 0.05..3），
/// 直接截断成整数会把绝大多数年份变成 0，分布上出现断崖。所以必须
/// 保留小数余数逐年累加。
constexpr std::uint64_t kFixedScale = 1ull << 16;

/// 潮位积分：六百年里 level 的总和（level 是 0..255 的字节）。
///
/// 换算系数用它做锚点：按实际潮位曲线，全世界的存量恰好在当代耗尽。
std::uint64_t tide_integral(std::uint32_t epoch, std::uint32_t now)
{
    std::uint64_t sum = 0;
    for (std::uint32_t year = epoch; year < now; ++year)
    {
        sum += gamev2::world::compute_spirit_tide(year).level;
    }
    return sum;
}
```

在 `build_ledger` 里，推定与分配之后：

```cpp
    if (ledger.now <= ledger.epoch || ledger.veins.empty())
    {
        return ledger;
    }

    // 换算系数：流速（累计流量，无量纲归一化得分的来源）与储量
    // （段上归一化得分的积分）不是同一个量纲，必须折算。锚点取世界
    // 总量：按实际潮位曲线，全世界的存量恰好在当代耗尽。
    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral = tide_integral(ledger.epoch, ledger.now);
    if (integral == 0 || sum_rate == 0)
    {
        return ledger;
    }
    // kFlowToStock = sum_total * 255 / (integral * sum_rate)
    //              = kNum / kFixedScale
    const std::uint64_t kNum =
        sum_total * 255ull * kFixedScale / (integral * sum_rate);
    if (kNum == 0)
    {
        // 换算系数被整除成了 0：流速与储量的比例离谱到这个地步，
        // 算不出任何开采量。这是一种退化，按设计 §5 由诊断量报出，
        // 不静默返回一份「什么都没采」的台账。
        diagnostics.flow_conversion_collapsed = 1u;
        return ledger;
    }
```

**`LedgerDiagnostics` 要加一个字段**（本任务要动公共头文件）：

```cpp
    /// 换算系数整除成 0：流速与储量的比例离谱，推演不出开采量。
    std::uint32_t flow_conversion_collapsed = 0;
```

然后是逐年推演：

```cpp
    const std::uint64_t den = 255ull * kFixedScale;
    std::vector<std::uint64_t> remainder(ledger.veins.size(), 0);
    for (std::uint32_t year = ledger.epoch; year < ledger.now; ++year)
    {
        const std::uint32_t level =
            gamev2::world::compute_spirit_tide(year).level;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            QuotaEntry& entry = ledger.veins[i];
            if (entry.extracted >= entry.surveyed_total)
            {
                continue;
            }
            const std::uint64_t num =
                static_cast<std::uint64_t>(entry.surveyed_rate) * level * kNum;
            const std::uint64_t total = num + remainder[i];
            remainder[i] = total % den;
            const std::uint64_t eff = total / den;

            const std::uint32_t quota_left =
                entry.surveyed_total - entry.extracted;
            const std::uint32_t take =
                (eff >= quota_left)
                    ? quota_left
                    : static_cast<std::uint32_t>(eff);
            if (eff >= quota_left)
            {
                // 被配额卡住：未消费的小数没有意义，余数清零。
                remainder[i] = 0;
            }
            entry.extracted += take;
            entry.balance = entry.surveyed_total - entry.extracted;
            if (take > 0u)
            {
                ExtractionEvent event;
                event.vein = entry.vein;
                event.year = year;
                event.amount = take;
                event.over_quota = false;
                ledger.events.push_back(event);
            }
        }
    }
```

- [ ] **Step 4: 构建并运行测试**

Expected: 全部通过。

**自查**：跑一遍 demo 之前，先加一条临时的 `std::println` 看看
「多少条脉在六百年里枯竭」。

**判据是「有几条」而不是「几条」**：具体条数随种子变，参考值是
原型在 256×256 上实测的「50 条脉里 35~40 条枯竭，枯竭年铺开在
8345..8912」。**一条都没枯竭才是坏信号**——那说明定点累加没生效
（又退回截断了），或者换算系数的锚点算错了。

- [ ] **Step 5: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 5: 越采与枯竭的认领

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: `QuotaEntry::overmined_amount`、`World::veins[].overmined`、
  `World::eyes[].depleted`、`Stream::ledger`
- Produces: `QuotaEntry::depleted_year`、`Ledger::events` 里的越采事件、
  `LedgerDiagnostics::depletion_claim_unbacked`、
  `LedgerDiagnostics::depletion_unmarked`、
  `LedgerDiagnostics::overmine_before_epoch`

- [ ] **Step 1: 写失败的测试**

```cpp
TEST(every_overmined_vein_is_claimed)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 13;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            continue;
        }
        ++checked;
        // 越采必须足额认领：认领不全的话守恒恒等式 3 会塌。
        CHECK_EQ(entry.overmined_amount, entry.surveyed_total - vein.reserve);
        std::uint32_t claimed = 0;
        for (const auto& event : ledger.events)
        {
            if (event.vein == entry.vein && event.over_quota)
            {
                claimed += event.amount;
            }
        }
        CHECK_EQ(claimed, entry.overmined_amount);
    }
    CHECK(checked > 0);
}

TEST(ledger_never_double_counts_overmining)
{
    // 守恒恒等式 3：reserve − balance = 正常开采量 ≥ 0。
    // 越采被重复计一次的话，这条会变成负。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 13;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        const std::int64_t normal =
            static_cast<std::int64_t>(vein.reserve) - entry.balance;
        CHECK(normal >= 0);
    }
}

TEST(every_depleted_eye_is_claimed)
{
    // worldgen 标了枯眼的脉，台账里必有归零年。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 21;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!w.eyes[vein.eye].depleted)
        {
            continue;
        }
        ++checked;
        CHECK(entry.depleted_year != 0u);
    }
    CHECK(checked > 0);
}
```

- [ ] **Step 2: 运行测试确认失败**

Expected: `every_overmined_vein_is_claimed` 失败——还没有越采事件。

- [ ] **Step 3: 实现两遍推演**

`src/history/ledger.cpp` 顶部追加：

```cpp
#include "gamev2/world/rng.hpp"
```

`Pcg32` 与 `stage_seed` 在 `rng.hpp` 里。

把 Task 4 的单遍推演改成两遍。**第一遍只算正常开采**，额外记录每条脉
每年的余额轨迹。

**先把 Task 4 的循环体抽成一个 lambda**，再在它的前后加越采：

```cpp
    // 内部状态：推演过程的可变部分。
    struct VeinState
    {
        std::uint32_t overmine_year = 0;  ///< 越采落在哪一年
        std::uint32_t drain_year = 0;     ///< 余额归零那年
    };

    const std::uint32_t years = ledger.now - ledger.epoch;
    std::vector<VeinState> states(ledger.veins.size());
    std::vector<std::uint32_t> trajectory(ledger.veins.size() * years, 0u);

    // 推演一遍。with_overmining 为真时才插越采。
    // 轨迹按年记录，供第二遍定越采的可行窗口。
    const auto advance = [&](std::vector<std::uint32_t>* trace,
                             bool with_overmining)
    {
        std::vector<std::uint64_t> remainder(ledger.veins.size(), 0);
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            ledger.veins[i].extracted = 0;
            ledger.veins[i].balance = ledger.veins[i].surveyed_total;
            states[i].drain_year = 0;
        }
        ledger.events.clear();

        for (std::uint32_t year = ledger.epoch; year < ledger.now; ++year)
        {
            const std::uint32_t level =
                gamev2::world::compute_spirit_tide(year).level;
            for (std::size_t i = 0; i < ledger.veins.size(); ++i)
            {
                QuotaEntry& entry = ledger.veins[i];
                if (entry.extracted >= entry.surveyed_total)
                {
                    continue;
                }
                // 越采在该年的正常开采之前扣除：第一遍算出的
                // 「该年起点的余额」在第二遍里原样成立。
                if (with_overmining && states[i].overmine_year == year)
                {
                    const std::uint32_t over =
                        std::min(entry.balance, entry.overmined_amount);
                    if (over > 0u)
                    {
                        entry.extracted += over;
                        entry.balance =
                            entry.surveyed_total - entry.extracted;
                        ExtractionEvent event;
                        event.vein = entry.vein;
                        event.year = year;
                        event.amount = over;
                        event.over_quota = true;
                        ledger.events.push_back(event);
                    }
                }
                const std::uint64_t num =
                    static_cast<std::uint64_t>(entry.surveyed_rate) * level
                    * kNum;
                const std::uint64_t total = num + remainder[i];
                remainder[i] = total % den;
                const std::uint64_t eff = total / den;

                const std::uint32_t quota_left =
                    entry.surveyed_total - entry.extracted;
                const std::uint32_t take =
                    (eff >= quota_left)
                        ? quota_left
                        : static_cast<std::uint32_t>(eff);
                if (eff >= quota_left)
                {
                    remainder[i] = 0;
                }
                entry.extracted += take;
                entry.balance = entry.surveyed_total - entry.extracted;
                if (take > 0u)
                {
                    ExtractionEvent event;
                    event.vein = entry.vein;
                    event.year = year;
                    event.amount = take;
                    event.over_quota = false;
                    ledger.events.push_back(event);
                }
                if (entry.balance == 0u && states[i].drain_year == 0u)
                {
                    states[i].drain_year = year;
                }
            }
            // 年末余额入轨迹
            if (trace != nullptr)
            {
                for (std::size_t i = 0; i < ledger.veins.size(); ++i)
                {
                    (*trace)[i * years + (year - ledger.epoch)]
                        = ledger.veins[i].balance;
                }
            }
        }
    };
```

**枯竭年要在正常开采之后判定**：越采把余额打空的那一年也算枯竭年。

**三步的先后顺序不能换**：采样要读第一遍算出的余额轨迹，所以第一遍
必须先跑。

```cpp
    // ① 第一遍：只算正常开采，产出余额轨迹。
    advance(&trajectory, false);
```

**② 越采年的采样**（只跑第一遍之后）：

```cpp
    // 越采年采样。窗口 = 「该年起点的余额仍装得下这笔越采」的年份。
    //
    // 脉律是 8300 编的，越采案若发生在 8300 之前脉律不会不管，所以
    // 正常情况下窗口从 8301 起。窗口为空时只能落在 8300（余额必装得下），
    // 报诊断。
    const std::uint64_t ledger_seed = gamev2::world::stage_seed(
        world.params().seed,
        static_cast<std::uint32_t>(gamev2::world::Stream::ledger));

    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        const QuotaEntry& entry = ledger.veins[i];
        if (entry.overmined_amount == 0u)
        {
            continue;
        }
        // 该年起点的余额 = 上一年年末的余额（t = 0 时是 8300 的快照）。
        std::uint32_t last_ok = 0;
        bool any = false;
        for (std::uint32_t t = 0; t < years; ++t)
        {
            const std::uint32_t start_balance =
                (t == 0) ? entry.surveyed_total
                          : trajectory[i * years + t - 1u];
            if (start_balance >= entry.overmined_amount)
            {
                last_ok = t;
                any = true;
            }
        }
        if (!any)
        {
            // 理论上不可达：8300 的快照余额就是 surveyed_total。
            states[i].overmine_year = ledger.epoch;
            ++diagnostics.overmine_before_epoch;
            continue;
        }
        if (last_ok == 0)
        {
            states[i].overmine_year = ledger.epoch;
            ++diagnostics.overmine_before_epoch;
            continue;
        }
        gamev2::world::Pcg32 rng(gamev2::world::stage_seed(
            ledger_seed, entry.vein));
        states[i].overmine_year =
            ledger.epoch + 1u + rng.below(last_ok);
    }
```

**③ 第二遍**：带越采跑一遍。

```cpp
    advance(&trajectory, true);
```

**枯竭认领**（第二遍之后）：

```cpp
    // 枯竭认领：台账的枯竭年是推演得出（余额归零那年），
    // worldgen 的 eye.depleted 标记作交叉校验。强行抽干会打破
    // 守恒恒等式 3，所以不抽干，只报冲突。
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        QuotaEntry& entry = ledger.veins[i];
        const auto& vein = world.veins[entry.vein];
        const bool claimed = world.eyes[vein.eye].depleted;
        const bool drained = entry.balance == 0u;
        if (claimed && drained)
        {
            entry.depleted_year = states[i].drain_year;
        }
        else if (claimed && !drained)
        {
            ++diagnostics.depletion_claim_unbacked;
            entry.depleted_year = ledger.now;
        }
        else if (!claimed && drained)
        {
            ++diagnostics.depletion_unmarked;
            entry.depleted_year = states[i].drain_year;
        }
    }
```

`states[i].drain_year` 由 `advance` 在余额归零的那一刻记下（上面的代码
里已有）。注意它要在**正常开采之后**判定，这样越采把余额打空的那一年
也算枯竭年。

**`depletion_claim_unbacked` 不是缺陷**：worldgen 按身份哈希标枯眼，
与经济性无关；台账按推演判枯竭。两者不一致时台账报冲突，不假装一致。

- [ ] **Step 4: 构建并运行测试**

Expected: 全部通过。

**自查**：加一条临时打印，确认
（a）越采事件数 == 越采脉数；
（b）每条越采脉的 `overmined_amount` 与事件求和相等；
（c）`depletion_claim_unbacked` 的数量在个位数（实测 256×256 是 4 条）。

- [ ] **Step 5: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 6: 偏差表与口径差分解

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: `QuotaEntry`、`Ledger::veins`
- Produces: `Ledger::sects`、`Ledger::structural_deviation`、
  `Ledger::overmining_deviation`、`Ledger::unregistered_extracted`

- [ ] **Step 1: 写失败的测试**

```cpp
TEST(the_deviation_is_not_trivial)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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

    // 至少有一条脉的配额与当代余额不相等。
    bool any = false;
    for (const auto& entry : ledger.veins)
    {
        if (entry.surveyed_total != entry.balance)
        {
            any = true;
        }
    }
    CHECK(any);
}

TEST(the_structural_deviation_dominates)
{
    // 招牌断言：8300 是峰值年、8914 是谷底年，口径差必然压过越采差。
    // 潮位公式若改了，这条会先变红——那正是它存在的意义。
    //
    // fixture 选 128x128/seed 26：实测台账里有 4 条越采脉，越采差非零。
    // 挑一个没有越采脉的种子，这条比较就成了 0 与正数的比较，白写。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.structural_deviation > 0);
    CHECK(ledger.structural_deviation >= ledger.overmining_deviation);
}

TEST(unregistered_extraction_is_separate)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::uint32_t unregistered = 0;
    std::uint32_t unregistered_current = 0;
    for (const auto& entry : ledger.veins)
    {
        if (!entry.registered)
        {
            unregistered += entry.extracted;
            unregistered_current += entry.balance;
        }
    }
    // 守卫：没有未登记的脉，上面两条断言就整条空跑。
    CHECK(unregistered > 0u);
    CHECK_EQ(ledger.unregistered_extracted, unregistered);

    // 未登记的脉不计入任何登记宗的汇总。
    std::uint32_t registered_current = 0;
    for (const auto& sect : ledger.sects)
    {
        if (!sect.registered)
        {
            continue;
        }
        registered_current += sect.current_total;
    }
    std::uint32_t all_registered = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.registered)
        {
            all_registered += entry.balance;
        }
    }
    CHECK_EQ(registered_current, all_registered);
}

TEST(the_rate_deviation_is_the_tide_shortfall)
{
    // 8300 是峰值年、当代是谷底年：流速偏差是「把核定的流速按当代潮位
    // 折下来」差的那一份。对每一宗，偏差都不超过核定流速，且不为负。
    //
    // 这条守住 deviation_rate 的定义：它不是存量减存量，也不是恒零。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    const std::uint32_t level_epoch =
        gamev2::world::compute_spirit_tide(ledger.epoch).level;
    const std::uint32_t level_now =
        gamev2::world::compute_spirit_tide(ledger.now).level;
    // 前提：8300 是峰值、当代是谷底。哪天潮位公式改了，这里先红。
    CHECK(level_epoch > level_now);

    std::size_t checked = 0;
    for (const auto& sect : ledger.sects)
    {
        if (sect.quota_rate == 0u)
        {
            continue;
        }
        ++checked;
        // 核定的流速不会低于当代潮位能支撑的流速，所以偏差非负。
        CHECK(sect.deviation_rate >= 0);
        // 潮位比差小于 1，所以偏差不会超过核定流速本身。
        CHECK(sect.deviation_rate
              <= static_cast<std::int64_t>(sect.quota_rate));
    }
    CHECK(checked > 0);
}
```

**别忘了加 `#include "gamev2/world/spirit_tables.hpp"`**（`compute_spirit_tide`
在 `spirit_tables.hpp` 里，`ledger.hpp` 不引它）。

- [ ] **Step 2: 运行测试确认失败**

Expected: `the_deviation_is_not_trivial` 失败——`sects` 还是空的。

- [ ] **Step 3: 实现偏差表**

在 `build_ledger` 的第二遍推演之后：

```cpp
    // 宗级汇总。
    ledger.sects.assign(static_cast<std::size_t>(Sect::count), SectLedger{});
    for (std::size_t s = 0; s < ledger.sects.size(); ++s)
    {
        ledger.sects[s].sect = static_cast<Sect>(s);
    }
    for (const auto& entry : ledger.veins)
    {
        const auto index = static_cast<std::size_t>(entry.holder);
        SectLedger& sect = ledger.sects[index];
        if (!entry.registered)
        {
            sect.registered = false;
        }
        sect.quota_total += entry.surveyed_total;
        sect.quota_rate += entry.surveyed_rate;
        sect.current_total += entry.balance;
        sect.overmined_amount += entry.overmined_amount;
    }
    for (auto& sect : ledger.sects)
    {
        sect.deviation_total =
            static_cast<std::int64_t>(sect.quota_total)
            - static_cast<std::int64_t>(sect.current_total);
    }
```

**`deviation_rate` 的定义**：设计文档只说「流速偏差，同号约定」，没给公式。
`quota_rate` 是流速（每年吞吐），`current_total` 是存量（还能采多少），
两者相减是拿流量减存量，量纲不搭。所以这里定成**流量侧的口径差**，
与下一段 `structural_deviation` 的算法同构（同一个潮位比），只是作用在
流速上：

```
deviation_rate = Σ_{v ∈ 名下} surveyed_rate(v) × (level_8300 − level_当代) / level_8300
```

读作「8300 年按峰值潮位核定的流速，按当代谷底潮位折下来差了多少」。
正 = 核定流速高于当代潮位能支撑的流速。它是 `quota_rate` 唯一的用处，
不定义的话 `quota_rate` 就是一列悬空的数字。

**口径差与越采差**：

```cpp
    // 口径差：8300 按峰值口径核定、当代按谷底口径衡量，与谁偷采无关。
    const std::uint32_t level_epoch =
        gamev2::world::compute_spirit_tide(ledger.epoch).level;
    const std::uint32_t level_now =
        gamev2::world::compute_spirit_tide(ledger.now).level;
    if (level_epoch == 0u)
    {
        return ledger;
    }
    // 潮位差必须先转成有符号再相减：level_now > level_epoch 时，
    // uint32 相减会回绕成一个巨大的正数。
    const std::int64_t level_gap =
        static_cast<std::int64_t>(level_epoch)
        - static_cast<std::int64_t>(level_now);
    for (const auto& entry : ledger.veins)
    {
        ledger.structural_deviation +=
            static_cast<std::int64_t>(entry.surveyed_total)
            * level_gap / static_cast<std::int64_t>(level_epoch);
        ledger.overmining_deviation += entry.overmined_amount;

        // 流量侧的口径差：与上面的存量侧同构，作用在流速上。
        SectLedger& sect =
            ledger.sects[static_cast<std::size_t>(entry.holder)];
        sect.deviation_rate +=
            static_cast<std::int64_t>(entry.surveyed_rate)
            * level_gap / static_cast<std::int64_t>(level_epoch);
    }
```

**未登记开采**：

```cpp
    for (const auto& entry : ledger.veins)
    {
        if (!entry.registered)
        {
            ledger.unregistered_extracted += entry.extracted;
        }
    }
```

- [ ] **Step 4: 构建并运行测试**

Expected: 全部通过。

**自查**：`the_structural_deviation_dominates` 若失败，打印两个数看看。
预期口径差远大于越采差（实测 256×256：8853 vs 2842，75%）。

- [ ] **Step 5: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add tests/test_history_ledger.cpp
```

---

### Task 7: 台账文本导出与 demo 接入

**Files:**
- Modify: `src/history/ledger.cpp`
- Modify: `include/gamev2/history/ledger.hpp`
- Modify: `src/demo/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_history_ledger.cpp`

**Interfaces:**
- Consumes: `Ledger`
- Produces: `export_ledger(const Ledger&, const std::filesystem::path&)`、
  demo 的台账统计输出、`agent-temp/worldgen/ledger.txt`

- [ ] **Step 1: 写失败的测试**

```cpp
TEST(the_ledger_is_exportable_as_text)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gamev2_ledger_test.txt";
    CHECK(gamev2::history::export_ledger(ledger, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    std::size_t lines = 0;
    while (std::getline(in, line))
    {
        ++lines;
    }
    in.close();
    std::filesystem::remove(path);

    // 表格行数 = 脉数 + 事件数，加上固定的头部行数。
    CHECK(lines > ledger.veins.size() + ledger.events.size());
    CHECK(lines < ledger.veins.size() + ledger.events.size() + 64u);
}
```

**别忘了 `#include <fstream>` 与 `#include <filesystem>`。**

- [ ] **Step 2: 运行测试确认失败**

Expected: 链接期报 `export_ledger` 未定义。

- [ ] **Step 3: 实现文本导出**

**先把声明加进公共头文件**——声明与定义必须同批次落地。Task 1 刻意
没有声明它，就是为了避免留下只有签名没有实体的符号。

在 `include/gamev2/history/ledger.hpp` 里，`build_ledger` 的两个重载之后、
`}  // namespace gamev2::history` 之前：

```cpp
/// 导出人可读台账文本。返回 false 表示文件写不动。
[[nodiscard]] bool export_ledger(const Ledger& ledger,
                                 const std::filesystem::path& path);

/// 宗门的汉字名。台账是给人读的，编号对机器友好、对人无意义。
///
/// 导出的台账与 demo 的对照表都要用这张表，所以放在公共头里：
/// 两处各留一份的话，改了名字就会有一处对不上。
[[nodiscard]] const char* sect_name(Sect sect);
```

**头文件要补 `#include <filesystem>`**：`export_ledger` 的签名用了
`std::filesystem::path`。Task 1 把这个 include 跟 `export_ledger` 的声明
一起删掉了（避免留下只有签名没有实体的符号），现在声明回来，include
也得回来。

然后在 `src/history/ledger.cpp` 里加：

```cpp
#include <fstream>
```

**不要 `#include <print>`**：导出用的是 `std::ofstream`，`std::println`
只在 demo 里用。

```cpp
namespace {

const char* rank_name(gamev2::world::VeinRank rank)
{
    switch (rank)
    {
    case gamev2::world::VeinRank::dragon: return "龙";
    case gamev2::world::VeinRank::main:   return "主";
    case gamev2::world::VeinRank::branch: return "支";
    default:                               return "?";
    }
}

}  // namespace

const char* sect_name(Sect sect)
{
    switch (sect)
    {
    case Sect::taixuan:   return "太玄宗";
    case Sect::daxia:     return "丹霞谷";
    case Sect::hanjiang:  return "寒江剑府";
    case Sect::wanshou:   return "万兽山";
    case Sect::qiangnang: return "青囊谷";
    case Sect::tianji:    return "天机阁";
    case Sect::jiuyou:    return "九幽宫";
    case Sect::unowned:   return "无主";
    default:              return "?";
    }
}

bool export_ledger(const Ledger& ledger,
                   const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        return false;
    }
    out << "灵脉台账\n";
    // 跨度可能为负（now < epoch）：台账为空时不能拿它去减。
    const std::uint32_t span =
        (ledger.now > ledger.epoch) ? (ledger.now - ledger.epoch) : 0u;
    out << "纪元 " << ledger.epoch << " —— 当代 " << ledger.now
        << "（" << span << " 年）\n";
    out << "脉 " << ledger.veins.size() << " 条，事件 "
        << ledger.events.size() << " 笔，未登记开采 "
        << ledger.unregistered_extracted << "\n";
    out << "\n=== 七宗配额与当代余额 ===\n";
    out << "宗门\t脉数\t8300配额\t8300流速\t当代余额\t偏差\t越采\n";
    for (const auto& sect : ledger.sects)
    {
        out << sect_name(sect.sect) << '\t'
            << sect.quota_total << '\t'
            << sect.quota_rate << '\t'
            << sect.current_total << '\t'
            << sect.deviation_total << '\t'
            << sect.overmined_amount << '\n';
    }
    out << "\n=== 偏差分解 ===\n";
    out << "口径差（8300 峰值口径 vs 当代谷底口径）\t"
        << ledger.structural_deviation << '\n';
    out << "越采差（人为多采）\t" << ledger.overmining_deviation << '\n';
    out << "\n=== 脉级台账 ===\n";
    out << "脉\t宗门\t品阶\t8300承载\t安全流速\t已采\t越采\t当代余额\t枯竭年\n";
    for (const auto& entry : ledger.veins)
    {
        out << entry.vein << '\t'
            << sect_name(entry.holder) << '\t'
            << rank_name(entry.rank) << '\t'
            << entry.surveyed_total << '\t'
            << entry.surveyed_rate << '\t'
            << entry.extracted << '\t'
            << entry.overmined_amount << '\t'
            << entry.balance << '\t'
            << entry.depleted_year << '\n';
    }
    out << "\n=== 开采事件 ===\n";
    out << "脉\t年份\t数额\t越采\n";
    for (const auto& event : ledger.events)
    {
        out << event.vein << '\t'
            << event.year << '\t'
            << event.amount << '\t'
            << (event.over_quota ? 1 : 0) << '\n';
    }
    return static_cast<bool>(out);
}
```

**用制表符而不是空格对齐**：台账要被别的工具读，制表符不会因为
汉字宽度错位。

- [ ] **Step 4: 接入 demo**

`src/demo/main.cpp` 顶部加 include：

```cpp
#include "gamev2/history/ledger.hpp"
```

新增一个统计函数：

```cpp
void print_ledger_stats(const gamev2::history::Ledger& ledger,
                        const gamev2::history::LedgerDiagnostics& diagnostics)
{
    // 跨度可能为负（now < epoch）：与导出同一套钳位，别让 demo 印出
    // 一个巨大的年数。
    const std::uint32_t span =
        (ledger.now > ledger.epoch) ? (ledger.now - ledger.epoch) : 0u;
    std::println("灵脉台账：纪元 {} —— 当代 {}（{} 年）",
                 ledger.epoch, ledger.now, span);
    std::println("  脉 {} 条，事件 {} 笔，未登记开采 {}",
                 ledger.veins.size(), ledger.events.size(),
                 ledger.unregistered_extracted);
    std::println("七宗配额与当代余额：");
    for (const auto& sect : ledger.sects)
    {
        std::println("  {:<10} 配额 {:>6}  当代 {:>6}  偏差 {:>7}  越采 {:>5}",
                     gamev2::history::sect_name(sect.sect),
                     sect.quota_total, sect.current_total,
                     sect.deviation_total, sect.overmined_amount);
    }
    std::println("  口径差 {}，越采差 {}", ledger.structural_deviation,
                 ledger.overmining_deviation);
    // 诊断量是台账与 worldgen 之间的接缝：对不上就报出来，不藏。
    if (diagnostics.depletion_claim_unbacked > 0u
        || diagnostics.depletion_unmarked > 0u
        || diagnostics.missing_yin_vein > 0u
        || diagnostics.short_main_veins > 0u
        || diagnostics.overmine_before_epoch > 0u)
    {
        std::println("  诊断：枯眼无账 {}，枯竭未标 {}，缺阴脉 {}，主脉缺 {}，"
                     "越采早于纪元 {}",
                     diagnostics.depletion_claim_unbacked,
                     diagnostics.depletion_unmarked,
                     diagnostics.missing_yin_vein,
                     diagnostics.short_main_veins,
                     diagnostics.overmine_before_epoch);
    }
}
```

**demo 不再自带宗门名表**：用 `gamev2::history::sect_name`。
demo 里的 `kBiomeNames` 是因为 worldgen 侧没有第二份生物群系名表；
宗门名有两处消费者（导出文本 + demo 对照表），留两份就会有一处对不上。

在 `main` 里，`print_spirit_stats(world)` 之后：

```cpp
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(world, diagnostics);
    print_ledger_stats(ledger, diagnostics);
    if (!gamev2::history::export_ledger(ledger, dir / "ledger.txt"))
    {
        std::println(stderr, "写不了 {}", (dir / "ledger.txt").string());
        ++g_write_failures;
    }
```

**CMake**：demo 目标改成链 `history`（`history` 公开链 `worldgen`）：

```cmake
add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE history)
```

- [ ] **Step 5: 构建并运行测试与 demo**

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
./build/history_tests.exe
./build/worldgen_demo.exe --size 256 --seed 3
```

Expected: 测试全过；demo 打印七宗对照表，并在
`agent-temp/worldgen/ledger.txt` 生成台账文本。

- [ ] **Step 6: 向用户报告并等待提交指令**

```powershell
git add src/history/ledger.cpp
git add include/gamev2/history/ledger.hpp
git add src/demo/main.cpp
git add CMakeLists.txt
git add tests/test_history_ledger.cpp
```

---

### Task 8: 全量测试与确定性校验

**Files:**
- Modify: `tests/test_history_ledger.cpp`
- Modify: `src/history/ledger.cpp`

**Interfaces:**
- Consumes: 全部既有测试目标
- Produces: 设计 §6 的 13 条测试全部落地

- [ ] **Step 1: 补齐剩余的测试**

设计 §6 列了 13 条。本计划到 Task 7 为止已落地 12 条。剩下的一条是
「逐年守恒」，它的实现需要台账暴露每一年的余额。**本轮不为此扩充
公共接口**：改为在测试内部用事件表复算——

```cpp
TEST(conservation_holds_at_every_year)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
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

    // 逐年守恒：任何一年都不许把余额采成负数，且最后一年的年末余额
    // 恰好等于台账记的当代余额。
    //
    // 台账没有公开每一年的余额，所以用事件表复算：余额(t) = 8300 承载
    // 上限 − 截至 t 年的开采累加。事件按年份递增，顺序遍历即时间序。
    std::map<std::uint32_t, std::uint32_t> running;
    for (const auto& entry : ledger.veins)
    {
        running[entry.vein] = entry.surveyed_total;
    }
    std::uint32_t last_year = ledger.epoch;
    bool year_went_backwards = false;
    for (const auto& event : ledger.events)
    {
        // 事件必须落在推演区间内，且年份不倒退——否则「顺序遍历即
        // 时间序」这个前提就不成立，后面的复算全部作废。
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        if (event.year < last_year)
        {
            year_went_backwards = true;
        }
        last_year = event.year;
        auto& balance = running[event.vein];
        // 这一笔采完还剩——任何一年都不许采穿。
        CHECK(balance >= event.amount);
        balance -= event.amount;
    }
    CHECK(!year_went_backwards);
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(running[entry.vein], entry.balance);
    }
    // 守卫：没有事件的话上面两条断言整条空跑。
    CHECK(!ledger.events.empty());
}
```

**别忘了 `#include <map>`。**

- [ ] **Step 2: 跑全量测试**

```powershell
cmake --build build
./build/ecs_tests.exe
./build/worldgen_tests.exe
./build/history_tests.exe
```

Expected: 三个测试目标全绿。

**尤其注意** `vein_counts_match_the_setting_exactly`（1 龙 / 9 主 / 40 支）
——时序层只读 worldgen，不应影响灵脉数量。若它挂了，说明本计划写了
不该写的字段。

- [ ] **Step 3: 跑一次 demo 并核对实测数字**

```powershell
./build/worldgen_demo.exe --size 256 --seed 3
```

Expected（量级参考，具体数值随种子变；原型在 256×256 上实测过
seed 1 与 seed 7 两组）：

| 项 | 期望 |
|---|---|
| 脉数 | 50 |
| 枯竭的脉 | 35~40 条，枯竭年铺开在 8345..8912 |
| 口径差 | 约 8000..14000 |
| 越采差 | 约 2000..3000 |
| 口径差占比 | 70% 以上（**低于 50% 才是坏信号**） |
| `depletion_claim_unbacked` | 个位数 |

**若「枯竭的脉」接近 0**，说明定点累加没生效或换算系数算错——回 Task 4
的 Step 4 自查。**若口径差占比低于 50%**，说明潮位公式或换算锚点出了问题
——回 Task 6 的 Step 4 自查。

- [ ] **Step 4: 核对设计 §6 的 13 条测试全部落地**

逐条对照设计文档 §6 的表格，确认每条都有对应的 `TEST`：

| # | 用例 | 落地位置 |
|---|---|---|
| 1 | `ledger_is_deterministic` | Task 4 |
| 2 | `balances_never_go_negative` | Task 4 |
| 3 | `conservation_holds_at_every_year` | Task 8 |
| 4 | `final_balance_matches_the_ledger` | Task 4 |
| 5 | `ledger_never_double_counts_overmining` | Task 5 |
| 6 | `every_overmined_vein_is_claimed` | Task 5 |
| 7 | `every_depleted_eye_is_claimed` | Task 5 |
| 8 | `the_sects_hold_the_setting_six_veins` | Task 3 |
| 9 | `the_nine_main_veins_are_all_assigned` | Task 3 |
| 10 | `unregistered_extraction_is_separate` | Task 6 |
| 11 | `the_deviation_is_not_trivial` | Task 6 |
| 12 | `the_structural_deviation_dominates` | Task 6 |
| 13 | `the_ledger_is_exportable_as_text` | Task 7 |

- [ ] **Step 5: 向用户报告并等待提交指令**

**不得执行 `git commit`。** 供用户参考的暂存命令：

```powershell
git add src/history/ledger.cpp
git add include/gamev2/history/ledger.hpp
git add tests/test_history_ledger.cpp
git add src/demo/main.cpp
git add include/gamev2/world/enums.hpp
git add CMakeLists.txt
```

---

## 已知限制与风险（实施后仍然成立）

设计 §8 列的限制本计划全部继承。以下三条是本计划**新增**的：

1. **换算系数是标定值，不是推导值。** `kFlowToStock` 的锚点
   （世界存量恰好在当代耗尽）是一个选择，不是从设定里推出来的。
   换一个锚点，枯竭年的分布会平移，但偏差表的**相对结构**
   （口径差压过越采差、谁被亏空）不变。
2. **台账的绝对数字不可跨图幅比较。** `vein.reserve` 在 worldgen 里
   就是按全图最大得分归一化过的无量纲指数（`veins.cpp` 的归一化那一段），
   所以「8300 年承载上限 737」这种数只在本图幅内有意义。
   这与灵气层 §9.2 的结论同源。
3. **`depletion_claim_unbacked` 会有个位数。** worldgen 按身份哈希标枯眼，
   与经济性无关；台账按推演判枯竭。两者不一致时台账报冲突，
   不假装一致。

## 后续纵切（本轮不做）

- 私采点：散修盗采荒脉（灵气层 §9.3 划给时序层）
- 七宗兼并、结盟、覆灭史
- 兽潮、渡劫、鬼修、魔修（灵气层 §10 的阶段 18 已给出渡劫选址）
- 台账进 ECS：让派系演化读台账

---

**不得执行 `git commit`。**

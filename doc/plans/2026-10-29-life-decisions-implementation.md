# 众生层刀 4 实施计划：决策与行为（全量动作集）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 众生自己拿主意：厚属性判定式年度决策（`Motive`）＋行为执行
（历练/闭关/经营/社交/盗采/夺位/寻仇）＋战斗一步判定＋断供出走与
宗主换代＋突破资源门槛付账——无玩家注入跑 300 年，涌现验收招牌
四条成立。

**Architecture:** 年循环从五步扩成七步（设计 §1.2）：
Clock→Aging→Birth→Cultivate→**Decide→Act**→Economy。`DecideSystem`
按判据级联写 `Motive`（性格轴是岔路选择器）；`ActSystem` 执行意图、
写 `Standing`/`BondGraph`/`Purse`/`Office`/`Site`，横死者当场清账销毁。
`Bonds`/事件表与 `EconomyLedger` 同一先例：`Life` 持有的普通结构体，
不是组件。战斗是「power 差 + 无状态掷运」的一步判定，不做回合制。

**Tech Stack:** C++23、clang++、MSVC STL、CMake、Unix Makefiles、零第三方依赖。

**Spec:** `doc/2026-10-26-life-layer-design.md`（§1.2 年循环、§2.1–§2.4
数据模型与判定、§5.2 动作集、§6 数值纪律、§7 诊断、§8 测试、§11 刀 4
范围与验收）
**前置计划:** `doc/plans/2026-10-27-life-economy-implementation.md`（刀 3；
其「差异与补齐」裁定继续有效。刀 3 留给刀 4 的携带项 ①③④⑤⑦ 在本刀
逐一落地，见下「差异与补齐」13–17）

---

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23；编译器固定 clang++；禁止第三方库（含测试框架）；
  禁止 `shared_ptr`/`unique_ptr`；禁止头文件 `using namespace`
- 注释 doxygen `///` 只解释「为什么」；任何一行不超过 90 个字符
- Windows 下经 `pwsh` 调终端；**不得执行 `git add`/`git commit`/`git stash`/
  `git clean`**：每个任务末尾向用户报告并等待提交指令
- 随机性一律走 `Pcg32` + `world::stage_seed` 链，无状态派生、按
  `(seed, 流号, 年, 灵魂序号, 子流号)`，与遍历顺序无关（设计 §4.1）。
  **既有 Cultivate 掷运的派生链一字不动**（不加子流层）——动了它，
  刀 2 的探针读数全体漂移
- 子流号登记表 `Roll`（本计划新增，constants.hpp）：同一子流号只能
  被一种掷运用途占用，新增必须同改登记表（照 `kStreamIds` 的纪律）
- 掷运键用 `SoulId::index`（稳定身份），不用 `EntityId` 槽号——实体槽
  会被池回收（PR #6 修的就是它），身份键才能保证「同 seed 重跑逐位
  相同」
- **本层只读 world/faction**：读 `World`（domain_grade、params().seed）
  与 `EraReport`（无主脉表），不回写；依赖方向照旧
- system 一律遍历 `Life` 的实体序号表 `souls_`；`get_if` 空指针 = 已死，
  跳过；**不使用 `View::each_parallel`**
- **每新建 `src/life/` 下的 .cpp 必须同步加进 CMakeLists 的 `LIFE_SOURCES`**；
  每新建 `tests/test_life_*.cpp` 必须同步加进 `LIFE_TEST_SOURCES`
- **测试只走公共入口**：`life::Life`、`ecs_world()` 上的视图、公共类型与
  常数；私有头 `src/life/systems.hpp`/`generate.hpp` 不被任何测试包含
- 刀 1/2/3 的测试原样全绿；**唯一例外**：改动人口动力学的任务（任务 8、
  9）会让 `test_life_cultivation.cpp` 里钉死的人口/突破读数漂移——这些
  断言在本任务内用探针重钉（步骤里写明），不是放宽
- 编译与运行：`pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
  （全量回归再跑其余六个 `*_tests.exe`）
- 新测试夹具沿用刀 3 的 `256²/seed13`（`economy_world` + `make_stack`
  两个 helper 抄 `tests/test_life_economy.cpp` 顶部，含「128² 只有太玄
  有活脉」的理由注释）；探针临时程序放 `agent-temp/`，钉完即删
- 本刀的字段写入者表（继承刀 3 并新增）：

  | 字段 | 唯一写入者 |
  |---|---|
  | `Motive` | `DecideSystem`（年年覆写即「年后清空」） |
  | `Standing` | 建实体初值 0；`ActSystem`（社交/盗采/寻仇/夺位） |
  | `Site::cell` | 建实体时 `site_init` 掷运定一次；`ActSystem`（闭关/历练迁移） |
  | `Office::kind/years` | `ActSystem` 政事段（晋升/推举/夺位/出走） |
  | `Office::starved` | `EconomySystem`（断供计数） |
  | `SoulId::sect` | 建实体写一次；**`ActSystem` 出走/被逐——设计 §4.3 批准的迁移写入者** |
  | `Temper` 轴值 | 建实体生成；`ActSystem::nudge`（大事推移，十年一档） |
  | `Cultivation::seclude_until` | `CultivateSystem`（元婴锁） |
  | `Cultivation::progress/realm` | `CultivateSystem`（历练加成由 `ActSystem` 写 progress） |
  | `BondGraph`/事件表/`EconomyLedger` | `Life` 持有的普通结构体：`ActSystem` 写边与事件；`ActSystem`（盗采私铸）与 `EconomySystem` 写账本 |

---

## 与设计文档的差异与补齐（实现前必须先解决的口径问题）

1. **`MotiveKind` 是设计 §5.2 动作集的 NPC 子集**。政事(表决)、渡劫、
   收徒、传位是玩家动作，分别属刀 5/6/7，本刀不进枚举。突破(尝试)
   也不是 Motive：设计 §1.2 把突破判定放在步 4 Cultivate（步 5 决策
   在其后），NPC 的突破仍由 Cultivate 自动判定，判据 1 的「优先突破」
   落地为「付不起门槛时以贪婪轴选盗采还是经营」（见差异 5）。
2. **`Bonds` 不是组件**。设计 §2.1 组件表虽列 Bonds 行，但注明「存边、
   两端同写」——边是「两个人之间的账」，与 `EconomyLedger` 同一先例
   （金库也不是组件），由 `Life` 持有 `BondGraph`（`std::vector<BondEdge>`）。
   两端以 `SoulId::index` 标识，死亡时 `erase_soul` 清边，漏清由
   `orphan_bonds` 诊断报出。
3. **战斗一步判定的具体公式（设计只给要素）**：
   `power = 10 × realm + body + 3 × 法器档位`；法器档位 0..3 由私囊
   灵石分档（`kArtifactGradeStone`：0/100/10000/100000，档位取「不超过
   私囊的最高档」）——设计 §9.3 法器不是实体、只是战力的一个档位，
   财力入战力是它的最薄落地。胜率（千分制）=
   `clamp(500 + 60 × (power_a − power_b), 50, 950)`，掷 `Roll::combat`
   （键 = 两个灵魂序号，小者在先）。夺位不走此式——设计 §2.3 点名
   夺位成功率读境界差与门内声望，见差异 10。
4. **判据级联的精确阈值（设计给的是文字）**。全部落在
   `DecideSystem`，按序短路，轴值 3 为中值：
   - 判据 0（锁）：`seclude_until > year` → 闭关（元婴锁，Motive 无法
     覆盖，闭关被寻衅的掷运在 Act 侧）；
   - 判据 0.5（厚域寻址）：筑基境且 `progress ≥ kRealmYears[zhuji] −
     kThickSeekLead` 且当前格非厚域 → 闭关（arg = 厚域格）；
   - 判据 1（寿元危机）：剩余寿元 `< kRealmYears[realm]` 且私囊付不起
     `kBreakthroughCost[realm+1]` → 贪婪 >3 盗采，否则经营；
   - 判据 2（缺口）：`Office::starved > 0` 或 `purse.stone <
     kRealmBurn[realm]` → 悖逆（order ≤2）盗采，守序（≥4）经营，
     中值续走；
   - 判据 3（亲缘/师徒有难）：本年事件表里有 `expulsion`/`slain`/被夺，
     受害者与本我有师徒或亲缘边 → 勇烈（≥4）寻仇（arg = 加害者），
     怯懦（≤2）记仇（arg = 加害者）；
   - 判据 4（声望收益）：掷 `Roll::decide` < `kSocialChancePermille` →
     社交（arg = 无状态择定的对象）；仁厚（≥4）结善缘、酷烈（≤2）
     立威、中值续走；
   - 夺位机会：长老、order ≤2、境界 ≥ 宗主 −1，掷 < 
     `kContestChancePermille` → 夺位（arg = 宗主）；
   - 判据 5（默认）：慵懒（vigor ≤2）闭关原地、进取（≥4）历练、
     中值修炼。
5. **「优先突破」的账面含义**。判据 1 只在「付不起门槛」时分岔——
   付得起者什么都不用做（Cultivate 自动尝试），付不起者才需要搞钱，
   「贪婪轴 > 中值且无路可走时选盗采」逐字落在此处。
6. **历练/闭关/经营的收支（设计未给数值）**：历练 = 额外 `progress +1`
   （Act 写）+ 落点迁移 + 遇险掷运（`kRoamHazardPermille`，中者横死）；
   闭关 = 落点迁移（元婴锁期间另有寻衅掷运）；经营 = 银两 +60（与散修
   营生同额，任务赏钱）。历练不给钱——搞钱是经营与盗采的事，判据 2
   自会把缺钱者引过去。
7. **盗采的账（刀 3 携带项 ④ 的裁定）**：盗采目标是**无主脉**（含枯脉
   残余），年产 `kPoachYieldStone` 下品**直接入盗采者私囊**，同时在
   `EconomyLedger` 记 `poached_total` 并入 `minted_total`（「私铸」入
   铸币侧，恒等式 `Σ账 = 铸 − 烧` 两侧同增，守恒不动）。刀 3 计划写的
   「盗采把灵石送进无主金库」**不采用**：绕公账一圈会消灭盗采动机
   （盗采者要的是自用的石头，不是再去排队买）。无主金库 `vault[7]`
   在本刀维持 0，散修「买入成交」侧仍不可达——这是刀 5 产出清单的
   事，不是刀 4 的验收面。
8. **盗采的风险**：每年盗采时散修圈声望 −1；被擒掷运
   `kPoachCaughtPermille` 中者与执法者（合成对手：境界 = 盗采者 +1、
   体魄 5、法器档 1）走一步战斗，败 = 横死，胜 = 脱身但记罪
   （`Standing::crimes +1`）。派系层的发现判定是刀 5 的接口，本刀的
   被擒率是标定值。
9. **社交的两分支（设计只给方向）**：结善缘 = 与对象互加恩义边
   （强度 +1 钳 5）、双方所在宗声望各 +1（钳 5）；立威 = 择弱者
   （power 低者），己方声望 +1、对象声望 −1，掷 `kGrudgePermille` 中
   则结仇怨边。社交对象由 `Roll::decide` 无状态择定（同宗优先）。
10. **夺位与出走都写 `SoulId::sect`（设计 §4.3 批准的迁移写入者）**。
    夺位败：宗主仁厚（mercy ≥3）则逐（sect → unowned、Office → none、
    与宗主互结仇怨边），酷烈则杀（横死）；胜：挑战者接任，前宗主按其
    自己的 mercy 对挑战者的旧账…前宗主一律逐（胜者不杀旧主——赢得
    了位子就不需要尸体），与旧主互结仇怨。断供出走（判据之外的
    制度性逃生门）：`starved ≥ kStarveExitYears` 且 order ≤2 →
    sect → unowned、Office → none、与宗主结仇怨边、事件 `departure`。
11. **政事段是状态驱动，不是 Motive**（设计 §2.3「上位与换代状态驱动
    无性格门槛」）：外门→内门（境界 ≥ 炼气且内门缺额，缺额 = 内门数
    < 在册 × kInnerNum/kInnerDen）、内门→长老（长老数 < max(kMinElders,
    在册/kElderDivisor)）、宗主缺位→长老团推举（score = realm×10 +
    standing[sect]+5 + age/50 + 掷 `Roll::succession` 的 ±2 噪声；同分
    时亲缘边多的优先——设计说读亲缘，但死者师徒/亲缘边已随死亡清掉，
    亲缘只在活人之间当 tiebreak，裁定记录在此）。**晋升不设性格门槛，
    夺位才读 Temper**。
12. **突破概率重标（刀 3 携带项 ①）**：base 300 / per 50 / min 200 /
    max 500（千分制）——中位悟性 30%、最聪颖 500 恰好顶格、最愚钝
    100 被钳到 20%，三个锚点逐条对上。再乘潮位因子：
    `tide_factor = max(kTideBreakthroughFloor, compute_spirit_tide(
    year).level × 1000 / 255)`（千分制，整数）。谷底潮位 ≈ 86 时因子
    86——低潮期突破近于停摆，这是招牌④的人口杠杆。
13. **突破资源门槛逐字 + 标定**（设定 §5.1 门槛列，按「进入该境」索引）：
    进炼气 1（「引气一枚下品」挂这一步）、进筑基 2500（结基灵物
    20~30 中品取中值）、进金丹 33333（「结丹材料」设定没给数，取金丹
    一年烧毁同额）、进元婴 333333（「高阶灵脉」取十年烧毁额）、化神
    以上 ×10 外推（测试跨度内结构性不可达）。付账在**尝试时**、成败
    都烧（「失败损资源」逐字）：`purse.stone -= cost;
    ledger.burned_total += cost`。金丹另需**当前格 domain_grade ==
    thick**（Cultivate 读 `Site::cell` 查 `world.domain_grade`，不满足则
    不尝试不付账）；元婴进度满门槛后先锁 `seclude_until = year + 100`
    （「百年闭关」逐字——kRealmYears[jindan] 的 100 就是这百年，锁满
    才付账掷运）。
14. **断供计数与收徒受供给约束（刀 3 携带项 ③ + 常数注释里的稳态
    缺口）**：`EconomySystem` 检测当年该宗任一职级 `per_head < want`
    即 `Office::starved` 连续 +1（足额清零）并在 `EconomyLedger::
    shorted[sect]` 置位；`BirthSystem` 次年读到 shorted 就**整宗停收**
    （连 `max(1,·)` 的下限也停——下限是防绝嗣，断供年饿死徒弟更糟）。
    加上出走，人口「稳态」的三件套齐了两件，第三件是潮位（差异 12）。
15. **买入扩面**：银两换灵石不再只走无主公账——本宗弟子攒够
    `kLiangPerStone` 两可从**本宗金库**购一枚（划转不铸币，守恒两侧
    同在账内）。散修侧照旧走 `vault[7]`。这让判据 2 的「守序者做任务
    营生」有了经济意义（经营攒银两 → 买石头续命）。
16. **宗主换代的两条路（刀 3 携带项 ⑤）**：寿尽推举（差异 11）或夺位
    （差异 10）。招牌① 的「非脚本」= 事件表里出现 `succession` 或
    `contest`，推举路径依赖宗主寿尽——开局宗主多为金丹/元婴，寿元
    数百到千二，300 年窗口内**可能等不到**；等不到时靠夺位满足①，
    探针（任务 10）若两条都不出现，先调 `kContestChancePermille`，
    仍不行再回头议初始宗主年龄分布——不动 generate 的境界金字塔。
17. **刀 3 终审携带项 ⑦ 的两处顺手清理**（都在本刀必改的文件里，
    不算越界）：`EconomySystem` 分配两趟的存活谓词抽成共享私有函数
    `alive_member(world, e, sect, cls)`；突破概率常数注释的旧读数
    （「2%/10%/18%」）随任务 9 重标一并更新。「营生段无寿元守卫」
    核实后若确属误报（现行 `is_alive` 已判寿元）只在计划中记录结论，
    不改代码。`Life::economy()` 可变引用的取舍留给刀 6。
18. **Temper 大事推移（设计 §2.1「±1 档、十年缓变」的最薄落地）**：
    `nudge(Temper&, 轴, 方向, year)`——距上次推移 ≥10 年才动一档、
    钳 [1,5]、记 `drift_year`。两条规则：出走/被逐者 order −1（离开
    秩序的人更悖逆）；被擒脱身者 valor −1（差点死掉的人胆气受损）。
    不再多做——性格漂移的完整面留给有更多大事的后续刀。

---

## 文件结构（纵切完成后）

```
include/gamev2/life/
    constants.hpp       // +Roll 子流表、kBreakthroughCost、盗采/战斗/社交/
                        //  夺位/潮位常数、重标突破概率
    soul.hpp            // +MotiveKind/Motive、Standing、Site、BondKind/
                        //  BondEdge/BondGraph；Office/Temper/Cultivation 扩字段
    interface.hpp       // +LifeEventKind/LifeEvent、orphan_bonds、
                        //  Life::events()/bonds()/thick_cells() 接线
    combat.hpp         // 战斗一步判定：duel / combat_roll / contest_won
    economy.hpp         // +EconomyLedger::poached_total/shorted
src/life/
    setup.cpp           // 初始师徒/亲缘布网、厚域格表、站位初始化、
                        //  run_years 插入 Decide/Act
    systems.hpp         // +DecideSystem/ActSystem、life_roll/combat 声明
    systems.cpp         // +DecideSystem、ActSystem（含政事段与战斗）
    economy.cpp         // 断供计数、买入扩面、共享谓词
    generate.cpp        // 不动（PersonInit 无新字段）
    chronicle.cpp       // 不动（编年导出是刀 7）
tests/
    test_life_decisions.cpp   // 任务 2–4
    test_life_conflict.cpp    // 任务 5–6（盗采/战斗/夺位/寻仇）
    test_life_politics.cpp    // 任务 7–8（政事/出走/经济接线）
    test_life_emergence.cpp   // 任务 10（招牌四条 + 确定性）
    test_life_cultivation.cpp // 任务 9 重钉受影响断言
```

---

### Task 1: 组件、事件与子流登记表

**Files:**
- Modify: `include/gamev2/life/soul.hpp`（+Motive/Standing/Site/Bond 三类；
  Office/Temper/Cultivation 扩字段）
- Modify: `include/gamev2/life/interface.hpp`（+LifeEvent/LifeDiagnostics::
  orphan_bonds/Life 持有 bonds_/events_/thick_cells_ 及访问器）
- Modify: `include/gamev2/life/constants.hpp`（+Roll 表与全部新常数）
- Modify: `src/life/setup.cpp`（seed：挂新组件、初始落格、厚域格表）
- Modify: `src/life/systems.cpp`（BirthSystem：新生儿挂 Site/Standing/Motive
  并落格）
- Modify: `src/life/systems.hpp`（+`life_roll` 声明）
- Create: `include/gamev2/life/combat.hpp`（公共头：`duel`/`combat_roll`，
  刀 4 的战斗只有这一处实现，测试要直调）
- Test: `tests/test_life_decisions.cpp`（新建，进 `LIFE_TEST_SOURCES`）

**Interfaces:**
- Produces（后续任务全部依赖，签名逐字）:

```cpp
// soul.hpp
enum class MotiveKind : std::uint8_t
{
    none, cultivate, seclude, roam, operate, poach,
    social, grudge, avenge, contest,
};

struct Motive
{
    MotiveKind kind = MotiveKind::none;
    std::uint32_t arg = 0;  ///< 格号 / 无主脉号 / 对象 SoulId::index
};

struct Standing
{
    std::int8_t by_sect[8] = {};  ///< 按 history::Sect 序；第 8 位散修圈
    std::uint8_t crimes = 0;      ///< 罪名计数
};

struct Site
{
    std::uint32_t cell = 0;  ///< row-major 线性格号
};

struct Office
{
    OfficeKind kind = OfficeKind::none;
    std::uint32_t years = 0;   ///< 在位年数（政事段写）
    std::uint32_t starved = 0; ///< 连续断供年数（EconomySystem 写）
};

struct Temper
{
    // 五轴照旧，另加：
    std::uint32_t drift_year = 0;  ///< 上次大事推移的年（0 = 从未）
};

struct Cultivation
{
    Realm realm = Realm::yinqi;
    std::uint32_t progress = 0;
    std::uint32_t seclude_until = 0;  ///< 元婴闭关锁：year < 它则锁死
};

enum class BondKind : std::uint8_t
{
    master,  ///< 师徒
    kin,     ///< 亲缘
    partner, ///< 道侣（本刀只建结构不产生边，刀 6 用）
    favor,   ///< 恩义
    feud,    ///< 仇怨
};

struct BondEdge
{
    BondKind kind = BondKind::favor;
    std::uint32_t a = 0;       ///< SoulId::index，恒取两端较小者
    std::uint32_t b = 0;       ///< SoulId::index，恒取两端较大者
    std::uint8_t strength = 1; ///< 1..5
    std::uint32_t year = 0;    ///< 结缘年
};

/// 关系边集。不是组件（差异 2）：与 EconomyLedger 同一先例。
struct BondGraph
{
    std::vector<BondEdge> edges;
    /// 两端加边或强边；找不到才新建。返回操作后的边。
    BondEdge& tie(std::uint32_t a, std::uint32_t b, BondKind kind,
                  std::uint32_t year);
    /// 找边；无则 nullptr。
    [[nodiscard]] const BondEdge* find(std::uint32_t a, std::uint32_t b,
                                       BondKind kind) const;
    /// 死者清边：抹掉任一端为 index 的全部边，返回抹除数。
    std::size_t erase_soul(std::uint32_t index);
    /// 同分 tiebreak：与 index 有 kind 边的活人序号集。
    [[nodiscard]] std::vector<std::uint32_t> neighbors(
        std::uint32_t index, BondKind kind) const;
};
```

```cpp
// interface.hpp
enum class LifeEventKind : std::uint8_t
{
    succession,  ///< 宗主换代（推举）
    promotion,   ///< 晋升（外→内、补长老）
    departure,   ///< 断供出走
    expulsion,   ///< 被逐（夺位败/夺位后的旧宗主）
    poach,       ///< 盗采（amount = 得石）
    contest,     ///< 夺位（amount = 1 胜 0 败）
    avenge,      ///< 寻仇（amount = 1 胜 0 败）
    social,      ///< 社交（amount = 1 结善缘 2 立威）
    slain,       ///< 横死（历练/被擒/寻衅/寻仇败）
};

struct LifeEvent
{
    LifeEventKind kind = LifeEventKind::succession;
    std::uint32_t year = 0;
    std::uint32_t actor = 0;        ///< SoulId::index；0 = 无人
    std::uint32_t counterparty = 0; ///< SoulId::index；0 = 无
    history::Sect sect = history::Sect::unowned;
    std::uint32_t amount = 0;
};

// LifeDiagnostics 增加：
std::uint32_t orphan_bonds = 0;  ///< 关系边一端已死未清理
// Life 增加（构造与 seed 不变）：
[[nodiscard]] const std::vector<LifeEvent>& events() const noexcept;
[[nodiscard]] const BondGraph& bonds() const noexcept;
[[nodiscard]] const std::vector<std::uint32_t>& thick_cells()
    const noexcept;
```

```cpp
// constants.hpp —— 子流登记表 + 本刀新常数（锚点写在各自注释里）
enum class Roll : std::uint32_t
{
    decide = 0,          ///< 判据 4 社交机会、夺位机会、社交对象
    site_init = 1,       ///< 开局/新生落格
    poach_find = 2,      ///< 盗采被擒
    combat = 3,          ///< 一步战斗
    roam = 4,            ///< 历练遇险与落点
    succession = 5,      ///< 推举噪声
    contest = 6,         ///< 夺位成败
    seclude_hazard = 7,  ///< 元婴闭关被寻衅
    bonds_init = 8,      ///< 初始师徒/亲缘布网
};
// 数值以本表为准；新增用途必须同改（照 kStreamIds 纪律）。

inline constexpr std::array<std::uint64_t, 9> kBreakthroughCost{{
    0, 1, 2500, 33333, 333333, 3333330, 33333300, 333333000, 3333330000,
}};  ///< 按「进入该境」索引（差异 13）

inline constexpr std::uint32_t kBreakthroughBasePermille = 300;
inline constexpr std::uint32_t kBreakthroughPerAptitudePermille = 50;
inline constexpr std::uint32_t kBreakthroughMinPermille = 200;
inline constexpr std::uint32_t kBreakthroughMaxPermille = 500;
inline constexpr std::uint32_t kTideBreakthroughFloorPermille = 50;

inline constexpr std::uint32_t kThickSeekLead = 10;  ///< 筑基寻厚域提前量
inline constexpr std::uint32_t kPoachYieldStone = 800;
inline constexpr std::uint32_t kPoachCaughtPermille = 30;
inline constexpr std::uint32_t kRoamHazardPermille = 5;
inline constexpr std::uint32_t kSecludeHazardPermille = 10;
inline constexpr std::uint32_t kSocialChancePermille = 200;
inline constexpr std::uint32_t kGrudgePermille = 300;
inline constexpr std::uint32_t kContestChancePermille = 50;
inline constexpr std::uint32_t kStarveExitYears = 2;
inline constexpr std::uint32_t kCombatSwingPermille = 60;
inline constexpr std::uint32_t kCombatMinPermille = 50;
inline constexpr std::uint32_t kCombatMaxPermille = 950;
inline constexpr std::array<std::uint64_t, 4> kArtifactGradeStone{{
    0, 100, 10000, 100000,
}};  ///< 法器档位门槛（档 = 不超过私囊的最高档，0..3）
```

```cpp
// systems.hpp（私有头）—— 掷运帮手
/// 无状态派生：stage_seed 链 (seed, Stream::life, year, soul_index,
/// roll)。与 Cultivate 既有掷运（无 roll 层）派生不同——同一实体同年
/// 的两类掷运不共流。
[[nodiscard]] gamev2::world::Pcg32 life_roll(std::uint64_t seed, Roll roll,
    std::uint32_t year, std::uint32_t soul_index);
```

```cpp
// include/gamev2/life/combat.hpp（新建，公共头）—— 战斗一步判定
/// 一步战斗（设计 §2.2：境界差 × 体魄 × 档位 + 无状态随机）。
/// 返回 true = a 胜。power/胜率公式见计划差异 3；越级手段
/// （设定 §5.3）第一版不进判定。
///
/// 公共头而非私有头：刀 4 的战斗只有这一处实现（盗采被擒、寻仇、
/// 夺位败北共用），测试要直调它（与 lifespan_reached 同例）。
[[nodiscard]] bool duel(std::uint64_t seed, std::uint32_t year,
    const Cultivation& ca, const Aptitude& aa, std::uint64_t purse_a,
    const Cultivation& cb, const Aptitude& ab, std::uint64_t purse_b,
    std::uint32_t index_a, std::uint32_t index_b);

/// 战斗掷运：键含双方序号（小者在先）。
[[nodiscard]] gamev2::world::Pcg32 combat_roll(std::uint64_t seed,
    std::uint32_t year, std::uint32_t a, std::uint32_t b);
```

- [ ] **Step 1: 写失败测试**（`tests/test_life_decisions.cpp`，夹具抄
      `test_life_economy.cpp` 的 `economy_world`/`make_stack`）

```cpp
TEST(every_soul_gets_site_standing_and_blank_motive)
{
    // 新组件随建实体挂上（含新生儿——终审 B2 的先例：断言必须打在
    // 有新生儿的世界上）。落格是 site_init 掷运：确定性、非零格号
    // 混布（初始 970 人不该挤在同一个格）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world) { return; }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    std::uint32_t with_all = 0;
    std::uint32_t named = 0;
    std::set<std::uint32_t> cells;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId&)
        { ++named; });
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Site,
                          const gamev2::life::Standing,
                          const gamev2::life::Motive>().each(
        [&](const gamev2::life::SoulId&,
            const gamev2::life::Site& site,
            const gamev2::life::Standing& st,
            const gamev2::life::Motive& m)
        {
            ++with_all;
            cells.insert(site.cell);
            CHECK_EQ(st.crimes, 0u);
            CHECK_EQ(m.kind, gamev2::life::MotiveKind::none);
        });
    CHECK_EQ(with_all, named);
    CHECK(cells.size() > 100u);
}

TEST(thick_cells_are_precomputed_and_events_start_empty)
{
    // 厚域格表来自 world.domain_grade；256² 世界非空（128² 亦非空，
    // 但夹具统一 256²——刀 3 差异 2 的理由）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world) { return; }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    CHECK(!life.thick_cells().empty());
    CHECK(life.events().empty());
    // 关系网是任务 7 在 seed 里布的，本任务不碰（此处的空集断言放在
    // 任务 7——那里它才是要钉的东西）。
}

TEST(same_seed_same_sites_and_strict_determinism_of_new_fields)
{
    // 两个同 seed 世界跑 2 年：站位与声望逐位相同（新掷运的确定性钉）。
    // 跑 2 年不跑 1 年：让 site_init 也在「有新生儿」的年份上跑过。
    const Stack a = make_stack(economy_world(13));
    const Stack b = make_stack(economy_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world) { return; }
    gamev2::life::LifeDiagnostics da, db;
    gamev2::life::Life la(*a.world, a.era, da);
    gamev2::life::Life lb(*b.world, b.era, db);
    la.seed(); lb.seed();
    la.run_years(2); lb.run_years(2);
    // 按 SoulId::index 收齐再比对——视图遍历序不保证一致。
    std::map<std::uint32_t, std::uint32_t> cells_a, cells_b;
    auto collect = [](gamev2::life::Life& l, auto& out)
    {
        l.ecs_world().view<const gamev2::life::SoulId,
                            const gamev2::life::Site>().each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Site& site)
            { out[id.index] = site.cell; });
    };
    collect(la, cells_a); collect(lb, cells_b);
    CHECK_EQ(cells_a.size(), cells_b.size());
    CHECK(cells_a == cells_b);
}
```

- [ ] **Step 2: 跑测试确认失败**（组件不存在，编译错即失败）
      Run: `pwsh -NoProfile -Command "cmake --build build; & .\build\life_tests.exe"`
- [ ] **Step 3: 实现**：按 Interfaces 逐字落三个头文件；
  `setup.cpp::seed()` 在建实体循环里追加
  `(void)ecs_world_.add<Site>(e, Site{cell})`（cell 由
  `life_roll(seed, Roll::site_init, era_.now, i).below(宽×高)` 派生——
  注意用**开局年**派生初始队、新生儿在 BirthSystem 里用**当年**派生，
  两处共用同一 Roll，序号不同不共流）、`add<Standing>`、`add<Motive>`；
  seed() 末尾扫 `world_.domain_grade` 收集 `thick_cells_`（`at(i) ==
  world::DomainGrade::thick` 的线性格号，升序）。`BirthSystem::recruit`
  同样挂三件（site 掷运用 `clock->year`）。
- [ ] **Step 4: 跑测试确认通过；全量回归七个 `*_tests.exe` 全绿**
- [ ] **Step 5: 向用户报告，等待提交指令（不执行 git 命令）**

---

### Task 2: DecideSystem——判据级联写 Motive

**Files:**
- Modify: `src/life/systems.hpp` / `src/life/systems.cpp`（+DecideSystem）
- Modify: `src/life/setup.cpp`（run_years 注册，插在 Cultivate 与
  Economy 之间）
- Test: `tests/test_life_decisions.cpp`（追加）

**Interfaces:**
- Consumes: Task 1 全部类型；`life_roll`；`kRealmYears`/`kRealmBurn`/
  `kBreakthroughCost`
- Produces:

```cpp
/// 决策（设计 §1.2 步 5、§2.2 判据级联）。年年覆写 Motive——
/// 覆写即「年后清空」，玩家接管（刀 6）插在本 system 之前。
class DecideSystem final : public gamev2::ecs::System
{
public:
    /// const Cultivation/Purse 进读集是数据依赖也是调度声明：Purse 与
    /// Economy 的写集冲突（本 system 先注册、先执行）；Cultivation 与
    /// Cultivate 的写集冲突（落在其后）。Motive 在写集——与 Act 的
    /// 读集冲突，Act 因此必在其后。
    using ReadSet = std::tuple<const LifeClock, const SoulId,
                               const Mortal, const Cultivation,
                               const Aptitude, const Temper,
                               const Purse, const Office>;
    using WriteSet = std::tuple<Motive>;

    DecideSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                 const std::vector<gamev2::ecs::EntityId>& souls,
                 const BondGraph& bonds,
                 const std::vector<LifeEvent>& events,
                 const std::vector<std::uint32_t>& thick_cells,
                 const std::vector<std::uint32_t>& unowned_veins,
                 const gamev2::world::World& world);
    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;
};
```

- [ ] **Step 1: 写失败测试**——判据各分支的存在性断言（人口级，不构造
      单个个体——个体由种子决定，人口级断言对种子不敏感、对逻辑敏感）：

```cpp
namespace {
/// 按 MotiveKind 数一遍活人的当年意图。
std::map<gamev2::life::MotiveKind, std::uint32_t> motive_census(
    gamev2::life::Life& life)
{
    std::map<gamev2::life::MotiveKind, std::uint32_t> out;
    life.ecs_world().view<const gamev2::life::Motive>().each(
        [&](const gamev2::life::Motive& m) { ++out[m.kind]; });
    return out;
}
}  // namespace

TEST(decide_covers_default_branches_after_one_year)
{
    // 判据 5 是默认：修炼/闭关/历练三类动机在 vigor 三段上都有人。
    // 跑 2 年不是 1 年：第 1 年人人私囊为 0（判据 2 的「灵石 < 年
    // 消耗」成立），全部涌去盗采/经营，判据 5 到第 2 年才轮得到
    // （第 1 年末宗门弟子才领到份额）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world) { return; }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(2);
    const auto census = motive_census(life);
    CHECK(census.at(gamev2::life::MotiveKind::cultivate) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::seclude) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::roam) > 0u);
}

TEST(decide_sends_the_starved_to_poach_or_operate)
{
    // 判据 2 的触发链：断供宗（青囊/天机/九幽 mint=0）第 1 年被克扣、
    // starved 落账（任务 8 才写 starved——本任务先用「purse.stone <
    // kRealmBurn」这半边触发：开局私囊全 0，人人触发判据 2 的第二
    // 判据式，但散修本就有营生入账……所以钉第 2 年：宗门弟子分到份额
    // 仍不够烧）。断言：盗采与经营两类动机都出现（order 轴两段都有人）。
    // 任务 8 落地 starved 后本测试不重写——它的断言对两种触发式都真。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world) { return; }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(2);
    const auto census = motive_census(life);
    CHECK(census.at(gamev2::life::MotiveKind::poach) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::operate) > 0u);
}

TEST(decide_is_deterministic_under_same_seed)
{
    // 两个同 seed 世界跑 3 年：Motive 感知逐位相同。3 年覆盖
    // 「断供→判据 2」与「厚域寻址」两条路径。
    const Stack a = make_stack(economy_world(13));
    const Stack b = make_stack(economy_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world) { return; }
    gamev2::life::LifeDiagnostics da, db;
    gamev2::life::Life la(*a.world, a.era, da);
    gamev2::life::Life lb(*b.world, b.era, db);
    la.seed(); lb.seed();
    la.run_years(3); lb.run_years(3);
    using MotiveSnapshot =
        std::map<std::uint32_t,
                 std::pair<gamev2::life::MotiveKind, std::uint32_t>>;
    MotiveSnapshot sa, sb;
    auto collect = [](gamev2::life::Life& l, MotiveSnapshot& out)
    {
        l.ecs_world().view<const gamev2::life::SoulId,
                            const gamev2::life::Motive>().each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Motive& m)
            { out[id.index] = {m.kind, m.arg}; });
    };
    collect(la, sa); collect(lb, sb);
    CHECK_EQ(sa.size(), sb.size());
    CHECK(sa == sb);
}
```

- [ ] **Step 2: 跑测试确认失败**（DecideSystem 不存在/动机恒 none）
- [ ] **Step 3: 实现**。`execute` 的骨架逐判据（差异 4 的阈值）：

```cpp
void DecideSystem::execute(world, cmd)
{
    // 每个活人独立走级联；掷运键 = (seed, Roll::decide, year, index)，
    // 一把掷运只掷一次、复用于判据 4 与夺位（同一实体同年的「机会」
    // 是同一个数——两判据互斥使用，不构成相关性）。
    for (const auto e : souls_)
    {
        const auto* id = get_if<SoulId>(e);
        const auto* cult = get_if<Cultivation>(e);
        const auto* mortal = get_if<Mortal>(e);
        const auto* apt = get_if<Aptitude>(e);
        const auto* temper = get_if<Temper>(e);
        const auto* purse = get_if<Purse>(e);
        const auto* office = get_if<Office>(e);
        auto* motive = get_if<Motive>(e);
        // 八件缺一 = 死者空位，跳过（与 Aging 同一纪律）。
        if (/* 任一为空 */) { continue; }
        if (lifespan_reached(*mortal, *cult)) { continue; }  // 照
                                                             // Cultivate
        auto Pcg32 rng = life_roll(seed_, Roll::decide,
                                   clock->year, id->index);
        Motive m{};
        // 判据 0：元婴锁 → seclude（arg = 0 原地）。
        // 判据 0.5：厚域寻址（见差异 4，含 world.domain_grade 查询）。
        // 判据 1–5 与夺位机会：按差异 4 的阈值逐条短路。
        *motive = m;
    }
}
```

  判据 3 读 `events_`（只读本年：`ev.year == clock->year` 且 kind 为
  `expulsion`/`slain`）、`bonds_.find(..., master/kin)`；社交对象与
  夺位判空照差异 4。盗采目标（arg）＝ `life_roll(..., Roll::decide,
  ...)` 在「无主脉表」上择一——无主脉表由 `Life` 在 seed 时从
  `era_.veins` 收集（`owner == Sect::unowned` 的脉号，含枯脉；空表则
  无人得起盗采心）。DecideSystem 构造参数加
  `const std::vector<std::uint32_t>& unowned_veins`。
- [ ] **Step 4: 跑测试确认通过；全量回归**（刀 1–3 测试不受影响——
      Motive 是新组件，无人读它）
- [ ] **Step 5: 向用户报告，等待提交指令**

---

### Task 3: ActSystem 骨架——修炼/闭关/历练/经营与横死通道

**Files:**
- Modify: `src/life/systems.hpp` / `src/life/systems.cpp`（+ActSystem 四
  个 handler + 死亡清算私有函数）
- Modify: `src/life/setup.cpp`（注册 Act，在 Decide 之后、Economy 之前）
- Test: `tests/test_life_decisions.cpp`（追加）

**Interfaces:**
- Consumes: Task 1/2 全部；`kRoamHazardPermille`、
  `kFreeCultivatorIncomeLiang`
- Produces:

```cpp
/// 行为（设计 §1.2 步 6）。执行 Motive：本任务先落修炼/闭关/历练/
/// 经营四支与横死清算；社交/盗采/夺位/寻仇/政事段随后续任务落进
/// 同一 execute。
class ActSystem final : public gamev2::ecs::System
{
public:
    /// 写集宽是刻意的：SoulId 的写者只有本 system（出走/被逐，设计
    /// §4.3 批准的迁移写入者）；Purse 与 Economy 冲突（Act 先注册）；
    /// Cultivation 与 Cultivate 冲突（历练加成在其后落账）。
    /// const Motive 进读集：与 Decide 的写集冲突，Act 必在其后。
    using ReadSet = std::tuple<const LifeClock, const Motive, const Mortal>;
    using WriteSet = std::tuple<SoulId, Cultivation, Purse, Office,
                                Standing, Site, Temper>;

    ActSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
              const std::vector<gamev2::ecs::EntityId>& souls,
              BondGraph& bonds, std::vector<LifeEvent>& events,
              const std::vector<std::uint32_t>& thick_cells,
              EconomyLedger& ledger, LifeDiagnostics& diagnostics,
              const gamev2::world::World& world);
    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;
};
```

- 死亡清算（私有，横死者通用——盗采被擒/寻衅/寻仇败共用）：

```cpp
/// 横死清算：先清账再销毁（照 Economy 死者账消的口径裁定 13——
/// 实体销毁会把私囊带出账外，守恒从此年年破）。仇怨/恩义边随死清理，
/// 漏网的由年末 orphan_bonds 诊断兜底。事件 slain 由调用方在清算前
/// 先写入（清算函数只管账与销毁）。
void slay(World& world, CommandBuffer& cmd, EntityId e,
          const SoulId& id, Purse& purse, BondGraph& bonds,
          EconomyLedger& ledger);
```

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(roam_grants_extra_progress_and_income_none)
{
    // 历练者当年 progress +2（Cultivate 的 +1 加 Act 的 +1），不增银两
    // （差异 6）；落点迁移（历练者在动）。跑 2 年后抽一个 roam 者
    // 验证 progress==2（第 1 年判据 2 全员触发、无人 roam，故从第 2
    // 年起才有 roam 者）；再跑 9 年，断言存在 progress>10 的炼气者
    // （没人是 2×10+ 起步的，能超 10 说明加成在滚）。
    // ……（按 motive_census 找 roam 个体；用 view 收 (index, progress)）
}

TEST(operate_pays_silver_and_cultivate_stays_put)
{
    // 经营者银两 +60；修炼者 progress 恰 +1、Site 不变。
    // ……（同上模式；operate 者第 1 年末 silver==60）
}

TEST(roam_hazard_kills_conserves_stones_and_logs_slain)
{
    // 长跑 200 年：历练遇险（5‰/年）必有死者。断言三点：事件表
    // slain 非空；死者私囊在死亡当年入 burned（恒等式不破——逐年
    // 校验照 test_life_economy 的 the_identity_holds_every_year 形态，
    // 本测试只再钉 diag.stone_conservation_broken == 0）；死者不出现在
    // 任何后续年的 view 里（销毁落地）。探针先跑出「200 年内 slain
    // 事件数」的实测值再钉死下界（不得小于实测——防回归加严）。
    // ……（探针：agent-temp/ 下临时 TEST 打印 count；钉死后删探针）
}
```

- [ ] **Step 2: 确认失败** → **Step 3: 实现**。execute 骨架：

```cpp
for (const auto e : souls_)
{
    // 取件与判死照 DecideSystem 的模式（八件 + Motive）。
    switch (motive->kind)
    {
    case MotiveKind::cultivate: break;  // 修炼无额外效果（Cultivate
                                        // 已 +1）
    case MotiveKind::seclude:
        // arg != 0 且 != 当前格 → site->cell = arg（闭关迁址）。
        // 元婴锁期间另掷 seclude_hazard（任务 9 接 Cultivate 后才有
        // 锁，本任务先留分支位不掷）。
        break;
    case MotiveKind::roam:
        ++cult->progress;                      // 历练加成
        site->cell = life_roll(seed_, Roll::roam, year, index)
                         .below(w * h);        // 落点迁移
        if (rng2.below(1000) < kRoamHazardPermille)
        {
            // events_.push_back({slain, year, index, 0, sect, 0});
            slay(world, cmd, e, ...);
        }
        break;
    case MotiveKind::operate:
        purse->silver += kFreeCultivatorIncomeLiang; break;
    default: break;  // 社交/盗采/夺位/寻仇：任务 4–6
    }
}
// 年末孤儿边诊断：bonds 里任一端不在活人 index 集里 → ++orphan
```

  注意：roam 的两把掷运（落点、遇险）**必须用不同的派生输入**——
  落点用 `Roll::roam` 单键，遇险在同一把 rng 上再取一次
  （`rng.below(w*h)` 后接着 `rng.below(1000)`，一把流两个数，顺序固定，
  这也是无状态且确定的）。slay 实现：`ledger.burned_total +=
  purse->stone; purse->stone = 0; bonds.erase_soul(id->index);
  cmd.destroy(e);`。
- [ ] **Step 4: 通过 + 全量回归**（注意 test_life_cultivation 的人口
      读数可能因历练加成漂移——若漂移，用探针重钉该断言并在报告里
      说明）
- [ ] **Step 5: 报告并等提交指令**

---

### Task 4: 社交与记仇——Standing 与 BondGraph 落地

**Files:**
- Modify: `src/life/systems.cpp`（Act 的 social/grudge 分支）
- Test: `tests/test_life_decisions.cpp`（追加）

**Interfaces:**
- Consumes: `kSocialChancePermille`/`kGrudgePermille`、`BondGraph::tie`
- Produces: 社交/记仇的完整语义（差异 9），后续任务的 Standing 读写
  与仇怨边都依赖它

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(social_makes_favor_edges_and_lifts_standing)
{
    // 跑 3 年（社交机会 20%/年）：事件表 social 非空且 amount==1 的
    // 条目存在；BondGraph 出现 favor 边；存在 by_sect 值 > 0 的活人
    // （声望升过）。钳位测试：跑 200 年后遍历所有 Standing，
    // 每维 ∈ [-5, 5]。
}

TEST(bullying_makes_feuds_and_grudge_deepens_them)
{
    // 立威（amount==2）事件存在；立威者仇家（counterparty）的
    // by_sect[立威者宗] 声望出现过负值；feud 边存在（立威结怨或记仇
    // 两路都算——断言只钉「feud 边存在」与「强度 ≤ 5」）。
}

TEST(grudge_motive_adds_a_feud_edge)
{
    // 判据 3 的怯懦侧需要「有难」事件——被逐是任务 6 的产物，本测试
    // 用 200 年长跑让立威结怨喂出 feud 边后再验：存在 strength ≥ 2 的
    // feud 边（记仇强边或立威+记仇叠加）。探针钉实测下界。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现** social/grudge 分支（差异 9 的
      语义逐字：对象由 Decide 已择定写入 `motive.arg`；Act 只执行。
      结善缘的声望 +1 落在「双方各自所在宗」的两维上钳 5；立威择弱
      ——对象 power 低于本我才立威，否则降级为结善缘（对象是 Decide
      择的，Act 里判 power，弱才威）。`Temper` 推移本任务不用——
      出走/被擒在任务 6/8）→ **Step 4: 通过 + 回归** → **Step 5: 报告**

---

### Task 5: 盗采——私铸入账、被擒与横死

**Files:**
- Modify: `include/gamev2/life/economy.hpp`（`EconomyLedger` +
  `poached_total`、`shorted`）
- Modify: `src/life/systems.cpp`（Act 的 poach 分支 + 执法战斗）
- Test: `tests/test_life_conflict.cpp`（新建，进 `LIFE_TEST_SOURCES`）

**Interfaces:**
- Consumes: `kPoachYieldStone`/`kPoachCaughtPermille`、Task 3 的 slay
- Produces:

```cpp
// economy.hpp
struct EconomyLedger
{
    // ……既有字段照旧……
    std::uint64_t poached_total = 0;   ///< 盗采私铸累计（并入 minted 侧）
    std::array<bool, 8> shorted{};     ///< 上年该宗是否克扣（Birth 读）
};
```

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(poaching_mints_into_purses_and_the_identity_survives)
{
    // 跑 200 年（断供宗弟子 + 贪婪散修持续盗采）：poached_total > 0；
    // 逐年恒等式照 the_identity_holds_every_year 的形态成立（盗采入
    // purse 同时入 minted_total——两侧同增）。minted_total ==
    // 420000 × 年数 + poached_total（快照铸币 + 私铸，逐字断言）。
}

TEST(poachers_lose_standing_and_some_die_or_get_branded)
{
    // 跑 200 年：存在 by_sect[7] < 0 的活人或死者记录（散修圈声望扣）；
    // poach 事件存在；被擒的两条出路至少一条可见——slain 事件存在
    // （被擒败）或存在 crimes > 0 的活人（被擒胜、记罪）。被擒率
    // 30‰/年 × 盗采人数 × 年数，200 年内两路都该出现；探针钉实测。
}

TEST(poach_deaths_clear_accounts_exactly_once)
{
    // 横死者私囊的清算只发生一次（slay 清零在先、Economy 的死者账消
    // 对已死空位跳过）：逐年恒等式 + 死者 purse 为 0（若实体还在
    // flush 前被 view 看到）——用恒等式逐年成立代证，另加
    // stone_conservation_broken == 0。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现** poach 分支（差异 7/8 逐字：
      `purse->stone += kPoachYieldStone; ledger_.minted_total +=
      kPoachYieldStone; ledger_.poached_total += kPoachYieldStone;
      standing->by_sect[7] = clamp(by_sect[7] − 1, −5, 5);` 事件
      `poach{amount = yield}`；被擒掷运 `Roll::poach_find`；执法战斗用
      `combat_roll(seed, year, poacher, 0)`（对手键恒 0——合成执法者
      不是灵魂，键位让出）＋合成 power（差异 8），败走 slay + slain
      事件，胜走 `standing->crimes += 1`）→ **Step 4: 通过 + 回归** →
      **Step 5: 报告**

---

### Task 6: 战斗一步判定、夺位与寻仇

**Files:**
- Modify: `include/gamev2/life/combat.hpp`（+`contest_won` 公共纯函数）
- Modify: `src/life/systems.hpp` / `src/life/systems.cpp`（avenge/contest
  分支、SoulId 迁移）
- Test: `tests/test_life_conflict.cpp`（追加）

**Interfaces:**
- Consumes: `kCombatSwingPermille`/`kCombatMin/MaxPermille`/
  `kArtifactGradeStone`、`kContestChancePermille`
- Produces:

```cpp
// combat.hpp（续 Task 1）
/// 夺位成败（设计 §2.3：读境界差与门内声望，不走 duel）。
/// 胜率 = clamp(500 + 100×(境界差) + 40×(门内声望差), 50, 950)‰。
[[nodiscard]] bool contest_won(std::uint64_t seed, std::uint32_t year,
    const SoulId& challenger, const Cultivation& ca,
    const Standing& sa, const SoulId& master, const Cultivation& cm,
    const Standing& sm);
```

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(duel_is_deterministic_and_upside_is_bounded)
{
    // 同 (seed, year, 双方) 连掷 1000 次同果（确定性）；构造双方——
    // 不经人口，直接调 duel（它是自由函数，测试可直调——放 systems.hpp
    // 私有头？不行：测试不进私有头。裁定：duel/contest_won 放
    // soul.hpp 公共头（纯函数、无状态，与 lifespan_reached 同例）。
    // 断言：同境界同体魄同财力 → 胜率约半（1000 个不同年份采样，
    // 胜次数 ∈ [400, 600]）；差一境（10 点 power）→ 胜率 > 60%；
    // 差两境 → 胜率 > 85%。
}

TEST(contest_transfers_the_seat_or_exiles_the_loser)
{
    // 长跑 300 年：contest 事件存在（50‰/年 × 合格长老数——探针钉）。
    // 每条 contest 事件后：胜者（amount==1）actor 的 Office ==
    // sect_master 且 counterparty 不再是该宗 master；败者（amount==0）
    // 的 counterparty……——事件表只记 index，验证走「按 index 查实体」
    // 的 helper：胜者接位、旧主被逐（sect 变 unowned）或已在 slain 里；
    // 败者被逐或被杀。SoulId 迁移后 diag.office_without_sect == 0
    // （迁移写入者纪律的自检）。
}

TEST(avenge_settles_feuds_through_duels)
{
    // 长跑 300 年：avenge 事件存在；每条 avenge 的 actor 与
    // counterparty 之间在事件前必有 feud 边（因果方向钉死：仇在先、
    // 报在后——按 (year, index) 序在事件表与边表上交叉验证）。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现**（duel/contest_won 放公共头
      soul.hpp（测试要直调，与 `lifespan_reached` 同例——纯函数）；
      avenge 分支：duel(actor, wrongdoer)，胜 = 对方 slay + avenge 事件
      {amount 1} + 己方 standing[宗] +1；败 = 己 slay + slain 事件；
      contest 分支照差异 10 的胜败清算：胜 = `office->kind =
      sect_master`（旧主 Office→none、SoulId→unowned、互结 feud、
      expulsion 事件 + contest{amount 1}）；败 = 宗主 mercy ≥3 → 逐
      （同上迁移 + expulsion + contest{0}），否则 slay + slain +
      contest{0}。Temper 推移 `nudge` 在此接入：被逐者 order −1
      （差异 18））→ **Step 4: 通过 + 回归** → **Step 5: 报告**

---

### Task 7: 政事段——晋升阶梯、宗主换代、断供出走与初始关系网

**Files:**
- Modify: `src/life/setup.cpp`（seed：初始师徒/亲缘布网——`Roll::
  bonds_init` 派生：每宗宗主→诸长老 master 边（强度 3）、每长老→
  在册/40 名内门外门弟子 master 边（强度 2）、在册/20 对随机 kin 边
  （强度 3）；散修圈 kin 对 = 300/20）
- Modify: `src/life/systems.cpp`（Act 末尾政事段：Office.years 维护、
  晋升、长老补位、宗主推举、断供出走）
- Test: `tests/test_life_politics.cpp`（新建）

**Interfaces:**
- Consumes: `kInnerNum/kInnerDen`/`kMinElders`/`kElderDivisor`/
  `kStarveExitYears`、Task 5 的 `shorted`、Task 6 的 SoulId 迁移先例
- Produces: `promotion`/`succession`/`departure` 事件与完整晋升阶梯
  （差异 11），后续任务的换代/出走断言依赖

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(initial_bond_web_covers_every_sect)
{
    // seed 后即验：每宗都有 master 边（宗主对长老）；kin 边存在；
    // 边两端 index 都在名册里（无悬空边——orphan_bonds == 0）。
}

TEST(the_ladder_refills_offices_and_keeps_ratios)
{
    // 跑 50 年：promotion 事件存在（新生儿都是外门，晋升必发生）；
    // 逐年检查每宗 inner 数 ≥ 在册 × kInnerNum / kInnerDen − 1
    // （−1 容整除余量）；elder 数 ≥ kMinElders（有足够活人时）；
    // 每宗至多一个 sect_master（任何年）。
}

TEST(succession_follows_master_death_within_a_year)
{
    // 长跑 300 年（或探针找到的宗主寿终年）：succession 事件存在；
    // 事件年该宗前任 master 已不在活人集（寿终或夺位——本测试钉
    // 「宗主位空悬后一年内补位」：逐年扫每宗，master 缺位只允许出现
    // 在死亡当年）。若 300 年无寿终换届，用夺位事件验同一不变量
    // （差异 16 的探针先跑）。
}

TEST(the_starved_low_order_outer_flee_the_sect)
{
    // 断供宗（青囊/天机/九幽）跑 5 年：departure 事件存在；出走者
    // 事发前 starved ≥ 2 且 order ≤ 2（按 index 回查事件前状态——
    // 探针辅助）；出走后 sect == unowned 且 office == none；
    // office_without_sect == 0。出走者与原宗主之间有 feud 边
    // （差异 10）。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现**（政事段逐年跑在 motive 分支
      之后：先 `office->years += 1`（活人且 kind != none）；断供出走
      （读 `office->starved`——**注意任务 8 才让 Economy 写 starved，
      本任务政事段的出走判定读到的恒 0，测试 Step 1 的第 4 条会因此
      挂——把它挪到任务 8 的测试文件里实现**，本任务只写出走代码路径
      与事件；其余三条测试本任务内必须绿）；晋升三段照差异 11 的
      score 与缺额公式，遍历序按 `SoulId::index` 升序保证确定性；
      推举噪声 `Roll::succession`）→ **Step 4: 通过 + 回归** →
      **Step 5: 报告**

---

### Task 8: 经济接线——断供计数、收徒受供给约束、买入扩面

**Files:**
- Modify: `src/life/economy.cpp`（分配段写 `Office::starved` 与
  `ledger_.shorted`；买入段扩面；两趟谓词抽 `alive_member`（差异 17））
- Modify: `src/life/systems.hpp`（EconomySystem WriteSet + Office）
- Modify: `src/life/systems.cpp`（BirthSystem：shorted 宗停收）
- Test: `tests/test_life_politics.cpp`（追加，含任务 7 挪来的出走测试）

**Interfaces:**
- Consumes: Task 5 的 `shorted` 字段、Task 7 的政事段
- Produces: `starved` 的唯一写入者（EconomySystem）、断供→出走与
  断供→停收两条反馈回路

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(starved_counts_consecutive_shortfall_and_resets)
{
    // 青囊（mint 0）第 1 年分配 per_head == 0：全宗 starved == 1；
    // 第 3 年 == 3。太玄（mint 260000）全宗恒 0。中途太玄不会断供
    // ——重置分支用「造一个金库见底的世界」测不了（mint 是快照），
    // 改在 128²/seed13（只有太玄有脉，六宗断供）上验「断供宗 starved
    // 递增」；重置分支的验证并入 emergence 长跑（人口回落期断供解除
    // 后出走停止——间接断言：departure 事件年份分布不贯穿全程）。
    // 直测重置不可行是快照铸币的边界，记录在测试注释里。
}

TEST(births_stop_in_shorted_sects_and_continue_elsewhere)
{
    // 跑 5 年数各宗人数增量：断供三宗第 2 年起增量为 0（第 1 年
    // Economy 还没跑、shorted 全 false，照常收徒——次序记录在测试
    // 注释）；太玄逐年 +≥1。散修批不受 shorted 影响（vault[7] 无份
    // 可断）。
}

TEST(sect_members_buy_stones_from_their_own_vault)
{
    // 银两只在 Economy 与 Act 写、来源是经营/营生；长跑 200 年：
    // 存在 stone > 0 的非散修且其 silver 曾降到 < 2000（买了）——
    // 直测难，改断言形态：恒等式逐年成立（划转两侧同在账内）+
    // 探针打印「非散修 stone>0 计数」钉下界 > 0（经营者攒钱买入）。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现**（Economy 分配两趟合并谓词后
      在第二趟写 starved：`per_head < want` → `++office->starved`，否则
      `= 0`；任一职级克扣 → `ledger_.shorted[sect] = true`（每年开头
      全清再置）；Birth 的 recruit 循环里 `if (ledger 短供) continue`
      ——BirthSystem 构造参数加 `const EconomyLedger&`；买入段把
      「散修走 vault[7]」改成「sect 成员走 vault[sect]、散修走
      vault[7]」——同一循环两分支，写集不变）→ **Step 4: 通过 +
      回归 + 重钉**（收徒停了之后 `test_life_cultivation.cpp` 的
      人口读数（如 100 年 1923 人）必漂移：探针重跑钉新值，更新
      断言与 `kSectRecruitDivisor` 注释里的读数段——注明「刀 4 断供
      停收后重钉」）→ **Step 5: 报告**

---

### Task 9: 突破资源门槛、潮位耦合与元婴闭关锁

**Files:**
- Modify: `include/gamev2/life/constants.hpp`（+`breakthrough_chance` 公共
  纯函数；与 `lifespan_reached` 同处一文件）
- Modify: `src/life/systems.cpp`（CultivateSystem 判定改造；Decide 的
  判据 0/0.5 已在 Task 2 落位，本任务接上锁的读侧与闭关寻衅掷运）
- Modify: `include/gamev2/life/constants.hpp`（重标突破四常数与潮位
  下限——差异 12/13 的值已在 Task 1 落位，本任务核对注释里的旧读数
  段并更新（差异 17））
- Test: `tests/test_life_cultivation.cpp`（重钉）＋
  `tests/test_life_conflict.cpp`（追加门槛与潮位断言）

**Interfaces:**
- Consumes: `kBreakthroughCost`、`kTideBreakthroughFloorPermille`、
  `compute_spirit_tide`（`world/spirit_tables.hpp`）、`Site`、
  `world.domain_grade`
- Produces: CultivateSystem 构造参数 + `const gamev2::world::World&`；
  突破判定的新语义（后续 emergence 测试的人口动力学基础）

- [ ] **Step 1: 写失败测试**：

```cpp
TEST(breakthrough_requires_paying_the_cost_both_ways)
{
    // 门槛是「尝试时付、成败都烧」：判据用 burned_total 的增量——
    // 掷输也烧钱，故 burned 的增量比 realm 的变化更可靠。
    // 场景一（付不起）：取一个外门弟子，把 progress 拉到引气门槛、
    // purse.stone 清 0，跑 1 年：无尝试（burned_total 不变、私囊不变）。
    // 场景二（付得起）：同一场景给 purse.stone = 1（进炼气的门槛），
    // 跑 1 年：burned_total 恰增 1（付账即烧），realm 变不变由掷运决定
    // ——不钉 realm，钉「付费发生了」。
    // 场景三（筑基门槛）：筑基者 progress 拉满、私囊 2500 → burned
    // 增量恰 2500。
    // 注意：不能断言「100 年内无人突破」——进炼气的门槛只有 1 块，
    // 外门弟子第一年就有 150 份额，炼气突破早晚会发生。
    // 反面校验：把 purse 撤走重跑，burned 增量归零。
}

TEST(jindan_attempts_need_thick_ground)
{
    // 注资一批准筑基（progress 拉满、给足 33333），跑 1 年：只有
    // Site 在 thick 格（domain_grade == thick）者可能突破；非厚域者
    // 原地不动、不扣钱。断言：突破者 ⊆ 厚域者集合。
}

TEST(yuanying_locks_a_century_and_seclusion_invites_feuds)
{
    // 注资一批金丹（progress 拉满），跑 1 年：seclude_until ==
    // year + 100 且当年不掷突破；跑到第 100 年：锁满者开始掷
    // （付 333333——探针注资）。锁内每年 seclude_hazard 掷运
    // （10‰）——300 年长跑下 slain 事件里含「闭关被寻衅」死者
    // （事件 counterparty = 寻衅者 index，与 actor 有 feud 边——
    // 探针钉存在性）。
}

TEST(breakthrough_odds_hit_the_three_anchors)
{
    // 潮位钉死的年份上统计：造 999 个九档悟性的临时世界太贵——
    // 直测公式：把 chance 计算抽成公共纯函数
    // breakthrough_chance(aptitude, tide_level)（放 soul.hpp，与
    // lifespan_reached 同例），断言悟性 5 档、潮位满（255）时
    // == 300；悟性 9 == 500；悟性 1 == 200（钳位生效）；潮位 86
    // （谷底）时三值 × 86 / 1000 向下取整，且不低于
    // kTideBreakthroughFloorPermille。
}
```

- [ ] **Step 2: 失败** → **Step 3: 实现**（CultivateSystem 改造按差异
      12/13 逐字：progress 满门槛后依次过「元婴锁（jindan 满门槛时置
      `seclude_until = year + kRealmYears[jindan]`、当年返回）→ 金丹
      厚域（`world.domain_grade.at(site->cell) != thick` 则返回）→
      付账（不足返回；足则 purse 扣、burned 加）→ 掷运（**既有派生链
      一字不动**，chance 换 `breakthrough_chance(悟性, 潮位)`，潮位 =
      `compute_spirit_tide(clock->year).level`）」；元婴锁期间 Decide
      的判据 0 自动写 seclude（Task 2 已落位）、Act 的 seclude 分支补
      寻衅掷运：有 feud 边的活人存在且 `Roll::seclude_hazard` 中 →
      与最近仇家 duel，败 slay）→ **Step 4: 通过 + 全量回归 + 重钉**
      （突破变贵变难：`test_life_cultivation.cpp` 的全部人口/境界读数
      用探针重跑重钉；「寿元表逐字」与确定性类断言不动）→
      **Step 5: 报告**

---

### Task 10: 涌现验收招牌四条与终检

**Files:**
- Create: `tests/test_life_emergence.cpp`（进 `LIFE_TEST_SOURCES`）
- Modify: 受探针结果牵动的常数（`kContestChancePermille`/
  `kPoachYieldStone`/`kRoamHazardPermille` 等——只许动标定值，
  不许动结构）

**Interfaces:**
- Consumes: 前九个任务的全部行为面
- Produces: 招牌四条的自动判定（设计 §8：比对「事件类型出现与否」与
  「人口统计的形状」，不比具体人物命运）

- [ ] **Step 1: 探针先行**（`agent-temp/` 临时程序，跑 256²/seed13
      300 年无注入，打印：逐年活人数、逐类事件计数、换届/出走/冲突
      的首末年份、灵潮相位切换年（8914+86=9000 灵潮期始）前后的人口
      与突破计数）。**校准循环**：若 ① 不成立（无 succession/contest）
      先调 `kContestChancePermille`；② 不成立调 `kStarveExitYears`
      降为 1 或 `kPoachYieldStone`（让断供宗更绝望）；③ ③类事件不足
      调 `kPoachCaughtPermille`/`kSocialChancePermille`；④ 不成立先调
      `kTideBreakthroughFloorPermille`（调低 = 谷底期突破近停 → 低境
      早死 → 人口下降），仍不成立则加**潮位收徒门**（谷底期各宗停收，
      新增标定常量 `kRecruitTideGate`，BirthSystem 读潮位相位决定收不
      收徒）——这是收徒侧唯一的结构缺口，加它要连带在常数注释里写明
      锚点。每调一轮重跑探针，全过为止；
      钉死的值回写 constants.hpp 注释（锚点 + 实测读数，照
      kSectRecruitDivisor 的注释体例）
- [ ] **Step 2: 写四条招牌断言**（fixture 256²/seed13，300 年，跑在
      一个 `Life` 实例上、四年段共用事件表与人口采样）：

```cpp
TEST(banner_1_unscripted_succession_or_contest)
{
    // 事件表含 succession 或 contest；且换代不是「每年都换」的噪声：
    // 换代类事件总数 ∈ [1, 30]（探针钉上下界）。
}

TEST(banner_2_starvation_drives_departures)
{
    // departure 事件 ≥ 1（探针钉实测值）；每条 departure 的
    // actor 在事发年属断供宗（青囊/天机/九幽——按事件年回查事件表
    // 前该 actor 的 sect 不可行，改为断言「出走事件的 sect 字段 ==
    // 断供宗」——departure 事件落表时 sect 记出走前宗门）。
}

TEST(banner_3_conflicts_of_the_poach_contest_avenge_kind)
{
    // poach/contest/avenge 三类事件合计 ≥ 探针实测值（下界钉死，
    // 上界不设——冲突是涌现不是配额）。
}

TEST(banner_4_population_follows_the_tide)
{
    // 探针钉死的年份段：谷底段（探针给出的下降区间）末活人数 <
    // 段初；灵潮期始（9000 年附近，探针确认）之后存在一个年份的
    // 活人数 > 谷底段末。断言只钉「形状」，不钉绝对数（绝对数随
    // 标定变，形状是结构）。
}

TEST(full_pipeline_determinism_300_years)
{
    // 两个同 seed 世界 300 年：事件表逐条相同（年份/类型/actor/
    // counterparty/amount）；按 index 收齐的 (realm, progress,
    // stone, silver, office, sect) 逐位相同。这是设计 §8 确定性类
    // 在刀 4 形态下的完整形态。
}
```

- [ ] **Step 3: 通过 + 全量回归七个 `*_tests.exe`**（605 条基线 +
      本刀新增，全绿；`test_life_cultivation.cpp` 的重钉断言已在
      任务 8/9 完成）
- [ ] **Step 4: 变异自检（不写进测试，写在报告里）**：对照设计 §8
      的变异要求逐条说明——性格轴退出判定 → 招牌②红（出走读
      order）；盗采不入账 → 恒等式红 + ③红；断供不计数 → ②红；
      潮位因子删掉 → ④红
- [ ] **Step 5: 报告并等提交指令；提交后按 [[feedback-sdd-knife-workflow]]
      走收尾菜单（本地合并回 main、删分支、删 SDD 工作区、不 push）**

---

## 收尾检查清单（终审用）

- 恒等式在「盗采私铸 + 门槛烧毁 + 横死清账 + 买入划转 + 被逐迁移」
  五条新通道下逐年成立（300 年）
- `office_without_sect == 0`、`orphan_bonds == 0`、
  `stone_conservation_broken == 0`，全跑程
- 既有掷运（Cultivate 突破）派生链未被触碰——刀 2 确定性资产完好
- 事件表是唯一「谁在何年对谁做了什么」的权威——刀 6 传承接管与
  刀 7 编年都从它取材，字段不得事后改义
- 刀 5 的接缝清单：铸币快照换产出清单（`mint_per_year` 退役）、
  派系层发现判定接入盗采被擒率、`FactionDemand` 从 Summarize 步来

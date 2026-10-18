# 游戏层设计文档

- 日期：2026-10-17
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23
- 前置模块：`ecs`、`genetics`、`environment`、`world`（见同目录四份设计文档）
- 配套：`doc/美术风格规格.md`（《世界意志》美术规格，视觉层不在本文范围）

## 1. 背景与目标

四个模拟模块已落地并能长期存续：同 `root_seed` 逐位可复现，`run_seasons(5)`
与五次 `run_season()` 逐项相同，饥荒与承载力都是涌现的。`src/sim/main.cpp`
的三个预设证明这套内核能回答真实问题（`evolve` 停在 Σcapacity 的 89.6%、
`persist` 停在 124.4%，两者只差 Lifespan 一个量）。这两个读数取自决策 14
那次行为变更之前，现行读数见决策 14。

缺的不是模拟器，是**玩家**。当前没有任何输入入口，`SeasonReport` 只有
几个标量出来——这是一台能跑没有人看的仪器。

而「没有人看」不只是缺 UI。现有内核在结构上产不出进化游戏唯一的看点：

- **一个世界只有一个物种。** `SpeciesRegistry` 构造期静态登记，每个 id
  一张固定 `SpeciesMask`，没有繁殖隔离、没有物种形成、没有生态位。
- **没有种间关系。** `threats` 恒为 0（`genetics_systems.cpp` 硬编码），
  `escape` 退化成 `clamp(Speed × escape_responsiveness)`；`Strength`
  全仓库无任何机制读取，那部分遗传方差在空转。这两条正是
  `doc/research/species-alre/analysis/gameplay-design.md` 对 Species 的
  核心批评（物种形成次序错、生态位过薄）。
- **`MovementSystem` 是纯随机游走。** 无地形阻挡、无趋向，一步一季，
  期望位移 √n。

### 1.1 设计目标

- 玩家是**抄经人**：只在世界层面施加压力与读取，永不直接操作个体
- 世界**自己**裂变出物种，玩家不手动分株
- 一季内 8 个子步连续播放，季末定格给玩家读与干预
- 胜利条件可由确定性模拟直接判定
- 不改动四个既有模块的语义；改动既有契约的地方逐条写明并推翻

### 1.2 非目标

本轮明确不做：

- 渲染与 UI 实现（美术规格已定，本文只定「游戏层提供什么给渲染」）
- 具体关卡与难度曲线
- 种间竞争与生态位细分的完整建模（本文只做到能裂变，不做食性分化）
- `threats` 的捕食来源（仍恒为 0，属遗传模块未决事项）
- 存档格式（`run_seasons` 与逐季推进的逐位等价性是它的前提，那条已成立）
- 手柄、无障碍、本地化

---

## 2. 设计决策记录

以下 15 项为本设计的显式决策。

| # | 决策 | 理由 |
|---|------|------|
| 1 | 玩家定位为抄经人，永不操作个体 | 现有内核的地基是「世界自己往前跑」，这是唯一不破坏该地基又能产生反馈闭环的定位。扮演个体要求把 `run_season` 从季降级为帧并给单个体开连续模拟，那是重写时序 |
| 2 | 多样性由世界自己裂变，不做玩家手动分株 | 手动分株零新模拟机制，但世界内部仍只有一条谱系，玩家所有干预作用在同一条血脉上。裂变才是「真实进化」的看头 |
| 3 | 胜利条件 = ≥N 个物种共存 ≥M 季 | 判据可由确定性模拟直接判定，天然适配回放。无目标沙盒会把「看不懂/不会玩」这个 Species 失败模式原样复现 |
| 4 | tick 化只拆 `EnvironmentSystem`，`MovementSystem` 与 `BreedingSystem` 留在季首/季末 | 8 个子步全封在 `sites.cpp` 的 `SeasonFeeder` 里，只有环境需要暴露。移动拆到 8 个 tick 会让扩散率涨 √8 倍并打乱既有平衡；改为渲染时插值 |
| 5 | 地形场并入 `environment/`，不新开模块 | 与 `ClimateField`/`ResourceField` 同为 4096 格定长遍历，再开一个平行模块只多一层同构代码 |
| 6 | 新物种的 `SpeciesMask` 从母物种继承 | 掩码表达的是「这个物种用哪些位点」，裂变出的子物种在遗传上与母物种同源；给玩家一份手挑掩码是玩家分株，属非目标 |
| 7 | 物种形成要四个条件同时满足 | 缺任一条就会复现 Species 的具体缺陷，见 §5 |
| 8 | 裂变群体基数下限 `min_founder` 与胜利判据 `min_viable` 是两个常数 | 前者是「这么小的群体裂出去必然夭折」，后者是「这么小的物种不叫共存」。混成一个数会让关卡难度不可调 |
| 9 | 胜利判定与干预结算都只读世界状态，不改世界 | 抄经人不能通过「判定」偷改世界。这也让判定可以被测试直接调用 |
| 10 | 交付拆四阶段，阶段 1 与阶段 3 各自可玩 | 阶段 2（地形）没有阶段 3 就只能看到同一物种分成两群；阶段 3 单独交付即完整 |
| 11 | `TerrainField` 参与确定性契约 | 地形由 `root_seed` 生成，同种子必同图。它若不参与，`equal_seeds_produce_identical_seasons` 立刻失效 |
| 12 | 不推翻 `world::World` 的「非重入」与「`worker_count` 只影响并发批次」两条契约 | 新增的 `GameSession` 仍是单线程驱动，与之相容 |
| 13 | `MovementSystem` 的趋向初版只取资源梯度，同种趋向推迟 | **结论保留，论证已被决策 15 整体替换**：原文的两条理由（屏障要窄到几乎不成立、承载力全局界会被破坏）经实测均不成立。**但本行随后那句「反而精准指向空而肥的格」也已被 §2.2 推翻** —— 梯度不填空，它重新分配 |
| 14 | 接受「寿命到期者当季不再参与繁殖」，不为它给 `Scheduler` 加免 flush 的入口 | 旧行为是 flush 时机的产物而非设计意图——当初老化与繁殖能共享同一次 flush，只因四个 system 都装在同一个 `Scheduler::run` 里（旧代码里 `tests/test_world_module.cpp` 的 `spawn_doomed` 注释把这称作「这对本模块无影响」，该注释已随行为变更改写）。要复原就得让 `World` 自己掌控 flush 时机，那要动 `ecs` 模块，越过本阶段的边界。代价是 `sim` 基准读数需重新取（`evolve 200` 末季 8688 → 8922，`persist 200` 12243 → 12205，`fragile` 归零点 44 → 52 季）。**这些读数取自 `MovementSystem` 重写之前（§4.2 属阶段 2）；阶段 2 改完需重新取数并回来更新本行。** |
| 15 | 阶段 2 的趋向选型由实测数据定案：追**存量**、4 邻域、50/50 与随机游走各半；地形噪声改值噪声，基频 0.10、海平面取 70 百分位 | 见 §2.1 的实测与 §2.2 的修正。简述：实测推翻了决策 13 的两条反对理由——45% 的有主格已经超载，承载力全局界早已失效；「富格」恰恰是空的那批（高存量四分位 331/666 为空格，低存量四分位 0/666）。**注意本行原写「追存量因此是填坑而非拥挤」，那句已被 §2.2 推翻** —— 梯度不填空，它重新分配。同种趋向无数据支持且会让空格更多，推迟到阶段 3 与 `SpeciationSystem` 同出 |

第 4 条值得单说，因为它是本设计与「把 `run_season` 整个拆成 8 帧」这个最
自然的想法之间唯一的分歧点。`steps_per_season = 8` 的 8 个子步只存在于
`src/environment/sites.cpp` 的 `SeasonFeeder` 里——`step()` 每次先再生、再逐格
按存量均分摄食，`finish()` 把累加值归一化后一次性把 `Feeding` 写回。
要让玩家看得见「资源被吃穿」，只有这一段需要变成可中断的。
`MovementSystem` 拆到 8 个 tick 会把一季的
总位移从 1 格变成 8 格，随机游走的期望位移从 √n 变成 √(8n)，而
`mating_radius_cells`、`build_sites` 的站点结构、乃至 `evolve` 89.6% 这个
平台值都会跟着动——为了一段动画去重调整套平衡不划算。渲染时把季首那一步
插值成 8 帧，视觉上等价，代价为零。

第 9 条同样要说清楚：`GameSession` 拥有 `world::World`，因此它在技术上
「能够」直接改世界。胜利判定写成只读函数、干预写成显式的
`apply_*` 方法且每条都返回它改了哪些格子，就是为了让「玩家没有作弊路径」
这件事在代码结构上看得见，而不是靠约定。

### 2.1 阶段 2 的实测依据（决策 15 的来源）

`evolve` 预设、种子 20261016、200 季末态，逐格统计取自 `world::World` 的
公开只读接口（`environment().resources()` 的 occupants / capacity / stock）。
探针是一次性的，源码留在 `agent-temp/probe_diffusion.cpp`，未进版本库。

| 指标 | 读数 |
|------|------|
| 有个体的格 | 3570 / 4096（87.2%），第 20 季起稳定 |
| 单格最多个体 | 10 |
| 超载格（occupants > capacity） | 1609 / 3570（45%） |
| 空但养得起 ≥1 个体的格 | 519，且永久空着 |
| 平均资源存量 | 0.40（容量区间 [0, 3.9]） |
| 单季位移均值 | 1.59 |
| x / y 象限分布 | 25.0 / 25.5 / 25.2 / 24.8（初始即各 25%） |

交叉表 —— 只看 capacity ≥ 2 的 2664 格：

| 分组 | 平均 occupants | 其中空格 |
|------|----------------|----------|
| 存量最低四分位（666 格） | 8.55 | **0 / 666** |
| 存量最高四分位（666 格） | 8.55 | **331 / 666** |

四条结论，逐条推翻或修正既有认知：

1. **「`population ≤ Σcapacity` 是要守住的不变量」不成立。** 45% 的有主格
   已经超载。决策 13 用它当资源梯度的主要反对理由，理由本身是空的。
2. **「资源梯度会让个体全挤到富格」恰好相反。** 富格就是空的那批：高存量
   四分位有 331 个空格，低存量四分位一个都没有。均值为 8.55 对 8.55 则说明
   占位数与存量本身无关。**原文此处写「追存量梯度是填坑，不是拥挤」，已被
   §2.2 推翻** —— 实测表明梯度是重新分配而非填充，详见 §2.2。
3. **地图的均匀是均匀播种的产物，不是扩散抹平的。** 象限分布初始就各
   25%，而 200 季的扩散长度仅 1.59 × √200 ≈ 22 格，不到地图半宽 32。
   因此纯随机游走在屏障面前本来就够用，决策 13 原文「屏障要窄到几乎不
   成立才隔离得稳」算过了头。
4. **「空掉的格会被重新殖民」确实成立，但余量不大**：87.2%，且 519 个
   空而肥的格一次也没被填上——随机游走没有方向感，走不回去。

   **两点限定**：其一，87.2% 是**地形之前**的数字，阶段 2 之后的平台值是
   76.8%（见 §7.1），两者口径不同（可通行格数从 4096 变成 1230）。其二，
   「519 个空而肥的格一次也没被填上」不能读成「梯度会填上它们」——那是
   §2.2 推翻的推断。这两条观察各自为真，合起来的结论当时是错的。

### 2.2 §2.1 结论 2 与 4 的修正（阶段 2 实现后实测）

上面结论 4 说「梯度会把那 519 个空格填上」，结论 2 说「追存量梯度是填坑，
不是拥挤」。**两句都被阶段 2 的实测推翻了**，推翻的过程本身值得留档。

**梯度不填空，它重新分配。** 富格被填上，中等格被腾空，净效果是空格**变多**。

判据「空格数」在结构上就测不出梯度：随机游走是扩散系数最高的过程，它让个体
铺到最多不同格子上，因此**最小化空格数**；梯度是吸引力，制造聚集、腾空其余，
因此空格数**上升**。**用空格数当判据等于奖励扩散、惩罚聚集** —— 与要测的性
质正好相反。实测三组单调趋势：第 8 季 11 对 21、稀疏配置 172 对 305、
权重拉满 9 对 675（随机对梯度，三组都是梯度侧更多）。

另外那条测试的夹具自身也是坏的：它复用的 `creature_phenotype()` 给
`Lifespan = 10`，而子代拿出生格真实气候、父代拿默认快照（完全免疫气候），
于是世界**第 9 季全灭**，读到第 60 季时「空格数」恒等于全部陆地格数 1230，
两组必然相等。

**梯度本身的价值没有被推翻**，只是代理指标选错了：它的本意是让个体待在好格
（摄食更好、存活更高），而正确的度量（个体落点格的实际存量均值，梯度臂显著高于
随机臂）由 `tests/test_movement.cpp` 的两条测试守着。

**这个错误的教训值得记住**：写测试判据时要先问「这个量在机制上会随被测行为往
哪个方向变」，而不只是问「它看起来相关吗」。519 这个数字是真的，那个方向也是
真的，中间那一步「梯度会走过去」是我没验的。

---

## 3. 总体架构

### 3.1 文件划分

```
include/gamev2/environment/terrain.hpp   TerrainField
src/environment/terrain.cpp
include/gamev2/ecs/adapt/movement_system.hpp   重写
include/gamev2/ecs/adapt/speciation.hpp   SpeciationSystem
include/gamev2/game/session.hpp          GameSession / GameConfig / 干预接口
include/gamev2/game/report.hpp           抄经人读的那一卷
src/game/session.cpp
src/game/report.cpp
tests/test_terrain.cpp
tests/test_speciation.cpp
tests/test_game_session.cpp
```

渲染层（`graphics/`）不在本文档范围，它只消费 `GameSession` 的只读接口。

### 3.2 依赖方向

```
            game::GameSession        graphics（未来）
                    │
                    ▼
             world::World           ← 编排粒度下沉到 tick
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   ecs::adapt   environment   genetics
   （五 system）   （+地形场）
```

`game/` 是新的最顶层，依赖方向与 `world/` 同样单向向下。它不引入任何对
渲染库的依赖——UI 要读的量全部由 `GameSession` 的只读访问器提供。

### 3.3 一季的执行序列

```cpp
SeasonReport World::run_season()      // 既有，保留为便捷包装
{
    for (std::uint32_t i = 0; i < config.steps_per_season; ++i)
    {
        step_once();
    }
    return season_report();
}
```

`step_once()` 推进一个子步，一季 = `steps_per_season` 个 `step_once()`。
`GameSession::advance_one_tick()` 直接调它。三者的逐位等价性由测试守住
（§9），它是「渲染层可以在任意 tick 边界暂停/加速」的前提。

```cpp
void World::step_once()
{
    if (tick_ == 0)
    {
        open_season();      // 推进三处季号 → scheduler_.run（移动 + 老化）
                            // → environment_.begin_season（气候推进 + 收集
                            //   forager，把子步的输入一次性备齐）
    }
    environment_.step();   // 第 i 步：再生 + 按存量均分摄食
    ++tick_;
}

SeasonReport World::season_report()
{
    // 少跑子步时在这里报一条 error 级诊断，不拦下
    environment_.finish(entities_);   // 写回 Feeding、算子代预算
    breeding_.execute(entities_, cmd_);
    cmd_.flush(entities_);            // 换代在此生效
    statistics_.execute(entities_, cmd_);
    ++season_;
    tick_ = 0;
    return report;
}
```

一季内 `step_once` 的调用次数由调用方保证等于 `config.steps_per_season`；
不等就是 bug，`season_report()` 会报一条 `WORLD_INCOMPLETE_SEASON`
error 级诊断——它不拦下，因为少跑一步只让摄食累加器少累一轮、季末 feeding
偏高，是静默偏松而不是崩溃。

#### 老化与繁殖不再共享同一次 flush

tick 化把 `EnvironmentSystem` 与 `BreedingSystem` 移出调度器之后，
`AgingSystem` 的销毁命令与 `BreedingSystem` 的创建命令**不再落在同一次
`cmd.flush` 上**。`AgingSystem` 仍在调度器里（决策 4），而 `Scheduler::run`
在自己末尾无条件 flush，于是老化的 `cmd.destroy` 在 `open_season()` 里就当场
生效——早于本季的摄食与繁殖。

后果：**寿命到期的个体在死亡当季不再参与繁殖，也不再参与存活抽签**。
tick 化之前它能被判死却仍完整地繁殖一次。

旧行为是 flush 时机的产物，不是设计意图：`AgingSystem` 与 `BreedingSystem`
当初能共享一次 flush，只因为四个 system 都装在同一个 `Scheduler::run` 里。
按决策 14 接受这个变更，并重新取 `sim` 的基准读数。

---

## 4. 四个新增子系统

### 4.1 TerrainField

```cpp
/// 与 ClimateField / ResourceField 同尺寸的定长场。本身不含任何模拟逻辑，
/// 只回答两个问题：这一格能不能站人（passable），这一格的海拔是多少
/// （elevation，供渲染做水墨晕染与玩家画屏障时的合法性提示）。
class TerrainField
{
public:
    /// 值噪声 3 个八度铺 elevation，再按全图第 water_fraction 百分位切水陆。
    static TerrainField generate(std::uint64_t root_seed,
                                 const EnvironmentConfig& config) noexcept;
    [[nodiscard]] bool passable(CellIndex cell) const noexcept;
    [[nodiscard]] float elevation(CellIndex cell) const noexcept;
};
```

`water_fraction` 是 `EnvironmentConfig` 的一个字段（默认 0.70，见下），取 config
而不是单值是为了跟 `ClimateField(root_seed, config)` 的既有惯例一致，也
好让「全陆地世界」只是 `water_fraction = 0` 这个合法配置而非特例分支。
`elevation` / `passable` 用 `std::array` 而非 `std::vector`，与
`ClimateField`、`ResourceField` 同形状（`kCellCount` 是编译期常量）。

**噪声用值噪声（value noise），不用 Simplex。** 美术规格 §2.4 写的是
「Simplex（或 Perlin，2D 即可）」，两者都在许可范围内；选值噪声有两条
理由。其一是确定性：Simplex 的 `F2 = 0.5(√3 − 1)` 与 `t = (x + y)·G2`
这串浮点运算对精度敏感，而本项目对逐位确定性的要求是硬契约（决策 11）。
其二是依赖：仓库至今零第三方依赖——ECS、线程池、测试框架全是手写的，
为一个二维高度场引一个噪声库与这个惯例不合。值噪声只用 lerp 与
smoothstep，跨编译器更稳，对一个只用来切水陆的高程场观感等价。

**八度参数照美术规格 §2.4**：3 个八度、lacunarity 2.0、gain 0.5。基频
取 **0.10** —— 既不是规格给的 0.02，也不是中途试过的 0.06。

**决定陆地是否碎成多块的不是基频，而是海平面落在高程分布的哪个位置。**
这一条是实测推翻的：先扫 (基频, 八度数, gain) 共 30 组，`largest/land` 的
取值范围**永远**在 74%–99% 之间，一个能用的都没有。原因是方差权重 ——
gain 0.5 下三个八度贡献 0.76 / 0.19 / 0.05，基频那层独占 76%，相关长度
16.7 格；而阈值取在第 35 百分位远低于中位数，平滑场的超水平集在这个高度
几乎总是连成一整块。**抬八度能量、降基频，都不动这个数。**

**海平面取 70 百分位，不写死常数。** 对 4096 个高程排序，取第 70 百分位
作为海平面。写死常数的话，不同种子的水陆比会在 15%–50% 之间乱跳，那个数
就成了一个平均数而不是保证值；排序只在世界构造时做一次（4096 个 float），
代价可忽略。

70 这个数是扫出来的，不是拍的。六个种子实测：

| 水陆比 | 最大陆块占陆地 | 次大陆格数 | Σcapacity |
|--------|----------------|------------|-----------|
| 35%（原案） | 73%–99% | 15–637（多为湖心小岛） | 6489 |
| 45% | 48%–98% | 17–956 | 5491 |
| 64% | 39%–90% | 71–572 | 3594 |
| **70%** | **33%–53%** | **226–474** | **2995** |
| 70%（基频 0.14） | 22%–46% | 206–344 | 2995 |

35% 那档是本设计最初写下的数，它给出的却是「2640 格单一大陆 + 15–26 格
湖心小岛」——小岛个体绕过去就行，**不构成屏障**，阶段 2 的招牌产出无从
发生。70% 是唯一一档每个种子都给出「主大陆 + 一块 226–474 格次大陆」的
组合。附带的好处：Σcapacity 落到 2995，正好是 §10 写的游戏规模目标
「3000 上下」，不必再单独调 `capacity_base`。

留白占七成也与水墨方向相合（美术规格 §2「晕染边缘柔和」「不做位图地形贴图」）。

`passable` 是编译期定长的 `std::vector<bool>`，`elevation` 是
`std::vector<float>`，与 `ClimateField` 同形状，`environment/` 内部遍历
场的方式完全一致。

**`ResourceField` 的构造签名相应改为 `(climate, config, terrain)`，不可通行
的格 capacity 直接给 0。** 这样 `Σcapacity` 如实反映可用面积（否则水格会
贡献一份永远用不到的承载力），`build_sites` 无需任何改动（水格 headroom
为 0 → progeny 为 0），`sim` 的「占 Σcapacity 百分比」读数也继续有意义。

**`TerrainField` 参与确定性契约。** 它由 `root_seed` 生成并被
`MovementSystem` 每季消费，因此必须逐位确定——否则
`equal_seeds_produce_identical_seasons` 立刻失效。这条由 `test_terrain.cpp`
的「同 seed 两张图逐位相同、不同 seed 不同」守住。

### 4.2 MovementSystem 重写

现状（22 行）：每季朝一个随机方向推一步，不看地形、不看资源、不看同种。
重写后每季依次：

1. 步长 `max(1, lround(clamp(Speed, 0, 3)))`，**整数格**。整数化是被
   沿墙滑行逼出来的：浮点步长没法逐格判碰撞。
2. 掷一次硬币（`uniform_index(rng, 2) == 0`）决定跟梯度还是随机游走，
   **p = 0.5**。
3. 跟梯度时，在 4 个**可通行**邻格里取**存量严格高于本格**者；若没有比本格
   更富的邻格（已在局部峰顶，或四面环水），退回随机方向。

   **必须是「严格高于本格」，不是「4 邻里最高」。** 后者会让个体在站上局部
   峰顶后仍被推向更贫的邻格，上面那条负反馈整个落空；而且存量全同时它退化成
   遍历序第一个邻格恒赢 —— 实测 200 个种子得到 128 / 22 / 21 / 29 的位移分布，
   **64% 朝 +x**，一条与资源毫无关系的恒定漂移。`tests/test_movement.cpp` 的
   `the_gradient_direction_is_symmetric_when_no_neighbour_is_richer` 守着这一条。
4. 随机时照旧 `uniform_index(rng, 4)`；落点不可通行则沿墙滑（先试 x 再试 y）。
5. 逐格推进，写回 `Position`。若当前格在推进后被判为不可通行，退回最后
   一个可通行格，不留 NaN。

**趋向追的是存量（stock），不是容量（capacity）。** 这一点是本节最关键的
取舍，两者行为差别很大：

- **存量**会被吃穿，因此带负反馈——挤进富格 → 存量被吃光 → 梯度翻转 →
  离开。不会终局拥挤在固定几个格。而且实测显示它精准指向那 519 个空而肥
  的格（§2.1 结论 2）。
- **容量**是静态的，永远给一个干净的梯度，但没有反向作用力：所有个体会
  一起爬向同一批格并留在那里。这正是决策 13 原文担心的「全挤到富格」，
  只是它发生在 capacity 上，不发生在 stock 上。

**梯度读的是上一季遗留的存量。** `MovementSystem` 在 `World::open_season()`
里跑，早于 `EnvironmentSystem::begin_season`，此刻资源场正是上季被吃穿后
的状态（实测均值 0.40）。因此梯度指向的是**上季吃剩得多**的格。

原稿在此处接了一句「实测显示它精准指向那 519 个空而肥的格」——**已由 §2.2
推翻**。实测表明梯度是重新分配：富格被填上、中等格被腾空，空格数反而上升。
正确度量是个体落点格的实际存量均值，由 `tests/test_movement.cpp` 守着。

**p = 0.5 是玩法分水岭。** 每季一次抉择，200 季就是 200 次，趋向与随机游走
势均力敌；随机性保留，不会全军爬上同一个局部极大。

**原稿在这里写的是「累计定向行程约 300 格，远大于地图半宽 32，足以跜到对岸」——
那句话因果反了，现予撤回。** 屏障**不需要**靠「走不远」来维持：4 邻域移动
逐格判通行 ⇒ 个体所在陆块的连通分量在移动下是不变量，而繁殖侧同样封闭
（水格 capacity = 0 ⇒ headroom ≤ 0 ⇒ 不发站点）。所以跨屏扩散在**结构上**
不可能发生，与走了多远无关。梯度步永远落在可通行邻格上，跨屏需要一条路径，
而屏障没有缝。

**接入方式**：`EnvironmentWorld` 多持一个 `TerrainField` 成员，并加一个
const 访问器（阶段 4 的 `paint_terrain` 才需要可变的那一个，现在不加）；
`MovementSystem` 的构造签名从 `(root_seed, season_index)` 改为额外接
`const ResourceField&` 与 `const TerrainField&`。**给两个窄引用而不是一个
`EnvironmentWorld&`**：本 system 真正读到的只有这两个场，把依赖写成它
实际读的东西，与它那份窄访问集是同一个原则。访问集不变（仍只读
`CreaturePhenotype`、只写 `Position`），因此它与 `AgingSystem` 仍然排成
**一批并行**——`World` 装配的那条 `batch_count() == 1` 断言应当一字不改
仍成立。

**这一层与地形互相成就。** 如果个体朝富格走，贴水的陆地格可选项天然比
内陆少，海岸与内陆会自发分化出不同的占位密度。**地形本身就构成栖息地
异质性，不需要玩家画屏障就已经在制造地理结构**；阶段 4 的玩家画墙只是把
这套结构冻结下来。

移动后的个体若落在不可通行格（浮点越界或地形被玩家改写），钳回最近的
可通行格，不留 NaN。

### 4.3 SpeciationSystem

`order = 2`，排在 `BreedingSystem`（order 1）之后。**不在调度器里**，
与 `PopulationStatisticsSystem` 同理由 `World::run_season` 在 flush 之后
直接调用——它读刚 flush 出来的实体表，而调度器批次的时序管不到这里。

它对 `World` 的写入只有两种：新登记一个 `SpeciesId`、改若干实体的
`SpeciesTag`。因此它 **必须** 在 `PopulationStatisticsSystem` 之前跑完，
否则季末报告会把刚裂出来的物种连同它的全部个体一起漏掉。

### 4.4 GameSession

```cpp
struct GameConfig
{
    std::uint64_t root_seed = 20261017;
    /// 胜利所需的并存物种数下限。
    std::size_t target_species = 3;
    /// 达到 target_species 后还需持续的季数。
    std::uint32_t target_seasons = 20;
    /// census 低于此值不算「存活」，因此不计入并存物种数。
    std::size_t min_viable = 20;
    /// 群体小于此值不裂变，挡住 Species 的「只有 1 个个体的物种」。
    std::size_t min_founder = 40;
    /// 遗传距离阈值 Σ|f_群体 − f_母种| / 2 的下限。
    float divergence_threshold = 0.06f;
    /// 裂变条件需连续满足的季数（迟滞）。
    std::uint32_t speciation_hysteresis = 4;
};
```

**阶段 1 已落地的只有前四个**（`root_seed` / `target_species` /
`target_seasons` / `min_viable`，见 `include/gamev2/game/session.hpp`）。后三个
`min_founder` / `divergence_threshold` / `speciation_hysteresis` 属 §5 的物种
形成规则，**阶段 3 才落地**，届时由新增的分裂 system 消费；本 spec 的代码块
保持完整形态作为阶段 2–4 的目标，读当前实现时不要按七字段去想。

GameSession 持有 `world::World`，提供：

- 只读：`environment()` / `species_registry()` / `report()` / `tick_in_season()`
- 推进：`advance_one_tick()`、`advance_one_season()`
- 干预：`paint_terrain(cells)`、`paint_climate(cells, delta)`、
  `cull_genome_pool(SpeciesId, range)`
- 判定：`check_victory()`（只读）、`check_extinction()`（只读）

**下面这份能力清单是阶段 2–4 的目标接口，阶段 1 一条都没落地。**
阶段 1 的读数来源只有一个：`scroll()`（`Scroll` 快照）。清单里的
`report()` / `check_victory()` 落地形态是 `scroll().report` /
`scroll().victory` 与 `scroll().seasons_to_victory`；`advance_one_tick()`
落地为 `advance_one_substep()`。`check_extinction()` / `environment()` /
`species_registry()` **不拟实现**：前两者属阶段 2 的地形与物种形成（§5），
灭绝判定属阶段 3；§1.2 非目标另已把「存档格式」等划出本轮范围。
清单原文保留作为设计目标的可读记录，**读当前实现请以
`scroll()` 为唯一读数入口，不要照这些名字加转发函数**
（胜利条件在 §10 里归阶段 3，但判定逻辑已在阶段 1 落地）。

三条干预都是**批量、可撤销性靠回滚存档**的，不做单格 undo。抄经人改的是
一整片天地，不是拧一个旋钮。

---

## 5. 物种形成规则

每季对每个物种 `s` 执行：

```
1. 连通分量：按 8 邻域连通图切分该物种的个体集合
2. 逐连通分量 g：
     a. size(g) >= min_founder               否则跳过
     b. g 与 s 的其余个体之间存在不可通行的水  否则跳过
     c. divergence(g, s) > divergence_threshold  否则跳过
     d. 满足上述三条的连续季数 >= speciation_hysteresis  否则跳过
     → register_species(继承 s 的掩码)，把 g 内全部实体的 SpeciesTag 改为新 id
```

四个条件各自挡掉一个具体缺陷：

| 条件 | 挡的是什么 |
|------|-----------|
| 空间连通分量 | 无隔离时全图连成一片，漂变互相抹平，永不裂变 |
| 不可通行的水 | 基因流仍然畅通，「裂变」只是采样噪声 |
| `divergence > θ` | 凭距离硬切出「看起来一样但算不同种」的物种 |
| `min_founder` | **Species 被玩家批评得最狠的那一条**：新物种只有 1 个个体样本，无法繁殖或繁殖困难而灭绝 |
| `hysteresis` | 群体规模在阈值上下抖时抖出一棵全是死枝的进化树 |

`divergence(g, s)` 定义为 `Σ_locus |f_locus(g) − f_locus(s)| / 2`，即
Nei 遗传距离的等位基因频率版本。它是纯 O(位点数 × 群体规模) 的计算，
不需要家谱——这与 `genetics` 模块已定的不追踪家谱契约相容。

新物种登记后**不回滚**。一旦裂变成立，两个群体在统计上就是两个物种，哪怕
它们随后又合流。这是有意的：`min_founder` 与 `hysteresis` 两道闸门已经
把「误判」压到足够低，而可回滚的分类学会让进化树变成一笔糊涂账。

---

## 6. 数据模型

### 6.1 抄经人读的那一卷

`SeasonReport`（既有）不足以支撑「读世界」。`game::Scroll`（季末卷）在其
之上补三样玩家真正要看的东西：

```cpp
struct Scroll
{
    /// 已完整结算的季数。阶段 1 落地时从 `season` 改名而来——
    /// report.season 是刚结算那一季的 0 基季号，两者只差 1，而两个都叫
    /// season 会让按 scroll().season 对齐 report 下标的代码静默错一季。
    /// 不变式 seasons_elapsed == report.season + 1 **只在结算之后成立**，
    /// 开局两者都是 0。
    std::uint32_t seasons_elapsed = 0;
    SeasonReport report;                        // 既有：population + 逐物种统计
    /// 本季新出现的物种。玩家靠这个知道自己的干预有没有生效。
    std::vector<BornSpecies> born;
    /// 本季 census 归零的物种。玩家靠这个知道哪一支灭了。
    std::vector<LostSpecies> lost;
    /// 是否满足胜利条件，以及还差几季。
    bool victory = false;
    std::uint32_t seasons_to_victory = 0;
};
```

`BornSpecies` / `LostSpecies` 携带 `SpeciesId` 与谱系信息（母物种 id 与
裂变发生的连通分量大小）。谱系是抄经人唯一能读懂的因果链：不带母物种，
玩家看到「物种 4 出现了」只会当成噪声。

### 6.2 victory 的判定顺序

`check_victory()` 在 `advance_one_season()` 的末尾调用，**只读**。它自己
维护一个 `coexist_seasons_` 计数：

```
并存物种数 = census >= min_viable 的物种个数
并存物种数 >= target_species  → coexist_seasons_ += 1，否则归零
coexist_seasons_ >= target_seasons → victory
```

玩家中途把物种数压回 `target_species` 以下，计数归零重来。这让「维持」
比「达到」更难，与 `persist` 预设暴露的「长寿命个体占住超载格、那部分
承载力永久停产」是同一种紧张感——玩家必须持续干预，而不能推一把就跑。

---

## 7. 契约

### 7.1 本设计推翻的既有决策

两条白纸黑字的既有决策被刻意推翻，实现时不要试图绕过：

| 出处 | 原决策 | 改为 |
|------|--------|------|
| `2026-10-16-world-design.md` §5.1 | 「`species()` 的登记必须在第一次 `run_season` 之前全部完成，之后不再登记」 | 可在 `SpeciationSystem` 内登记 |
| `2026-10-16-world-design.md` §1.2 | 「帧循环、tick 速率、渲染（`run_season` 是唯一推进单位，没有『帧』）」 | 编排粒度下沉到 tick，`step_once()` 是新的最小推进单位 |

第三条不算推翻而是**继承待验**：`MovementSystem` 当初是为修「一个空掉的格
永远不会再被填上」而加的（见该文件头部说明与 `world` 设计文档 §8）。重写
带趋向的移动之后，那条性质必须重新验证——否则世界会退回单调收缩。

**已验。** `sim` 的 `evolve` 跑 200 季，覆盖率随时间**上升后趋平**：
20 季 69.5%、50 季 72.3%、100 季 76.7%、200 季 76.8%。**不是在蚀** ——
若在单调收缩，这四个数会一路往下走。设计文档 §4.1 的那片「单次快照」测不出
这件事，必须看时间序列。

平台值比无地形时的 87.2% 低，原因是两股：��屏障限制移动，以及存量梯度会制造
聚集。**梯度不填空，它重新分配** —— 富格被填上，中等格被腾空。§2.1 里
「梯度会填掉那 519 个空格」的推断错了一步，阶段 3 接手时不要沿用。

`sim` 现在每次运行都打印覆盖率与最挤一格的个体数，这是这条性质的常驻观测点。

### 7.2 新契约

以下以 `\pre` / 注释形式写进头文件。

1. **推翻 `world` 设计文档 §5.1。** 原契约「`species()` 的登记必须在第一次
   `run_season` 之前全部完成」不再成立。`SpeciationSystem` 在运行中调用
   `register_species`。放宽后仍需保证：**未注册的 `SpeciesId` 让实体被两个
   system 各自跳过**这条既有行为不变——`EnvironmentSystem` 与
   `BreedingSystem` 都按 `1 <= value <= species_count()` 判别，而
   `species_count()` 每季现查，不是构造期快照。
2. **一批 `step_once()` 之后必须调用 `season_report()`，反之亦然。** 少调
   `season_report()` 会让繁殖整季不跑；多调会让同一季结算两次并把
   `coexist_seasons_` 记重。这条由 `World` 内部的 `tick_` 计数与
   `season_report()` 开头那条 `WORLD_INCOMPLETE_SEASON` 诊断守住，
   不做运行时断言。
3. **`TerrainField` 与气候场、资源场同生命周期。** 玩家改写地形后，
   落在新变成水的格上的个体在下一 tick 被钳回最近的可行走格。这条钳回
   是**静默**的，不报诊断——它每季都可能发生大量次，报诊断会淹没通道。
4. **`GameSession` 不可重入、不可并发。** 与 `World` 同。
5. **`root_seed` 决定地形。** 换种子等于换地图（也换气候），换图之后
   之前跑出的结论全部不作数。
6. **玩家的三条干预不保证立刻产生可见效果。** 气候笔刷改的是场，场要
   经一整个季的摄食与存活抽签才反映到 `report` 上。这是设计意图，不是
   bug——`apply_*` 的返回值只报告「改了哪些格子」，不预测后果。

---

## 8. 确定性

新增机制逐条确认：

- `TerrainField::generate` 只读 `root_seed`，不含任何随机流状态。海平面由
  分位数定出，同样只依赖 `root_seed`
- `MovementSystem` 的随机流仍是 `mix_seed(mix_seed(root_seed, season),
  kMovementPurpose)`，与既有实现同式
- **§8 原先那条「新增的趋向判定不得引入随机」按「不引入新随机流」读。**
  决策 15 的 50/50 硬币确实要取一次随机，但它取自既有的每体每季那一条流，
  因此同种子仍逐位可复现，玩家在同一 tick 边界暂停再续跑也不会分叉——这条
  规则要挡的是「新流」与「次序依赖」，两样都没发生。若按字面执行，替代
  方案是硬币改由 `hash(entity_id, season)` 导出，效果等价但不再消耗随机流
- `SpeciationSystem` 的连通分量与 `divergence` 全是确定性遍历，按
  `EntityId` 升序，因此分量划分与遍历顺序无关
- `hysteresis` 计数是整数，逐位确定
- `GameSession` 的判定层零随机

**`worker_count` 无关性是否还成立，本设计没有验证。** 新增的
`SpeciationSystem` 不进调度器（§4.3），所以它不影响批次划分；但
`MovementSystem` 重写后是否仍只读 `CreaturePhenotype`、只写 `Position`
（因而仍能与 `AgingSystem` 并行），必须在实现时确认。若访问集变了，`World`
装配的那条「一批装两个」的断言
（`movement_and_aging_do_not_conflict_so_a_batch_can_run_them_in_parallel`，
断言 `batch_count() == 1`）会红，那正是它该红的时候。

注意别找错断言：`a_structure_mutating_system_still_forces_its_own_batch`
断言的「排成三批」属于 `MovementSystem` + `EnvironmentSystem` +
`AgingSystem` + `BreedingSystem` 那个自建子装配，与 `World` 无关——
tick 化之后 `World` 只往调度器里装 `MovementSystem` 与 `AgingSystem` 两个。

`MovementSystem` 拆出多步之后，`mix_seed` 每季只调一次、步内不再取随机
这一点必须显式写进代码：每步都重取流会让第 k 步的方向依赖前 k−1 步，
玩家在同一 tick 边界暂停再续跑就可能续出不同轨迹。

---

## 9. 测试策略

| # | 测试 | 验证点 |
|---|------|--------|
| 1 | `terrain_is_deterministic_per_seed` | 同 seed 两张图逐位相同；不同 seed 不同 |
| 1b | `terrain_water_fraction_hits_the_configured_target` | 任意种子下水格占比都落在 70% ± 2%。**可变异性守卫**：海平面若写死常数，这条会随种子时红时绿 |
| 2 | `movement_never_leaves_a_creature_in_water` | 重写后跑 50 季，全世界无个体落在 `!passable` 格上 |
| 3 | `movement_respects_the_speed_trait` | 高速组与低速组的平均位移差 ≥ 1.5 倍。**可变异性守卫**：没有它，趋向写死了也会全绿。取值必须落在 `sim` 实际产出的 Speed 区间 `[0.3, 1.6]` 内（取整后只剩 {1, 2} 两档，比值上限 2.0），且要**跨在 lround 的分界上**（现取 1.4 / 1.6）：`floor` 下两者都塌成 1、比值 1.0 判红，于是这条守卫同时钉住取整方向 |
| 3b | ~~`gradient_movement_fills_the_empty_productive_cells`~~ **已作废，判据本身测不出梯度** | 理由见 §2.2：随机游走是扩散系数最高的过程、天然最小化空格数，梯度制造聚集因而必然抬高空格数；用空格数当判据等于奖励扩散、惩罚聚集。实测三组单调趋势（11/21、172/305、9/675）。**替代**：`a_gradient_bearing_creature_repeatedly_returns_to_rich_cells`（效应量下限 1.05，守梯度开关）+ `the_gradient_direction_is_symmetric_when_no_neighbour_is_richer`（守 §4.2 第 3 步的「严格高于本格」，并钉住遍历序偏置） |
| 4 | `water_actually_splits_the_population` | 一道贯穿的水墙两侧，200 季后两侧的 `divergence` 显著大于无墙对照组 |
| 5 | `speciation_produces_no_single_member_species` | 跑 300 季，每次裂变时记下新物种的初始 census，断言全部 ≥ `min_founder`。**直接挡 Species 的头号缺陷** |
| 6 | `speciation_does_not_fire_without_geographic_barrier` | 同一世界删掉水墙，300 季零裂变。**可证伪的那条**：把「隔离」判据写成恒真测试 4 仍绿、这条变红 |
| 7 | `speciation_respects_hysteresis` | 群体规模在阈值上下抖时，裂变次数为 0 |
| 8 | `divergence_is_symmetric_and_bounded` | `divergence(g, s)` 等于 `divergence(s, g)`，且 ∈ [0, 0.5] |
| 9 | `run_season_equals_eight_step_once` | `step_once() × 8 + season_report()` 与 `run_season()` 的 `SeasonReport` 逐字段相同（季号、population、每个物种的 census / heterozygosity / f_coefficient / effective_population），且返回后 `tick_in_season()` 归零。**已随阶段 1 落地并通过** |
| 10 | `victory_requires_the_full_season_streak` | 并存数达标后中途掉到目标以下，计数归零 |
| 11 | `victory_check_does_not_mutate_the_world` | 调 `check_victory()` 前后 `SeasonReport` 与实体表逐位相同 |

测试 1b 是阶段 2 新增的可变性守卫，挡「海平面写死常数、只在默认种子上对」。
原 3b 已作废（见上），它的替代者是那两条：一条守梯度开关生效且有可观的效应量，
一条守方向选择不带系统偏置。

**注意 3b 曾经是无效的判据这件事本身**：它在写进本表时看着合理、也在纸面上
给出了预期数字，但它奖励的是扩散、惩罚的是聚集 —— 与本阶段要测的性质正好
相反。写测试判据时要先问「这个量在机制上会随被测行为往哪个方向变」，而不只是
问「它看起来相关吗」。

测试 5、6 是这一轮的骨架：一个守「不许裂出错物种」，一个守「不许乱裂」。
只写 5 会得到一个永远不裂变的世界也全绿的假通过；只写 6 会得到一个满树
单个体物种的世界。

**刻意不写**：不为「地形屏障必须由玩家画」写测试。当前世界的地形由
`root_seed` 生成，玩家改写是阶段 4 才有的能力，那时再补。

---

## 10. 交付顺序

| 阶段 | 交付 | 能验证什么 |
|------|------|-----------|
| 1 | `GameSession` + tick 化 | 能看、能干预、只有一个物种。**这一步就能上手玩** |
| 2 | `TerrainField` + `MovementSystem` 重写 | 同一物种在屏障两侧分成两群，`H_obs` 出现地理分化。**水陆 70/30，基频 0.10，海平面取分位数，趋向追存量、p=0.5（决策 15）** |
| 3 | `SpeciationSystem` | 世界自己裂变，胜利条件生效 |
| 4 | 三条玩家干预工具 | 画屏障、调气候、育种所 |

阶段 1 与阶段 3 各自能独立交付可玩的东西，中间不会卡住。阶段 2 的成果
（地理分化）本身就有观测价值，不依赖阶段 3。

**规模问题（阶段 1 就要面对）**：`evolve` 稳态约 9800 个体、200 季约
1 分钟，即每季 0.3 秒。一季 8 个子步在 60 fps 下也正好 0.3 秒，数字对得上，
但 9800 个体没法全画。游戏形态大概要把 `capacity_base` 压到 1/3 上下
（3000 上下），靠密度换可视性。这是调参不是架构，但它会影响所有关卡设计
对「多大种群算多」的直觉。

---

## 11. 未决事项

- **屏障是绝对的，不是统计的。** 4 邻域移动逐格判通行 ⇒ 个体所在陆块的
  连通分量在移动下是不变量；繁殖侧同样封闭（水格 `capacity = 0` ⇒
  `headroom ≤ 0` ⇒ 不发站点）；`place_in` 的 jitter < 1 保证子代不出格；
  `sim` 播种跳过水格。四个环节各自独立，不依赖对方的善意。因此
  §4.2 原稿那句「累计定向行程约 300 格，足以跛到对岸」**因果反了**，已撤回
- **存量梯度那条负反馈目前是未验证假设。** `movement_system.hpp` 的类头
  无条件承诺「挤进富格 → 吃光 → 梯度翻转 → 离开」，而唯一相关的测试
  （`a_gradient_bearing_...`）的夹具不跑 `EnvironmentSystem`，资源场静态，
  负反馈从未被触发。阶段 3 若要靠它设计 hysteresis，先验一遍
- **`sim` 的「有主陆块」是开季快照，落后末季一季**（`occupants` 全仓只在
  `begin_season` 被写）。它还过滤掉无人陆块，所以「共 N 块」是**有主**陆块数，
  不是地图上的陆块总数。阶段 3 的地理分组判据若需要在尚无个体时就识别出
  一个陆块，当前的输出给不出
- ~~`MovementSystem` 的趋向到底取资源梯度还是同种趋向~~ **已定，见决策
  15。** 初版取资源梯度中的**存量**、p=0.5；同种趋向推迟到阶段 3 与
  `SpeciationSystem` 同出。原先挂着的顾虑「它会让 `population ≤ Σcapacity`
  的全局界彻底失效」经实测已不成立（45% 的有主格本来就超载），因此不需
  为它单写一条新不变量
- ~~**阶段 2 落地后需重取的三处读数**~~ **已完成。** 实际走向与两股力量的
  预判不同：面积降到 30%（不是 65% —— 海平面改取 70 百分位，见 §4.1）压低
  了种群，而梯度**没有**抬高它。种子 20261016、跑 200 季、初始 2460、
  Σcapacity 2958、可通行格 1230：`evolve` 末季 2418（Σcapacity 的 82%）、
  `persist` 末季 3554（120%）、`fragile` 第 **25** 季归零（原第 52 季）。
  决策 13、决策 14 的读数块已一并作废重写
- **`fragile` 的 0.7 已越过悬崖，不再停在临界点。** 陆地碎化让每个亲代池更小、
  N 更小、Σw² 基线更高，0.7 因此压得更狠，归零点从 52 季提前到 25 季。要在碎片化
  的世界上恢复「刚好压垮」的分岔，得把 0.25/0.4/0.5/0.7 全部重跑一遍 —— 本轮
  没有调，fragile 只证明「压得垮」
- `GameSession` 里的 `root_seed` 与 `world::World` 的 `root_seed` 是同一份。
  玩家若想在同一张图上重开，需要两套种子——是否把地形拆出去允许
  「换遗传种子但保留地形」，未定
- 干预是否要撤销：`paint_terrain` 改掉的水格能不能改回来（岸线侵蚀？还是
  一旦为水就永远是水）
- 生态位细分（食性分化、种间竞争）在胜利条件 `target_species` 调高到 5+
  之后可能成为必需。当前设计只能保证「裂得出 3 个」，保证不了「3 个之间
  有生态位差异」——它们可能只是同一种群的三个地理变种
- `Run` 的记录格式：抄经人一次局产出一卷经，天然适合存档复盘，但
  `TerrainField` / `ClimateField` / `ResourceField` / `LocusTable` /
  `SpeciesRegistry` 的序列化格式一条都还没有（`world` 与 `environment`
  两份文档的未决事项里都记着这一条）

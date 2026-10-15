# 基因遗传系统设计文档

- 日期：2026-10-14
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23
- 前置模块：`ecs`（见 [ECS 模块设计文档](./2026-10-12-ecs-design.md)）

## 1. 背景与目标

为 gamev2 提供真实遗传学内核，作为整个模拟器的基础系统。

规模定位：单场景约 1 万个体，每个体 300 个基因位点。

### 1.1 设计目标

- 基因组定长、无堆分配，可直接作为 ECS 组件
- 单基因位点承载孟德尔离散性状，多基因位点承载数量性状
- 群体层面的遗传漂变、近交衰退、瓶颈效应从采样机制中**涌现**，而非以规则强加
- 生存率是一组可逐项解释的生理约束之积，而非不透明的适应度分数
- 遗传学内核不依赖 ECS，可脱离引擎做纯断言测试

### 1.2 非目标

本轮明确不做，避免范围蔓延：

- 物种形成与生殖隔离（下一份 spec）
- 生态位维度定义（下一份 spec）
- 觅食、捕食、移动等行为系统
- 基因组序列化与存档格式
- 遗传距离矩阵与聚类算法（数据模型已支持其实现）
- 繁殖季结算的并行化
- 玩家基因编辑工具（育儿所 / 基因拼接 / 基因循环）

后续若需存档，在 `LocusTable` 上叠加稳定索引即可，内存布局不必变更。

---

## 2. 设计决策记录

以下 12 项为本设计的显式决策，后续实现若要推翻其中任何一项，需重新评估依赖它的部分。

| # | 决策 | 理由 |
|---|------|------|
| 1 | 二倍体，单基因位点 + 多基因位点 + 群体遗传学 | 显隐性、近交衰退、杂种优势均需二倍体才有意义 |
| 2 | 范围 = 表达层 + 群体遗传学 | 物种形成与生态位属人口遗传学之上的另一层，混入会使本 spec 变成整个游戏 |
| 3 | 全局共享位点表 + 每物种激活掩码 | 保住定长数组的 cache 优势，同时让物种结构差异可表达 |
| 4 | 表型 = 标量性状 + 固定结构目录 | 若只有标量，单基因位点的显隐性退化为"三个不同浮点数"，离散遗传无处承载 |
| 5 | `P = f(G) ⊕ g(环境快照)` | 环境是"同基因型在不同地点表现不同"的唯一来源，也是生态位分化的物理基础 |
| 6 | 连续生命史 + 离散繁殖季结算 | 世代不重叠要求上代全灭，在有寿命差异与幼体的游戏中不成立；全连续则频率更新无闭式解 |
| 7 | 空间局部交配 | 全局随机交配下 F 恒为 0，近交衰退永不发生 |
| 8 | 300 位点 / 1200 B 基因组 / 1 万个体 | 内存 12 MB；瓶颈在两两距离而非内存 |
| 9 | 串行确定性结算 | 单季全量重算是毫秒级，并行化收益不抵调试成本 |
| 10 | 纯函数内核 + 薄 ECS 适配层 | 显隐性、近交系数、漂变方差必须能在无 World 的前提下写成断言 |
| 11 | 连续生命史：存活个体留存并继续繁殖 | 见下 |
| 12 | 每季子代数 = `round(N × 平均 Fecundity)` | 见下 |

第 11 条：种群规模按 `N' = s·N + N·f ≈ N·(s+f)` 演化，稳定条件是
`s + f = 1`——繁殖力恰好补上存活率的缺口。全换代模型给的是 `s·f = 1`，
两者不是一回事。

第 12 条：它是**实现期补上的**——在前 11 条里根本没有对应项，
而缺少它的后果不是「数值不准」，是「世界会死」。
记在这里是为了让后来者知道它不是实现细节。

`N × Fecundity` 只是无承载力时的线性外推。生态 spec 交付后，
种群上限由环境承载力决定；在那之前，调 `Fecundity` 必须守住 `s + f ≈ 1`，
否则种群要么衰减到灭绝、要么指数增长。

---

## 3. 总体架构

### 3.1 文件划分

```
include/gamev2/genetics/
  locus.hpp        LocusDef / LocusTable / GeneticsConfig（AlleleId 与
                      kLocusCount 定义在 genome.hpp，本头文件包含它）
  genome.hpp       AlleleCall / Genome（二倍体）
  species.hpp      SpeciesId / SpeciesMask / SpeciesRegistry
  phenotype.hpp    TraitKind / Phenotype（标量性状 + 固定结构目录）
  environment.hpp  EnvironmentSnapshot
  expression.hpp   f(G) ⊕ g(E)
  breeding.hpp     局部交配 + 重组 + 突变 → 子代 Genome
  population.hpp   繁殖季结算 + 等位基因频率 / 杂合度 / F / Ne 统计
src/genetics/*.cpp

tests/
  test_genetics_locus.cpp
  test_genetics_expression.cpp
  test_genetics_breeding.cpp
  test_genetics_population.cpp
  test_genetics_determinism.cpp
  test_genetics_performance.cpp

include/gamev2/ecs/adapt/
  genetics_systems.hpp/cpp    Aging / Breeding / PopulationStatistics 三个系统
```

### 3.2 依赖方向

```
        ecs/adapt  ──────►  genetics
              │                 ▲
              ▼                 │
             ecs                （无反向依赖）
```

`genetics/` 不 `#include` 任何 `ecs/` 头文件。
这不是解耦洁癖，而是测试可行性要求：验证显隐性、近交系数、
漂变方差的断言必须在没有 `World`、没有实体、没有系统调度的前提下写成。

### 3.3 数据流

```
  Genome（二倍体）────┐
                      ├─► Expression ──► Phenotype ──┐
  EnvironmentSnapshot ┘                               │
                                                      ▼
  SpeciesMask ──────────────────────────────► Vitality = 存活率与繁殖力
                                                      │
  空间邻域 ──► 局部交配（邻域内抽亲代）               │
                  │                                    │
                  ▼                                    │
           Recombination ─► Mutation ──► 子代 Genome  │
                                                      ▼
                                    PopulationStatistics（频率 / H / F / Ne）
```

---

## 4. 数据模型

### 4.1 位点表（全局共享蓝图）

```cpp
namespace gamev2::genetics {

inline constexpr std::size_t kLocusCount = 300;
inline constexpr std::size_t kMaxAlleles = 4;

using AlleleId = std::uint8_t;   // 位点内索引，不是全局编号

enum class LocusKind : std::uint8_t {
    Trait,       ///< 数量性状位点，贡献到一个连续性状轴
    Structural,  ///< 结构位点，解析为离散形态状态
};

/// 固定结构目录的槽位。槽位集合编译期固定，才能定长进 ECS。
enum class StructuralSlot : std::uint8_t {
    BodySegments,
    LimbPairs,
    Eyes,
    Flags,   ///< 壳 / 翼 / 鳍 / 腺体的位域
    kSlotCount
};

struct AlleleDef
{
    float        additive;    ///< 加性取值 v_i
    std::uint8_t state;       ///< 结构态编码，Trait 位点为 0
    std::uint8_t dom_rank;    ///< 显性等级，越大越显
};

struct LocusDef
{
    LocusKind    kind;
    std::uint8_t allele_count;
    /// 目标索引，含义随 kind 而变：Trait 位点是 TraitKind 轴，
    /// Structural 位点是 StructuralSlot。两者分列两个字段会多出
    /// 一个必须保持同步的冗余状态，而这里一个字段就够。
    std::uint8_t target;
    float        scale;       ///< 该位点在该轴上的权重
    float        dominance;   ///< 显性系数 h，见 5.1；须 >= 0
    /// 该位点每代发生突变的概率。不是「替换等位基因的概率」——
    /// 后者会让突变率恒为 1。见 6.3。
    float        switch_rate;
    std::array<AlleleDef, kMaxAlleles> alleles;
};

struct LocusTable
{
    std::array<LocusDef, kLocusCount> loci;
};

}  // namespace gamev2::genetics
```

`LocusTable` 随游戏配置加载一次，之后全程只读。所有物种共享同一张表——物种之间的差异体现为等位基因分布与激活掩码，而非基因表本身。

### 4.2 基因组（二倍体）

```cpp
/// jitter 是等位基因取值上的有界微扰（±config.jitter_step），使 Trait 位点
/// 的突变成为平滑步进而非在离散等位基因间跳变；Structural 位点忽略它。
struct AlleleCall
{
    AlleleId    allele;
    std::int8_t jitter;
};

struct Genome
{
    std::array<AlleleCall, kLocusCount> a;  ///< 一路单倍型
    std::array<AlleleCall, kLocusCount> b;  ///< 另一路
};
```

`sizeof(Genome) == 1200 B`，1 万个体约 12 MB。定长、无堆分配、平凡可复制，可直接作为 ECS 组件。

单倍型拆成两个数组而非交错存储，是为了让重组（按单倍型整体交叉）与统计（按单倍型遍历）都能顺序访问。

### 4.3 物种

```cpp
struct SpeciesMask
{
    std::bitset<kLocusCount> active;
};

struct SpeciesId
{
    std::uint32_t value = 0;
};

class SpeciesRegistry
{
public:
    /// 物种在本轮是外部给定的静态标签。形成新物种属于下一份 spec；
    /// 这里只保证位点掩码的接口在那里。
    [[nodiscard]] std::expected<SpeciesId, SpeciesError>
    register_species(std::bitset<kLocusCount> mask);

    [[nodiscard]] const SpeciesMask& mask(SpeciesId) const;
};
```

```cpp
enum class SpeciesError
{
    EmptyMask,       ///< 掩码未激活任何位点，该物种无表型可言
    TooManySpecies,  ///< 超出本轮的物种数上限
};
```

`register_species` 拒绝空掩码：一个没有任何激活位点的物种无法产生表型，也无法参与表达层——让它存在只会把问题推到运行时。

### 4.4 表型

```cpp
enum class TraitKind : std::uint8_t
{
    BodySize, Speed, Strength, Metabolism,
    Lifespan, SenseRange, Fecundity, Resilience,
    kTraitCount
};

inline constexpr std::size_t kTraitCount =
    static_cast<std::size_t>(TraitKind::kTraitCount);

struct Phenotype
{
    std::array<float, kTraitCount> traits;

    /// 固定结构目录：字段集合编译期固定，才能定长进 ECS。
    std::uint8_t body_segments;
    std::uint8_t limb_pairs;
    std::uint8_t eyes;
    std::uint8_t flags;  ///< bit 0 壳 / 1 翼 / 2 鳍 / 3 腺体
};
```

固定目录而非任意字段表，是为了让 `sizeof(Phenotype)` 稳定。需要新的形态维度时在枚举尾部追加槽位，而不是引入运行时字段表。

### 4.5 环境快照

```cpp
struct EnvironmentSnapshot
{
    float temperature;  ///< 相对该物种适宜温度的偏移
    float fertility;    ///< 0..1 食物充足度
    float light;
};
```

环境在个体建立时采样一次并锁定，之后不再变化——个体因此具有确定的表型，可测、可存档。

### 4.6 配置常量

表达层与突变中需要调平衡的标量放在 `GeneticsConfig`，随配置加载一次：

```cpp
struct GeneticsConfig
{
    std::array<float, kTraitCount> trait_base;
    std::array<float, kTraitCount> trait_min;
    std::array<float, kTraitCount> trait_max;
    std::int8_t jitter_step;              ///< 单次 jitter 突变的步进上限
    float        jitter_unit;             ///< 1 单位 jitter 对应的 additive 偏移
    float        allele_switch_share;     ///< Trait 位点突变时选择替换而非微调的概率
    /// 以下三项是 g(E) 的系数：环境偏移映射到性状轴的强度。
    /// 见 5.3——环境只修正性状轴，不改结构字段。
    float        temperature_metabolic_load;
    float        fertility_fecundity_gain;
    float        light_sense_gain;
    float        thermal_base_width;      ///< Resilience = 0 时的温度耐受半宽
    float        thermal_resilience_gain; ///< 每点 Resilience 扩展的半宽
    float        escape_responsiveness;   ///< 逃脱余量转为概率的速度
    std::uint8_t max_overdominant_loci;   ///< 允许 dominance > 1 的位点数上限
};
```

`kTraitCount` 定义于 4.4。`GeneticsConfig` 里的量不做成编译期常量，
是因为它们恰恰是需要反复调平衡的参数——
固化进二进制会让每次调参都变成一次重编。

`jitter_unit` 把整数 jitter 换算成 `additive` 单位。它必须是显式常量而非隐含约定：
`jitter` 是 `int8_t`、`additive` 是 `float`，两者之间若没有写明的比例尺，
同一个 jitter 值在不同位点上会表示量级完全不同的效应。

---

## 5. 表达层 f(G) ⊕ g(E)

### 5.1 Trait 位点：显性由等位基因等级推导

基因型 `(i, j)` 的取值只需一个标量显性系数 `h`，不需要位点内的 N×N 显性矩阵：

```
m     = (v_i + v_j) / 2
a     = (v_i - v_j) / 2
h_eff = (rank_i == rank_j) ? 0 : h · sign(rank_i - rank_j)
value = m + h_eff · a
```

| `h` | 杂合子表现 |
|-----|-----------|
| 0 | 取中点（加性） |
| 1 | 与显性纯合子同值（完全显性） |
| > 1 | 优于两个纯合子（超显 / 杂种优势） |

符号由 `dom_rank` 自动给出。这正是"一个 A 对一个 a"的真实语义：
显性不是某个被命名为 A 的等位基因的固有属性，而是设计数据指定的等级关系。
`h` 恒取非负——负值等价于把两个 `dom_rank` 对调，不构成额外自由度。

累加：

```
traits[t] = clamp(trait_base[t] + Σ_{l : kind==Trait ∧ target==t} scale_l · value_l)
```

夹取先于环境修正发生：基因效应的累加结果必须先夹到
`[config.trait_min[t], config.trait_max[t]]`，
再由 g(E) 叠加环境增量并再次夹取。
若合并成一次夹取，一个极端基因型会被环境增量救回，掩盖掉本应淘汰的表型。

### 5.2 Structural 位点

```
(i == j)           → state_i
(rank_i > rank_j)  → state_i                                  // 完全显性
(rank_i == rank_j) → max(state_i, state_j)                    // 共显近似
(rank_i < rank_j)  → state_j
```

配置校验要求一个 Structural 位点内 `dom_rank` 两两不等，因此第三行实际不会触发；写它只为让规则完备，不留未定义路径。

多个 Structural 位点写向同一结构槽位时，按位点 id 升序依次覆盖，后者胜。
槽位到 `LocusDef::target` 的映射在配置期固定，不在运行时查找——
因此表达层是一个无分支的写表，槽位合法性由 `validate` 保证（见第 10 节）。

### 5.3 环境作用 g(E)

环境修正在**性状轴**上，不触碰结构字段：

```
traits[t] = clamp(traits[t] + env_delta(t, environment))
```

这是明确的简化：形态本身具有反应规范（reaction norm），
但那会让结构位点的表达规则、遗传方差分解与调参难度同时膨胀。
"同基因型在不同环境体能与代谢不同，但不会长出不同腿数"足以支撑生态位分化。

---

## 6. 繁殖层

### 6.1 局部交配

繁殖季结算时，对每个**待繁殖位置**（由地图 / 行为 spec 在本季提交的位置集合），
以该位置为中心在半径 `R` 内收集**同物种**存活个体构成亲代池，
按 `fitness` 加权抽取两个亲代。

半径 `R`、空间分桶、以及待繁殖位置本身的产生都属于生态 / 地图 spec；本模块只接受一个已分桶的亲代池作为输入，不关心它从哪来。

### 6.2 重组（减数分裂）

单倍型内部做均匀随机交叉点，染色体残段交替来自两亲。交叉点数量取固定均值。

### 6.3 突变

按位点独立，两份单倍型各自独立。`switch_rate` 是**该位点每代发生突变的概率**，
不是"替换等位基因的概率"——后者会让突变率恒为 1。突变发生时，两位点走不同的路：

| 位点类型 | 突变内容 |
|---------|---------|
| Trait | 以 `config.allele_switch_share` 的概率替换为另一等位基因；否则 `jitter += δ` |
| Structural | 从该位点其余等位基因中均匀替换一个；`jitter` 忽略 |

`allele_switch_share` 是全局默认值（取低值，如 0.05）：Trait 位点的绝大多数
突变是平滑微调，偶发离散替换。若把它做成逐位点参数，就是 300 个无从校准的旋钮。

与真实生物学一致：定量性状的变异以小的加性微效为主，形态结构的改变是离散的跃迁。

`jitter` 累加后夹到 `[−config.jitter_step, +config.jitter_step]`。
这个界必须有：否则一个位点会沿某个方向无限漂移，
等于给了它一条不消耗突变代价的适应趋势，加性微效的统计性质也随之失效。

`switch_rate` 的存在是必要的：若 Trait 位点只靠 jitter 微调，等位基因多样性永远不会由突变产生，只能靠初始种群携带。

---

## 7. 群体层

### 7.1 繁殖季结算

五个步骤，单线程、顺序固定：

```
1. 存活筛选   按 survival_rate 加权重抽存活个体
2. 建亲代池   每个待繁殖位置，从半径 R 内同物种存活者取 2 个亲代（加权）
3. 遗传       重组（交叉点） → 突变（jitter 步进 + switch_rate 替换）
4. 表型       f(子代 Genome) ⊕ g(子代环境快照)
5. 统计       等位基因频率 / 杂合度 / F / Ne / census
```

第 2 步的池由 `settle` 建，不是调用方直接给的：`Parent` 带一个
指向输入 `genomes` 的下标，`settle` 据此把父池条目映射回本季的存活标记，
**已死亡者不进池**。权重取 `settle` 自己算出的本季适应力——调用方建池时
只有上一季的数据，两者相乘等于把同一个体的体质算两遍。调用方的
`Parent::fitness` 因此是「候选者」的先验筛子，不是权重本身。

池还带一个 `progeny`，即本季从该池产出多少子代。生态位尺度上「一个待繁殖
位置一个子代」是自然读法，但物种级池必须按 `N × f` 批量产出（见第 2 节
决策 12），否则种群与物种数成反比衰减。

子代先写入 staging 缓冲，整季结算完成后统一提交，保证"换代"是一次原子提交——行为系统不会读到半新半旧的种群。

### 7.2 生存率：生理约束之积

```
survival_rate = Π 各项约束存活率
fitness      = survival_rate × traits[Fecundity] / 当前季最优值
```

两个量分工不同，不可混用：`survival_rate` 是本季活到下一季的概率
（决定 7.1 第 1 步的存活筛选），`fitness` 是相对繁殖贡献（决定亲代抽样权重）。
两者都由下表约束算出，不含任何独立的适应度评分。

表型与环境是这些约束的全部输入，适应力的差异因此完全来自基因与环境的交互，没有开发者私下加入的权重：

| 约束 | 形式 | 失败含义 |
|------|------|---------|
| 能量收支 | 摄食 ∝ SenseRange × Fecundity；消耗 ∝ Metabolism × BodySize × (1+Speed) | 饿死 |
| 温度耐受 | `\|temperature\| ≤ width`，`width ∝ Resilience` | 冻死 / 热死 |
| 捕食逃脱 | 逃脱率 ∝ Speed 对当前威胁等级 | 被捕食 |
| 寿命 | `age ≥ traits[Lifespan]` | 老死 |

这四项是本轮的全部约束。每增加一项都是一处需要独立配平的平衡，收益不足以抵偿调参风险。

**明确的假设**：能量收支按资源充足时的理论摄食上限计算。
真实觅食会显著改变该值，但觅食 / 捕食 / 移动属于行为 spec；
本模块预留 `feeding` 实参，接入后取实际摄入值替代理论值。

与 Species 最关键的分野正在于此：其生存筛选是不透明的，出了问题玩家无从判断因果；而这里的每一项约束都可以单独展示、单独调平衡、单独向玩家解释。

### 7.3 漂变、瓶颈、近交衰退：涌现

**本模块不实现任何漂变算法。** 有限亲代池 + 随机抽样即构成抽样误差，即遗传漂变：

- `M` 个亲代产出 `N` 个后代，`M` 越小，等位基因频率的抽样方差增长越快
- 种群骤降 → 亲代池变小 → 漂变加剧 → 多样性不可逆损失，即瓶颈效应
- 局部交配 + 小种群 → `H_obs < H_exp` → 杂合子不足 → 表现下降，即近交衰退

三者是同一采样机制在三个尺度上的表现，共享同一条统计路径。

### 7.4 近交系数与有效种群：本地估计

不追踪家谱，全部由当代等位基因频率估计：

```
F_locus = 1 - H_obs / H_exp            H_exp = Σ p_i (1 - p_i)
F       = Σ_locus w_locus · F_locus    w 按等位基因方差加权
Ne      = 1 / (2 · Σ_locus (1 - H_exp_locus))
```

按等位基因方差加权是因为多样性极低的位点上 `H_obs / H_exp` 的估计噪声极大，不加权会让近交系数被这些位点主导。

### 7.5 统计输出与分级

```cpp
struct AlleleStats
{
    std::array<float, kMaxAlleles> frequency;
    float h_observed;
    float h_expected;
};

struct SpeciesStatistics
{
    /// 只包含激活位点。用 vector 而非定长 kLocusCount 数组：上百个物种时
    /// 定长数组会把未激活位点一并驻留，而激活掩码通常是稀疏的。
    std::vector<AlleleStats> loci;
    float heterozygosity = 0.0f;
    float f_coefficient = 0.0f;
    float effective_population = 0.0f;
    std::uint32_t census = 0;
};
```

出生与死亡数不进本结构：`offspring.size()` 与 `n - survived` 已经给出，
再存一份就是同一事实的第二处真相。

每季对全部物种计算摘要（`heterozygosity` / `f_coefficient` /
`effective_population` / `census`），仅对**玩家选中的物种**计算逐位点明细。
上百个物种时全量逐位点统计不可行，分级是唯一出路。

---

## 8. ECS 集成

| 组件 | 内容 |
|------|------|
| `Genome` | 1200 B，二倍体基因组 |
| `SpeciesTag` | `SpeciesId` |
| `Phenotype` | 标量性状 + 固定结构目录 |
| `EnvironmentSnapshot` | 建立时锁定 |
| `Vitality` | `survival_rate`、`fitness`、年龄（季节数） |

三个系统，均为"取组件 → 调纯函数 → 写回"的薄适配：

| 系统 | 职责 |
|------|------|
| `AgingSystem` | 年龄递增，按 `traits[Lifespan]` 结算死亡 |
| `BreedingSystem` | 7.1 的五步结算，单线程 |
| `PopulationStatisticsSystem` | 7.5 的分级统计，写入 `SpeciesStatistics` |

新建个体的表型在出生时计算一次（环境已锁定），此后不再重算——除非玩家通过工具改动环境，那属于后续 spec。

### 8.1 CommandBuffer 需要一个 create，World 需要一个 for_each_entity

`BreedingSystem` 要为每季的新生个体建实体，但两条既有路径都不可用：
`World::create()` 改写 `slots_` / `free_indices_` 且非原子，
其文档明确禁止在 System 执行期调用；而 `CommandBuffer` 只有
`destroy` / `add` / `remove`，没有创建实体的命令。

因此本轮需要给 `CommandBuffer` 增加一条 create 命令，新实体在帧末 flush 时创建。

第二个缺口在遍历侧：`View` 的回调只给组件引用、不给实体句柄，而
`AgingSystem` 需要句柄才能把死亡个体交给 `cmd.destroy`。
因此需要给 `World` 增加一个 `for_each_entity`，回调收到真实句柄。
把 `storage().archetypes()` 那套存储层遍历搬进适配层会让适配层变厚
并泄露存储内部结构，与它「只做取组件 → 调纯函数 → 写回」的定位相反。

这两处是本设计对既有 ecs 模块的全部改动，也是遗传学接入 ECS 的必要条件。

---

## 9. 确定性

同一种子两次运行必须逐位相同，这是可测的硬要求。

- 根种子派生季节种子：`season_seed = mix(root_seed, season_index)`
- **每个用途独立子流**：交配抽样、存活重抽、交叉点、jitter 步进、等位基因替换各用一条独立派生子流。共用一条流会导致"多抽一次随机数全盘皆变"的脆弱性
- 空间遍历按 `(位置索引, EntityId)` 固定顺序，归约顺序固定

---

## 10. 错误处理与配置校验

错误处理只有一处：`LocusTable::validate()` 返回 `std::expected<void, LocusConfigError>`，在加载配置时调用一次。

```cpp
enum class LocusConfigError
{
    AlleleCountOutOfRange,///< allele_count 不在 [2, kMaxAlleles]
    DuplicateRank,     ///< Structural 位点内 dom_rank 重复
    SlotOutOfRange,    ///< Structural 位点的 target 越界
    TraitOutOfRange,   ///< Trait 位点的 target 越界
    NegativeDominance, ///< dominance < 0
    BadSwitchRate,     ///< switch_rate 不在 [0, 1]
    TooManyOverdominant,
    BadTraitRange,     ///< 某个性状的 trait_min >= trait_max
};
```

校验项：

- 每个位点的 `allele_count` 在 `[2, kMaxAlleles]`
- Structural 位点内 `dom_rank` 两两不等
- Trait 位点的 `target < kTraitCount`
- Structural 位点的 `target < StructuralSlot::kSlotCount`
- `dominance >= 0`
- `switch_rate ∈ [0, 1]`
- 超显（`dominance > 1`）的位点数量不超过 `config.max_overdominant_loci`
- 每个 `TraitKind` 的 `trait_min[t] < trait_max[t]`

超显那条是本轮明确的产品决策：超显在 F1 有杂种优势，但纯合化后消失，会让同一物种内的长期表现与杂交后代不一致。因此只允许少数位点配置超显，其余按完全显性或加性处理。

`trait_min < trait_max` 这条尤其重要：`trait_max[Lifespan] == 0` 会让所有个体的寿命恒为 0，
`outlived` 恒真，全员每季判死，亲代池恒空，**永远零子代且无任何报错**。
等界的区间不会崩溃，只会让世界悄悄死掉——这正是配置期校验存在的理由。

热路径零校验。这些条件在配置期不成立，就是配置错误，应当在启动时失败，而不是在第 3 季第 4000 个个体身上失败。

---

## 11. 测试策略

全部为纯断言测试，不构造 `World`。

| # | 测试 | 验证点 |
|---|------|--------|
| 1 | 结构位点显隐性 | `Aa` 表达显性态；`AA × Aa` 后代基因型比 1:2:1 |
| 2 | 多基因加性 | 给定位点表与基因组后，各性状值与手算结果逐位精确匹配 |
| 3 | 超显 | `h = 1.5` 时 `Aa` 严格优于 `AA` 与 `aa` |
| 4 | 环境作用 | 同基因组不同环境 → 性状不同；**结构字段相同** |
| 5 | 重组 | 子代每位点必来自某一亲代，连续段交替 |
| 6 | 突变频率 | `switch_rate` 与 jitter 步进实测频率收敛到设定值 |
| 7 | 近交 | 小种群 `F` 显著大于大种群 |
| 8 | 漂变 | 小种群杂合度衰减显著快于大种群 |
| 9 | 选择 | 高存活率型在重抽样中被富集 |
| 10 | 确定性 | 同种子两次结算逐位相同 |
| 11 | 性能 | 1 万个体 × 300 位点单季结算在预算内 |

测试 7、8、9 为统计断言，需固定种子 + 足够样本量，并设阈值而非精确期望值。

---

## 12. 与后续 spec 的接口

本设计刻意为下一份 spec（物种形成与生态位）保留了三个接缝：

1. `SpeciesRegistry::register_species` 接受外部位点掩码——新物种的诞生只需以不同掩码注册
2. 局部交配已按半径与物种分池——地理隔离只需把 `R` 收缩或切断邻接
3. `SpeciesStatistics` 携带逐位点频率——遗传距离矩阵与物种聚类可直接在其上实现

研究报告指认的两项硬伤——物种形成的逻辑次序颠倒、生态位维度过薄——其根源都在基因系统之外：前者是隔离与阈值触发的次序，后者是环境与资源维度的丰富度。本轮补齐的是它们依赖的底座。

---

## 13. 未决事项

- 生态 / 地图 spec 交付后，`EnvironmentSnapshot` 是否需要增加湿度、海拔等维度
- 行为 spec 接入后，`survival_rate` 的 `feeding` 实参形态，以及捕食威胁等级的来源
- 存档需求出现时，`Genome` 的序列化格式（内存布局已为此留出余地）

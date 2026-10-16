# 环境模块设计文档

- 日期：2026-10-16
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23
- 前置模块：`ecs`（见 [ECS 模块设计文档](./2026-10-12-ecs-design.md)）、`genetics`（见 [基因遗传系统设计文档](./2026-10-14-genetics-design.md)）

## 1. 背景与目标

遗传系统已经落地，但它对环境的全部认知是一个三元组：

```cpp
struct EnvironmentSnapshot
{
    float temperature;  ///< 相对该物种适宜温度的偏移
    float fertility;    ///< 0..1 食物充足度
    float light;
};
```

这个结构体至今没有生产者。遗传设计文档 §6.1 把「待繁殖位置、空间分桶、半径 R」整块划给了生态 / 地图 spec，§12 把它列为下一份 spec 的三处接缝之一，§13 的未决事项第一条就是「生态 spec 交付后，`EnvironmentSnapshot` 是否需要增加湿度、海拔等维度」。本设计就是那份生态 spec。

规模定位沿用遗传系统：单场景约 1 万个体。

### 1.1 设计目标

- 气候与资源是**格级场**，遍历成本与个体数解耦
- 资源在季内真实地被吃穿，饥荒是涌现的，不是公式给的
- 种群规模由环境承载力封顶，不再依赖 `s + f ≈ 1` 这条需要人去守的调参纪律
- 环境模块**不认识**遗传学：`environment/` 不 include 任何 `genetics/` 头文件

### 1.2 非目标

本轮明确不做：

- 个体在格内的移动（位置由行为系统写，环境层只读）
- 群系（biome）枚举与群系表
- 湿度、海拔等新环境维度
- 捕食与威胁等级的来源（`threats` 仍由调用方给）
- 迁移 / 扩散
- 资源 / 气候场的存档格式
- 物种形成与生态位划分（遗传设计文档的下一份 spec）

---

## 2. 设计决策记录

以下 7 项为本设计的显式决策。

| # | 决策 | 理由 |
|---|------|------|
| 1 | 粗粒度 64×64 气候格 + 浮动个体坐标 | 场遍历 4096 格，与个体数解耦；个体可自由移动而不需要重算场 |
| 2 | `EnvironmentSnapshot` 迁入 `environment/`，`genetics/` 反向 include 它 | 快照是环境层的产出，不是遗传层的契约。两个模块零直接反向依赖 |
| 3 | `ParentPool` 的组装落在 `ecs/adapt` | 环境层不认识 `SpeciesId`，只有 adapt 同时看到两边 |
| 4 | 季内连续 K 步喂食；存活抽签仍只发生在 `settle` | 两处各判一次等于把存活率平方 |
| 5 | `stock` 只卡存活率，`capacity` 只卡出生数 | 资源循环的两条通路各管一件事，不互相代偿 |
| 6 | 出生态上限 `headroom × share` 与繁殖上限 `n × offspring_scale` 并用 | 前者防指数增长，后者防独苗产满窝后代（一次纯自交） |
| 7 | `kGridSide` 固定编译期 | 字段集合与尺寸都要编译期确定，场才能定长 |

第 2 条的方向值得单说：`genetics/` 因此**新增**了对 `environment/climate.hpp` 的 include，这看着像倒挂，但它只为了一个类型。`environment/` 侧的依赖表里没有任何 `genetics/` 项——这才是本设计要守住的线。测试可行性依赖它：遗传层的断言必须在没有气候场、没有资源场的前提下写成。

第 4 条是被上一轮讨论推翻的一个更自然的想法的残骸。最初设想的形态是「季内每步抽签判定存活，死亡个体当场移除」，它在语义上更彻底，但它与 `settle` 第 1 步的存活抽签直接冲突。保留两处抽签的后果是存活率被平方——不崩、不报错，只是世界悄悄地死得比预期快。改成环境只测量 `feeding`、判定仍归 `settle` 之后，抽签恰好发生一次。

---

## 3. 总体架构

### 3.1 文件划分

```
include/gamev2/environment/
  climate.hpp      EnvironmentSnapshot / CellIndex / ClimateCell / ClimateField
  resource.hpp     ResourceCell / ResourceField
  sites.hpp        Forager / BreedingSite / run_season / build_sites
  config.hpp       EnvironmentConfig
src/environment/
  climate.cpp
  resource.cpp
  sites.cpp

include/gamev2/ecs/adapt/
  components.hpp           Position / Feeding / BreedingTarget
  environment_systems.hpp EnvironmentWorld / EnvironmentSystem

tests/
  test_environment_climate.cpp
  test_environment_resource.cpp
  test_environment_sites.cpp
  test_environment_systems.cpp
```

删除 `include/gamev2/genetics/environment.hpp`，`EnvironmentSnapshot` 迁到 `environment/climate.hpp`。

### 3.2 依赖方向

```
   ecs/adapt/environment_systems   ← 唯一同时看到两边的地方
          │            │
          ▼            ▼
   environment      genetics ──► environment/climate.hpp（仅 EnvironmentSnapshot）
   （不 include 任何 genetics 头）
```

`environment/` 不 `#include` 任何 `genetics/` 头文件。
这是可测性要求：气候、资源、承载力的断言必须在没有 `Genome`、没有 `Phenotype`、没有随机流的前提下写成。

具体表现是 `Forager::demand` 由调用方算出（`SenseRange × Fecundity`），环境层不读 `Phenotype`；`BreedingSite::species_key` 是裸 `std::uint32_t`，环境层不认识 `SpeciesId`。

### 3.3 数据流

```
  root_seed ──► ClimateField ──advance(season)──► EnvironmentSnapshot
                     │                                  │
                     ▼                                  │
                ClimateCell ──► ResourceField            │
                     │            │                      │
                     │            ▼                      │
                     │      run_season(Forager[])        │
                     │            │                      │
                     │            ▼                      │
                     │      Feeding 组件（逐实体）        │
                     │            │                      │
                     │            ▼                      │
                     └─────────► build_sites ──► BreedingSite[]
                                                          │
                                    EnvironmentSystem 转成 SeasonEnvironment::Site
                                                          │
                                    BreedingSystem 分组 + 空间分桶 → genetics::ParentPool
                                                          │
                                                          ▼
                                              genetics::settle
```

---

## 4. 数据模型

### 4.1 气候

```cpp
/// 环境在个体建立时采样一次并锁定，之后不再变化——个体因此具有确定的表型，
/// 可测、可存档。
///
/// 三项的取值范围约定：temperature 是相对该物种适宜温度的偏移，
/// fertility 与 light 是 0..1 的充足度，默认值即「无环境影响」。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

/// 编译期固定的格数。场要能定长进 ECS 组件，字段集合与尺寸都必须在编译期确定。
inline constexpr std::size_t kGridSide = 64;
inline constexpr std::size_t kCellCount = kGridSide * kGridSide;

/// 世界坐标空间即格坐标空间，[0, kGridSide)。个体在格内浮动，气候与资源是格级的。
struct CellIndex
{
    std::uint16_t value = 0;

    /// 世界坐标 -> 所在格。调用方须保证 x, y ∈ [0, kGridSide)。
    [[nodiscard]] static CellIndex of(float x, float y) noexcept
    {
        const auto ix = static_cast<std::uint16_t>(x);
        const auto iy = static_cast<std::uint16_t>(y);
        return CellIndex{static_cast<std::uint16_t>(
            iy * kGridSide + ix)};
    }
};

struct ClimateCell
{
    float base_temperature = 0.0f;  ///< 逐季不变
    float base_fertility = 1.0f;    ///< 逐季不变
    float base_light = 1.0f;        ///< 逐季不变
};

class ClimateField
{
public:
    explicit ClimateField(std::uint64_t root_seed);

    /// 只改派生量，不累加：温度与光照在基准值上加一个由季节决定的正弦偏移。
    /// 因此任意一季的气候都能直接算出，不需要历史，也就没有「忘掉推进过
    /// 几季」这种可能出错的状态。
    void advance(std::uint32_t season_index) noexcept;

    [[nodiscard]] EnvironmentSnapshot at(CellIndex cell) const noexcept;
    [[nodiscard]] const ClimateCell& base(CellIndex cell) const noexcept;

private:
    std::array<ClimateCell, kCellCount> cells_{};
    std::uint32_t season_ = 0;
};
```

`fertility` 逐季不变是刻意的：食物承载力是**生境**属性，气候才是**季节**属性。把 fertility 也做成季节量会让承载力随季节抖、种群规模跟着抖，而那不是任何真实机制。季节性全部由 `temperature` 与 `light` 承担。

构造时每格的基准值由 `root_seed` 与格下标经 splitmix64 混合得出，`base_temperature ∈ [-8, 8]`、`base_fertility ∈ [0.25, 1]`、`base_light ∈ [0.6, 1]`。**`base_fertility` 不为 0 是硬要求**：零承载力的格永远不会有生物，地图上是一块死地。

### 4.2 资源

```cpp
struct ResourceCell
{
    float stock = 0.0f;     ///< 当前存量，[0, capacity]
    float capacity = 0.0f;  ///< = capacity_base × 该格 base_fertility
};

class ResourceField
{
public:
    ResourceField(const ClimateField& climate, const EnvironmentConfig& config);

    void regrow() noexcept;
    void draw(CellIndex cell, float amount) noexcept;

    [[nodiscard]] float stock(CellIndex) const noexcept;
    [[nodiscard]] float capacity(CellIndex) const noexcept;
    [[nodiscard]] std::uint16_t occupants(CellIndex) const noexcept;

    /// 清零后按 foragers 的坐标重数一遍。季初调一次即可：季内没有移动，
    /// run_season 与 build_sites 共用同一份计数。
    void count_occupants(std::span<const Forager> foragers);

private:
    std::array<ResourceCell, kCellCount> cells_{};
    std::array<std::uint16_t, kCellCount> occupants_{};
};
```

`stock` 的单位是「个体」，与 `capacity` 同量纲。`stock / occupants` 因此直接读作人均可得食物，量纲不需要转换——这是把 `stock` 定成个体数而非「资源单位」的唯一理由。

`regrow` 是 `stock += regen_rate × (capacity - stock)`，指数逼近而非线性：线性在贴近 `capacity` 时仍以固定速率加，会在边界上反复穿越。`regen_rate ∈ (0, 1]` 保证单调不越界。

### 4.3 配置

```cpp
struct EnvironmentConfig
{
    std::uint32_t steps_per_season = 8;
    /// fertility = 1 时一格支持几个体。默认种群稳定在
    /// Σcapacity = 4096 × capacity_base × mean(base_fertility)，mean = 0.625。
    /// 默认 3.9 → ≈ 9984，落在 1 万个体的规模定位上。
    float capacity_base = 3.9f;
    float regen_rate = 0.25f;
    /// 单体单步的绝对摄取上限。与 share 同量纲（都是「个体」）。
    /// \pre demand >= 0。
    float demand_rate = 0.35f;
    /// 每个体本季最多产几个子代。
    float offspring_scale = 2.0f;
    /// 局部交配半径，单位为格。0 表示只有本格内个体互相交配。
    float mating_radius_cells = 1.0f;
    float temperature_amplitude = 6.0f;
    float temperature_period = 8.0f;  ///< 季节数 / 周期
    float light_amplitude = 0.15f;
};
```

`EnvironmentConfig` 的量不做成编译期常量，与 `GeneticsConfig` 同一个理由：它们恰恰是需要反复调平衡的参数，固化进二进制会让每次调参都变成一次重编。

---

## 5. 季内循环

```cpp
/// 一个摄食者。demand 由表型算出（SenseRange × Fecundity），环境层不读
/// Phenotype —— 这是 genetics 依赖方向不被反转的关键。
struct Forager
{
    float x = 0.0f;
    float y = 0.0f;
    float demand = 0.0f;
};

/// 跑完 steps_per_season 步：先再生，再逐格按存量均分摄食。
/// 返回与 foragers 同序的累计 feeding（0..1，1 = 从未受限）。
std::vector<float> run_season(ResourceField& field,
                               std::span<const Forager> foragers,
                               const EnvironmentConfig& config);
```

每步三趟，顺序固定：

1. `field.regrow()`
2. 对每个个体，令 `c = CellIndex::of(x, y)`、`n = occupants(c)`：
   - `want = demand_rate × demand`
   - `got  = min(want, stock(c) / n)`
3. 按格汇总 `Σ got`，统一 `draw(c, total)`

第 2、3 趟必须分开，因为**每个体的份额必须基于同一份扣减前的存量快照**。
合并成边算边扣后，第 k 个个体看到的是 `S₀ - Σ_{j<k} got_j` 缩小的存量，
份额随之递减，总扣减量系统性地偏少。存量偏多 → 下一步 `regrow` 的基数
偏大 → `build_sites` 的承载力预算随之偏松。

**`want` 必须是绝对量，不能乘 `share`。** 这不是风格问题，是正确性问题：

```
want  = demand_rate × demand × share      ← 错
ratio = got/want = min(1, share/want) = min(1, 1/(demand_rate × demand))
```

`share` 被约掉，`ratio` 只取决于 `demand_rate × demand`，与存量无关。阈值
因此固定在 `demand > 1 / demand_rate ≈ 2.86`，而 `demand = SenseRange ×
Fecundity` 的典型值约 1 —— **`feeding` 恒为 1，饥荒永不涌现，资源循环对
遗传层完全无效**（实测 demand = 0.25 / 1.0 全部输出 1.000000；只有
demand = 3.0 才降到 0.952381）。

改成绝对量后：

```
want  = demand_rate × demand             ← 对
ratio = got/want = min(1, share/(demand_rate × demand))
```

`share` 回来了。实测（demand = 1、demand_rate = 0.35、want = 0.35）：
`share = 2.34`（单体富格）→ 1.00；`share = 0.234`（10 体拥挤）→ 0.67；
`share = 0.05`（重度亏空）→ 0.14。

残留的顺序依赖只在第二趟的 `per_cell[cell] += got_i`——同格浮点累加，
输入反转会改变累加顺序。严格误差界 `(n−1)·2⁻²⁴·Σ|terms|`，`capacity`
上界 3.9、`n = 40` 时约 `9e-6`，且**随同格个体数线性增长**。改为按
`(x, y, demand)` 的规范序累加可逐位消除，而排序**只需在季初做一次**
（`\pre` 已保证季内不换格），每步成本降为 O(n)。本轮仍选择不消除。

`feeding[i]` = 各步 `got / want` 的均值，夹到 [0, 1]。两个退化情形各有定义：

- `want == 0`（本格存量已空）：记 0 分。那就是饿。
- `demand == 0`：记满分。什么都不需要的个体不该被饿死。

**`run_season` 与 `build_sites` 共用同一份 occupancy 计数**，由 `count_occupants` 在季初数一次。季内不重数：移动不在本轮范围内，季内个体不换格，重数只会让结果依赖实体遍历顺序。两者的 `foragers` 与 `species_keys` 必须等长同序。

---

## 6. 承载力与待繁殖位置

```cpp
struct BreedingSite
{
    CellIndex cell;
    /// 外部物种标签，值域与 genetics::SpeciesId::value 一致。
    /// 环境层不认识 SpeciesId，翻译发生在 ecs/adapt。
    std::uint32_t species_key = 0;
    std::uint32_t progeny = 0;
    EnvironmentSnapshot environment;
};

[[nodiscard]] std::vector<BreedingSite>
build_sites(const ResourceField& field, const ClimateField& climate,
            std::span<const Forager> foragers,
            std::span<const std::uint32_t> species_keys,
            const EnvironmentConfig& config);
```

对每个有占用的格 `c`、每个出现在该格的物种 `s`：

```
headroom(c)      = max(0, capacity(c) - occupants(c))
share(c, s)      = n(c, s) / occupants(c)
progeny(c, s)    = round( min( headroom(c) × share(c, s),      // 生态上限
                               n(c, s) × offspring_scale ) )    // 繁殖上限
```

两个上限各管一件事：

- **生态上限**由生境承载力决定。缺了它种群会指数增长——这正是遗传设计文档决策 12 警告的那件事。
- **繁殖上限**保证一个独苗产不出满窝后代。那会是一次纯自交，直接毁掉多样性，而这正是遗传系统最不该丢掉的东西。

`share` 按当前物种构成分配空余容量：新物种无法凭空出现在一个已经被占满的格子里。

输出按 `(cell.value, species_key)` 排序后成组，因此与输入实体顺序无关。

**算出 `progeny == 0` 的格不发站点。** `settle` 把 `ParentPool::progeny == 0`
按 1 处理（老行为：一池一子代的兼容默认值），一个 0 预算的站点送进去会
凭空生出 1 个后代，承载力封顶因此失效。跳过而不是发 0，是不改动遗传层
既有语义的前提。

### 6.1 资源循环的两条通路

`capacity` 只卡出生数（季末），`stock` / `feeding` 只卡存活率（季内）。两条通路各管一件事，不互相代偿：

- 种群逼近 `Σ capacity` → `headroom` 归零 → 出生态收缩
- 种群超过 `Σ capacity` → 人均摄入不足 → `feeding` 掉下来 → 走遗传层的存活判定

种群规模因此被机械结构封顶在 `Σ capacity` 附近，`s + f ≈ 1` 那条调参纪律被取代——不再需要人去守住它。

---

## 7. 对 genetics 的改动

共 2 处，`settle` 内部一行不改。

```cpp
// include/gamev2/genetics/population.hpp — SeasonInput 末尾追加
    /// 与 genomes 同序，本季的实际摄入满足度（0..1）。
    ///
    /// 空 span 表示调用方未接入环境层，逐个按理论摄食上限 1.0 处理。
    /// 放在末尾而非与 threats 并列，是为了不动既有调用方的位置参数顺序
    /// —— 语义分组上的不便，不值二十几处与本次改动无关的测试编辑。
    std::span<const float> feedings{};
```

```cpp
// src/genetics/population.cpp:60
const SurvivalInput s{
    input.phenotypes[i], input.environments[i],
    input.feedings.size() == input.genomes.size() ? input.feedings[i] : 1.0f,
    input.threats[i]};
```

存活抽签仍然只发生在 `settle` 第 1 步，死者仍由 `settle` 剔除，死亡仍是一次季末的原子提交。遗传设计文档 §7.2 里「能量收支按资源充足时的理论摄食上限计算」这条假设从本轮起成为**降级路径**而非默认路径——它现在只在未接入环境层时成立。

其余改动是 `EnvironmentSnapshot` 的机械搬家：12 个文件换一行 include，`adapt::Environment` 组件本身不动。

---

## 8. ECS 集成

```cpp
struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

/// 本季实际摄入满足度。EnvironmentSystem 写，BreedingSystem 读。
///
/// 必须走组件而不是跨 system 传下标数组：两个 system 各自 for_each_entity
/// 一遍，遍历顺序是各自的实现细节，拿同一个下标去对齐它们是隐患。
struct Feeding
{
    float satisfaction = 1.0f;
};

/// 一个待繁殖位置的输入。EnvironmentSystem 填，BreedingSystem 读。
///
/// 放在 components.hpp 而不是 environment_systems.hpp：BreedingSystem 也要用
/// 它，而 environment_systems.hpp 已经 include 了 genetics_systems.hpp，
/// 反向再 include 就是头文件环。
struct BreedingTarget
{
    gamev2::genetics::SpeciesId species;
    gamev2::environment::CellIndex cell;
    std::uint32_t progeny = 0;
    gamev2::environment::EnvironmentSnapshot environment;
};

/// 气候场与资源场。整局唯一一份，不进 ECS——它是单一实例的场景状态，
/// 放进组件只会让「每个实体各有一份气候」这个错误写法看起来合法。
class EnvironmentWorld
{
public:
    EnvironmentWorld(std::uint64_t root_seed, const EnvironmentConfig& config);

    ClimateField& climate() noexcept;
    ResourceField& resources() noexcept;
    const EnvironmentConfig& config() const noexcept;
};

/// 推进气候 → 跑完季内摄食 → 算承载力预算 → 写 Feeding 与 targets。
///
/// **必须排在 BreedingSystem 之前**。与 AgingSystem 必须排在 BreedingSystem
/// 之前是同一类约束：WriteSet 只能让调度器分批，表达不了批次之间的先后。
/// 顺序反了不会崩，只会让本季出生数为零。
///
/// targets 只传站点，不传 ParentPool：Parent::index 必须是子代数组
/// SeasonInput::genomes 内的下标，而那个数组由 BreedingSystem 按物种切出，
/// 环境层无从知道它的形状。空间分桶因此留在 adapt 层。
class EnvironmentSystem final : public System
{
public:
    EnvironmentSystem(EnvironmentWorld& env, std::uint32_t season_index);

    using ReadSet = std::tuple<const Position, const CreaturePhenotype,
                               const SpeciesTag>;
    using WriteSet = std::tuple<Feeding>;

    void execute(World& world, CommandBuffer& cmd) override;

    [[nodiscard]] const std::vector<BreedingTarget>& targets() const noexcept;
};
```

### 8.1 `BreedingSystem` 的改动

多两个可选参数：`const std::vector<BreedingTarget>* targets` 与
`float mating_radius_cells`。

- 有：`SeasonInput::feedings` 逐个取自实体的 `Feeding` 组件（缺该组件的按 1.0），父池改用属于本物种的站点，子代 `Position` 取所在格的中心加确定性抖动
- 没有：`feedings` 留空，父池退化为现在这一条「一物种一池 + `progeny_for`」

这不是两条代码路径，是**一份站点列表的两种默认值**：池的组装代码完全共用，只有「站点从哪来」不同。不接环境层的调用方行为与本轮之前逐位相同。

`progeny_for`（遗传设计文档决策 12 的 `N × Fecundity`）在有环境时让位给承载力预算，在无环境时保留——它同时成为环境层的对照基线，正好用来验证承载力确实压住了规模。

`Position` 组件缺失的个体在任何半径内都不进父池：环境层同样只为有 `Position`
的实体产出站点，两者对「谁参与繁殖」的答案必须一致。

### 8.2 顺序约束

`EnvironmentSystem` **必须排在 `BreedingSystem` 之前**。

这与 `AgingSystem` 必须排在 `BreedingSystem` 之前是同一类约束：`WriteSet` 只能让调度器把二者分进不同批次，表达不了批次之间的先后。这里必须先有站点和 `Feeding` 才谈得上结算，顺序反了不会崩，只会让本季出生数为零。

### 8.3 空间父池的组装

**在 `BreedingSystem` 内完成，不在 `EnvironmentSystem` 内。**

理由是索引：父池的 `Parent::index` 必须是本物种 `SeasonInput::genomes` 内的下标，而那个数组由 `BreedingSystem` 按物种切出。环境层若要直接产出 `ParentPool`，就得知道这个数组的形状——那是遗传层调用点特有的知识。环境层因此只负责「哪个格、哪个物种、该产几个」。

组装顺序：读 `season->sites` 一遍，按 `species` 分组（一次遍历建索引，不排序——站点顺序是环境层的实现细节）；对每个物种，用 `Position` 与 `SpeciesTag` 把半径 `config.mating_radius_cells` 内的同物种实体挑出来，映射成该物种数组内的下标，逐个填 `Parent{index, &pool_genomes[index], 1.0f}`。

子代认回自己的格靠顺序：`settle` 按 `sites` 顺序逐个追加子代，因此 `offspring[i]` 对应的站点就是第 i 个产出过子代的站点。`settle` 本身不回传这层归属，这是 `BreedingSystem` 现有的「一次一种、归属由调用点确定」约定的延续。

---

## 9. 确定性

同一种子两次运行必须逐位相同。

- 气候场由 `root_seed` 与格下标混合得出，不消耗随机数
- 季内循环与 `build_sites` 完全无随机
- `build_sites` 输出按 `(cell, species_key)` 排序，与输入实体顺序无关
- 摄食的计算与扣减分两趟，结果不依赖实体遍历顺序

环境层没有随机流，因为它的每一步都是确定的。需要随机的只有遗传层的抽签，那部分仍在 `RandomStreams` 里。

两条已知的、由实测确定而非设计意图的性质：

- `feeding` 的浮点累加存在顺序依赖，量级 `(n−1)·2⁻²⁴·Σ|terms|`，以该界
  为容差由 `feeding_is_independent_of_forager_order` 守住。
- **本项目已经三次栽在同一个形状的 bug 上**：`f(g(x))` 里的 `x` 被约掉，
  于是整个机制对 `x` 恒为常数，而所有夹具都落在「未饱和」的一侧，测试全绿。
  本轮的实例是 `want` 曾乘 `share`，把阈值固定在 `demand ≈ 2.86`，
  而典型 `demand ≈ 1`，于是 `feeding` 恒为 1。见 §5。
  `feeding_varies_with_resource_pressure` 是为此留的回归守卫。

---

## 10. 测试策略

| # | 测试 | 验证点 |
|---|------|--------|
| 1 | 气候确定性 | 同种子两次构造，`at()` 逐位相同 |
| 2 | 季节演化 | 同一格跨季温度不同；且恒在 `base ± amplitude` 内 |
| 3 | 再生不越界 | 反复 `regrow` 后 `stock <= capacity` |
| 4 | 供过于求 | 低占用 + 存量充足时跑满一季，全员 `feeding == 1` |
| 5 | 拥挤摄食 | `occupants >> capacity` 时 `feeding < 1` |
| 6 | **顺序无关** | 打乱 `foragers` 输入顺序，`stock` 终态与 `feedings` 在 `(n−1)·2⁻²⁴·Σ\|terms\|` 误差界内相同（非逐位） |
| 5b | **存量敏感** | 同一批个体只改 `stock`：富格 `feeding == 1`、拥挤格 ∈ (0,1)、亏空格 ≈ 0 |
| 6b | **两趟分离** | 闭式解比对存量轨迹：合并两趟会偏少，测试必须变红 |
| 7 | 生态上限 | `occupants > capacity` 的格 `progeny == 0` |
| 8 | 独苗上限 | 单体格的 `progeny <= round(offspring_scale)` |
| 9 | 站点稳定 | 打乱输入，`sites` 逐位相同 |
| 10 | **遗传兼容** | 空 `feedings` 与显式全 1.0 的 `feedings` 两次调用，`settle` 输出逐位相同 |
| 11 | 端到端规模 | `World` 内 N 个体跑一季，种群不越过 `Σ capacity` |
| 12 | 端到端确定性 | 同种子两次端到端逐位相同 |

测试 5b、6、6b 和 10 是本次改动里最要紧的四条：**5b 挡住 `feeding` 对存量
退化为常数**（这条曾经失守，且失守时全部测试全绿）；6 挡住「结果依赖实体
遍历顺序」；6b 挡住「两趟被合并」；10 挡住遗传层的回归。其中只有 10 是逐位
比较；6 用浮点误差界，6b 用闭式解加误差界。

测试 10 的构造方式是对同一份固定输入跑两遍 `settle`——第一遍 `feedings` 留空，第二遍显式传入等长的全 1.0 数组——比对两次输出逐位相同。这验证的是空 span 的回落路径与显式全 1.0 等价，也顺带把新字段的默认值钉死。

---

## 11. 未决事项

- 行为系统接入后，`Position` 的移动规则；以及 `mating_radius_cells` 的取值（当前默认 1，即 3×3 格邻域）
- `threats` 的来源：捕食威胁等级仍由调用方给，环境层不产生
- 存档需求出现时，`ClimateField` / `ResourceField` 的序列化格式
- 生态位 spec 交付后，`species_key` 是否需要从裸整数换成更强的类型

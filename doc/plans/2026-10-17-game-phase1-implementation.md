# 游戏层阶段 1（tick 化 + GameSession）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `World` 的推进粒度从「一季」下沉到「一个季内子步」，并在
其上建 `game::GameSession`，让渲染层能在任意 tick 边界暂停与读数。

**Architecture:** 把 `environment::run_season` 的 8 个子步拆成可逐步调用的
`SeasonFeeder`；`EnvironmentSystem` 随之拆成 `begin_season` / `step` /
`finish`，并**离开调度器**——它需要被 World 显式钉在 tick 的位置关系上，
而调度器管不到跨 tick 的次序。`MovementSystem` 与 `AgingSystem` 仍留在
调度器里，那一对并行因此保留。`BreedingSystem` 与
`PopulationStatisticsSystem` 一样改为直接 `execute`，由 World 自己 flush。

**Tech Stack:** C++23、clang++ 23、CMake + Ninja、手写测试框架
（`TEST` / `CHECK` / `CHECK_EQ`，见 `tests/test_framework.hpp`）

**Spec:** `doc/2026-10-17-game-design.md`（阶段 1 对应 §3.3、§4.4、§10 第一行）

## 范围

本计划只做 spec §10 的**阶段 1**。阶段 2（地形 + MovementSystem 重写）、
阶段 3（SpeciationSystem）、阶段 4（玩家干预工具）各出一份独立计划。

阶段 2 不与本计划同出，是因为它的核心未知量——「移动要加资源梯度趋向还是
同种趋向」——取决于本阶段跑出来的实测数据。先写出来就是猜测。

## Global Constraints

- 每行不超过 90 字符；长条件在 `&&` / `||` 处断行，每个操作数独占一行
- 注释只写「为什么」与约束，不写「做什么」；既有注释一律保留
- 不新增 `shared_ptr` / `unique_ptr`
- 改动既有代码时只碰本次需求牵动的行，不顺手重构相邻代码
- **不执行 `git commit`**。每个任务末尾照常给出命令，由用户自己决定何时提交
- 构建：`pwsh -File scripts/build.ps1 -Test`
- 单套件运行：`& build/world_tests.exe`（其余同理 `ecs_tests` /
  `genetics_tests` / `environment_tests`）
- 新增源文件必须同时加进 `CMakeLists.txt` 的目标源列表与
  `GAMEV2_ENABLE_ASAN` 的 foreach 列表（后者漏掉会让 ASan 构建悄悄跳过整块代码）

---

### Task 1: SeasonFeeder——把季内摄食拆成可逐步调用的形式

**Files:**
- Modify: `include/gamev2/environment/sites.hpp`
- Modify: `src/environment/sites.cpp`
- Modify: `tests/test_environment_sites.cpp`

**Interfaces:**
- Consumes: 既有的 `Forager`、`EnvironmentConfig`、`ResourceField`
- Produces:
  - `class SeasonFeeder`，公开成员：
    `explicit SeasonFeeder(EnvironmentConfig config)` /
    `void reset(std::size_t forager_count)` /
    `void step(ResourceField& field, std::span<const Forager> foragers)` /
    `[[nodiscard]] std::vector<float> finish() const`
  - `run_season` 保留，签名与语义完全不变（8 处测试与
    `EnvironmentSystem` 都在用它）

- [ ] **Step 1: 写失败测试**

加到 `tests/test_environment_sites.cpp` 的匿名 namespace 之后、其余
`TEST` 之前，并在文件顶部的 using 列表补一行：

```cpp
using gamev2::environment::SeasonFeeder;
```

```cpp
TEST(stepping_a_feeder_matches_running_the_whole_season)
{
    // 逐位比较：两个 ResourceField 从同一种子构造、同一次 count_occupants
    // 开始，差别只在「谁来驱动那 8 步」。任何一步的顺序或公式改动都会让它红。
    auto config = EnvironmentConfig{};
    config.steps_per_season = 8;

    const std::vector<Forager> foragers{
        Forager{3.5f, 4.5f, 1.0f},
        Forager{5.5f, 6.5f, 0.4f},
        Forager{20.25f, 20.75f, 2.0f},
    };

    ClimateField whole_climate{42, config};
    ResourceField whole{whole_climate, config};
    whole.count_occupants(foragers);
    const auto whole_season = run_season(whole, foragers, config);

    ClimateField stepped_climate{42, config};
    ResourceField stepped{stepped_climate, config};
    stepped.count_occupants(foragers);
    SeasonFeeder feeder{config};
    feeder.reset(foragers.size());
    for (std::uint32_t i = 0; i < config.steps_per_season; ++i)
    {
        feeder.step(stepped, foragers);
    }
    const auto one_by_one = feeder.finish();

    CHECK_EQ(one_by_one.size(), whole_season.size());
    for (std::size_t i = 0; i < one_by_one.size(); ++i)
    {
        CHECK_EQ(one_by_one[i], whole_season[i]);
    }
}

TEST(reset_clears_the_previous_seasons_accumulation)
{
    // 不调 reset 就连跑两季，satisfaction 会跨季累加——finish 的归一化
    // 除以的仍是单季步数，于是第二季的读数被顶到 1 并夹住，饥荒静默失效。
    //
    // 判据是**逐位相等于一个全新 feeder**，不是任何阈值比较：阈值版本
    // （如「第二季 ≤ 1」）在跨季累加下依然成立——累加的结果正被夹到 1，
    // 断言恒绿。逐位相等才有鉴别力。
    auto config = EnvironmentConfig{};
    const std::vector<Forager> foragers{Forager{3.5f, 4.5f, 1.0f}};

    const auto run_one_season = [&](ResourceField& field, SeasonFeeder& feeder)
    {
        feeder.reset(foragers.size());
        for (std::uint32_t i = 0; i < config.steps_per_season; ++i)
        {
            feeder.step(field, foragers);
        }
        return feeder.finish();
    };

    // 跑一季，再在同一块地上 reset 后跑一季。
    ClimateField a_climate{7, config};
    ResourceField a{a_climate, config};
    a.count_occupants(foragers);
    SeasonFeeder reused{config};
    (void)run_one_season(a, reused);
    const auto second = run_one_season(a, reused);

    // 另一块同样状态的地上，用一个从没跑过的 feeder 跑一季。
    ClimateField b_climate{7, config};
    ResourceField b{b_climate, config};
    b.count_occupants(foragers);
    SeasonFeeder fresh{config};
    const auto reference = run_one_season(b, fresh);

    CHECK_EQ(second.size(), reference.size());
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        CHECK_EQ(second[i], reference[i]);
    }
}

TEST(a_fresh_feeder_reports_nothing_before_its_first_step)
{
    // finish 在零步时必须给出全 0 而不是 UB：EnvironmentSystem 若在
    // begin_season 之后、任何 step 之前被结算，渲染层会拿到未定义值。
    auto config = EnvironmentConfig{};
    SeasonFeeder feeder{config};
    feeder.reset(4);
    const auto nothing = feeder.finish();
    CHECK_EQ(nothing.size(), std::size_t{4});
    for (const auto value : nothing)
    {
        CHECK_EQ(value, 0.0f);
    }
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```
Expected: 编译失败，`SeasonFeeder` 未声明。

- [ ] **Step 3: 声明 SeasonFeeder**

在 `include/gamev2/environment/sites.hpp` 的 `run_season` 声明**之前**插入：

```cpp
/// 季内摄食的分步器。把 run_season 的 K 步拆开，使渲染层能在步与步之间
/// 暂停、观察存量被吃穿的过程。
///
/// 持状态而非每步重建缓冲：steps_per_season 默认 8，每步重新分配会让一季
/// 的摄食成为世界的主要分配源。
class SeasonFeeder
{
public:
    /// \pre config.steps_per_season >= 1：finish 要除以它做归一化，0 会
    ///      得到 0.0f / 0.0f，而 std::clamp 拦不住 NaN，下游抽签一路带下去。
    explicit SeasonFeeder(EnvironmentConfig config);

    /// 开一季：按新的 forager 数量重置累加器。
    void reset(std::size_t forager_count);

    /// 跑一步：先 field.regrow()，再按扣减前的存量算人均份，最后按格统一
    /// 扣减，并把该步的满意度累加进内部缓冲。
    ///
    /// \pre `field.count_occupants(foragers)` 已用**同一批** `foragers`
    ///      调用过：换一批会让 occupants 与实际在格个体数不符，
    ///      share = stock / n 会取到 inf 并把结果变成 NaN。
    /// \pre `foragers.size()` 等于 reset 给定的数量。
    void step(ResourceField& field, std::span<const Forager> foragers);

    /// 本季结束：累加值除以 steps_per_season 并夹到 [0, 1]。返回与 reset
    /// 时给定数量同序的实测 feeding。
    [[nodiscard]] std::vector<float> finish() const;

private:
    EnvironmentConfig config_;
    std::vector<float> satisfaction_;
    std::vector<float> got_;
    std::vector<float> want_of_;
    std::vector<float> per_cell_;
};
```

- [ ] **Step 4: 实现 SeasonFeeder，并把 run_season 改成它的包装**

在 `src/environment/sites.cpp` 里，把 `run_season` 的**整个循环体**
（第 22 行 `for (std::uint32_t step = ...)` 到第 102 行闭括号）**逐字**
搬进 `SeasonFeeder::step`，把三处局部缓冲换成成员 `_` 后缀版本，其余
一个字都不改——包括那段解释「第二趟必须与第一趟分开」的长注释，它解释的
是本函数内部的不变量，跟着走才对。

```cpp
SeasonFeeder::SeasonFeeder(EnvironmentConfig config)
    : config_{config}, per_cell_(kCellCount, 0.0f)
{
}

void SeasonFeeder::reset(const std::size_t forager_count)
{
    satisfaction_.assign(forager_count, 0.0f);
    got_.assign(forager_count, 0.0f);
    want_of_.assign(forager_count, 0.0f);
}

void SeasonFeeder::step(ResourceField& field,
                        const std::span<const Forager> foragers)
{
    // 原 run_season 循环体的逐字搬运，见上。
}

std::vector<float> SeasonFeeder::finish() const
{
    const float steps = static_cast<float>(config_.steps_per_season);
    std::vector<float> out(satisfaction_.size());
    for (std::size_t i = 0; i < satisfaction_.size(); ++i)
    {
        out[i] = std::clamp(satisfaction_[i] / steps, 0.0f, 1.0f);
    }
    return out;
}

std::vector<float>
run_season(ResourceField& field, const std::span<const Forager> foragers,
           const EnvironmentConfig& config)
{
    // 便捷包装：单线程「跑完一整季」的调用方走它。World::run_season 不再
    // 走这条路径——它要逐步驱动，好让渲染层在步与步之间暂停。
    SeasonFeeder feeder{config};
    feeder.reset(foragers.size());
    for (std::uint32_t i = 0; i < config.steps_per_season; ++i)
    {
        feeder.step(field, foragers);
    }
    return feeder.finish();
}
```

- [ ] **Step 5: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 四套件全过。新增三条测试红过、实现后转绿。

- [ ] **Step 6: 提交（由用户决定）**

```bash
git add include/gamev2/environment/sites.hpp src/environment/sites.cpp tests/test_environment_sites.cpp
git commit -m "feat(environment): split the season loop into a steppable feeder"
```

---

### Task 2: EnvironmentSystem 拆成 begin / step / finish

**Files:**
- Modify: `include/gamev2/ecs/adapt/environment_systems.hpp`
- Modify: `src/ecs/adapt/environment_systems.cpp`

**Interfaces:**
- Consumes: Task 1 的 `SeasonFeeder`
- Produces: `EnvironmentSystem` 新增三个公开方法
  - `void begin_season(gamev2::ecs::World& world)`
  - `void step()`
  - `void finish(gamev2::ecs::World& world)`
  `execute(World&, CommandBuffer&)` 保留，语义变成三者的组合
- 消费方：Task 3 的 `world::World`

- [ ] **Step 1: 写失败测试**

加到 `tests/test_environment_systems.cpp`。该文件已有的 helper 全部直接用，
**不要新造同名 helper**：

| helper | 签名 | 位置 |
|--------|------|------|
| `all_loci` | `SpeciesMask all_loci()` | 56 行 |
| `one_species` | `SpeciesRegistry one_species()` | 68 行 |
| `place_creature` | `EntityId place_creature(World&, float x, float y, SpeciesId)` | 75 行 |
| `has_feeding` | `bool has_feeding(World&, const EntityId)` | 129 行 |
| `neutral_table` | `LocusTable neutral_table()` | 144 行 |
| `default_genetics` | `GeneticsConfig default_genetics()` | 158 行 |

```cpp
TEST(environment_system_can_be_driven_one_substep_at_a_time)
{
    // execute() 是三者的组合。这条直接逐段驱动，钉住「begin 备齐输入、
    // step 跑一步、finish 落结果」这个次序约定——World::run_season 之后
    // 就靠它。
    //
    // place_creature 只挂 Position / SpeciesTag / CreaturePhenotype，而这三者
    // 正是 EnvironmentSystem 的 ReadSet，因此不需要 Genome 或 Environment。
    EnvironmentWorld env{51, EnvironmentConfig{}};
    const auto species = one_species();
    EnvironmentSystem environment{env, species, 0};

    World entities;
    const auto e = place_creature(entities, 6.5f, 1.5f, SpeciesId{1});

    CommandBuffer cmd;
    environment.begin_season(entities);
    // 还没跑任何一步：targets 必须还是空的，Feeding 也还不该存在。
    CHECK_EQ(environment.targets().size(), std::size_t{0});
    CHECK(!has_feeding(entities, e));

    for (std::uint32_t i = 0; i < env.config().steps_per_season; ++i)
    {
        environment.step();
    }
    // 跑完步但没 finish：Feeding 仍未写回——begin 备的输入在 step 里只被
    // 消费到内部累加器，写回在 finish。
    CHECK(!has_feeding(entities, e));

    environment.finish(entities);
    CHECK(has_feeding(entities, e));
    CHECK_EQ(environment.targets().size(), std::size_t{1});
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 编译失败，`begin_season` / `step` / `finish` 未声明。

- [ ] **Step 3: 声明三个方法**

在 `include/gamev2/ecs/adapt/environment_systems.hpp` 的 `execute` 声明
**之后**插入，并把私有成员段替换为：

```cpp
    void execute(World& world, CommandBuffer& cmd) override;

    /// 季首：推进气候、收集摄食输入、点一次格内人数。step 与 finish 的
    /// 全部输入都在这里备齐。
    ///
    /// 三段必须按 begin → step×steps_per_season → finish 的次序调用。
    /// 跨过 begin 直接 step 会拿空的 foragers 跑完一季，跨过 finish
    /// 直接让调用方结算则整季没有 Feeding——两者都不崩，只是让饥荒与
    /// 出生预算静默失效，因此次序写在这里而不靠运行时拦截。
    void begin_season(World& world);

    /// 跑一个季内子步。次数由调用方决定，等于 steps_per_season。
    /// \pre begin_season 已被调用。
    void step();

    /// 季末：把累计满意度写回 Feeding，按格与物种算子代预算填 targets_。
    /// \pre 本季的 step 已被调用 steps_per_season 次。
    void finish(World& world);
```

```cpp
private:
    EnvironmentWorld& env_;
    const gamev2::genetics::SpeciesRegistry& species_;
    std::uint32_t season_index_;
    std::vector<BreedingTarget> targets_;

    // begin_season 备齐、finish 消费。留在成员里而不是每季构造，是为了让
    // step() 无参：它每季被调用 steps_per_season 次，任何一次分配都会被
    // 乘上这个倍数。
    std::vector<EntityId> handles_;
    std::vector<Forager> foragers_;
    std::vector<std::uint32_t> species_keys_;
    gamev2::environment::SeasonFeeder feeder_{env_.config()};
```

`feeder_` 用成员初始化器吃 `env_.config()` 是安全的：`env_` 声明在
`feeder_` 之前（成员按声明顺序构造），且 `EnvironmentSystem` 与
`EnvironmentWorld` 同寿命。

- [ ] **Step 4: 实现三个方法**

把 `src/ecs/adapt/environment_systems.cpp` 的
`EnvironmentSystem::execute` 整个拆成三段。**语句顺序逐字保持不变**，
只把三处局部 vector 换成成员、把 `run_season` 换成 `feeder_.step`：

```cpp
void EnvironmentSystem::begin_season(World& world)
{
    env_.climate().advance(season_index_);

    // 一次遍历收齐摄食所需的全部输入。缺 Position 或 SpeciesTag 的实体
    // 本季不参与环境结算：BreedingSystem 的空间分桶用同一把尺子量它们，
    // 两个 system 对「谁参与繁殖」必须给出同一个答案。
    handles_.clear();
    foragers_.clear();
    species_keys_.clear();

    const auto species_count = species_.species_count();
    world.for_each_entity([&](const EntityId e)
    {
        const auto* pos = world.get_if<Position>(e);
        const auto* phe = world.get_if<CreaturePhenotype>(e);
        const auto* tag = world.get_if<SpeciesTag>(e);
        // 判据与 BreedingSystem 的那处逐字相同。
        if (pos == nullptr || phe == nullptr || tag == nullptr
            || tag->species.value == 0
            || tag->species.value > species_count)
        {
            return;
        }
        const float demand =
            phe->phenotype.traits[trait_index(TraitKind::SenseRange)]
            * phe->phenotype.traits[trait_index(TraitKind::Fecundity)];

        handles_.push_back(e);
        foragers_.push_back(Forager{pos->x, pos->y, demand});
        species_keys_.push_back(tag->species.value);
    });

    env_.resources().count_occupants(foragers_);
    feeder_.reset(foragers_.size());
}

void EnvironmentSystem::step()
{
    feeder_.step(env_.resources(), foragers_);
}

void EnvironmentSystem::finish(World& world)
{
    const auto feeding = feeder_.finish();

    // 首次见到就加上组件，之后各季只改值。add 会把实体迁往新 archetype，
    // 因此它只能在这一段里做，绝不能落回 begin_season 那次遍历之内。
    for (std::size_t i = 0; i < handles_.size(); ++i)
    {
        if (auto* f = world.get_if<Feeding>(handles_[i]); f != nullptr)
        {
            f->satisfaction = feeding[i];
        }
        else
        {
            (void)world.add<Feeding>(handles_[i], Feeding{feeding[i]});
        }
    }

    const auto sites = gamev2::environment::build_sites(
        env_.resources(), env_.climate(), foragers_, species_keys_,
        env_.config());

    targets_.clear();
    targets_.reserve(sites.size());
    for (const auto& s : sites)
    {
        targets_.push_back(BreedingTarget{
            gamev2::genetics::SpeciesId{s.species_key}, s.cell, s.progeny,
            s.environment});
    }
}

void EnvironmentSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    // 便捷组合，供直接构造本 system 的调用方（genetics_tests 等）用。
    // World::run_season 走的是 begin/step/finish 三段，不走这里。
    begin_season(world);
    for (std::uint32_t i = 0; i < env_.config().steps_per_season; ++i)
    {
        step();
    }
    finish(world);
}
```

原有的注释里有几段解释「为什么」（判据与 BreedingSystem 相同、add 的
archetype 迁移、per_cell 的两趟分离）——**全部保留**，分别落到它解释的
那一段里。

- [ ] **Step 5: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 四套件全过。`test_environment_systems.cpp` 里那些直接调
`execute` 的测试应当一字不改地继续通过——那是本任务成立的前提。

- [ ] **Step 6: 提交（由用户决定）**

```bash
git add include/gamev2/ecs/adapt/environment_systems.hpp src/ecs/adapt/environment_systems.cpp tests/test_environment_systems.cpp
git commit -m "feat(adapt): split EnvironmentSystem into begin/step/finish"
```

---

### Task 3: World 的 tick API 与调度器重组

**Files:**
- Modify: `include/gamev2/world/world.hpp`
- Modify: `src/world/world.cpp`
- Modify: `tests/test_world_module.cpp`
- Modify: `tests/test_environment_systems.cpp`
- Modify: `doc/2026-10-16-world-design.md`

**Interfaces:**
- Consumes: Task 2 的 `EnvironmentSystem::begin_season` / `step` / `finish`
- Produces: `world::World` 新增
  - `void step_once()`
  - `[[nodiscard]] SeasonReport season_report()`
  - `[[nodiscard]] std::uint32_t tick_in_season() const noexcept`
- `run_season()` 签名不变，内部改为「跑满 steps_per_season 次 step_once
  再 season_report」

**这一步会改掉一条既有断言，先说清楚：**

`tests/test_environment_systems.cpp` 里的
`world_assembly_plans_into_three_batches_with_one_parallel_pair` 断言
「四个 system 排成三批」。`EnvironmentSystem` 与 `BreedingSystem` 离开
调度器后，`World` 自己的调度器只装 `MovementSystem` 与 `AgingSystem`，
排成 **1 批**。

那条测试自建了一个四 system 的 `Scheduler`，并不走 `World`，因此它**不会
自动变红**——但它的名字与注释从此描述的是一个 `World` 已经不再使用的装配。
本任务要把它改名并改注释，不删它（它仍然是一条真实成立的断言：一个
`mutates_structure` 的 system 加两个 WriteSet 相交的 system，确实排成三批）。
并行那一对由 `movement_and_aging_do_not_conflict_so_a_batch_can_run_them_in_parallel`
守着，它断言的 `batch_count() == 1` **恰好就是 `World` 新的装配**，一字不改
仍然成立。

- [ ] **Step 1: 写失败测试**

加到 `tests/test_world_module.cpp`。该文件已有一个 `Fixture`（匿名 namespace
末尾，约 170 行起），它持有 `LocusTable table` / `GeneticsConfig genetics` /
`EnvironmentConfig config` / `SpeciesId sid` / `gamev2::world::World world`，
构造用种子 31，并提供 `spawn(x, y)`、`spawn_doomed(x, y)`、
`seed(count, per_cell)`、`alive()`。**直接用它，不要新造 fixture。**

`Fixture::config` 取 `roomy_config()`，`steps_per_season` 是默认的 8，
正好就是测试需要的步数，因此不需要覆盖任何配置。

```cpp
TEST(run_season_equals_eight_step_once)
{
    // tick 化是纯重组：分八步驱动与一步到位必须逐位相同。任何一步被挪到
    // 另一个 tick、或步数不等，这条立刻红。
    //
    // 两个 Fixture 同种子、同铺法，因此两条轨迹本该逐位一致。
    // 报告的比较逐字段手比：SeasonReport 没有现成的逐项比较函数。
    Fixture whole;
    whole.seed(kCellCount, 2);
    const auto whole_report = whole.world.run_season();

    Fixture stepped;
    stepped.seed(kCellCount, 2);
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        stepped.world.step_once();
        CHECK_EQ(stepped.world.tick_in_season(), i + 1);
    }
    const auto stepped_report = stepped.world.season_report();

    CHECK_EQ(stepped_report.season, whole_report.season);
    CHECK_EQ(stepped_report.population, whole_report.population);
    CHECK_EQ(stepped_report.species.size(), whole_report.species.size());
    for (std::size_t i = 0; i < whole_report.species.size(); ++i)
    {
        CHECK_EQ(stepped_report.species[i].census,
                 whole_report.species[i].census);
        CHECK_EQ(stepped_report.species[i].heterozygosity,
                 whole_report.species[i].heterozygosity);
        CHECK_EQ(stepped_report.species[i].f_coefficient,
                 whole_report.species[i].f_coefficient);
        CHECK_EQ(stepped_report.species[i].effective_population,
                 whole_report.species[i].effective_population);
    }
    // 报告归零，下一季从头开始。
    CHECK_EQ(stepped.world.tick_in_season(), std::uint32_t{0});
}

TEST(a_season_that_did_not_run_every_substep_is_reported)
{
    // 少跑一步就结算会让摄食累加器少累一轮，季末 feeding 偏高——不崩，
    // 只是静默偏松。因此这里要的是一条 error 级诊断，而不是断言数值。
    Fixture f;
    f.seed(64);

    std::vector<Diagnostic> seen;
    f.world.entities().set_diagnostic_handler(
        [&](const Diagnostic& d)
        {
            if (d.code == "WORLD_INCOMPLETE_SEASON")
            {
                seen.push_back(d);
            }
        });

    f.world.step_once();
    f.world.step_once();
    (void)f.world.season_report();

    CHECK_EQ(seen.size(), std::size_t{1});
}
```

`set_diagnostic_handler` 在 `ecs::World`（`include/gamev2/ecs/world.hpp:55`），
`Diagnostic` 与 `DiagnosticLevel` 由该头传递包含。若编译报缺 include，补
`#include "gamev2/ecs/diagnostic.hpp"`。

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 编译失败，`step_once` / `season_report` / `tick_in_season`
未声明。

- [ ] **Step 3: 声明 World 的 tick API**

在 `include/gamev2/world/world.hpp` 的 `run_seasons` 声明**之前**插入：

```cpp
    /// 推进一个季内子步。一季 = config.steps_per_season 次。
    ///
    /// 第一次调用顺带跑完「季首」那一步：推进三处季号、跑 MovementSystem
    /// 与 AgingSystem、推进气候、收集摄食输入。之后每次只跑一步摄食。
    ///
    /// \pre 不可重入，不可多线程并发调用（同 run_season）。
    void step_once();

    /// 季末结算并返回报告：算子代预算、繁殖、换代、统计。返回后世界停在
    /// 下一季的开头，tick_in_season() 归零。
    ///
    /// \pre 本季已被 step_once 推进 config.steps_per_season 次。少跑会
    ///      让摄食累加器少累一轮、季末 feeding 偏高——不崩，只是静默偏松，
    ///      因此这里报一条 WORLD_INCOMPLETE_SEASON 诊断而非拦下。
    [[nodiscard]] SeasonReport season_report();

    /// 本季已推进的子步数。0 表示「季末刚过或尚未开始」，两种状态对调用方
    /// 是同一个——渲染层看到 0 就该准备进入下一季。
    [[nodiscard]] std::uint32_t tick_in_season() const noexcept
    {
        return tick_;
    }
```

把 `species()` 的 `\pre` 注释改掉——阶段 3 才放宽它，本任务不动：

```cpp
    /// \pre 全部物种必须在第一次 run_season 之前登记完，之后不再登记。
```
（保持原样。放宽属阶段 3。）

私有成员段加一行，挨着 `season_`：

```cpp
    gamev2::genetics::GeneticsConfig genetics_config_;
    gamev2::genetics::SpeciesRegistry species_;
    std::uint64_t root_seed_;
    std::uint32_t season_ = 0;
    std::uint32_t tick_ = 0;
```

- [ ] **Step 4: 重排 World 的构造与推进**

`src/world/world.cpp` 的构造函数里，把注册段替换为：

```cpp
    // 调度器现在只装 MovementSystem 与 AgingSystem。两者访问集不相交
    // （movement_system.hpp 的 ReadSet/WriteSet 说明了为什么），因此排成
    // 一批并发执行——这是 World 层唯一的并发点，也是 tick 化之后仍然
    // 保留的那一处。
    //
    // EnvironmentSystem 与 BreedingSystem 刻意不进：前者的 8 个子步要由
    // World 逐步驱动（跨 tick 的位置关系调度器表达不了），后者必须在
    // EnvironmentSystem::finish 之后、换代 flush 之前跑，与
    // PopulationStatisticsSystem 是同一条理由——直接 execute，位置由 World
    // 的显式序列钉死。
    scheduler_.add(movement_);
    scheduler_.add(aging_);
    scheduler_.plan();
```

把 `run_season` 替换为三个函数：

```cpp
void World::step_once()
{
    if (tick_ == 0)
    {
        open_season();
    }
    environment_.step();
    ++tick_;
}

void World::open_season()
{
    // 三处 season_index_ 都在各自消费它的入口一次性使用（移动的随机流、
    // climate().advance、RandomStreams），因此推进必须整体发生在它们各自
    // 读之前。
    movement_.advance_season(season_);
    environment_.advance_season(season_);
    breeding_.advance_season(season_);

    // 季首这一批：移动与老化排在摄食之前，与 EnvironmentSystem 的
    // order = 0 那条约束同源——摄食要按当季坐标分格。
    scheduler_.run(entities_, cmd_);

    environment_.begin_season(entities_);
}

SeasonReport World::season_report()
{
    if (tick_ != env_.config().steps_per_season)
    {
        entities_.report(Diagnostic{
            DiagnosticLevel::error, "WORLD_INCOMPLETE_SEASON",
            "season_report called after " + std::to_string(tick_)
                + " of "
                + std::to_string(env_.config().steps_per_season)
                + " substeps"});
    }

    environment_.finish(entities_);

    // BreedingSystem 经 targets_ 指针读 EnvironmentSystem 的产物，因此它
    // 必须排在 finish 之后。
    breeding_.execute(entities_, cmd_);

    // 换代在这里生效。调度器在它自己的 run 末尾 flush 一次，但此刻不在
    // run 内部——breeding 直接 execute 入的是 cmd_，需要 World 自己冲。
    cmd_.flush(entities_);

    // 统计落在换代之后，因此 census 含刚出生的子代、不含本季将死者。
    statistics_.execute(entities_, cmd_);

    SeasonReport report{season_, 0, statistics_.statistics()};
    for (const auto& s : report.species)
    {
        report.population += s.census;
    }
    ++season_;
    tick_ = 0;
    return report;
}

SeasonReport World::run_season()
{
    for (std::uint32_t i = 0; i < env_.config().steps_per_season; ++i)
    {
        step_once();
    }
    return season_report();
}
```

`open_season` 是私有辅助，加进头文件的 private 段。
`entities_.report(...)` 与 `Diagnostic` 需要 include
`gamev2/ecs/diagnostic.hpp`——`ecs/world.hpp` 大概率已经传递包含了，
报编译错误时再补。

- [ ] **Step 5: 改掉那条描述旧装配的测试**

在 `tests/test_environment_systems.cpp` 里把
`TEST(world_assembly_plans_into_three_batches_with_one_parallel_pair)` 改名
为 `TEST(a_structure_mutating_system_still_forces_its_own_batch)`，并把注释
第二段改成：

```cpp
    // 世界模块在 tick 化之后只把 MovementSystem 与 AgingSystem 装进调度器
    // （见 doc/2026-10-17-game-design.md 决策 4），排成一批并行。这条用例
    // 守的是另一件事：一个 mutates_structure 的 system 与两个 WriteSet
    // 相交的 system 仍然排成三批——它的形状与 World 的装配同源，因此
    // World 那一侧少装两个 system 时它依然有效。
    //
    // **这里断言的是「这个子装配排成三批」，不是「World 排成三批」。**
    // World 的批次条数由 movement_and_aging_do_not_conflict_so_a_batch_can_
    // run_them_in_parallel 守着，它断言的 batch_count() == 1 正是 World
    // 现在的装配。
```

其余断言与装配代码一字不动。

- [ ] **Step 6: 更新 world 设计文档**

`doc/2026-10-16-world-design.md` 的 §3.3「一季的执行序列」已经与新实现
不符（它写的是 `scheduler_.run` 内部跑完四步、且不得再加一次 flush）。
在那一节的开头插入一条指向新文档的说明，并把「不要再加一次 flush」那条
改成「换代 flush 归 `World::season_report` 里的 `cmd_.flush`」：

```markdown
> **已被 `doc/2026-10-17-game-design.md` 取代。** 编排粒度下沉到 tick：
> `EnvironmentSystem` 与 `BreedingSystem` 不再进调度器，改由 `World` 显式
> 驱动；调度器只留 `MovementSystem` 与 `AgingSystem` 那一对并行。本节保留
> 以记录 tick 化之前的样子，读当前行为请读新文档 §3.3。
```

- [ ] **Step 7: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 四套件全过。`test_world_module.cpp` 原有 13 条一字不改全部
仍过——它们是「重组未改语义」的唯一证据。特别地
`population_never_exceeds_the_capacity_left_below_it` 与
`mating_radius_widens_the_pool_and_relieves_inbreeding` 若变红，说明
执行次序被挪动了，回去查 `open_season` 与 `season_report` 里的语句顺序。

- [ ] **Step 8: 跑一次演示程序确认没崩**

```bash
& build/sim.exe evolve 50
```
Expected: 50 行逐季输出，种群落在平台上（对照 `src/sim/main.cpp` 顶部
记录的 500 季读数：末季约 Σcapacity 的 89.6%）。若种群归零或暴涨，
说明顺序错了。

- [ ] **Step 9: 提交（由用户决定）**

```bash
git add include/gamev2/world/world.hpp src/world/world.cpp tests/test_world_module.cpp tests/test_environment_systems.cpp doc/2026-10-16-world-design.md
git commit -m "feat(world): drive a season one substep at a time"
```

---

### Task 4: game::GameSession 与 Scroll

**Files:**
- Create: `include/gamev2/game/session.hpp`
- Create: `src/game/session.cpp`
- Create: `tests/test_game_session.cpp`
- Modify: `CMakeLists.txt`
- Modify: `scripts/build.ps1`

**Interfaces:**
- Consumes: Task 3 的 `World::step_once` / `season_report` / `tick_in_season`
- Produces:
  - `struct GameConfig`（spec §4.4 的九个字段，全部带默认值）
  - `struct Scroll`（spec §6.1：season / report / victory /
    seasons_to_victory）
  - `class GameSession`：
    - `explicit GameSession(GameConfig config)`
    - `gamev2::genetics::LocusTable& locus_table() noexcept`
    - `gamev2::world::World& world() noexcept`
    - `void advance_one_substep()` —— 跑一个季内子步，渲染层每帧一次
    - `void advance_one_season()` —— 把本季剩余子步补齐，季末结算并刷新
      `scroll_`
    - `[[nodiscard]] const Scroll& scroll() const noexcept`
    - `[[nodiscard]] std::uint32_t tick_in_season() const noexcept`
  - `world::World` 新增 `[[nodiscard]] std::uint32_t steps() const noexcept`

**本任务不做**（spec §4.4 里的三条干预：画地形 / 气候笔刷 / 编辑基因库）。
地形要到阶段 2 才有；气候笔刷与基因库编辑属阶段 4。现在加空壳只会写出
一个改不了任何东西的接口。

- [ ] **Step 1: 写失败测试**

`tests/test_game_session.cpp`（新建）：

```cpp
#include "gamev2/game/session.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::game::GameConfig;
using gamev2::game::GameSession;

TEST(a_session_starts_on_season_zero_with_no_creatures)
{
    GameSession session{GameConfig{}};
    CHECK_EQ(session.scroll().season, std::uint32_t{0});
    CHECK_EQ(session.scroll().report.population, std::size_t{0});
    CHECK_EQ(session.tick_in_season(), std::uint32_t{0});
    CHECK(!session.scroll().victory);
}

TEST(a_session_advances_a_whole_season_per_advance_one_season)
{
    // 两个粒度必须分开，名字就是它们的唯一区别：advance_one_substep 走一
    // 步，advance_one_season 走完一季。混用会让渲染层一帧跑掉一整季。
    GameSession session{GameConfig{}};
    session.advance_one_season();
    CHECK_EQ(session.scroll().season, std::uint32_t{1});
    CHECK_EQ(session.scroll().report.season, std::uint32_t{0});
    CHECK_EQ(session.tick_in_season(), std::uint32_t{0});
}

TEST(advancing_one_substep_does_not_finish_a_season)
{
    GameConfig config{};
    config.steps_per_season = 8;
    GameSession session{config};
    session.advance_one_substep();
    CHECK_EQ(session.tick_in_season(), std::uint32_t{1});
    CHECK_EQ(session.scroll().season, std::uint32_t{0});
}

TEST(victory_needs_the_species_count_and_the_full_streak)
{
    GameConfig config{};
    config.target_species = 2;
    config.target_seasons = 3;
    GameSession session{config};

    // 世界上一个个体都没有，零物种：判定恒不成立。
    CHECK(!session.scroll().victory);
    CHECK_EQ(session.scroll().seasons_to_victory, std::uint32_t{3});
}

}  // namespace
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```
Expected: 编译失败，`gamev2/game/session.hpp` 不存在。

- [ ] **Step 3: 写头文件**

`include/gamev2/game/session.hpp`：

```cpp
/// @file session.hpp
/// @brief 游戏层：持有 world::World，提供 tick 级推进与季末读数。
///
/// 本层只做编排与读数，不含任何模拟逻辑，也不认识渲染库——渲染层要的量
/// 全部由这里的只读访问器提供。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.4。
#pragma once

#include "gamev2/genetics/locus.hpp"
#include "gamev2/world/world.hpp"

#include <cstddef>
#include <cstdint>

namespace gamev2::game {

/// 一局的配置。玩家干预工具（画屏障 / 气候笔刷 / 育种所）在阶段 4 才接进来。
struct GameConfig
{
    std::uint64_t root_seed = 20261017;
    /// 胜利所需的并存物种数下限。
    std::size_t target_species = 3;
    /// 达到 target_species 后还需持续的季数。
    std::uint32_t target_seasons = 20;
    /// census 低于此值不算「存活」，因此不计入并存物种数。
    std::size_t min_viable = 20;
};

/// 抄经人读的那一卷。SeasonReport 之上只多一样东西：是否已经抄完。
struct Scroll
{
    std::uint32_t season = 0;
    gamev2::world::SeasonReport report;
    bool victory = false;
    /// 距离胜利还差几季。已达标且尚未走满 target_seasons 时逐季递减。
    std::uint32_t seasons_to_victory = 0;
};

class GameSession
{
public:
    explicit GameSession(GameConfig config);

    /// 位点表。构造世界要用，且必须活得比本对象久——World 持有它的引用。
    [[nodiscard]] gamev2::genetics::LocusTable& locus_table() noexcept
    {
        return table_;
    }

    /// 铺初始个体用。本层不生成初始种群，理由同 world::World。
    [[nodiscard]] gamev2::world::World& world() noexcept
    {
        return world_;
    }

    /// 推进一个季内子步。渲染层每帧调一次。
    void advance_one_substep();

    /// 跑完整一季：把剩下的子步补齐，季末结算，刷新 scroll_。
    void advance_one_season();

    [[nodiscard]] const Scroll& scroll() const noexcept
    {
        return scroll_;
    }

    [[nodiscard]] std::uint32_t tick_in_season() const noexcept
    {
        return world_.tick_in_season();
    }

private:
    /// 只读地重算并存物种数与胜利进度。不改世界。
    void refresh_scroll() noexcept;

    GameConfig config_;
    gamev2::genetics::LocusTable table_;
    gamev2::world::World world_;
    Scroll scroll_;
    std::uint32_t coexist_seasons_ = 0;
};

}  // namespace gamev2::game
```

成员声明顺序即构造顺序：`table_` 必须在 `world_` 之前，因为
`World` 的构造函数要拿 `table` 的引用。

- [ ] **Step 4: 实现**

`src/game/session.cpp`：

```cpp
#include "gamev2/game/session.hpp"

namespace gamev2::game {

GameSession::GameSession(GameConfig config)
    : config_{config},
      table_{},
      world_{table_, make_genetics(), make_environment(), config.root_seed}
{
    scroll_.seasons_to_victory = config_.target_seasons;
}

void GameSession::advance_one_substep()
{
    world_.step_once();
}

void GameSession::advance_one_season()
{
    while (world_.tick_in_season() < world_.steps())
    {
        world_.step_once();
    }
    scroll_.report = world_.season_report();
    scroll_.season = scroll_.report.season;
    refresh_scroll();
}

void GameSession::refresh_scroll() noexcept
{
    std::size_t coexisting = 0;
    for (const auto& s : scroll_.report.species)
    {
        if (s.census >= config_.min_viable)
        {
            ++coexisting;
        }
    }

    if (coexisting >= config_.target_species)
    {
        ++coexist_seasons_;
    }
    else
    {
        // 中途把物种数压回目标以下，计数归零重来——「维持」比「达到」更难。
        coexist_seasons_ = 0;
    }

    scroll_.victory = coexist_seasons_ >= config_.target_seasons;
    scroll_.seasons_to_victory =
        scroll_.victory ? 0
                        : config_.target_seasons - coexist_seasons_;
}

}  // namespace gamev2::game
```

上面这段是骨架，三个辅助还没写。补齐：

```cpp
namespace {

gamev2::genetics::GeneticsConfig make_genetics()
{
    gamev2::genetics::GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    c.thermal_base_width = 18.0f;
    c.escape_responsiveness = 1.5f;
    return c;
}

gamev2::environment::EnvironmentConfig make_environment()
{
    gamev2::environment::EnvironmentConfig c{};
    // 9800 个体没法全画。压到 1/3 上下，靠密度换可视性——见
    // doc/2026-10-17-game-design.md §10 末段。
    c.capacity_base = 1.3f;
    return c;
}

}  // namespace
```

`make_genetics` / `make_environment` 是刻意的空架子：基因表与物种登记
在阶段 1 不参与任何判定，玩家造什么样的人生下来都一样。等阶段 2 有了
地形、阶段 4 有了干预工具，这两处才会有真正的内容。现在不猜。

以及 `World::steps()`——本类需要知道一季几步。加到 `world.hpp`：

```cpp
    /// 一季有几个季内子步。GameSession 用它把一季补齐。
    [[nodiscard]] std::uint32_t steps() const noexcept
    {
        return env_.config().steps_per_season;
    }
```

`World::steps()` 加在 `include/gamev2/world/world.hpp` 的 `tick_in_season`
旁边——它是 `GameSession` 补齐一季时唯一的步数来源，藏在实现里会让
`advance_one_season` 的那个 while 循环读起来像魔法数字。

- [ ] **Step 5: 接 CMake**

`CMakeLists.txt` 里，`WORLD_TEST_SOURCES` 段之后加：

```cmake
set(GAME_SOURCES
    src/game/session.cpp
)
add_library(game STATIC ${GAME_SOURCES})
target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game PUBLIC world)

set(GAME_TEST_SOURCES
    tests/main.cpp
    tests/test_game_session.cpp
)
add_executable(game_tests ${GAME_TEST_SOURCES})
target_link_libraries(game_tests PRIVATE game)
```

**同一处还要改两行，漏掉任一条后果不同：**

1. ASan 的 foreach 列表加上 `GAME_SOURCES GAME_TEST_SOURCES`，
   否则 `-DGAMEV2_ENABLE_ASAN=ON` 的构建会悄悄漏掉整个游戏层
   （world 设计文档 §7 已经为同一条坑留过记录）
2. `add_executable(sim ...)` 不变——`sim` 仍直接用 `world`，不经游戏层

- [ ] **Step 6: 接构建脚本**

`scripts/build.ps1` 的 `$suites` 加一项：

```powershell
$suites = @('ecs_tests', 'genetics_tests', 'environment_tests', 'world_tests',
            'game_tests')
```

- [ ] **Step 7: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```
Expected: 五个套件全过。

- [ ] **Step 8: 用 ASan 跑一遍**

```bash
pwsh -File scripts/build.ps1 -Test -Asan
```
Expected: 五个套件全过，无 sanitizer 报告。这一步是验 Step 5 的 foreach
改动确实生效——若漏了 `GAME_SOURCES`，这里仍然会过（只是没编进去），
所以要额外确认 `build/` 下确实生成了 `game_tests.exe`。

- [ ] **Step 9: 提交（由用户决定）**

```bash
git add include/gamev2/game/session.hpp src/game/session.cpp tests/test_game_session.cpp CMakeLists.txt scripts/build.ps1 include/gamev2/world/world.hpp src/world/world.cpp
git commit -m "feat(game): add a session that drives the world one tick at a time"
```

---

## 阶段 1 完成后的验收

跑一遍确认，然后就可以出阶段 2 的计划：

```bash
pwsh -File scripts/build.ps1 -Test
& build/sim.exe evolve 200
& build/sim.exe persist 200
```

两个预设的末季种群应分别落在 Σcapacity 的 89.6% 与 124.4% 附近
（`src/sim/main.cpp` 顶部记录的 500 季读数）。**这两条是对「tick 化纯
重组、未改语义」最硬的验证**——它们是既有的、跑过 500 季的基准读数，
任何次序错位都会把它们推到别处。

若读数对得上，`MovementSystem` 的「空掉的格会被重新殖民」那条性质就仍然
成立（它由 `sim` 跑长局实测出来的，不是单测）。此时才有数据支撑
「移动要加哪种趋向」那个决定，阶段 2 的计划才写得出来。

## 不在本计划内

- 阶段 2：`TerrainField`、`MovementSystem` 重写（趋向 + 地形阻挡）
- 阶段 3：`SpeciationSystem`、运行中登记物种、胜利条件的完整形态
- 阶段 4：三条玩家干预工具
- 渲染与 UI：美术规格见 `doc/美术风格规格.md`，另出计划
- 存档：spec §11 末条，五种序列化格式一条都还没有

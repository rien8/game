# 世界模块实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增 `gamev2::world::World`，把 `ecs` / `genetics` / `environment` 三模块装配成一个会自己按季推进、并给出与真实存续状态一致的季末报告的世界。

**Architecture:** 最顶层编排模块，不引入任何新依赖。持有 `ecs::World`、`EnvironmentWorld`、`SpeciesRegistry`、两份 config、`Scheduler` 与四个 system，只引用外部的 `LocusTable`。一季 = 推进季号 → `scheduler.run`（只含三个 system）→ `cmd.flush` → 直接驱动 `PopulationStatisticsSystem` → 组装 `SeasonReport`。

**Tech Stack:** C++23 / clang++ / CMake + Ninja / 自研 ECS（无外部依赖）/ 自研测试框架 `tests/test_framework.hpp`

**Spec:** [doc/2026-10-16-world-design.md](../2026-10-16-world-design.md) — 本计划逐条对应它，spec 与计划一起读。

## Global Constraints

以下约束来自 `AGENTS.md`，对每个任务无条件生效：

- C++23，用 `clang++`；构建入口是 `pwsh -File scripts/build.ps1 -Test`（**不要**直接调 cmake，本机没有 make/cl.exe，且 CMakeLists 硬 pin 编译器）
- 头文件里禁止 `using namespace`
- 任何一行不得超过 90 字符；长条件在 `&&` / `||` 处换行，每行一个操作数
- 注释用 doxygen `///`，只解释**为什么**，不解释做什么；保留所有既有注释
- 不使用 `shared_ptr` / `unique_ptr`
- 不引入任何第三方依赖
- 临时文件放 `agent-temp/`，生成文档放 `doc/`
- **禁止执行 `git commit`**（AGENTS.md 非-negotiable）。每个任务的最后一步是「提交检查点」：列出待提交文件后**停下**，等用户决定。本计划中的提交步骤一律不执行
- 注释与文档用中文；代码标识符用英文
- Windows 上一律通过 `pwsh` 调用终端

**测试套件的既有约定**：每个可执行目标都自带 `tests/main.cpp`（它定义 `main` 与 `test::run_all`），因此 ASan 那个合并目标靠 `list(REMOVE_DUPLICATES)` 去重。新套件必须照做。

---

## File Structure

| 文件 | 责任 |
|------|------|
| `include/gamev2/world/world.hpp` | `SeasonReport` 与 `World` 的声明、契约注释。不含任何逻辑 |
| `src/world/world.cpp` | `World` 的构造（装配四个 system + 订阅调度）与 `run_season` / `run_seasons` |
| `tests/test_world_module.cpp` | 世界模块的 8 条测试与共享夹具 |
| `include/gamev2/ecs/adapt/environment_systems.hpp` | 加 `EnvironmentSystem::advance_season` |
| `include/gamev2/ecs/adapt/genetics_systems.hpp` | 加 `BreedingSystem::advance_season` |
| `tests/test_environment_systems.cpp` | 加 `EnvironmentSystem::advance_season` 的测试 |
| `tests/test_genetics_systems.cpp` | 加 `BreedingSystem::advance_season` 的测试 |
| `CMakeLists.txt` | `world` 库、`world_tests` 目标、ASan 源列表 |
| `scripts/build.ps1` | `$suites` 加入 `world_tests` |

> **`tests/test_world.cpp` 不是本模块的文件。** 它是 ECS 模块自己的世界测试
> （809 行、29 条 TEST），先于本模块存在。本模块的测试叫
> `tests/test_world_module.cpp`。实施中曾按原名新建，把那 29 条测试整个
> 覆盖掉，链接期才炸出来——已还原。别把它「改回」更整齐的名字。

---

## 共享夹具（Task 2 建立，Task 3–6 复用）

写在 `tests/test_world_module.cpp` 的匿名命名空间里。**注意这里没有可共享的测试头文件**——`neutral_table()` / `default_genetics()` 各自是 `test_environment_systems.cpp` 匿名命名空间里的私有副本，必须在这里重写一份。

```cpp
namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::Environment;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::Vitality;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kGridSide;
using gamev2::genetics::Genome;
using gamev2::genetics::GeneticsConfig;
using gamev2::genetics::LocusKind;
using gamev2::genetics::LocusTable;
using gamev2::genetics::Phenotype;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kLocusCount;
using gamev2::genetics::trait_index;

LocusTable neutral_table()
{
    LocusTable t{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        t.loci[i].kind = LocusKind::Trait;
        t.loci[i].allele_count = 2;
        t.loci[i].scale = 1.0f;
        t.loci[i].switch_rate = 0.0f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

/// trait_base 只经 phenotype_of 传给**子代**；手工搭的亲代拿不到它，
/// 两处各管一半、缺一不可。Metabolism 归零与 Speed 取 1 使 survival_rate
/// 恒为 1，需要「只测编排、不测生存抽签」的测试才能断言确定的数量。
GeneticsConfig steady_genetics()
{
    GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    c.trait_base[trait_index(TraitKind::Metabolism)] = 0.0f;
    c.trait_base[trait_index(TraitKind::Fecundity)] = 0.05f;
    return c;
}

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 承载力放大到 [10, 40]。默认的 capacity_base = 3.9 让单格容量落在
/// [0.975, 3.9]，两三个占位者就能把 headroom 吃光、整季出生数为零——而
/// 「会出生」是本模块一半测试的前提。
EnvironmentConfig roomy_config()
{
    EnvironmentConfig c{};
    c.capacity_base = 40.0f;
    return c;
}

/// 判据必须与 PopulationStatisticsSystem 逐字相同：带 Genome，且物种 id
/// 落在 [1, species_count()]。少一条守卫，引用未注册物种的实体会让
/// 本数比 report.population 多出来。
std::size_t census(ecs::World& w, const std::size_t species_count)
{
    std::size_t n = 0;
    w.for_each_entity([&](const EntityId e)
    {
        const auto* tag = w.get_if<SpeciesTag>(e);
        if (w.get_if<Genome>(e) != nullptr && tag != nullptr
            && tag->species.value != 0 && tag->species.value <= species_count)
        {
            ++n;
        }
    });
    return n;
}

/// 表型模板。Lifespan 必须在 outlived 判据（age >= lifespan）之外留出余量，
/// 否则没有个体活得过第二季，多季测试全部作废；Metabolism 归零才让手工搭的
/// 亲代真的拿到 survival_rate = 1。SenseRange 与 Fecundity 各 0.5，因此摄食
/// demand 恒为 0.25。
Phenotype creature_phenotype()
{
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
    p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
    p.traits[trait_index(TraitKind::Lifespan)] = 10.0f;
    p.traits[trait_index(TraitKind::Metabolism)] = 0.0f;
    return p;
}

/// table / genetics / config 三个成员必须声明在 world 之前。World 的构造
/// 参数是 const LocusTable&，直接写 world{neutral_table(), ...} 会让表以
/// 临时对象绑定、构造返回即悬垂；且逆序析构还需要 world 先死。
struct Fixture
{
    LocusTable table{neutral_table()};
    GeneticsConfig genetics{steady_genetics()};
    EnvironmentConfig config{roomy_config()};
    SpeciesId sid{};
    gamev2::world::World world;

    explicit Fixture(const std::size_t workers = 1)
        : world{table, genetics, config, 31, workers}
    {
        sid = *world.species().register_species(all_loci());
    }

    /// 给满六个组件。这是 World::entities() 的唯一合法用法。
    EntityId spawn(const float x, const float y)
    {
        auto& w = world.entities();
        const EntityId e = w.create();
        (void)w.add<Position>(e, Position{x, y});
        (void)w.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)w.add<Environment>(e, Environment{});
        (void)w.add<Genome>(e, Genome{});
        (void)w.add<Vitality>(e, Vitality{});
        (void)w.add<CreaturePhenotype>(e,
                                       CreaturePhenotype{creature_phenotype()});
        return e;
    }

    /// 缺 Position 的幽灵个体：参与存活抽签，但不摄食、不进任何父池。
    EntityId spawn_without_position()
    {
        auto& w = world.entities();
        const EntityId e = w.create();
        (void)w.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)w.add<Environment>(e, Environment{});
        (void)w.add<Genome>(e, Genome{});
        (void)w.add<Vitality>(e, Vitality{});
        (void)w.add<CreaturePhenotype>(e,
                                       CreaturePhenotype{creature_phenotype()});
        return e;
    }

    /// outlived 判的是 age >= lifespan，而 age 由 AgingSystem 当季递增到 1，
    /// 缺省 Lifespan = 10.0 时 1 >= 10 不成立；显式设 0 才能让它第一季就过寿。
    /// Age 先跑而死亡在 flush 才生效，因此它当季仍参与繁殖与存活抽签——这对本
    /// 模块无影响，只在写测试时别被吓到。
    EntityId spawn_doomed(const float x, const float y)
    {
        const EntityId e = spawn(x, y);
        auto* p = world.entities().get_if<CreaturePhenotype>(e);
        p->phenotype.traits[trait_index(TraitKind::Lifespan)] = 0.0f;
        return e;
    }

    /// 每格 n 个，铺满前 count 个格。i 独占 (i % kGridSide, i / kGridSide)。
    void seed(const std::size_t count, const std::size_t per_cell = 2)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto x = static_cast<float>(i % kGridSide) + 0.5f;
            const auto y = static_cast<float>(i / kGridSide) + 0.5f;
            for (std::size_t k = 0; k < per_cell; ++k)
            {
                (void)spawn(x + static_cast<float>(k) * 0.05f, y);
            }
        }
    }

    /// 非 const：ecs::World::for_each_entity 不是 const 成员，写成 const
    /// 就得靠 const_cast 绕，而这里没有理由要一个 const 的普查函数。
    std::size_t alive()
    {
        return census(world.entities(), world.species().species_count());
    }
};

}  // namespace
```

---

### Task 1: 给两个 system 加 `advance_season`

季节号目前只能在构造时写死，而 `Scheduler` 自带真线程池、每季重建它等于每季重建线程。两个 system 各自持有一个非 const 的 `season_index_` 成员，且都在 `execute` 开头消费一次，所以推进它只需要一个写成员的方法。

**Files:**
- Modify: `include/gamev2/ecs/adapt/environment_systems.hpp`（`EnvironmentSystem` 公有段）
- Modify: `include/gamev2/ecs/adapt/genetics_systems.hpp`（`BreedingSystem` 公有段）
- Test: `tests/test_environment_systems.cpp`、`tests/test_genetics_systems.cpp`

**Interfaces:**
- Consumes: 无（既有构造）
- Produces:
  ```cpp
  void EnvironmentSystem::advance_season(std::uint32_t season_index) noexcept;
  void BreedingSystem::advance_season(std::uint32_t season_index) noexcept;
  ```
  Task 2 的 `World::run_season` 在 `scheduler_.run` 之前调用这两个方法。

- [ ] **Step 1: 写失败测试（EnvironmentSystem）**

追加到 `tests/test_environment_systems.cpp`。夹具复用该文件已有的 `one_species()` / `place_creature()` / `seed_one_per_cell()`：

```cpp
TEST(advance_season_replaces_the_season_index)
{
    EnvironmentConfig config{};
    EnvironmentWorld a{64, config};
    EnvironmentWorld b{64, config};
    auto species = one_species();

    World wa;
    World wb;
    seed_one_per_cell(wa, 64);
    seed_one_per_cell(wb, 64);

    // 直接构造到季 3，与「构造到季 0 再推进到季 3」必须得到同一份气候。
    // 两者不一致就说明 advance_season 没写进真正被消费的那个成员。
    EnvironmentSystem direct{b, species, 3};
    EnvironmentSystem pushed{a, species, 0};
    pushed.advance_season(3);

    CommandBuffer cmd_a;
    CommandBuffer cmd_b;
    direct.execute(wa, cmd_a);
    cmd_a.flush(wa);
    pushed.execute(wb, cmd_b);
    cmd_b.flush(wb);

    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const CellIndex cell{static_cast<std::uint16_t>(c)};
        CHECK_EQ(a.climate().at(cell).temperature,
                 b.climate().at(cell).temperature);
    }
}
```

需要在该文件的 using 列表里补 `using gamev2::environment::kCellCount;`（该文件目前只导入了 `kGridSide`）。

- [ ] **Step 2: 跑测试，确认它红**

```
pwsh -File scripts/build.ps1
```
预期：`clang++` 报 `no member named 'advance_season' in 'EnvironmentSystem'`。

- [ ] **Step 3: 实现 EnvironmentSystem::advance_season**

加在 `environment_systems.hpp` 的 `EnvironmentSystem` 公有段，`execute` 声明之后：

```cpp
    /// 改写本 system 消费的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：本类在 execute 的第一行就把
    ///       season_index_ 交给 climate().advance()，之后改它不会有效果。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = season_index;
    }
```

- [ ] **Step 4: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：`environment_tests` 全绿，`ecs_tests` / `genetics_tests` 不受影响。

- [ ] **Step 5: 写失败测试（BreedingSystem）**

追加到 `tests/test_genetics_systems.cpp`。`Harness::spawn` 接收 `World&`（该夹具只持有表、配置与登记表，不持有世界），所以两个世界要自己声明。`offspring_genomes` 是该文件已有的私有辅助函数，签名是 `std::vector<Genome> offspring_genomes(World&, const std::vector<EntityId>& founders)`。

**这段代码是实施中修过的**：初版直接照抄 `Harness::spawn` 加的全零 `CreaturePhenotype{}`，`Fecundity` 为 0 → 一个子代都生不出来 → 比对的是两个空 vector。把 `advance_season` 变异成空操作后它仍然全绿，是一条恒真的假测试。下面的版本修了三处：给每个个体真实表型、给互异等位、加一条「夹具确实生出了子代」的凭证。

```cpp
TEST(breeding_advance_season_replaces_the_season_index)
{
    Harness ha;
    Harness hb;
    World wa;
    World wb;
    std::vector<EntityId> founders_a;
    std::vector<EntityId> founders_b;
    // 夹具：两个世界各自跑一遍完全相同的布置。不能用 EntityId 判断某个
    // 实体属于哪个世界——两个 World 都从空开始，句柄逐个相同，e == fa 对
    // 两边都成立。改用下标奇偶选世界。
    const auto dress = [](World& w, const EntityId e, const int i)
    {
        // 表型必须给出真的繁殖力与真的抽签：默认全零的 CreaturePhenotype
        // 让 fecundity 为 0、一个子代都生不出来，这条用例就在比对两个空
        // vector 而恒真。Speed 0.9 把 survival_rate 压到 0.9，于是存活
        // 集合真由抽签决定，而抽签流正是季号唯一的作用点。
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 0.9f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.5f);
        // 每个个体的四段等位互不相同。子代由抽中的两个亲代重组而来，
        // 亲代集合一变子代逐位就变；全体 Genome{} 相同时选谁都一样，
        // 抽签结果这条链在测试里根本不存在，这条用例就成了恒真。
        auto* g = w.get_if<Genome>(e);
        g->a[0].allele = static_cast<AlleleId>(i % 2);
        g->a[1].allele = static_cast<AlleleId>((i / 2) % 2);
        g->b[0].allele = static_cast<AlleleId>((i / 4) % 2);
        g->b[1].allele = static_cast<AlleleId>((i / 8) % 2);
    };

    for (int i = 0; i < 6; ++i)
    {
        const EntityId fa = ha.spawn(wa);
        founders_a.push_back(fa);
        dress(wa, fa, i);
        const EntityId fb = hb.spawn(wb);
        founders_b.push_back(fb);
        dress(wb, fb, i);
    }

    CommandBuffer cmd_a;
    CommandBuffer cmd_b;

    // 两个 system 的 root_seed 相同，唯一变量是季号；推进到同一季之后
    // 抽签流必须逐位重合一。若 advance_season 没写进真正被消费的那个
    // 成员，pushed 仍停在季 0，两边的子代就分岔了。
    BreedingSystem direct{ha.table, ha.species, ha.config, 99, 5};
    direct.execute(wa, cmd_a);
    cmd_a.flush(wa);

    BreedingSystem pushed{hb.table, hb.species, hb.config, 99, 0};
    pushed.advance_season(5);
    pushed.execute(wb, cmd_b);
    cmd_b.flush(wb);

    CHECK_EQ(wa.entity_count(), wb.entity_count());
    const auto ga = offspring_genomes(wa, founders_a);
    const auto gb = offspring_genomes(wb, founders_b);
    // 夹具确实生出了子代，否则下面那个循环一次都不跑，整条用例是空的。
    CHECK(!gb.empty());
    CHECK_EQ(ga.size(), gb.size());
    for (std::size_t i = 0; i < ga.size() && i < gb.size(); ++i)
    {
        CHECK(ga[i] == gb[i]);
    }
}
```

写完这条测试后**做一次变异确认**：把 `BreedingSystem::advance_season` 的方法体换成 `static_cast<void>(season_index);`（编译能过，行为等价于不写成员），该测试必须报红。environment 那条测试不需要这一步，它在同样的变异下本就报红。

- [ ] **Step 6: 跑测试，确认它红**

```
pwsh -File scripts/build.ps1
```
预期：`no member named 'advance_season' in 'BreedingSystem'`。

- [ ] **Step 7: 实现 BreedingSystem::advance_season**

加在 `genetics_systems.hpp` 的 `BreedingSystem` 公有段，两个构造函数之后：

```cpp
    /// 改写本 season 抽签所用的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：本类在 execute 里构造
    ///       RandomStreams{root_seed_, season_index_}，中途改它只会让
    ///       同一批个体抽到两套流。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = static_cast<std::uint64_t>(season_index);
    }
```

- [ ] **Step 8: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：三个套件全绿。

- [ ] **Step 9: 提交检查点**

待提交（**不要自己执行 `git commit`**）：

```
include/gamev2/ecs/adapt/environment_systems.hpp
include/gamev2/ecs/adapt/genetics_systems.hpp
tests/test_environment_systems.cpp
tests/test_genetics_systems.cpp
```

建议消息：`feat(ecs/adapt): let the season index advance without rebuilding systems`

---

### Task 2: `World` 骨架与构建接线

**Files:**
- Create: `include/gamev2/world/world.hpp`
- Create: `src/world/world.cpp`
- Create: `tests/test_world_module.cpp`
- Modify: `CMakeLists.txt`
- Modify: `scripts/build.ps1`

**Interfaces:**
- Consumes: Task 1 的两个 `advance_season`
- Produces: `gamev2::world::SeasonReport`、`gamev2::world::World`，全部五个公有成员（`species` / `entities` / `environment` / `run_season` / `run_seasons`）。Task 3–6 只用这五个。

- [ ] **Step 1: 接好构建（先有目标，后有代码）**

`CMakeLists.txt`，在 `ENVIRONMENT_TEST_SOURCES` 那一段之后、`option(GAMEV2_ENABLE_ASAN ...)` 之前插入：

```cmake
set(WORLD_SOURCES
    src/world/world.cpp
)
add_library(world STATIC ${WORLD_SOURCES})
target_include_directories(world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(world PUBLIC environment)

set(WORLD_TEST_SOURCES
    tests/main.cpp
    tests/test_world_module.cpp
)
add_executable(world_tests ${WORLD_TEST_SOURCES})
target_link_libraries(world_tests PRIVATE world)
```

同一文件里 ASan 那段的 `foreach`，把两个新列表加进去（不加的话 `-DGAMEV2_ENABLE_ASAN=ON` 的构建会静默漏掉整个世界模块）：

```cmake
    foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES
                  GENETICS_SOURCES GENETICS_TEST_SOURCES
                  ENVIRONMENT_SOURCES ENVIRONMENT_TEST_SOURCES
                  WORLD_SOURCES WORLD_TEST_SOURCES)
```

`scripts/build.ps1`，`$suites` 那行：

```powershell
$suites = @('ecs_tests', 'genetics_tests', 'environment_tests', 'world_tests')
```

`tests/test_world_module.cpp` 先建出来，只放共享夹具那一整段（含 `#include "gamev2/world/world.hpp"`），末尾一个空测试占位：

```cpp
#include "test_framework.hpp"

#include "gamev2/world/world.hpp"

#include "gamev2/ecs/world.hpp"
#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <cstddef>
#include <cstdint>

// ... 上面的匿名命名空间夹具全文 ...

TEST(world_runs_an_empty_season)
{
    Fixture f;
    const auto report = f.world.run_season();
    CHECK_EQ(report.season, 0u);
    CHECK_EQ(report.population, 0u);
}
```

- [ ] **Step 2: 跑构建，确认它红**

```
pwsh -File scripts/build.ps1
```
预期：`src/world/world.cpp` 不存在 / `gamev2/world/world.hpp` 不存在。

- [ ] **Step 3: 写 `include/gamev2/world/world.hpp` 全文**

```cpp
/// @file world.hpp
/// @brief 世界模块：把 ecs / genetics / environment 装配成一个会自己按季
///        推进的整体，并给出与真实存续状态一致的季末报告。
///
/// 一季的时序只有这一处实现。三个前序模块各自用 System::order 注释守住的
/// 先后约束，在这里第一次被真正执行一次。
#pragma once

#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/config.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/species.hpp"
#include "gamev2/genetics/statistics.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 一次季节结算的结果。
struct SeasonReport
{
    std::uint32_t season = 0;
    /// 各物种 census 之和，即本季存活总数。判据与统计 system 相同：
    /// 缺 Genome 或引用未注册物种的实体不在其中。
    std::size_t population = 0;
    /// 下标 = SpeciesId::value - 1。拷贝而非引用：统计 system 的成员缓冲
    /// 会被下一季 clear() 重写，而报告要活得比下一季久。拷贝代价与物种数
    /// 成正比而与个体数无关——AlleleStats 是逐位点的定长统计。
    std::vector<gamev2::genetics::SpeciesStatistics> species;
};

/// 装配并驱动整个世界。构造完即可放进初始个体并按季推进。
///
/// 本类不生成初始种群：调用方本来就必须知道怎么造 Genome 与 Phenotype，
/// 把它藏在这里只会多一层无法验证的隐式行为。
class World
{
public:
    /// \pre table 的生命周期必须覆盖本对象——它是唯一被引用的外部数据。
    /// \pre worker_count >= 1。
    World(const gamev2::genetics::LocusTable& table,
          gamev2::genetics::GeneticsConfig genetics_config,
          gamev2::environment::EnvironmentConfig environment_config,
          std::uint64_t root_seed,
          std::size_t worker_count = 1);

    /// \pre 全部物种必须在第一次 run_season 之前登记完，之后不再登记。
    ///       未注册的 id 会让对应实体被 EnvironmentSystem 与
    ///       BreedingSystem 各自跳过，而后者报一条诊断。
    gamev2::genetics::SpeciesRegistry& species() noexcept
    {
        return species_;
    }

    /// 初始个体由调用方经这里放进世界。
    ///
    /// 一个参与世界运转的个体必须带齐六个组件：Genome、CreaturePhenotype、
    /// Environment、SpeciesTag、Position、Vitality。缺其中任何一个的后果见
    /// 实现处与设计文档 §5，其中最反直觉的是缺 Position——该个体参与存活
    /// 抽签，但不被摄食、不进任何父池，于是永不繁殖也永不被环境杀死。
    gamev2::ecs::World& entities() noexcept
    {
        return entities_;
    }

    /// 只读暴露气候场与资源场。测试要读真实承载力，UI 要画地图。
    const gamev2::ecs::adapt::EnvironmentWorld& environment() const noexcept
    {
        return env_;
    }

    /// 推进一季并返回季末报告。报告的 census 落在 flush 之后，因此等于
    /// 本季真实的存活存续数（死亡已生效、新生代已计入）。
    ///
    /// \pre 不可重入，不可多线程并发调用。
    [[nodiscard]] SeasonReport run_season();

    /// 逐季推进 count 次。与调用 count 次 run_season() 逐位等价——
    /// 这条等价性是可测的，也是日后加存档的前提。
    [[nodiscard]] std::vector<SeasonReport> run_seasons(std::uint32_t count);

    /// 本类持有引用成员（table_）与裸指针成员（Scheduler::entries_ 里指向
    /// 三个 system），移动会让 Scheduler 的裸 System* 指向已析构对象，而
    /// 拷贝会让 breeding_.species_ 指向另一个表的登记表。两条路都是静默损坏。
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

private:
    const gamev2::genetics::LocusTable& table_;
    gamev2::genetics::GeneticsConfig genetics_config_;
    gamev2::environment::EnvironmentConfig environment_config_;
    gamev2::genetics::SpeciesRegistry species_;
    std::uint64_t root_seed_;
    std::uint32_t season_ = 0;

    gamev2::ecs::World entities_;
    gamev2::ecs::CommandBuffer cmd_;
    gamev2::ecs::Scheduler scheduler_;

    // 声明顺序即依赖顺序：breeding_ 的构造参数要取
    // &environment_.targets()，而 env_ 又是 environment_ 的引用对象。
    // 逆序析构恰好把这三条依赖倒过来。
    gamev2::ecs::adapt::EnvironmentWorld env_;
    gamev2::ecs::adapt::EnvironmentSystem environment_;
    gamev2::ecs::adapt::AgingSystem aging_;
    gamev2::ecs::adapt::BreedingSystem breeding_;
    gamev2::ecs::adapt::PopulationStatisticsSystem statistics_;
};

}  // namespace gamev2::world
```

- [ ] **Step 4: 写 `src/world/world.cpp` 全文**

```cpp
#include "gamev2/world/world.hpp"

namespace gamev2::world {

World::World(const gamev2::genetics::LocusTable& table,
             gamev2::genetics::GeneticsConfig genetics_config,
             gamev2::environment::EnvironmentConfig environment_config,
             const std::uint64_t root_seed,
             const std::size_t worker_count)
    : table_{table},
      genetics_config_{genetics_config},
      environment_config_{environment_config},
      root_seed_{root_seed},
      entities_{},
      cmd_{},
      scheduler_{worker_count},
      env_{root_seed, environment_config},
      environment_{env_, species_, 0},
      aging_{},
      breeding_{table, species_, genetics_config_, root_seed, 0,
                &environment_.targets(),
                environment_config.mating_radius_cells},
      statistics_{species_}
{
    // 注册顺序不参与调度顺序：plan() 先按 System::order 稳定排序再贪心分层
    // （EnvironmentSystem 与 BreedingSystem 各带 order 静态常量）。这里只
    // 决定同 order 的 system 谁先占住靠前的批次。
    scheduler_.add(environment_);
    scheduler_.add(aging_);
    scheduler_.add(breeding_);
    scheduler_.plan();

    // statistics_ 刻意不进调度器：它与另三个 system 的访问集相交（都碰
    // SpeciesTag），注册进去既省不下任何并行，又只能在 cmd.flush 之前
    // 跑到——而 BreedingSystem 的死亡销毁与子代创建都在 flush 才生效，
    // 于是 census 会含本季将死者、不含刚出生的子代。
}

SeasonReport World::run_season()
{
    // 两处 season_index_ 都在各自 execute 的开头消费一次，因此推进必须
    // 整体发生在 scheduler_.run 之前。
    environment_.advance_season(season_);
    breeding_.advance_season(season_);

    scheduler_.run(entities_, cmd_);

    // 换代（死亡销毁与子代创建）**已在 scheduler_.run 内部生效**——它在末尾
    // 就 cmd.flush(world)（ecs/scheduler.cpp:142）。本类不重复 flush：
    // CommandBuffer::flush 末尾会 clear()，第二次面对的必然是空缓冲，而那行
    // 冗余代码会让「删掉它」这条变异在结构上无法被任何测试观测到。
    statistics_.execute(entities_, cmd_);

    SeasonReport report{season_, 0, statistics_.statistics()};
    for (const auto& s : report.species)
    {
        report.population += s.census;
    }
    ++season_;
    return report;
}

std::vector<SeasonReport> World::run_seasons(const std::uint32_t count)
{
    std::vector<SeasonReport> reports;
    reports.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        reports.push_back(run_season());
    }
    return reports;
}

}  // namespace gamev2::world
```

- [ ] **Step 5: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：`world_tests` 里 `world_runs_an_empty_season` 通过，其余三个套件不受影响。

- [ ] **Step 6: 提交检查点**

```
include/gamev2/world/world.hpp
src/world/world.cpp
tests/test_world_module.cpp
CMakeLists.txt
scripts/build.ps1
```

建议消息：`feat(world): assemble environment and genetics into a season loop`

---

### Task 3: 季末报告的口径

这是本模块最要紧的一条不变量：报告必须落在 `cmd.flush` 之后，否则 `population` 会变成一个静默错位的数字——含本季将死者、不含新生代，不崩不报错。

**Files:**
- Modify: `tests/test_world_module.cpp`

**Interfaces:**
- Consumes: Task 2 的 `World::run_season` / `SeasonReport::population` / `entities()`
- Produces: 无新接口

- [ ] **Step 1: 写失败测试**

追加到 `tests/test_world_module.cpp`（夹具已在匿名命名空间里，无需改动）：

```cpp
TEST(report_population_matches_the_survivors_after_flush)
{
    Fixture f;
    f.seed(64);
    const auto report = f.world.run_season();
    CHECK_EQ(report.population, f.alive());
}

TEST(report_counts_the_dead_as_gone)
{
    Fixture f;
    f.seed(32);
    // 每格塞一个必死者：Lifespan = 0 而 age 从 0 递增，第一季就过寿。
    for (std::size_t i = 0; i < 32; ++i)
    {
        const auto x = static_cast<float>(i % kGridSide) + 0.9f;
        const auto y = static_cast<float>(i / kGridSide) + 0.5f;
        (void)f.spawn_doomed(x, y);
    }
    const auto before = f.alive();

    const auto report = f.world.run_season();
    const auto after = f.alive();

    // 报告数的是 flush 之后的存续数，而 flush 之前那个数必然更大。
    CHECK_EQ(report.population, after);
    CHECK(report.population < before);
    CHECK(report.population > 0);
}
```

- [ ] **Step 2: 跑测试，确认它绿**（这一步是验证已有实现，不是补实现）

```
pwsh -File scripts/build.ps1 -Test
```
预期：两条都过。若 `report_counts_the_dead_as_gone` 红，说明 `population` 的口径错了——检查是不是把 `statistics_` 挪回了 `scheduler_.run` 之前，或 `cmd_.flush` 被漏掉。

- [ ] **Step 3: 写失败测试（子代计入）**

```cpp
TEST(report_counts_newborn_creatures)
{
    Fixture f;
    f.seed(32);
    const auto before = f.alive();

    // roomy_config 让每格 headroom 充足，且每格两个体使
    // BreedingSystem 的空间桶（mating_radius_cells = 1）凑得齐两个亲代。
    const auto report = f.world.run_season();

    // 稳态表型下 survival_rate 恒为 1，因此死亡只可能来自寿命，而
    // fixture 没有设过寿命：population 只会因出生而增。
    CHECK(report.population > before);
    CHECK_EQ(report.population, f.alive());
}
```

- [ ] **Step 4: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：三条全过。

若这条红，按这个顺序排查：① `roomy_config()` 有没有被用上（`Fixture` 的 `config` 成员）；② 每格个体数是否 ≥ 2（`BreedingSystem` 的桶内少于两个亲代就不产子代）；③ `genetics.trait_base` 里的 `Metabolism` 是否为 0。

- [ ] **Step 5: 提交检查点**

```
tests/test_world_module.cpp
```

建议消息：`test(world): pin the report to the post-flush census`

---

### Task 4: 季号推进与批量等价

**Files:**
- Modify: `tests/test_world_module.cpp`

**Interfaces:**
- Consumes: Task 2 的 `run_season` / `run_seasons`
- Produces: 无新接口

- [ ] **Step 1: 写失败测试**

```cpp
TEST(season_number_starts_at_zero_and_advances)
{
    Fixture f;
    f.seed(16);
    const auto reports = f.world.run_seasons(3);
    CHECK_EQ(reports.size(), 3u);
    CHECK_EQ(reports[0].season, 0u);
    CHECK_EQ(reports[1].season, 1u);
    CHECK_EQ(reports[2].season, 2u);
}

TEST(season_index_actually_moves_the_world)
{
    Fixture f;
    f.seed(16);
    (void)f.world.run_season();
    const float first = f.world.environment().climate()
                            .at(CellIndex{static_cast<std::uint16_t>(0)})
                            .temperature;

    (void)f.world.run_season();
    const float second = f.world.environment().climate()
                             .at(CellIndex{static_cast<std::uint16_t>(0)})
                             .temperature;

    // 气候按季节正弦偏移，同一格跨季必然不同。若这条红，说明
    // advance_season 没接上（或者两季恰好取到同一相位）。
    CHECK(first != second);
}

TEST(run_seasons_equals_repeated_run_season)
{
    Fixture batched;
    Fixture stepwise;
    batched.seed(16);
    stepwise.seed(16);

    const auto bulk = batched.world.run_seasons(5);
    std::vector<SeasonReport> one_by_one;
    for (std::uint32_t i = 0; i < 5; ++i)
    {
        one_by_one.push_back(stepwise.world.run_season());
    }

    CHECK_EQ(bulk.size(), one_by_one.size());
    for (std::size_t i = 0; i < bulk.size() && i < one_by_one.size(); ++i)
    {
        CHECK_EQ(bulk[i].season, one_by_one[i].season);
        CHECK_EQ(bulk[i].population, one_by_one[i].population);
        CHECK_EQ(bulk[i].species.size(), one_by_one[i].species.size());
        for (std::size_t s = 0;
             s < bulk[i].species.size() && s < one_by_one[i].species.size();
             ++s)
        {
            CHECK_EQ(bulk[i].species[s].census,
                     one_by_one[i].species[s].census);
            CHECK_EQ(bulk[i].species[s].heterozygosity,
                     one_by_one[i].species[s].heterozygosity);
        }
    }
}
```

- [ ] **Step 2: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：三条全过。

`season_index_actually_moves_the_world` 是本轮对 Task 1 接线是否正确的间接验证：若 `advance_season` 没写进 `EnvironmentSystem` 真正消费的那个成员，气候会永远停在季 0。

- [ ] **Step 3: 提交检查点**

```
tests/test_world_module.cpp
```

建议消息：`test(world): cover season numbering and batch/step equivalence`

---

### Task 5: 确定性与线程数无关性

**Files:**
- Modify: `tests/test_world_module.cpp`

**Interfaces:**
- Consumes: Task 2 的 `World` 构造的 `worker_count` 参数
- Produces: 无新接口

- [ ] **Step 1: 写失败测试**

```cpp
TEST(equal_seeds_produce_identical_seasons)
{
    Fixture a;
    Fixture b;
    a.seed(48);
    b.seed(48);

    const auto ra = a.world.run_seasons(4);
    const auto rb = b.world.run_seasons(4);

    CHECK_EQ(ra.size(), rb.size());
    for (std::size_t i = 0; i < ra.size() && i < rb.size(); ++i)
    {
        CHECK_EQ(ra[i].population, rb[i].population);
        CHECK_EQ(ra[i].species.size(), rb[i].species.size());
        for (std::size_t s = 0;
             s < ra[i].species.size() && s < rb[i].species.size(); ++s)
        {
            CHECK_EQ(ra[i].species[s].census, rb[i].species[s].census);
            CHECK_EQ(ra[i].species[s].heterozygosity,
                     rb[i].species[s].heterozygosity);
            CHECK_EQ(ra[i].species[s].f_coefficient,
                     rb[i].species[s].f_coefficient);
            CHECK_EQ(ra[i].species[s].effective_population,
                     rb[i].species[s].effective_population);
            CHECK_EQ(ra[i].species[s].loci.size(),
                     rb[i].species[s].loci.size());
        }
    }
}

TEST(worker_count_does_not_change_the_result)
{
    Fixture single{1};
    Fixture parallel{4};
    single.seed(48);
    parallel.seed(48);

    const auto a = single.world.run_seasons(3);
    const auto b = parallel.world.run_seasons(3);

    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        CHECK_EQ(a[i].population, b[i].population);
        for (std::size_t s = 0;
             s < a[i].species.size() && s < b[i].species.size(); ++s)
        {
            CHECK_EQ(a[i].species[s].census, b[i].species[s].census);
            CHECK_EQ(a[i].species[s].heterozygosity,
                     b[i].species[s].heterozygosity);
        }
    }
}
```

- [ ] **Step 2: 跑测试**

```
pwsh -File scripts/build.ps1 -Test
```

**这两条是本计划里唯一「预期可能先红后绿」的测试**，spec §6 已标注它们是未经验证的新断言：

- 若 `equal_seeds_produce_identical_seasons` 红，是真的确定性缺陷，必须查清再往下走，不要改成放宽比较。
- 若 `worker_count_does_not_change_the_result` 红，同样是真发现：说明有并发依赖（`EnvironmentSystem` 的 `World::add` archetype 迁移是最可疑的一处，它只被 `mutates_structure` 独占批次挡住）。先报告，不要自行放宽。

预期：通过。

- [ ] **Step 3: 提交检查点**

```
tests/test_world_module.cpp
```

建议消息：`test(world): cover determinism and worker-count independence`

---

### Task 6: 跨模块不变量与幽灵个体

**Files:**
- Modify: `tests/test_world_module.cpp`

**Interfaces:**
- Consumes: Task 2 的 `World::environment()`（读真实承载力）、`entities()`
- Produces: 无新接口

- [ ] **Step 1: 写失败测试**

```cpp
TEST(population_never_exceeds_total_capacity)
{
    Fixture f;

    // 按真实容量把人铺到接近上限的那一档。只放「远低于上限」的数量，
    // 这条断言就恒真、什么也测不出来：满占用下 headroom 归零才压得住
    // 出生，而那正是要验的机制。
    //
    // 取 256 个格（步长 16）而不是全部 4096 格：Genome 有 1200 字节，
    // 全铺会到十万量级。这 256 格与 env 的既有用例同一取法，且子代只会
    // 落在这些格上（build_sites 只为有占用的格发站点），因此上限在这个
    // 子集上就已经咬得住。
    constexpr std::size_t kStride = 16;
    constexpr std::size_t kCells = 256;
    std::vector<EntityId> handles;
    double capacity_sum = 0.0;
    for (std::size_t c = 0; c < kCells; ++c)
    {
        const auto cell =
            CellIndex{static_cast<std::uint16_t>(c * kStride)};
        const float cap = f.world.environment().resources().capacity(cell);
        capacity_sum += cap;

        const auto n = static_cast<std::size_t>(cap * 0.85f);
        const auto x = static_cast<float>(cell.value % kGridSide);
        const auto y = static_cast<float>(cell.value / kGridSide);
        for (std::size_t i = 0; i < n; ++i)
        {
            // 同一格里铺开，避免全部压在同一个亚格坐标上。
            handles.push_back(f.spawn(x + 0.1f
                                          + static_cast<float>(i % 60) * 0.01f,
                                      y + 0.5f));
        }
    }
    CHECK(!handles.empty());

    // build_sites 逐格 lround(headroom)，取整误差 <= 0.5/格。这点余量比
    // 总占位数小一个数量级，因此把 headroom 误算成 capacity 的 mutant
    // 必然越界——与 env 的既有用例同一条余量的推导。
    const auto limit = static_cast<std::size_t>(capacity_sum)
                       + kCells / 2 + 1;

    for (std::uint32_t season = 0; season < 3; ++season)
    {
        const auto report = f.world.run_season();
        CHECK(report.population <= limit);
    }
}

TEST(creatures_without_position_never_die_and_never_breed)
{
    Fixture f;
    f.seed(32);

    // 五个幽灵：缺 Position，因此不被摄食、不进任何父池。
    std::vector<EntityId> ghosts;
    for (std::size_t i = 0; i < 5; ++i)
    {
        ghosts.push_back(f.spawn_without_position());
    }

    auto count_ghosts = [&]
    {
        std::size_t n = 0;
        f.world.entities().for_each_entity([&](const EntityId e)
        {
            for (const auto& g : ghosts)
            {
                if (g == e)
                {
                    ++n;
                }
            }
        });
        return n;
    };

    const auto ghosts_before = count_ghosts();
    const auto pop_before = f.alive();

    (void)f.world.run_seasons(3);

    // 既不被摄食也不繁殖，于是既不会被环境饿死，也不会进任何父池。
    CHECK_EQ(count_ghosts(), ghosts_before);
    // 它们仍在册，所以总普查数只会从「有 Position 的一侧」增长。
    CHECK(f.alive() >= pop_before + 5);
}
```

- [ ] **Step 2: 跑测试，确认它绿**

```
pwsh -File scripts/build.ps1 -Test
```
预期：两条全过。

- [ ] **Step 3: 全量回归**

```
pwsh -File scripts/build.ps1 -Clean -Test
pwsh -File scripts/build.ps1 -Clean -Asan -Test
```
预期：四个套件（`ecs_tests` / `genetics_tests` / `environment_tests` / `world_tests`）在普通构建与 ASan 构建下都全绿。ASan 构建的合并目标叫 `game_tests_asan`，它把四套件合进一个二进制——若出现同名测试，本套件的 8 条名字与既有的都不重名。

- [ ] **Step 4: 提交检查点**

```
tests/test_world_module.cpp
doc/2026-10-16-world-design.md
```

建议消息：`test(world): cover the capacity ceiling and positionless creatures`

---

## Self-Review

**Spec 覆盖**

| Spec 节 | 落在哪 |
|---------|--------|
| §2 决策 1（命名） | Task 2 Step 3 的命名空间与成员名 |
| §2 决策 2（`advance_season`） | Task 1 |
| §2 决策 3（统计不进调度器） | Task 2 Step 4 的构造与 `run_season`，Task 3 守口径 |
| §2 决策 4（所有权） | Task 2 Step 3 的成员列表与注释 |
| §2 决策 5（报告是拷贝） | Task 2 Step 3，`SeasonReport::species` 的注释 |
| §2 决策 6（population 取 census 之和） | Task 2 Step 4 |
| §2 决策 7（批量是朴素循环） | Task 2 Step 4，Task 4 守等价 |
| §2 决策 8（不生成初始种群） | Task 2 Step 3，`entities()` 的注释 |
| §3.3 一季的执行序列 | Task 2 Step 4，四步逐条对应 |
| §4.1 / §4.2 数据模型 | Task 2 Step 3–4 |
| §5 契约 1–5 | Task 2 Step 3 的 `\pre` 与 `entities()` 注释；契约 2 的四行后果表见 Task 6 Step 1 的幽灵测试 |
| §6 确定性 | Task 5 |
| §7 测试 1–8 | Task 3（1、2）、Task 4（3、4）、Task 5（5、6）、Task 6（7、8） |
| §7 构建接线（ASan 源列表） | Task 2 Step 1 |
| §7 刻意不写顺序约束测试 | 无任务——由测试 7 覆盖，理由记在 Task 6 Step 1 的注释里 |
| §8 未决事项 | 不实现，仅保留在文档 |

**无占位符**：所有代码块是可直接粘贴的完整实现或完整测试；没有 TBD、没有「仿照 Task N」。

**类型一致性**：`advance_season(std::uint32_t)` 两处签名一致，`BreedingSystem` 内部转 `uint64_t`（它原本的 `season_index_` 就是 `uint64_t`）；`SeasonReport::population` 是 `std::size_t`，测试里 `CHECK_EQ` 时与 `alive()` 的 `std::size_t` 对齐；`Fixture::spawn` 返回 `EntityId`。

**已知需要实现者临场判断的三处**（不是占位符，是真实的不确定性）：
1. Task 1 Step 5 里 `Harness` 的 `world` 字段名要照抄 `test_genetics_systems.cpp` 的实际命名。
2. Task 1 Step 1 需要给 `test_environment_systems.cpp` 的 using 列表补 `kCellCount`。
3. Task 2 Step 1 中的 CMakeLists 片段插入位置：必须在 `option(GAMEV2_ENABLE_ASAN` 之前，否则新目标会落到 ASan 的子目录里。

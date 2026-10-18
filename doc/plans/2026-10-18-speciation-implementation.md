# 阶段 3（SpeciationSystem）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让世界自己裂变出物种 —— 新增 `SpeciationSystem`，并把初始条件改成有
空间遗传结构的样子，使「分化」第一次成为一个可测、可调的量。

**Architecture:** 先把 `sim` 的播种从「每体独立随机基因型」改成「全球 32 个创始
基因型 + 极轻突变」，世界开幕时才真的是一个物种；再在 `genetics` 加一个纯函数
`divergence`，在 `ecs/adapt` 加不进调度器的 `SpeciationSystem` —— 它按 4 邻域
连通分量逐块判三条条件，第四条（迟滞）用**中位数格锚 + 跨季最近邻匹配**实现，
因为连通分量本身没有稳定身份。`World::season_report` 在换代 flush 之后、统计
之前直接 execute 它，让季末报告看得见刚裂出来的物种。

**Tech Stack:** C++23、clang++ 23、CMake + Ninja、手写测试框架
（`TEST` / `CHECK` / `CHECK_EQ`，见 `tests/test_framework.hpp`）。零第三方依赖。

**Spec:** `doc/2026-10-17-game-design.md`（阶段 3 对应 §2.3 实测、决策 16/17/18、
§4.3、§4.4、§5 与 §5.1、§6.1、§9 的测试 4/5/6/8）

## 范围

本计划只做 spec §10 的**阶段 3**，含用户已确认的三笔附带工作：设计文档 §9 的
测试 4、`fragile` 预设的 `inbreeding_load` 重调、以及播种改造后的基线重取。

阶段 4（画屏障 / 气候笔刷 / 育种所）另出一份独立计划。**条件 (b) 在阶段 4
之前测不出东西**（§5.1），因此本阶段不给它配 mutant 护栏。

## Global Constraints

- 每行不超过 90 字符；长条件在 `&&` / `||` 处断行，每个操作数独占一行
- 注释只写「为什么」与约束，不写「做什么」；既有注释一律保留
- 不新增 `shared_ptr` / `unique_ptr`；**不引入任何第三方库**
- **`src/environment/` 不得 include 任何 `genetics/` 或 `ecs/` 头文件**
- 改动既有代码时只碰本次需求牵动的行，不顺手重构相邻代码
- **不执行 `git commit`**。每个任务末尾照常给出命令，由用户自己决定何时提交
- 构建：`pwsh -File scripts/build.ps1 -Test`
- 单套件运行：`& build/world_tests.exe`（其余同理 `ecs_tests` /
  `genetics_tests` / `environment_tests` / `game_tests`）
- 新增源文件必须同时加进 `CMakeLists.txt` 的目标源列表与
  `GAMEV2_ENABLE_ASAN` 的 foreach 列表。**但本计划新增的两个源文件都落进
  已有的 `GENETICS_SOURCES` / `GENETICS_TEST_SOURCES` 变量，foreach 迭代的正是
  这两个变量本身，因此不必另加行 —— 改完 CMakeLists 后仍要回头确认这一点**

## 本阶段的四个定值（全部有实测依据，勿擅改）

| 量 | 值 | 依据 |
|----|----|------|
| `divergence_threshold` | **10.0f** | spec §2.3。主大陆封顶 6.4 永不裂；小陆块第 15~80 季陆续越过 |
| `min_founder` | **40** | 同上。200 季实测各陆块 1225/517/161/104/72/71，6~9 块够格 |
| `speciation_hysteresis` | **4** 季 | spec §4.4 |
| `hysteresis_radius_cells` | **8.0f** 格 | 单季位移 1~2 格，200 季漂移 σ≈15 格 |

**`divergence` 的值域是 [0, 位点数/2]（300 位点全激活时 [0, 150]），不是 [0, 0.5]。**
spec 原来的 θ=0.06 错约两个数量级。任何拿它当「0 到 1 之间」来读的地方都是 bug。

---

### Task 1: divergence——纯函数与它的真实值域

**Files:**
- Modify: `include/gamev2/genetics/statistics.hpp`
- Modify: `src/genetics/statistics.cpp`
- Test: `tests/test_genetics_statistics.cpp`（既有文件，加测试）

**Interfaces:**
- Consumes: 既有的 `SpeciesStatistics`（`std::vector<AlleleStats> loci`，
  `AlleleStats::frequency` 是 `std::array<float, kMaxAlleles>`）
- Produces:
  `[[nodiscard]] double divergence(const SpeciesStatistics& a,
  const SpeciesStatistics& b) noexcept`
  —— 对 `loci` 取公共前缀的 `|f_a − f_b|` 之和除以 2

**放在 `statistics.hpp` 而不是新开文件**：它是两个 `SpeciesStatistics` 之间的纯
比较，而 `SpeciesStatistics` 就定义在那里。`AlleleStats::frequency` 已经在里面，
所以这个量不引入任何新的统计路径。

- [ ] **Step 1: 写失败测试**

加到 `tests/test_genetics_statistics.cpp` 末尾。先确认该文件的 using 列表里有
`SpeciesStatistics` 与 `kMaxAlleles`；没有就补上。

```cpp
TEST(divergence_is_symmetric_and_bounded)
{
    // 设计文档 §9 测试 8。**值域是 [0, 位点数/2]，不是 [0, 0.5]** —— 原稿写
    // [0, 0.5] 那个错正是 θ=0.06 的来源（§2.3）。`loci` 只含激活位点，因此上界
    // 用实际长度算而不是写死 150。
    SpeciesStatistics a{};
    SpeciesStatistics b{};
    a.loci.resize(4);
    b.loci.resize(4);

    CHECK_EQ(gamev2::genetics::divergence(a, a), 0.0);

    // 上界：每格都取一端相反的频率 ⇒ Σ|Δf| = n，除以 2 ⇒ n/2。
    for (std::size_t i = 0; i < a.loci.size(); ++i)
    {
        a.loci[i].frequency[0] = 0.0f;
        b.loci[i].frequency[0] = 1.0f;
    }
    CHECK_EQ(gamev2::genetics::divergence(a, b), 2.0);

    // 对称。
    CHECK_EQ(gamev2::genetics::divergence(a, b),
             gamev2::genetics::divergence(b, a));

    // 部分差异：两个位点全反、两个位点相同 ⇒ 2/2 = 1。
    b.loci[2].frequency[0] = 0.0f;
    b.loci[3].frequency[0] = 0.0f;
    CHECK_EQ(gamev2::genetics::divergence(a, b), 1.0);
}

TEST(divergence_uses_only_the_shared_prefix_of_the_loci)
{
    // 掩码不同时两边的 loci 长度可能不同；取公共前缀而不是索引越界。
    SpeciesStatistics a{};
    SpeciesStatistics b{};
    a.loci.resize(3);
    b.loci.resize(2);
    for (std::size_t i = 0; i < a.loci.size(); ++i)
    {
        a.loci[i].frequency[0] = 0.0f;
    }
    for (std::size_t i = 0; i < b.loci.size(); ++i)
    {
        b.loci[i].frequency[0] = 1.0f;
    }
    CHECK_EQ(gamev2::genetics::divergence(a, b), 1.0);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`divergence` 未声明。

- [ ] **Step 3: 声明并实现**

`include/gamev2/genetics/statistics.hpp`，在 `summarize` 声明之后加：

```cpp
/// 两组之间的 Nei 遗传距离（等位基因频率版本）：
/// `Σ_locus |f_a − f_b| / 2`。
///
/// \pre 无需成立：任意两组都能比——包括空的，恒得 0.0。
/// \post 返回值 ∈ [0, min(a.loci.size(), b.loci.size()) / 2]。
///   **注意这是 [0, 150] 而不是 [0, 0.5]**（300 位点全激活时）。把它当
///   归一化量读是设计文档 §2.3 里 θ 定错两个数量级的直接原因。
///
/// 只取公共前缀：两边掩码不同则 loci 长度可能不同。
[[nodiscard]] double divergence(const SpeciesStatistics& a,
                                const SpeciesStatistics& b) noexcept;
```

`src/genetics/statistics.cpp` 末尾加：

```cpp
double divergence(const SpeciesStatistics& a,
                  const SpeciesStatistics& b) noexcept
{
    const auto n = std::min(a.loci.size(), b.loci.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        sum += std::fabs(static_cast<double>(a.loci[i].frequency[0])
                         - static_cast<double>(b.loci[i].frequency[0]));
    }
    return sum / 2.0;
}
```

需要 `<algorithm>`（`std::min`）与 `<cmath>`（`std::fabs`）；`<cstddef>`
（`std::size_t`）确认已在。

- [ ] **Step 4: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五套件全过，`genetics_tests` 多出 2 条。

- [ ] **Step 5: 提交（由用户决定）**

```bash
git add include/gamev2/genetics/statistics.hpp src/genetics/statistics.cpp tests/test_genetics_statistics.cpp
git commit -m "feat(genetics): add the pairwise divergence measure"
```

---

### Task 2: 播种改造——让世界开幕时真的是一个物种

**Files:**
- Modify: `src/sim/main.cpp`
- Modify: `include/gamev2/environment/config.hpp`（加 `seed_founder_pool`）
- Test: `tests/test_environment_smoke.cpp`（加一条地形无关的播种断言）
  —— 见 Step 6 的说明

**Interfaces:**
- Consumes: Task 1 的 `divergence`（本任务用不上，此处仅说明它不消费）
- Produces:
  - `EnvironmentConfig::seed_founder_pool`（`std::uint32_t`，默认 `32`）
  - `EnvironmentConfig::seed_mutation_rate`（`float`，默认 `0.001f`）
  - `sim` 的播种改为全球单一创始池

**这一步会作废 `sim` 的全部现有基线读数**（第三次重取）。设计文档 §11 已记。

**为什么必须改**：原播种是「每个个体一份独立的随机基因型」（300 位点各自独立
掷 50/50 硬币），于是同一个格里的邻居毫无亲缘关系，主大陆从第 0 季起就是几千
个互不相关谱系的混合物。实测第 0 季的 divergence 就是 **1.75**，而真分化与初始
抽样噪声根本分不开 —— θ 定低于 1.75 则第 0 季全体触发，定高于 1.75 则第 5 季
主大陆就到 3.86。详见设计文档 §2.3。

- [ ] **Step 1: 写失败测试**

播种逻辑住在 `src/sim/main.cpp` 的 `seed()` 里（匿名 namespace，`sim` 刻意不
进任何测试目标 —— 见 `CMakeLists.txt` 的排除注释）。因此**播种算法本身无法被
单元测试覆盖**。本任务的可测部分只有一件事：两个配置项存在且默认值正确。

加到 `tests/test_environment_smoke.cpp`：

```cpp
TEST(the_seed_founder_pool_defaults_to_a_single_world_wide_lineage)
{
    // 播种住在 sim 的匿名 namespace 里（不进测试目标），因此这里只能钉住
    // 配置项本身。真正的守卫是 Task 6 的 θ 实测与 `water_actually_splits_the
    // _population` —— 它们会在播种被改回「每体独立随机」时变红。
    const EnvironmentConfig config{};
    CHECK_EQ(config.seed_founder_pool, std::uint32_t{32});
    CHECK(config.seed_mutation_rate > 0.0f);
    CHECK(config.seed_mutation_rate < 0.01f);
}
```

该文件的 using 列表需要 `gamev2::environment::EnvironmentConfig` 与
`<cstdint>`。

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`seed_founder_pool` 未声明。

- [ ] **Step 3: 加两个配置项**

`include/gamev2/environment/config.hpp`，在 `gradient_weight` 之后加：

```cpp
    /// 初始种群共同的创始基因型个数。整个世界从这一小撮起步，而不是每个个体
    /// 各拿一份独立随机基因型 —— 后者让第 0 季的 divergence 就达 1.75
    /// （值域 [0, 150]），真分化与初始抽样噪声分不开，任何 θ 都修不了。
    ///
    /// 取 32 使全图 ≈ 2400 个个体承受一次强瓶颈，之后分化只因屏障两侧无法
    /// 基因交流而累积。实测见 doc/2026-10-17-game-design.md §2.3。
    ///
    /// \pre >= 1：0 会让 founders 容器为空，播种时索引越界。
    std::uint32_t seed_founder_pool = 32;
    /// 每个个体在自己那份创始基因型上，按此概率把某个位点的两个等位基因
    /// 重新掷一遍。32 × 0.001 × 300 ≈ 每个个体约 0.3 个位点被重掷——足够
    /// 让同源个体之间有一点点私有差异，不足以让它们变成互不相关的谱系。
    float seed_mutation_rate = 0.001f;
```

- [ ] **Step 4: 改 `sim` 的播种**

`src/sim/main.cpp` 的 `seed()`，把「逐格调 `random_genome`」改成「先建一个
全球创始池，再逐格从池里取」。**只改这个函数体，签名与调用点不动。**

```cpp
    // 全球单一创始池：全图 ≈ 2400 个个体 descended from 这一小撮。原来的
    // 「每体一份独立随机基因型」让同格的邻居毫无亲缘关系，主大陆从第 0 季
    // 起就是几千个互不相关谱系的混合物——实测第 0 季 divergence 就 1.75，
    // 真分化与初始抽样噪声根本分不开。实测见设计文档 §2.3。
    std::vector<Genome> founders;
    founders.reserve(env.config().seed_founder_pool);
    for (std::uint32_t f = 0; f < env.config().seed_founder_pool; ++f)
    {
        founders.push_back(random_genome(rng));
    }
    std::uniform_int_distribution<std::size_t> pick_founder{
        0, founders.size() - 1};
    std::uniform_real_distribution<float> mutate{
        0.0f, env.config().seed_mutation_rate};

    // …原有的 for (c < kCellCount) 循环里，水格 continue 与 fill/n 的
    // 计算一字不动，只把原来那行 `const auto genome = random_genome(rng);`
    // 换成：
            auto genome = founders[pick_founder(rng)];
            for (std::size_t l = 0; l < kLocusCount; ++l)
            {
                if (mutate(rng) < env.config().seed_mutation_rate)
                {
                    genome.a[l].allele = random_allele(rng);
                    genome.b[l].allele = random_allele(rng);
                }
            }
```

`random_allele` 是 `random_genome` 里已经在用的那个 `uniform_int_distribution<int>{0,1}`
的取值。**若它当时是个局部分布，把那行提升成 `seed()` 里的具名变量**并让
`random_genome` 复用它 —— 不要复制第二份同义代码。

需要 `#include <vector>`（确认已在）与 `<cstdint>`。

- [ ] **Step 5: 跑测试并重取基线**

```bash
pwsh -File scripts/build.ps1 -Test
& build/sim.exe evolve 200
& build/sim.exe persist 200
& build/sim.exe fragile 200
```

**把三个读数记下来，本任务结束前不回填文档**（Task 6 统一回填）。

**同时必须验证一件事**：θ = 10 是在这个新基线上量出来的，若重取后种群规模
变化很大，θ 需要重测。验证办法见 Task 6 Step 2 —— 那里有一个独立的探针步骤。

- [ ] **Step 6: 确认没有别处依赖旧播种**

```bash
grep -rn "random_genome" src/ include/
```

Expected: 只在 `src/sim/main.cpp` 的匿名 namespace 内。`game/session.cpp`
**刻意不播种**（设计文档 §4.4），所以游戏层的初始种群由调用方给，阶段 1 的
测试 `tests/test_game_session.cpp` 里的 `seed_species` 不受本任务影响。

- [ ] **Step 7: 提交（由用户决定）**

```bash
git add src/sim/main.cpp include/gamev2/environment/config.hpp tests/test_environment_smoke.cpp
git commit -m "feat(sim): seed the world from a single founder pool"
```

---
### Task 3: SpeciationSystem——连通分量与前三条条件

**Files:**
- Create: `include/gamev2/ecs/adapt/speciation.hpp`
- Create: `src/ecs/adapt/speciation.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_speciation.cpp`（新建）

**Interfaces:**
- Consumes: Task 1 的 `genetics::divergence`；既有的 `SpeciesStatistics`、
  `genetics::summarize`、`SpeciesRegistry::register_species`、`ecs::World::get_if`
- Produces:
  - `struct BornSpecies { SpeciesId id; SpeciesId parent; std::size_t founders;
    CellIndex anchor; }`
  - `class SpeciationSystem final : public System`，公开成员
    `SpeciationSystem(SpeciesRegistry&, const EnvironmentConfig&)` /
    `void execute(World&, CommandBuffer&) override` /
    `const std::vector<BornSpecies>& born() const noexcept` /
    `const std::vector<SpeciesId>& lost() const noexcept`
  - 访问集 `ReadSet = tuple<const Genome, const SpeciesTag, const Position>`、
    `WriteSet = tuple<SpeciesTag>`

**Task 3 只做到前三条条件，不含迟滞** —— 迟滞是 Task 4。Task 3 里条件 (d) 直接
视为已满足（`speciation_hysteresis` 取 1）。这样两个任务各自有独立可测的交付物。

**它不进调度器**，由 `World::season_report` 直接 execute（Task 5 接线）。

**`SpeciesRegistry&` 必须是非常量引用** —— 这是阶段 1/2 以来第一次。那些 system
拿的都是 `const SpeciesRegistry&`，因为物种此前是构造期登记的静态标签。

**不需要 `mutates_structure`**：它经 `get_if<SpeciesTag>` 拿可变指针原地改值，
不触发 archetype 迁移 —— 与 `MovementSystem` 写 `Feeding` 同一个套路。

**邻域是 4 而不是 8**：地形不可通行判定与 `MovementSystem` 的推进都是 4 邻域的，
「个体所在陆块」这个不变量只在 4 邻域下成立。8 邻域的分量能跨一道对角水缝
连起来，那道缝就不再是屏障。设计文档 §5 的同一处已改。

- [ ] **Step 1: 写失败测试**

新建 `tests/test_speciation.cpp`：

```cpp
#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/speciation.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/species.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::World;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::Genome;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::SpeciationSystem;
using gamev2::environment::EnvironmentConfig;
using gamev2::genetics::AlleleCall;
using gamev2::genetics::AlleleId;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::kLocusCount;

/// `all_loci()` 与 sim 同一份 —— 全激活时 divergence 的值域是 [0, 150]。
SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 全部位点同一个等位基因的一份基因组。裂变条件 (c) 读的就是两组的频率差，
/// 因此「一整块地纯 a、另一块纯 b」是构造出必然越过 θ 的最省事办法：
/// 每位点 |Δf| = 1，divergence = 300 / 2 = 150。
Genome pure(const AlleleId allele)
{
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i] = AlleleCall{allele, 0};
        g.b[i] = AlleleCall{allele, 0};
    }
    return g;
}

/// 在 x ∈ [2, 10)、y ∈ [2, 10) 铺 n 个体，另一块在 x ∈ [52, 60)。
/// 坐标刻意远离图边，滑行不会把它们推出去。
void place_patch(World& world, const SpeciesId sid, const Genome& genome,
                 const float ox, const float oy, const int n,
                 std::vector<EntityId>& out)
{
    for (int i = 0; i < n; ++i)
    {
        const auto x = ox + static_cast<float>(i % 8);
        const auto y = oy + static_cast<float>(i / 8);
        const EntityId e = world.create();
        (void)world.add<Position>(e, Position{x, y});
        (void)world.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)world.add<Genome>(e, Genome{genome});
        out.push_back(e);
    }
}

/// ecs::World 只有 add / get_if，没有 set<T> —— 改一个已有组件的值要经
/// get_if 写回。Genome 是聚合体，整体赋值即可。
void world_set_genome(World& world, const EntityId e, const Genome& genome)
{
    *world.get_if<Genome>(e) = Genome{genome};
}

[[nodiscard]] SpeciesId species_of(const World& world, const EntityId e)
{
    return world.get_if<SpeciesTag>(e)->species;
}

/// (a)(b)(c) 的公共配置。`speciation_hysteresis` 由各测试自己定。
[[nodiscard]] EnvironmentConfig speciation_config(const float water)
{
    EnvironmentConfig c{};
    c.min_founder = 40;
    c.divergence_threshold = 10.0f;
    c.speciation_hysteresis = 1;
    c.water_fraction = water;
    return c;
}

}  // namespace

TEST(components_below_min_founder_never_split)
{
    // 设计文档 §9 测试 5 的最小形式。两块地各 3 个体，远低于 min_founder
    // 40，即便 divergence 拉满（各位点 |Δf| = 1 ⇒ 150）也不该裂。
    const auto config = speciation_config(0.70f);
    EnvironmentWorld env{20261016, config};
    World entities;
    const auto sid = *env.species().register_species(all_loci());

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    place_patch(entities, sid, pure(0), 2.0f, 2.0f, 3, left);
    place_patch(entities, sid, pure(1), 52.0f, 2.0f, 3, right);

    SpeciationSystem speciation{env.species(), config};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(speciation.born().empty());
    CHECK_EQ(env.species().species_count(), std::size_t{1});
    for (const auto e : left)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
    for (const auto e : right)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
}

TEST(a_large_isolated_group_with_enough_divergence_splits)
{
    // 条件 (a) 与 (c) 同时满足时的正例。两块地各 60 个体（> 40），
    // 基因组一纯 a 一纯 b ⇒ divergence 150，远超 θ = 10。
    const auto config = speciation_config(0.70f);
    EnvironmentWorld env{20261016, config};
    World entities;
    const auto sid = *env.species().register_species(all_loci());

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    place_patch(entities, sid, pure(0), 2.0f, 2.0f, 60, left);
    place_patch(entities, sid, pure(1), 52.0f, 2.0f, 60, right);

    SpeciationSystem speciation{env.species(), config};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK_EQ(speciation.born().size(), std::size_t{2});
    for (const auto& b : speciation.born())
    {
        CHECK_EQ(b.parent, sid);
        CHECK(b.id != sid);
        CHECK(b.founders >= 40);
    }

    // 两块地必须落在**不同的**新物种上 —— 否则「两块地」这件事没被表达出来。
    const auto left_id = species_of(entities, left.front());
    const auto right_id = species_of(entities, right.front());
    CHECK(left_id != right_id);
    CHECK(left_id != sid);
    CHECK(right_id != sid);
    for (const auto e : left)
    {
        CHECK_EQ(species_of(entities, e), left_id);
    }
    for (const auto e : right)
    {
        CHECK_EQ(species_of(entities, e), right_id);
    }
}

TEST(no_speciation_without_a_second_landmass)
{
    // 设计文档 §9 的测试 6（**已改名**）。`water_fraction = 0` ⇒ 全图一个陆块
    // ⇒ 每个物种只有一个连通分量 ⇒ 没有可裂的 g。
    //
    // **这条不测隔离判据** —— 条件 (b) 当前冗余（设计文档 §5.1）：单一陆块下
    // 本就没有第二个分量，与 (b) 写成恒真还是真判无关。它守的是连通分量条件。
    const auto config = speciation_config(0.0f);
    EnvironmentWorld env{20261016, config};
    World entities;
    const auto sid = *env.species().register_species(all_loci());

    std::vector<EntityId> ids;
    for (int i = 0; i < 120; ++i)
    {
        const auto x = static_cast<float>(i % 12) * 5.0f + 0.5f;
        const auto y = static_cast<float>(i / 12) * 5.0f + 0.5f;
        const EntityId e = entities.create();
        (void)entities.add<Position>(e, Position{x, y});
        (void)entities.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)entities.add<Genome>(e, Genome{pure(static_cast<AlleleId>(i % 2))});
        ids.push_back(e);
    }

    SpeciationSystem speciation{env.species(), config};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(speciation.born().empty());
    CHECK_EQ(env.species().species_count(), std::size_t{1});
    for (const auto e : ids)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
}

TEST(a_new_species_inherits_the_mask_of_its_parent)
{
    // 决策 6：新物种的掩码从母物种继承。给玩家的一份手挑掩码是玩家分株，
    // 属非目标。
    const auto config = speciation_config(0.70f);
    EnvironmentWorld env{20261016, config};
    World entities;
    const auto sid = *env.species().register_species(all_loci());

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    place_patch(entities, sid, pure(0), 2.0f, 2.0f, 60, left);
    place_patch(entities, sid, pure(1), 52.0f, 2.0f, 60, right);

    SpeciationSystem speciation{env.species(), config};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    const auto parent_mask = env.species().mask(sid);
    for (const auto& b : speciation.born())
    {
        CHECK(env.species().mask(b.id).active == parent_mask.active);
    }
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`speciation.hpp` 不存在。

- [ ] **Step 3: 写头文件**

新建 `include/gamev2/ecs/adapt/speciation.hpp`：

```cpp
/// @file speciation.hpp
/// @brief 物种形成：按 4 邻域连通分量切分每个物种，逐块判条件后裂变。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.3 与 §5。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {

/// 本季裂出来的物种。`anchor` 是那个分量的中位数格 —— 抄经人要知道新物种
/// 出现在地图的哪一块，而不只是「物种 4 诞生了」。
struct BornSpecies
{
    gamev2::genetics::SpeciesId id;
    gamev2::genetics::SpeciesId parent;
    std::size_t founders;
    gamev2::environment::CellIndex anchor;
};

/// **不进调度器**：它读刚 flush 出来的实体表，而调度器批次的时序管不到那里。
/// `World::season_report` 在换代 flush 之后、统计之前直接 execute 它，否则
/// 季末报告会把刚裂出来的物种连同它的全部个体一起漏掉。
///
/// 写集只有 `SpeciesTag`，且经 `get_if` 拿**可变指针原地改值**，因此不触发
/// archetype 迁移，**不需要 `mutates_structure`**。
class SpeciationSystem final : public System
{
public:
    /// \pre `species` 必须与 `world` 活的一样久，且必须是**非常量**引用：
    ///   登记新物种要调 `register_species`。阶段 1/2 之前所有 system 拿的都是
    ///   const 引用，因为物种彼时是构造期登记的静态标签。
    SpeciationSystem(gamev2::genetics::SpeciesRegistry& species,
                     const gamev2::environment::EnvironmentConfig& config);

    using ReadSet = std::tuple<const Genome, const SpeciesTag, const Position>;
    using WriteSet = std::tuple<SpeciesTag>;

    void execute(World& world, CommandBuffer& cmd) override;

    /// 本季裂出来的物种。生命周期归调用方管 —— `World` 把它抄进 Scroll。
    [[nodiscard]] const std::vector<BornSpecies>& born() const noexcept
    {
        return born_;
    }

    /// 本季一个个体都不剩的物种。
    [[nodiscard]] const std::vector<gamev2::genetics::SpeciesId>& lost()
        const noexcept
    {
        return lost_;
    }

private:
    /// 一个「已满足前三条、正在攒连续季数」的分量。**纯派生态**：不进任何
    /// ECS 组件。它是纯派生数据，因此不进任何存档契约。
    struct Pending
    {
        gamev2::genetics::SpeciesId species;
        gamev2::environment::CellIndex anchor;
        std::uint32_t seasons = 1;
    };

    gamev2::genetics::SpeciesRegistry& species_;
    gamev2::environment::EnvironmentConfig config_;
    std::vector<BornSpecies> born_;
    std::vector<gamev2::genetics::SpeciesId> lost_;
    std::vector<Pending> pending_;
};

}  // namespace gamev2::ecs::adapt
```

- [ ] **Step 4: 实现**

新建 `src/ecs/adapt/speciation.cpp`：

```cpp
#include "gamev2/ecs/adapt/speciation.hpp"

#include "gamev2/environment/terrain.hpp"
#include "gamev2/genetics/statistics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::ecs::adapt {

namespace {

using gamev2::environment::CellIndex;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::environment::TerrainField;
using gamev2::genetics::Genome;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::SpeciesRegistry;

/// 一个连通分量。锚取**中位数**而不是质心 —— 质心会被一个远离群体的个-body
/// 拖走，中位数对此免疫。迟滞靠它跨季识别「同一个分量」（§4.3）。
struct Component
{
    std::vector<EntityId> members;
    std::vector<Genome> genomes;
    CellIndex anchor;
};

/// 分量内全部个体所在格的中位数：x 与 y 分别取中位数再合成一格。
[[nodiscard]] CellIndex median_anchor(const std::vector<EntityId>& members,
                                       const World& world)
{
    std::vector<std::uint32_t> xs;
    std::vector<std::uint32_t> ys;
    xs.reserve(members.size());
    ys.reserve(members.size());
    for (const auto e : members)
    {
        const auto* pos = world.get_if<Position>(e);
        if (pos == nullptr)
        {
            continue;
        }
        const auto c = CellIndex::of(pos->x, pos->y).value;
        xs.push_back(c % kGridSide);
        ys.push_back(c / kGridSide);
    }
    if (xs.empty())
    {
        return CellIndex{0};
    }
    const auto mid = static_cast<std::ptrdiff_t>(xs.size() / 2);
    std::nth_element(xs.begin(), xs.begin() + mid, xs.end());
    std::nth_element(ys.begin(), ys.begin() + mid, ys.end());
    return CellIndex{static_cast<std::uint16_t>(ys[static_cast<std::size_t>(mid)]
                                          * kGridSide
                                      + xs[static_cast<std::size_t>(mid)]});
}

/// 4 邻域连通分量，不是 8：地形不可通行判定与 MovementSystem 的推进都是
/// 4 邻域的，「个体所在陆块」这个不变量只在 4 邻域下成立。8 邻域的分量能
/// 跨一道对角水缝连起来，那道缝就不再是屏障（设计文档 §5）。
///
/// 按格分桶再泛洪，而不是逐个体「找邻居再跳」——后者在几千个体下是 O(n²)。
[[nodiscard]] std::vector<Component>
components_of(const World& world, const SpeciesRegistry& species,
              const TerrainField& terrain, const SpeciesId sid)
{
    struct Member
    {
        EntityId e;
        Genome genome;
        std::uint32_t cell;
    };
    std::vector<Member> members;
    world.for_each_entity(
        [&](const EntityId e)
        {
            const auto* tag = world.get_if<SpeciesTag>(e);
            const auto* pos = world.get_if<Position>(e);
            const auto* g = world.get_if<Genome>(e);
            // 与 EnvironmentSystem / BreedingSystem 同一把尺子：未登记的物种
            // id 各自被跳过（设计文档 §7.2 第 1 条）。
            if (tag == nullptr || pos == nullptr || g == nullptr
                || tag->species != sid || tag->species.value == 0
                || tag->species.value > species.species_count())
            {
                return;
            }
            members.push_back(Member{e, *g, CellIndex::of(pos->x, pos->y).value});
        });

    std::vector<std::uint32_t> head(kCellCount, ~0u);
    std::vector<std::uint32_t> next;
    next.reserve(members.size());
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        next.push_back(head[members[i].cell]);
        head[members[i].cell] = static_cast<std::uint32_t>(i);
    }

    std::vector<bool> used(kCellCount, false);
    std::vector<std::uint32_t> stack;
    std::vector<Component> out;
    for (const auto& seed : members)
    {
        if (used[seed.cell])
        {
            continue;
        }
        Component comp;
        stack.assign(1, seed.cell);
        while (!stack.empty())
        {
            const auto c = stack.back();
            stack.pop_back();
            if (used[c] || !terrain.passable(CellIndex{
                                static_cast<std::uint16_t>(c)}))
            {
                continue;
            }
            used[c] = true;
            for (auto it = head[c]; it != ~0u; it = next[it])
            {
                comp.members.push_back(members[it].e);
                comp.genomes.push_back(members[it].genome);
            }
            const auto cx = c % kGridSide;
            const auto cy = c / kGridSide;
            if (cx > 0)
            {
                stack.push_back(c - 1);
            }
            if (cx + 1 < kGridSide)
            {
                stack.push_back(c + 1);
            }
            if (cy > 0)
            {
                stack.push_back(c - kGridSide);
            }
            if (cy + 1 < kGridSide)
            {
                stack.push_back(c + kGridSide);
            }
        }
        comp.anchor = median_anchor(comp.members, world);
        out.push_back(std::move(comp));
    }
    return out;
}

}  // namespace

SpeciationSystem::SpeciationSystem(
    gamev2::genetics::SpeciesRegistry& species,
    const gamev2::environment::EnvironmentConfig& config)
    : species_{species}, config_{config}
{
}

void SpeciationSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    born_.clear();
    lost_.clear();

    const auto& terrain = world.environment().terrain();

    for (std::uint32_t s = 1; s <= species_.species_count(); ++s)
    {
        const auto sid = SpeciesId{s};
        const auto comps = components_of(world, species_, terrain, sid);
        if (comps.empty())
        {
            // 本季一个个体都不剩 ⇒ 该物种灭绝。**必须去重**：`species_count()`
            // 只增不减（没有注销接口），所以记过一次之后，本季 `for (s)` 的每一
            // 次迭代都会再次撞到这个 id。不去重的话玩家会看到同一支灭一整局。
            if (std::ranges::find(lost_, sid) == lost_.end())
            {
                lost_.push_back(sid);
            }
            continue;
        }
        if (comps.size() < 2)
        {
            continue;  // 没有第二个分量就无从裂变
        }

        const auto mask = species_.mask(sid);
        std::vector<Genome> all;
        for (const auto& c : comps)
        {
            all.insert(all.end(), c.genomes.begin(), c.genomes.end());
        }

        for (std::size_t ci = 0; ci < comps.size(); ++ci)
        {
            const auto& comp = comps[ci];
            // (a) min_founder
            if (comp.members.size() < config_.min_founder)
            {
                continue;
            }
            // (b) 不可通行的水 —— **当前冗余**：4 邻域分量按定义就是一个陆块，
            // 而陆块之间必有水。保留它是因为阶段 4 的玩家墙不是水，那时它
            // 第一次真正起作用（设计文档 §5.1）。

            // 「其余全部」= 同物种所有基因组减去本分量。逐分量现拼：同物种
            // 的个体数在 200 季实测里约 2400，最深 9 个分量，代价可接受。
            std::vector<Genome> rest;
            rest.reserve(all.size() - comp.genomes.size());
            for (std::size_t cj = 0; cj < comps.size(); ++cj)
            {
                if (cj == ci)
                {
                    continue;
                }
                rest.insert(rest.end(), comps[cj].genomes.begin(),
                            comps[cj].genomes.end());
            }

            // (c) divergence
            const auto here = gamev2::genetics::summarize(comp.genomes, mask);
            const auto other = gamev2::genetics::summarize(rest, mask);
            if (gamev2::genetics::divergence(here, other)
                <= config_.divergence_threshold)
            {
                continue;
            }

            // (d) 迟滞：Task 4 补上。当前直接过。
            const auto reg = species_.register_species(mask);
            if (!reg)
            {
                // EmptyMask / TooManySpecies：物种数到顶，不裂。
                continue;
            }
            for (const auto e : comp.members)
            {
                // 原地改值，不触发 archetype 迁移。
                world.get_if<SpeciesTag>(e)->species = *reg;
            }
            born_.push_back(
                BornSpecies{*reg, sid, comp.members.size(), comp.anchor});
        }
    }
}

}  // namespace gamev2::ecs::adapt
```

**`world.environment()` 只有 const 重载**，`terrain()` 走 const 通道，所以
`const auto& terrain = world.environment().terrain();` 合法。

- [ ] **Step 5: 接 CMake**

`CMakeLists.txt` 的 `GENETICS_SOURCES` 加 `src/ecs/adapt/speciation.cpp`；
`GENETICS_TEST_SOURCES` 加 `tests/test_speciation.cpp`。

**ASan 的 foreach 迭代的正是这两个变量本身，因此不必另加行。** 改完请确认：

```bash
pwsh -File scripts/build.ps1 -Test -Asan
```

Expected: 五套件全过且 `build/genetics_tests.exe` 存在。

- [ ] **Step 6: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五套件全过，`genetics_tests` 多出 4 条。

**特别地**：`test_world_module.cpp` 与 `test_genetics_systems.cpp` 的既有测试
必须一字不改仍过 —— `SpeciationSystem` 此刻**还没有被 `World` 调用**，因此对它
们应当完全没有影响。若它们变红，说明你误把它接进了调度器或某条既有路径。

- [ ] **Step 7: 提交（由用户决定）**

```bash
git add include/gamev2/ecs/adapt/speciation.hpp src/ecs/adapt/speciation.cpp tests/test_speciation.cpp CMakeLists.txt
git commit -m "feat(adapt): split species into isolated landmasses"
```

---

### Task 4: 迟滞——中位数格锚与跨季最近邻匹配

**Files:**
- Modify: `include/gamev2/environment/config.hpp`
- Modify: `src/ecs/adapt/speciation.cpp`
- Test: `tests/test_speciation.cpp`

**Interfaces:**
- Consumes: Task 3 的 `Component::anchor` 与 `SpeciationSystem::Pending`
- Produces: `EnvironmentConfig::hysteresis_radius_cells`（`float`，默认 `8.0f`）

**这是整个阶段最难的一块。** 条件 (d) 要求「满足前三条的**连续** N 季」，但连通
分量**没有稳定身份**：个体每季移动，分量的成员集合一直在变；分量编号按
`EntityId` 升序生成，死一个个体就整体重排。「上一季的那个分量」这一指称在当前
实现下**不存在**。

算法，三步且顺序不能换：

1. 本季**满足 (a)(b)(c) 的分量**，按锚格距离与 `pending_` 做最近邻匹配，命中
   （距离 ≤ `hysteresis_radius_cells`）则继承季数 +1，未命中则记 1。
2. 季数达到 `speciation_hysteresis` 的**裂变**，且**不进** `pending_`（同一处不该
   反复裂）；未达到的进下一季的 `pending_`。
3. 重建 `pending_`：本季没被匹配上的旧条目**丢弃**（这正是「打断即归零」）。

`pending_` 与 `claimed` 都必须**提到 `for (s)` 循环之外**：否则处理完物种 1 之后
`pending_` 被清空，物种 2 的计数会被误归零。

- [ ] **Step 1: 写失败测试**

加到 `tests/test_speciation.cpp`：

```cpp
TEST(speciation_waits_for_the_configured_number_of_consecutive_seasons)
{
    // 条件 (d)。同一个夹具跑两遍：hysteresis = 1 第 1 季就裂；= 4 则前 3 季
    // 安静、第 4 季一次裂出两个。
    const auto run = [](const std::uint32_t hysteresis)
    {
        auto config = speciation_config(0.70f);
        config.speciation_hysteresis = hysteresis;
        EnvironmentWorld env{20261016, config};
        World entities;
        const auto sid = *env.species().register_species(all_loci());
        std::vector<EntityId> left;
        std::vector<EntityId> right;
        place_patch(entities, sid, pure(0), 2.0f, 2.0f, 60, left);
        place_patch(entities, sid, pure(1), 52.0f, 2.0f, 60, right);

        SpeciationSystem speciation{env.species(), config};
        CommandBuffer cmd;
        std::vector<std::size_t> born_per_season;
        for (std::uint32_t s = 0; s < 6; ++s)
        {
            speciation.execute(entities, cmd);
            cmd.flush(entities);
            born_per_season.push_back(speciation.born().size());
        }
        return born_per_season;
    };

    const auto immediate = run(1);
    CHECK_EQ(immediate.front(), std::size_t{2});
    // 裂过之后各成一个物种，只剩 1 个分量，不再有新裂变。
    for (std::size_t i = 1; i < immediate.size(); ++i)
    {
        CHECK_EQ(immediate[i], std::size_t{0});
    }

    const auto delayed = run(4);
    for (std::size_t i = 0; i < 3; ++i)
    {
        CHECK_EQ(delayed[i], std::size_t{0});
    }
    CHECK_EQ(delayed[3], std::size_t{2});
    for (std::size_t i = 4; i < delayed.size(); ++i)
    {
        CHECK_EQ(delayed[i], std::size_t{0});
    }
}

TEST(a_season_that_breaks_the_streak_resets_the_counter)
{
    // 迟滞挡的是「群体规模在阈值上下抖时抖出一棵全是死枝的进化树」。
    // 测法：先让 (c) 连续成立三季（counter 到 3），第 4 季把 divergence 拉回
    // 阈值以下，第 5 季再拉回来 —— counter 必须从 1 重新数，凑满 4 才裂。
    // 若实现漏了「未命中即丢弃」，第 5 季就会直接裂。
    auto config = speciation_config(0.70f);
    config.speciation_hysteresis = 4;
    EnvironmentWorld env{20261016, config};
    World entities;
    const auto sid = *env.species().register_species(all_loci());

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    place_patch(entities, sid, pure(0), 2.0f, 2.0f, 60, left);
    place_patch(entities, sid, pure(1), 52.0f, 2.0f, 60, right);

    SpeciationSystem speciation{env.species(), config};
    CommandBuffer cmd;
    for (int s = 0; s < 3; ++s)
    {
        speciation.execute(entities, cmd);
        cmd.flush(entities);
        CHECK(speciation.born().empty());
    }

    // 第 4 季：右半边换成与左半边相同的基因组 ⇒ divergence 归零。
    for (const auto e : right)
    {
        world_set_genome(entities, e, pure(0));
    }
    speciation.execute(entities, cmd);
    cmd.flush(entities);
    CHECK(speciation.born().empty());

    // 第 5 季：换回来。第 4 季打断了连续，此时 counter 只有 1，凑不满 4。
    for (const auto e : right)
    {
        world_set_genome(entities, e, pure(1));
    }
    speciation.execute(entities, cmd);
    cmd.flush(entities);
    CHECK(speciation.born().empty());
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 两条新测试红 —— 它们依赖 Task 3 里那行「(d) 直接过」的占位。

- [ ] **Step 3: 加配置项**

`include/gamev2/environment/config.hpp`，在 `seed_mutation_rate` 之后加：

```cpp
    /// 迟滞锚点的跨季匹配半径，单位为格。上一季的锚与本季某分量的锚距离
    /// 不超过它，则该分量继承上一季的连续季数。
    ///
    /// 取 8 的依据：单季位移 1~2 格（max(1, lround(clamp(Speed,0,3))))，
    /// 200 季的漂移 σ ≈ 15 格。8 格既不会被一步移动甩掉，也不会把相邻的
    /// 两个小陆块错认成同一个。
    ///
    /// \pre >= 0：0 表示只有锚完全重合才算同一分量。
    float hysteresis_radius_cells = 8.0f;
```

- [ ] **Step 4: 实现匹配**

`src/ecs/adapt/speciation.cpp`，加文件内辅助（放在 `namespace {` 内）：

```cpp
/// 两格的切比雪夫距离 —— 网格上「移动要几步」的天然度量。
[[nodiscard]] double anchor_distance(const CellIndex a,
                                     const CellIndex b) noexcept
{
    const auto ax = a.value % kGridSide;
    const auto ay = a.value / kGridSide;
    const auto bx = b.value % kGridSide;
    const auto by = b.value / kGridSide;
    const auto dx = static_cast<double>(ax > bx ? ax - bx : bx - ax);
    const auto dy = static_cast<double>(ay > by ? ay - by : by - ay);
    return std::max(dx, dy);
}
```

`execute` 里把 `born_.clear(); lost_.clear();` 之后加上 pending 的重建骨架，
并把条件 (d) 替换成攒季数：

```cpp
void SpeciationSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    born_.clear();
    lost_.clear();

    // 提到物种循环**之外**：否则处理完物种 1 之后 pending_ 被清空，物种 2 的
    // 连续季数会被误归零。`claimed` 同理，它是对 pending_ 全局的占用标记。
    std::vector<Pending> next_pending;
    next_pending.reserve(pending_.size());
    std::vector<bool> claimed(pending_.size(), false);

    const auto& terrain = world.environment().terrain();

    for (std::uint32_t s = 1; s <= species_.species_count(); ++s)
    {
        // …（comps / mask / all 的部分与 Task 3 相同）

        for (std::size_t ci = 0; ci < comps.size(); ++ci)
        {
            // …（(a) 与 (c) 的判定不变，命中前 continue）

            // (d) 最近邻匹配：取距离最小且在半径内的同物种旧条目。
            // 平手由遍历序打破（用 `<=` 而非 `<`），因此逐位确定。
            std::size_t best = pending_.size();
            double best_d = static_cast<double>(config_.hysteresis_radius_cells);
            for (std::size_t i = 0; i < pending_.size(); ++i)
            {
                if (claimed[i] || pending_[i].species != sid)
                {
                    continue;
                }
                const auto d =
                    anchor_distance(pending_[i].anchor, comp.anchor);
                if (d <= best_d)
                {
                    best_d = d;
                    best = i;
                }
            }

            std::uint32_t streak = 1;
            if (best < pending_.size())
            {
                claimed[best] = true;
                streak = pending_[best].seasons + 1;
            }

            if (streak < config_.speciation_hysteresis)
            {
                next_pending.push_back(Pending{sid, comp.anchor, streak});
                continue;
            }

            const auto reg = species_.register_species(mask);
            if (!reg)
            {
                continue;
            }
            for (const auto e : comp.members)
            {
                world.get_if<SpeciesTag>(e)->species = *reg;
            }
            born_.push_back(
                BornSpecies{*reg, sid, comp.members.size(), comp.anchor});
            // 裂变后**不进** next_pending：同一处不该反复裂。
        }
    }

    // 本季没被任何分量匹配上的旧条目自然没有出现在 next_pending 里 ——
    // 这就是「打断即归零」。`claimed` 保证一个旧条目不会被两个分量继承。
    pending_ = std::move(next_pending);
}
```

- [ ] **Step 5: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五套件全过，`genetics_tests` 多出 6 条。

**特别确认**：`a_season_that_breaks_the_streak_resets_the_counter` 必须**绿**。
它是本任务唯一能证伪「打断即归零」那条的测试；实现若把旧条目无条件搬进
`next_pending`，它会红。

- [ ] **Step 6: 提交（由用户决定）**

```bash
git add include/gamev2/environment/config.hpp src/ecs/adapt/speciation.cpp tests/test_speciation.cpp
git commit -m "feat(adapt): make speciation wait for consecutive qualifying seasons"
```

---
### Task 5: 接线——World、GameSession 与 Scroll

**Files:**
- Modify: `include/gamev2/world/world.hpp`
- Modify: `src/world/world.cpp`
- Modify: `include/gamev2/game/session.hpp`
- Modify: `src/game/session.cpp`
- Modify: `include/gamev2/environment/config.hpp`（`min_founder` /
  `divergence_threshold` / `speciation_hysteresis` 三个字段）
- Test: `tests/test_world_module.cpp`（加一条）、`tests/test_game_session.cpp`

**Interfaces:**
- Consumes: Task 4 的 `SpeciationSystem`（含 `born()` / `lost()`）
- Produces:
  - `world::World::speciation_report()` —— 本季的
    `std::pair<std::vector<gamev2::ecs::adapt::BornSpecies>,
    std::vector<gamev2::genetics::SpeciesId>>`
  - `game::BornSpecies` / `game::LostSpecies` / `Scroll::born` / `Scroll::lost`

- [ ] **Step 1: 写失败测试**

加到 `tests/test_world_module.cpp`。**直接复用该文件已有的 `Fixture`** ——
它持有 `LocusTable` / `GeneticsConfig` / `EnvironmentConfig` / `SpeciesId` /
`World`，并提供 `spawn(x, y)`。该文件已有 `kCellCount`、`CellIndex` 的 using
（Step 4 之前已确认）。

```cpp
TEST(the_world_splits_a_species_across_two_landmasses)
{
    // World 层的接线守卫：SpeciationSystem 必须排在换代 flush 之后、统计之前。
    // 排在统计之前才看得见刚裂出来的物种 —— 排在之后，季末报告会把新物种
    // 连同它的全部个体一起漏掉（设计文档 §4.3）。
    //
    // 钉死 world 的夹具 roomy_config 是全陆地（water_fraction = 0），因此
    // 这里显式改回 0.70 —— 一个陆块下没有第二个分量，本就无从裂变。
    Fixture f;
    f.world = world_with_terrain();
    ...
}
```

上面这段写不下去 —— **`Fixture` 把 `World` 作为成员直接构造，而 `World` 不可
移动也不可析构后重建**（`world.hpp` 显式 delete 了拷贝与移动）。这是与阶段 2
的 `MovementProbe` 同一类问题，那里的解法是 `std::optional<World>`。

**因此本任务照搬那个解法**，在该文件的匿名 namespace 里加：

```cpp
/// 世界 × 地形的集成夹具。**不碰既有 `Fixture`**：它把 `EnvironmentConfig`
/// 与 `World` 的构造顺序焊死了，而这里需要在 World 建好之前就设
/// `water_fraction = 0.70`。
struct TerrainWorld
{
    LocusTable table{neutral_table()};
    GeneticsConfig genetics{steady_genetics()};
    EnvironmentConfig config{roomy_config()};
    SpeciesId sid{};
    std::optional<gamev2::world::World> world;

    explicit TerrainWorld(const float water_fraction)
        : config{roomy_config()}
    {
        config.water_fraction = water_fraction;
        // emplace 在 config 赋值之后发生，因此 World 看到的是改过的值。
        // World 不可移动，但 optional 的就地构造不需要移动。
        world.emplace(table, genetics, config, 31);
        const auto reg = world->species().register_species(all_loci());
        CHECK(reg.has_value());
        sid = *reg;
    }

    [[nodiscard]] gamev2::world::World& get() { return *world; }
};
```

**先把这段加进去，再写测试本体。** 需要 `#include <optional>`。

```cpp
TEST(the_world_splits_a_species_across_two_landmasses)
{
    TerrainWorld t{0.70f};
    auto& w = t.get();

    // 两块地各 60 个体、基因组一纯 a 一纯 b ⇒ 每位点 |Δf| = 1 ⇒
    // divergence = 300 / 2 = 150，远超 θ = 10。min_founder 走默认 40。
    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto seed_patch = [&](const float ox, const AlleleId allele,
                                std::vector<EntityId>& out)
    {
        for (int i = 0; i < 60; ++i)
        {
            const auto x = ox + static_cast<float>(i % 8);
            const auto y = 2.0f + static_cast<float>(i / 8);
            const EntityId e = w.entities().create();
            (void)w.entities().add<Position>(e, Position{x, y});
            (void)w.entities().add<SpeciesTag>(e, SpeciesTag{t.sid});
            Genome g{};
            for (std::size_t l = 0; l < kLocusCount; ++l)
            {
                g.a[l] = AlleleCall{allele, 0};
                g.b[l] = AlleleCall{allele, 0};
            }
            (void)w.entities().add<Genome>(e, Genome{g});
            out.push_back(e);
        }
    };
    seed_patch(2.0f, AlleleId{0}, left);
    seed_patch(52.0f, AlleleId{1}, right);

    (void)w.run_season();

    const auto [born, lost] = w.speciation_report();
    CHECK(lost.empty());
    CHECK_EQ(born.size(), std::size_t{2});
    for (const auto& b : born)
    {
        CHECK_EQ(b.parent, t.sid);
        CHECK(b.id != t.sid);
    }
    // 两个新物种的 census 之和必须等于全部 120 —— 没漏个体。
    std::size_t total = 0;
    for (const auto& s : w.run_season_report().species)
    {
        total += s.census;
    }
    CHECK_EQ(total, std::size_t{120});
}
```

`run_season()` 只返回 `SeasonReport`（不含 born/lost），因此最后那段需要一次
额外的读数。**更简单的写法**：直接读 `w.speciation_report()` 之前的那份
`SeasonReport` —— 但 `World` 不保留它。所以**给 `World` 加一个
`[[nodiscard]] SeasonReport last_season() const noexcept`，在 `season_report()`
里缓存一份**。这比让测试再跑一季（那会再裂一轮）正确得多。

- [ ] **Step 2: 跑测试确认失败**

```bash
pwsh -File scripts/build.ps1
```

Expected: 编译失败，`speciation_report` / `last_season` 未声明。

- [ ] **Step 3: 三个配置字段**

`include/gamev2/environment/config.hpp`，在 `hysteresis_radius_cells` 之后加：

```cpp
    /// 分裂出去的群体至少要有这么多个体。**Species 被玩家批评得最狠的一条**：
    /// 新物种只有 1 个个体样本，无法繁殖或繁殖困难而灭绝。
    ///
    /// 200 季实测各陆块 1225 / 517 / 161 / 104 / 72 / 71，因此 40 留下
    /// 6~9 块够格，`target_species = 3` 可达（设计文档 §2.3）。
    std::size_t min_founder = 40;
    /// 组对全体的 divergence 门槛。**它的值域是 [0, 位点数/2]（全激活时
    /// [0, 150]），不是 [0, 0.5]** —— 把它当归一化量读就是 θ 定错两个数量级
    /// 的来源。10.0 的实测依据见设计文档 §2.3。
    float divergence_threshold = 10.0f;
    /// 连续满足前几条条件的季数。挡的是「群体规模在阈值上下抖时抖出一棵
    /// 全是死枝的进化树」。
    std::uint32_t speciation_hysteresis = 4;
```

需要 `<cstddef>` 与 `<cstdint>`（`<cstdint>` 已在）。

- [ ] **Step 4: 给 World 加成员与两份读数**

`include/gamev2/world/world.hpp`：

```cpp
#include "gamev2/ecs/adapt/speciation.hpp"
```

私有段加两个成员（**声明顺序要紧**：`speciation_` 必须在 `environment_` 之后，
因为它的构造函数吃 `species_` 与 `env_.config()`）：

```cpp
    gamev2::ecs::adapt::SpeciationSystem speciation_;
    /// 上一季结算出来的裂变事件。拷贝而非引用：`SpeciationSystem` 每季
    /// `clear()` 再重建，而 `World` 的读数接口要活得比下一季久。
    std::vector<gamev2::ecs::adapt::BornSpecies> born_;
    std::vector<gamev2::genetics::SpeciesId> lost_;
    SeasonReport last_;
```

公开读数（放在 `steps()` 旁边）：

```cpp
    /// 上一季的裂变与灭绝事件。**只在 `season_report()` 之后有效**：
    /// 开局两者都是空。
    [[nodiscard]] std::pair<
        std::vector<gamev2::ecs::adapt::BornSpecies>,
        std::vector<gamev2::genetics::SpeciesId>>
    speciation_report() const noexcept
    {
        return {born_, lost_};
    }

    /// 上一季的季末报告。给需要把报告与 `speciation_report()` 对齐的调用方
    /// ——`run_season()` 只返回报告本身，而裂变事件得单独取，两者不同源时
    /// 会静默错一季。
    [[nodiscard]] SeasonReport last_season() const noexcept
    {
        return last_;
    }
```

需要 `<utility>`（`std::pair`）与 `<vector>`（已在）。

构造函数初始化列表里，`speciation_` 放在 `statistics_` **之前**：

```cpp
      speciation_{species_, environment_config},
```

**`species_` 是值成员**（`world.hpp:137` 的 `SpeciesRegistry species_;`），且声明
在 `genetics_config_` 之后、所有 system 之前 —— 所以等到 `statistics_` 那一段它
早已构造完成，直接传 `species_` 即可。另外注意 `environment_config` 是构造函数
的**按值形参**，在初始化列表里已经有效，而 `SpeciationSystem` 按值拷贝它
（不存引用）—— 这与 `BreedingSystem` 必须收 `genetics_config_` **成员**的写法
不同，收形参是对的，不要改成收成员。

- [ ] **Step 5: 在 season_report 里执行**

`src/world/world.cpp` 的 `World::season_report()`，在 `statistics_.execute(...)`
**之前**插入：

```cpp
    // 换代已经 flush 过，实体表是本季的最终状态。裂变必须排在统计之前，
    // 否则季末报告会把刚裂出来的物种连同它的全部个体一起漏掉。
    speciation_.execute(entities_, cmd_);
    cmd_.flush(entities_);
    born_ = speciation_.born();
    lost_ = speciation_.lost();

    statistics_.execute(entities_, cmd_);
```

然后把返回段改成缓存：

```cpp
    last_ = SeasonReport{season_, 0, statistics_.statistics()};
    for (const auto& s : last_.species)
    {
        last_.population += s.census;
    }
    ++season_;
    tick_ = 0;
    return last_;
```

**`speciation_` 不进调度器** —— 与 `statistics_` 同一理由，它读刚 flush 出来的
实体表，而调度器批次的时序管不到那里。

- [ ] **Step 6: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五套件全过，`world_tests` 多出 1 条。

**特别地**：`test_world_module.cpp` 既有 15 条必须**一字不改**全部仍过。它们用
`roomy_config()`（全陆地 ⇒ 单一连通分量 ⇒ 零裂变），因此 `SpeciationSystem` 对
它们**应当完全没有影响**。若某条变红，去查是不是把它误接进了调度器。

- [ ] **Step 7: GameSession 与 Scroll**

`include/gamev2/game/session.hpp`：

```cpp
/// 本局裂出来的物种。谱系是抄经人唯一能读懂的因果链：不带母物种，玩家
/// 看到「物种 4 出现了」只会当成噪声。
struct BornSpecies
{
    gamev2::genetics::SpeciesId id;
    gamev2::genetics::SpeciesId parent;
    std::size_t founders;
    gamev2::environment::CellIndex anchor;
};

/// 本季 census 归零的物种。
struct LostSpecies
{
    gamev2::genetics::SpeciesId id;
    std::uint32_t last_census;
};
```

`Scroll` 在 `seasons_to_victory` 之后加：

```cpp
    /// 本局新出现的物种。玩家靠这个知道自己的干预有没有生效。
    std::vector<BornSpecies> born;
    /// 本局 census 归零的物种。玩家靠这个知道哪一支灭了。
    std::vector<LostSpecies> lost;
```

`GameSession::advance_one_season()` 里，`scroll_.report = world_.season_report();`
之后加：

```cpp
    const auto [born, lost] = world_.speciation_report();
    scroll_.born.clear();
    for (const auto& b : born)
    {
        scroll_.born.push_back(
            BornSpecies{b.id, b.parent, b.founders, b.anchor});
    }
    scroll_.lost.clear();
    for (const auto id : lost)
    {
        // last_census 取本季报告里该物种的 census —— 它已经是 0（否则
        // SpeciationSystem 不会把它记进 lost），所以这里记的是「归零那一季
        // 之前的规模」，需要从上一季的报告里取。
        scroll_.lost.push_back(LostSpecies{id, 0});
    }
```

`last_census` 的取法需要上一季的报告。**`GameSession` 保留一个
`SeasonReport previous_`**，在 `refresh_scroll()` 之前把它更新：

```cpp
void GameSession::advance_one_season()
{
    while (world_.tick_in_season() < world_.steps())
    {
        world_.step_once();
    }
    previous_ = scroll_.report;          // ← 归零物种的上一季规模从这里取
    scroll_.report = world_.season_report();
    // …born / lost 同上，LostSpecies{id, previous_ 的该物种 census}
    refresh_scroll();
}
```

`previous_` 的取法：

```cpp
        std::uint32_t before = 0;
        for (const auto& s : previous_.species)
        {
            if (s.census > before && /* 该物种的 id 匹配 */)
            {
                before = s.census;
            }
        }
```

`SeasonReport::species` 的下标是 `SpeciesId::value - 1`，因此直接：

```cpp
        const auto idx = static_cast<std::size_t>(id.value) - 1;
        const auto before = idx < previous_.species.size()
                                ? previous_.species[idx].census
                                : 0;
        scroll_.lost.push_back(LostSpecies{id, before});
```

需要给 `GameSession` 加私有成员 `gamev2::world::SeasonReport previous_;`。

- [ ] **Step 8: 跑测试确认全绿**

```bash
pwsh -File scripts/build.ps1 -Test
```

Expected: 五套件全过。`test_game_session.cpp` 的既有 5 条必须**一字不改**仍过
—— `GameSession` 用 `make_environment()` 的默认 70% 水，而那些测试只断言季号与
census、从不放个体，所以裂变不触发。

- [ ] **Step 9: 提交（由用户决定）**

```bash
git add include/gamev2/world/world.hpp src/world/world.cpp include/gamev2/game/session.hpp src/game/session.cpp include/gamev2/environment/config.hpp tests/test_world_module.cpp tests/test_game_session.cpp
git commit -m "feat(game): report speciation events to the player"
```

---

### Task 6: 集成测试、sim 读数与基线重取

**Files:**
- Modify: `tests/test_world_module.cpp`（测试 4）
- Modify: `src/sim/main.cpp`（物种读数 + fragile 重调 + 注释回填）
- Modify: `doc/2026-10-17-game-design.md`
- Modify: `doc/2026-10-16-world-design.md`

**Interfaces:**
- Consumes: Task 5 的一切
- Produces: 三个预设的新基线读数；`sim` 的物种/裂变读数

- [ ] **Step 1: 实现设计文档 §9 的测试 4**

加到 `tests/test_world_module.cpp`，复用 Step 7 的 `TerrainWorld`：

```cpp
TEST(water_actually_splits_the_population)
{
    // 设计文档 §9 测试 4。**阶段 3 之前它不存在** —— 阶段 2 交付了地形与屏障，
    // 但没有机制去读它，因此招牌产出当时只是一句推断。
    //
    // 守两件事：
    //   (a) 有屏障时 divergence 随季上升并越过 θ，物种数随之增长
    //   (b) 无屏障时零裂变（与测试 6 同一个性质，但在这里守的是「屏障确实是
    //       分化的原因」，因为对照组只有它变了）
    const auto species_after = [](const float water,
                                  const std::uint32_t seasons)
    {
        TerrainWorld t{water};
        auto& w = t.get();
        // 铺满：个体越多漂变越快，分化越明显。
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
            if (!w.environment().terrain().passable(cell))
            {
                continue;
            }
            const auto bx = static_cast<float>(c % kGridSide) + 0.5f;
            const auto by = static_cast<float>(c / kGridSide) + 0.5f;
            const EntityId e = w.entities().create();
            (void)w.entities().add<Position>(e, Position{bx, by});
            (void)w.entities().add<SpeciesTag>(e, SpeciesTag{t.sid});
            (void)w.entities().add<Genome>(e, Genome{random_genome(t.sid)});
        }
        for (std::uint32_t s = 0; s < seasons; ++s)
        {
            (void)w.run_season();
        }
        return w.environment().species().species_count();
    };

    // (a) 有屏障：30 季内物种数必须增长到 2 以上 —— 主大陆永不裂（θ=10 而
    // 它封顶 6.4），但小陆块会陆续裂开（§2.3 实测第 15~80 季）。
    CHECK(species_after(0.70f, 30) > 1);
    // (b) 无屏障：单一陆块 ⇒ 单一连通分量 ⇒ 零裂变。
    CHECK_EQ(species_after(0.0f, 30), std::size_t{1});
}
```

`random_genome(sid)` 是 `tests/test_world_module.cpp` 里**还没有**的 helper ——
该文件既有的 `spawn()` 用的是全零 `Genome{}`。加一个：

```cpp
/// 随机基因型。全零 Genome 是全体纯合、零遗传方差的世界 —— 那样只有漂变，
/// 没有分化可言，而测试 4 守的正是分化。
Genome random_genome(const SpeciesId sid)
{
    (void)sid;
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i] = AlleleCall{static_cast<AlleleId>(i % 2), 0};
        g.b[i] = AlleleCall{static_cast<AlleleId>((i / 2) % 2), 0};
    }
    return g;
}
```

**不引入随机流** —— `i % 2` 与 `(i/2) % 2` 给出一个确定但逐位不同的模式，够让
各组频率有差异可测，且逐位可复现。

需要确认该文件的 using 里有 `kLocusCount` / `AlleleId` / `Genome`；没有就补。

- [ ] **Step 2: 独立复核 θ=10 仍成立**

Task 2 改了播种，因此 §2.3 那张 divergence 时间表**需要在新基线上重量**。
用阶段 2 留下的那个一次性探针（`agent-temp/probe_divergence.cpp`，已改成全球
单一池）：

```bash
"C:/Program Files/LLVM/bin/clang++.exe" -std=c++23 -O2 -I include -o build/probe_divergence.exe agent-temp/probe_divergence.cpp src/world/world.cpp src/environment/*.cpp src/ecs/adapt/*.cpp src/genetics/*.cpp src/ecs/*.cpp
& build/probe_divergence.exe 0
& build/probe_divergence.exe 20
& build/probe_divergence.exe 200
```

**判定**：0 季的 divergence 应仍在 1~2 之间，200 季主大陆应在 6 附近、小陆块
应越过 10。**若 0 季就已经大于 5，说明 Task 2 的播种改造没生效** —— 回查
`seed()` 是否真的从创始池取。

**若新基线上 θ=10 不再给出「第 15~80 季陆续裂开」**：把实测的第一条曲线写进
设计文档 §2.3，并把 `divergence_threshold` 调到新基线上合适的值（判据：主大陆
全程不裂、小陆块在第 15~80 季陆续越过）。**并在报告里写明改了多少、依据是什么** ——
θ 是实测值不是设计值。

- [ ] **Step 3: sim 加物种与裂变读数**

`src/sim/main.cpp` 的逐季打印循环之后、`if (previous == 0)` 那一段**之前**加：

```cpp
    // 物种数与裂变事件。阶段 3 之后物种数不再恒为 1，这是本阶段最直观的
    // 观测量。
    std::println("\n物种 {}，其中 census ≥ 20 的 {} 个。",
                 world.species().species_count(), /* 统计一遍 report */ 0);
```

统计「census ≥ `min_viable`」的数量：直接数 `report.species` 里 `census > 0`
的条目，并把 `report` 的 `species` 逐项打印：

```cpp
    std::size_t coexisting = 0;
    for (const auto& s : report.species)
    {
        if (s.census > 0)
        {
            ++coexisting;
            std::println("  物种 {}  census {}  H_obs {:.4f}", s.census, s.census,
                         s.heterozygosity);
        }
    }
```

**注意 `SeasonReport::species` 的下标是 `SpeciesId::value - 1`**，所以打印时
用 `s.census` 之外还要带上 `i + 1` 作为 species id：

```cpp
    for (std::size_t i = 0; i < report.species.size(); ++i)
    {
        const auto& s = report.species[i];
        if (s.census == 0)
        {
            continue;
        }
        ++coexisting;
        std::println("  物种 {}  census {}  H_obs {:.4f}  F {:.4f}",
                     i + 1, s.census, s.heterozygosity, s.f_coefficient);
    }
```

- [ ] **Step 4: fragile 重调 inbreeding_load**

`fragile` 的归零点从第 52 季提前到了第 25 季（阶段 2 把可用面积砍到 30%，
亲代池变小 ⇒ Σw² 基线 = 1/N 抬高 ⇒ 0.7 压得更狠）。**0.7 现在越过悬崖，不再
停在临界点上。**

扫四档，各跑 200 季，取 `evolve` 的配置只改 `inbreeding_load`：

```bash
for L in 0.20 0.25 0.30 0.35 0.40 0.50; do
  echo "--- inbreeding_load = $L ---"
  ./build/sim.exe evolve 200   # 临时改 inbreeding_load(Preset::Fragile) 为 $L
done
```

**做法**：把 `src/sim/main.cpp` 的 `inbreeding_load()` 临时改成从一个环境变量读，
或在每次跑之前改字面量。**不要为此加一个永久的旋钮** —— 它只在这次调平衡时
需要。

**判据**：挑一个「跑满 200 季但明显在下滑」的值，让 `fragile` 重新站在悬崖上。
把新值与它对应的读数写进 `inbreeding_load` 的注释（连同 0.25/0.4/0.5/0.7 那组
在新地图上的实测一起），并更新 `Preset` 注释里的「第 25 季归零」。

- [ ] **Step 5: 重取三个预设的基线**

```bash
pwsh -File scripts/build.ps1 -Test
& build/sim.exe evolve 200
& build/sim.exe persist 200
& build/sim.exe fragile 200
```

**不要预设期望方向**：播种改造既可能抬高也可能压低种群（瓶颈强 → 存活率低，
但分化快 → 隔离更多）。**如实记录**。

- [ ] **Step 6: 回填文档**

`src/sim/main.cpp` 的 `Preset` 注释块：把 Step 5 的读数逐字替换进去，包括
覆盖率、按陆块分组、物种数。`inbreeding_load` 的注释：替换成 Step 4 的新档位
与读数，并保留「阶段 2 之后 0.7 越过悬崖」那段历史。

`doc/2026-10-17-game-design.md`：

- §2.3 的 divergence 表：若 Step 2 的实测与原表不同，**替换成新表**，并在表下
  写明「第 2.3 节初版那组数是播种改造前测的，已作废」
- §11：把「`fragile` 的 0.7 已越过悬崖」标成已解决并写新值

`doc/2026-10-16-world-design.md` §8 末尾那条「空掉的格会被重新殖民」的实测段：
把覆盖率时间序列更新成新基线上的读数。

- [ ] **Step 7: 跑一次 ASan 全量**

```bash
pwsh -File scripts/build.ps1 -Test -Asan
```

Expected: 五套件全过，无 sanitizer 报告。`SpeciationSystem` 新增了按格分桶与
连通分量数组，ASan 是这一批改动的第一道防线。

- [ ] **Step 8: 提交（由用户决定）**

```bash
git add tests/test_world_module.cpp src/sim/main.cpp doc/2026-10-17-game-design.md doc/2026-10-16-world-design.md
git commit -m "test(sim): guard geographic speciation and re-baseline the presets"
```

---

## 阶段 3 完成后的验收

```bash
pwsh -File scripts/build.ps1 -Test
pwsh -File scripts/build.ps1 -Test -Asan
& build/sim.exe evolve 200
& build/sim.exe persist 200
& build/sim.exe fragile 200
```

逐条核对：

1. 五个套件全过，ASan 无报告
2. **既有测试一字未改**：world_tests 的 15 条、game_tests 的 5 条、
   genetics_systems 的全部。它们用全陆地配置或不放个体，因此对裂变**应当完全
   没有影响** —— 这是「新增机制没污染既有契约」的唯一证据
3. `speciation_waits_for_the_configured_number_of_consecutive_seasons` 与
   `a_season_that_breaks_the_streak_resets_the_counter` 全绿 —— 迟滞的两个方向
   都有守卫
4. `water_actually_splits_the_population` 全绿 —— §10 招牌产出的直接守卫，
   阶段 2 之前**不存在**这条测试
5. `sim evolve 200` 的物种数**大于 1**，且覆盖率仍呈「上升后趋平」而非下滑
6. `fragile` 重新站在悬崖上（跑满 200 季但明显下滑）
7. `Scroll.born` / `Scroll.lost` 在 `game_tests` 里至少被断言过一次

## 不在本计划内

- 阶段 4：`paint_terrain` / `paint_climate` / `cull_genome_pool`。**条件 (b) 在
  它们落地之前测不出东西**（§5.1），因此本阶段没有给它配 mutant 护栏 —— 那条
  护栏应在阶段 4 补，届时玩家墙是唯一非水屏障
- 同种趋向：仍推迟。它的风险是「种群稀疏时原地打转、空调殖民变慢」，而现在
  空调殖民由 `the_gradient_direction_is_symmetric_...` 与 `sim` 的覆盖率时间
  序列共同看着，加同种趋向后这两条要重新验
- 生态位细分（食性分化、种间竞争）：`target_species` 调高到 5+ 之后可能成为必需
- 存档：五种序列化格式一条都还没有，`SpeciesTag` 的持久化也在其中

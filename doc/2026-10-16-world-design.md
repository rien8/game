# 世界模块设计文档

- 日期：2026-10-16
- 状态：已实现
- 目标平台：Windows / clang++ 23 / C++23
- 前置模块：`ecs`（见 [ECS 模块设计文档](./2026-10-12-ecs-design.md)）、`genetics`（见 [基因遗传系统设计文档](./2026-10-14-genetics-design.md)）、`environment`（见 [环境模块设计文档](./2026-10-16-environment-design.md)）

## 1. 背景与目标

`ecs`、`genetics`、`environment` 三个模块与它们的适配层都已落地。四个
`System` 也已就位：`EnvironmentSystem`、`AgingSystem`、`BreedingSystem`、
`PopulationStatisticsSystem`。

缺的是最上面那层——把它们装配成一个会自己往前跑的世界。今天每一份端到端
验证都得在测试里手写一遍同样的编排：构造 `EnvironmentWorld`、登记物种、
new 四个 system、逐个 `scheduler.add`、调用 `plan()`、每季
`scheduler.run` + `cmd.flush`。这段样板在 `test_environment_systems.cpp` 与
`test_genetics_systems.cpp` 之间重复了两份，而它本身正是最需要被统一钉死
的东西——**一季的时序**。

时序一旦排错，后果一律是不崩不报错：顺序反了只会让本季出生数为零，让
饥荒静默失效，让整季摄食按上一季的站点结算。三个前一份设计文档各自花了
大量篇幅写 `order` 字段的注释来守这条约束，现在这些约束要在一处被真正
执行一次。

### 1.1 设计目标

- 一季的编排只有一处实现，调用方与测试共用
- 季末报告的口径与世界的真实存续状态一致
- 不改动三个已有模块的语义，只在 `ecs/adapt` 增加推进方法与 `MovementSystem`
- 批量推进与逐季推进逐位等价，使世界可被分段推进

### 1.2 非目标

本轮明确不做：

- 初始种群生成（调用方经 `entities()` 自行放入；`src/sim/main.cpp` 是一份
  完整的参考实现）
- 存档与序列化
- 帧循环、tick 速率、渲染（`run_season` 是唯一推进单位，没有「帧」）
- 气候场与资源场的写入入口（只读暴露）
- 群系、迁移、捕食威胁的来源

---

## 2. 设计决策记录

以下 8 项为本设计的显式决策。

| # | 决策 | 理由 |
|---|------|------|
| 1 | 命名 `gamev2::world::World`，成员为 `ecs::World entities_` | 与文档里「世界」的叫法一致；成员加前缀 `entities_` 而不是 `world_`，是为了让本文件内的 `World` 始终无歧义地指本类 |
| 2 | 季节号经 `advance_season()` 推进，不每季重建 system | `Scheduler` 自带一个真线程池，每季重建等于每季重建线程 |
| 3 | `scheduler` 只注册三个 system，统计在 `flush` 之后直接驱动 | 统计的口径必须落在死亡与出生都已生效之后 |
| 4 | 只引用外部 `LocusTable`，其余（ECS 世界、环境场、登记表、config、scheduler、四个 system）自持 | 位点蓝图按设计是全模块共享的静态数据，与「一次运行」的登记表所有权级别不同 |
| 5 | `SeasonReport::species` 是 `statistics()` 的拷贝 | 下一季 `stats_.clear()` 会重写成员里的数据，而报告要活得比下一季久 |
| 6 | `report.population` 取各物种 `census` 之和 | 与统计同一次遍历，不额外扫世界 |
| 7 | `run_seasons(n)` 是朴素循环，不做任何批量化 | 等价性是可测的，批量化是提前优化 |
| 8 | 不生成初始种群 | 调用方本来就必须知道怎么造 `Genome` 与 `Phenotype`；把它藏进世界模块只会多一层无法验证的隐式行为 |

第 3 条值得单说，因为它是本设计与「把四个 system 一起塞进 scheduler」这个
最自然的想法之间唯一的分歧点。`PopulationStatisticsSystem` 的
ReadSet 是 `Genome + SpeciesTag`，与另三个 system 的访问集都相交（都碰
`SpeciesTag`），因此注册进调度器并不省下任何并行——它照样会被排到单独
的一批。而注册进去的代价是：它只能在 `scheduler.run` 内部跑，也就是在
`cmd.flush` 之前。`BreedingSystem` 的死亡销毁（`cmd.destroy`）与子代创建
（`cmd.create`）都在帧末 flush 才生效，于是 flush 前统计出来的 `census`
必然**含本季将死的个体、不含刚出生的子代**。

那个数字本身不算错——它是「结算点」的普查。但它出现在季末报告里，面板
上的种群数就会比上一季大一截、比下一季小一截，看起来像种群在剧烈震荡。
因此本设计选择让统计落在 flush 之后。

代价要说清楚：`statistics_.execute` 是直接调用 `System::execute`
（public virtual），不是经由调度器派发。这是本模块对
`PopulationStatisticsSystem` 唯一的一条非调度路径，写在实现里并附理由。

---

## 3. 总体架构

### 3.1 文件划分

```
include/gamev2/world/world.hpp   SeasonReport / World
src/world/world.cpp
tests/test_world_module.cpp
```

测试文件叫 `test_world_module` 而不是 `test_world`：后者是 ECS 模块既有的
世界测试（31 条），与本模块无关。

`ecs/adapt` 侧只多两个方法：

```cpp
// include/gamev2/ecs/adapt/environment_systems.hpp
void EnvironmentSystem::advance_season(std::uint32_t season_index) noexcept

// include/gamev2/ecs/adapt/genetics_systems.hpp
void BreedingSystem::advance_season(std::uint32_t season_index) noexcept
```

### 3.2 依赖方向

```
              gamev2::world
                    │
      ┌─────────────┼─────────────┐
      ▼             ▼             ▼
   ecs::adapt   adapt::Env     genetics   environment
   （四 system）    World
```

`world/` 不引入任何新依赖：它 include 的头文件全是既有的。它是整棵树的
最顶层，因此**不存在它把依赖方向弄反的可能**——这也是它适合作为编排层的
一个额外理由。

### 3.3 一季的执行序列

> **已被 `doc/2026-10-17-game-design.md` 取代。** 编排粒度下沉到 tick：
> `EnvironmentSystem` 与 `BreedingSystem` 不再进调度器，改由 `World` 显式
> 驱动；调度器只留 `MovementSystem` 与 `AgingSystem` 那一对并行。本节保留
> 以记录 tick 化之前的样子，读当前行为请读新文档 §3.3。

```cpp
SeasonReport World::run_season()
{
    movement_.advance_season(season_);
    environment_.advance_season(season_);
    breeding_.advance_season(season_);

    scheduler_.run(entities_, cmd_);

    statistics_.execute(entities_, cmd_);

    SeasonReport report{season_, 0, statistics_.statistics()};
    for (const auto& s : report.species)
    {
        report.population += s.census;
    }
    ++season_;
    return report;
}
```

四步的顺序没有一条是靠调度器保证的，全部是本模块的显式序列：

1. **先推进季号，再跑调度器。** 三处的 `season_index_` 都在 `execute` 开头
   消费一次（移动的随机流、`climate().advance(season_index_)` 与
   `RandomStreams{root_seed_, season_index_}`），因此推进必须整体发生在
   `scheduler_.run` 之前。中途改会让同一批 system 看到不同的季。
2. **`scheduler_.run` 内部。** 四个 system 的相对先后由各自已声明的
   `order` 与访问集决定，本模块不干预；排出来的批次是
   `{Movement, Aging}` / `{Environment}` / `{Breeding}`，第一批并发派发。
   换代（死亡销毁与子代创建）也在此刻生效：`Scheduler::run` 把各 system 的
   缓冲并进 `cmd` 之后就在末尾 `cmd.flush(world)`
   （`src/ecs/scheduler.cpp:144`）。世界在这一步之后才处于「本季已结算」的
   状态。`PopulationStatisticsSystem` 仍刻意不在其中（决策 3）。
3. **换代 flush 归 `World::season_report` 里的 `cmd_.flush`。** tick 化之后
   `breeding_.execute` 不在 `Scheduler::run` 内部，它入的 `cmd` 无人冲，因此
   那一行由 `World` 自己负责，且必须排在 `breeding_.execute` 之后、
   `statistics_.execute` 之前：`CommandBuffer::flush` 末尾有
   `commands_.clear()`（`src/ecs/command_buffer.cpp:24`），只有这一次 flush，
   「统计必须排在换代之后」这个约束在代码结构上一眼可辨。
4. **统计。** 落在换代之后，因此 `census` 是真实存续数。

`run_seasons(count)` 是 `for (i < count) reports.push_back(run_season())`，
不批量化、不复用中间结果。这条等价性由测试 7 守住，而它是「世界可以被
分段推进 / 存档回放」的前提——一旦不成立，日后想加存档就得先把调用方
全部重写。

---

## 4. 数据模型

### 4.1 SeasonReport

```cpp
struct SeasonReport
{
    std::uint32_t season = 0;
    /// 各物种 census 之和，即本季存活总数。判据与统计 system 相同：
    /// 缺 Genome 或引用未注册物种的实体不在其中。
    std::size_t population = 0;
    /// 下标 = SpeciesId::value - 1，与 statistics() 的约定一致。
    /// 拷贝而非引用：statistics() 的成员缓冲会被下一季 clear() 重写。
    std::vector<gamev2::genetics::SpeciesStatistics> species;
};
```

`species` 是拷贝不是引用，这是决策 5。拷贝的代价与物种数成正比而与个体数
无关——`AlleleStats` 是逐位点的定长统计，不是逐个体的。

### 4.2 World

```cpp
class World
{
public:
    World(const gamev2::genetics::LocusTable& table,
          gamev2::genetics::GeneticsConfig genetics_config,
          gamev2::environment::EnvironmentConfig environment_config,
          std::uint64_t root_seed,
          std::size_t worker_count = 1);

    /// 必须在第一次 run_season 之前把物种全部登记完。
    gamev2::genetics::SpeciesRegistry& species() noexcept;

    /// 初始个体由调用方经这里放进世界。
    gamev2::ecs::World& entities() noexcept;

    /// 只读暴露气候场与资源场。测试要读真实承载力，UI 要画地图——
    /// 两者都不是本模块内部的实现细节。
    const gamev2::ecs::adapt::EnvironmentWorld& environment() const noexcept;

    [[nodiscard]] SeasonReport run_season();
    [[nodiscard]] std::vector<SeasonReport>
    run_seasons(std::uint32_t count);

private:
    gamev2::genetics::GeneticsConfig genetics_config_;
    gamev2::genetics::SpeciesRegistry species_;
    std::uint64_t root_seed_;
    std::uint32_t season_ = 0;

    gamev2::ecs::World entities_;
    gamev2::ecs::CommandBuffer cmd_;
    gamev2::ecs::Scheduler scheduler_;

    // 声明顺序即依赖顺序：breeding_ 的构造参数要取
    // &environment_.targets()，而 env_ 又是 environment_ 的引用对象。
    // 逆序析构恰好把这三条依赖倒过来，销毁时不留悬垂引用。
    //
    // 构造函数的 const 引用形参 `table` 不落成成员：它绑的就是调用方的
    // 对象，而按值形参 `environment_config` 只在构造时被读一次
    // （env_ 与 breeding_ 都直接吃形参）。两者都留一份成员只会诱使人
    // 改成从成员取值，那正是 BreedingSystem 绑到按值形参、构造一返回
    // 即悬垂的形状。
    gamev2::ecs::adapt::EnvironmentWorld env_;
    gamev2::ecs::adapt::EnvironmentSystem environment_;
    gamev2::ecs::adapt::AgingSystem aging_;
    gamev2::ecs::adapt::BreedingSystem breeding_;
    gamev2::ecs::adapt::PopulationStatisticsSystem statistics_;
};
```

`BreedingSystem` 的 `targets_` 指向 `&environment_.targets()`。这是安全的：
它取的是成员 `std::vector` 的**地址**，不是某次 `execute` 产出的数据的
地址，而该成员的地址在 `World` 的整个生命周期内不变。`EnvironmentSystem::execute`
每季 `clear()` 再重建的是这张表的内容，其生命周期契约（环境设计文档 §8）
「必须覆盖下一次 execute」在此处由「二者同为 `World` 成员」自动兑现。

`AgingSystem` 与 `PopulationStatisticsSystem` 不持有季号，因此不参与
`advance_season`。

---

## 5. 契约

以下五条以 `\pre` / 注释的形式写进头文件。

1. **`species()` 的登记必须在第一次 `run_season` 之前全部完成**，之后不再
   登记。`BreedingSystem` 与 `EnvironmentSystem` 都按
   `1 <= value <= species_count()` 判别，未注册的 id 会让该实体被两个
   system 各自跳过并报一条 `GENETICS_UNKNOWN_SPECIES` 诊断——而
   `EnvironmentSystem` 的跳过发生在 `BreedingSystem` 的诊断之前（同一实体
   两条诊断只报一条），排查时容易看漏。
2. **调用方放进 `entities()` 的个体必须带齐六个组件**：`Genome`、
   `CreaturePhenotype`、`Environment`、`SpeciesTag`、`Position`、
   `Vitality`。缺其中任何一个的后果各不相同：

   | 缺失 | 后果 |
   |------|------|
   | `Genome` / `CreaturePhenotype` / `Environment` / `SpeciesTag` | 被 `BreedingSystem` 的收集判据整个跳过——不参与存活抽签，不繁殖，但也不会被销毁，等于永久滞留 |
   | `Position` | 参与存活抽签，但不被摄食、不进任何父池。**永不繁殖，永不被环境杀死**，是个只消耗存量与随机流的幽灵 |
   | `Vitality` | `AgingSystem` 在 `get_if` 取到 nullptr 时整个跳过该实体，因此它**年龄永不递增，也永远不会因寿命到期而死**，只剩遗传层的存活抽签会判它死。`BreedingSystem` 的回写同样取到才写，取不到就跳过 |

   第二行是本模块最容易踩的坑：`BreedingSystem` 明确不把缺失的
   `Position` 压成 `Position{}`（那会让它落进 `(0, 0)` 那一格，凭空成为
   那一格站点的亲代），所以这类实体安静地活着，既不出现在
   `targets()` 里，也永远等不到子代。

   第三行是 `AgingSystem` 与 `BreedingSystem` 的判据不一致所致的：前者要
   `Vitality` 才判寿命，后者不要。两条路径就此分叉，但两者的判据都在各自
   system 内写死，本模块不介入。

3. **`run_season` 不可重入、不可多线程并发调用。** `ecs::World` 本身不是
   线程安全的（并发派发只发生在 `scheduler.run` 内部，由访问集保证同批
   system 之间不冲突），本模块没有也不打算让整季的编排并行。
4. **`worker_count` 只影响并发批次数，不影响结果。** 本套件排不出多 system
   同批的批次，因此这条是**结构性**成立的，由 ECS 侧的
   `adapt_systems_plan_into_three_singleton_batches` 断言守住，而不是由本套件
   的运行结果守住（原因见 §8 末条）。
5. **`season` 从 0 起。** 第一次 `run_season()` 返回的报告里
   `season == 0`，与 `EnvironmentSystem` / `BreedingSystem` 构造时接到的
   季号一致。

---

## 6. 确定性

同 `root_seed` 两次运行必须逐位相同，这是三个前序模块共同遵守的契约，
本模块不能是例外的那一个。

沿链逐条确认：

- 气候场由 `root_seed` 与格下标混合得出，不消耗随机数（环境设计文档 §9）
- 季内循环与 `build_sites` 完全无随机
- 摄食的计算与扣减分两趟，结果不依赖实体遍历顺序
- `RandomStreams{root_seed_, season_index_}` 每季重建，同季号必得同流
  （遗传设计文档的既有契约）
- `World::run_season` 内部不含任何随机调用，也不含任何依赖地址的顺序

已知的两条不由本模块引入、但会被它一并暴露的性质：

- `feeding` 的浮点累加存在顺序依赖，量级 `(n−1)·2⁻²⁴·Σ|terms|`
  （环境设计文档 §9）。测试 8 因此**不逐位比较 `Feeding` 组件**，
  只比较季末报告——报告侧的 `settle` 输出是逐位的。
- `BirthCell` 摊平的已知限制（站点中途 shortfall 时子代可能落进邻格）依旧
  成立，见 `genetics_systems.cpp` 的说明。本模块不修它。

`worker_count` 无关性：`EnvironmentSystem`、`BreedingSystem`、
`PopulationStatisticsSystem` 三者的 `execute` 都是收集到 `vector` 再顺序
计算，`for_each_entity` 走槽位下标序，与线程数无关。`AgingSystem` 与
`EnvironmentSystem` 之间那对「访问集不相交」的并发风险已由
`EnvironmentSystem::mutates_structure` 独占批次挡住（环境设计文档 §8.2）。
因此 `worker_count = 1` 与 `worker_count = 4` 应当逐位相同。这条**没有**在本套件
里用「跑两个不同 worker 数的世界」的方式守住：那个形状恒绿（并行压根不发生），
见 §8 末条。改为在 ECS 侧断言 `batch_count() == 3`。

---

## 7. 测试策略

`tests/test_world_module.cpp`，共 10 条（原先 11 条；
`season_number_reaches_the_breeding_random_stream` 因移动接入而不可构造，已删，
理由见 §8）。末列「实际鉴别力」不是客套，是逐条写明这条测试今天能抓住什么、
抓不住什么——它对着下面「逐条说明要害」与 §8。

| # | 测试 | 验证点 | 实际鉴别力 |
|---|------|--------|-----------|
| 1 | `world_runs_an_empty_season` | 空世界跑一季：`season == 0`、`population == 0` | 低。只覆盖空输入与季号从 0 起算；聚合循环空转一次 |
| 2 | `report_population_matches_the_survivors_after_flush` | `report.population` 等于遍历世界数出的存活个体数——判据须与 `PopulationStatisticsSystem` 逐字相同：带 `Genome`，且 `SpeciesTag::species.value` 落在 `[1, species_count()]` | 中。两侧走的是同一套判据，一起错时仍相等（例如把下界从 1 放宽到 0，两边一起放行） |
| 3 | `report_counts_the_dead_as_gone` | 统计落在换代之后：32 个必死者逐个查句柄已死，且 `population` 等于「新生儿 + 幸存者」 | **高。** 决策 3（换代之后统计）唯一的直接守卫 |
| 4 | `report_counts_newborn_creatures` | 稳态表型下亲代零死亡，故 `population > before` 等价于「报告把 flush 才创建的子代算在了里面」 | 中。结论依赖「亲代 survival_rate 恒为 1」这个前提，而 escape 那一半依赖 `genetics_systems.cpp` 里 `threat_level` 的硬编码 0 |
| 5 | `season_number_starts_at_zero_and_advances` | 连跑三季，报告的 `season` 为 0、1、2 | 只钉「自增在 `report` 构造之后」。对「自增排在 `statistics_.execute` 之前还是之后」无鉴别力——挪到 `report` 构造前照样全绿 |
| 6 | `season_index_actually_moves_the_world` | 季号真的进了气候场：跨图采 8 格，季 0 与季 1 的气温至少有一格不同 | 中高。对「振幅或相位整体不变」这类真失效敏感；对舍入不敏感（本测试比较的两季上季节项 4.24 °C 比 ulp 9.5e-7 大六个量级） |
| 7 | `run_seasons_equals_repeated_run_season` | `run_seasons(5)` 的五个报告与五次 `run_season()` 逐项相同 | 只验等价性。两个世界来自同一个二进制，任何变异同时作用在两边、轨迹一起变，原理上免疫「两边同时坏」 |
| 8 | `equal_seeds_produce_identical_seasons` | 同 `root_seed` 的两个世界跑四季，逐位相同 | 与第 7 条同类：同一种推进方式在两个实例上的复核，同样不免疫「两侧同时坏」 |
| 9 | `population_never_exceeds_the_capacity_left_below_it` | 跨模块不变量：铺满整张图、每格放 `floor(cap × 0.85)` 个体，逐季断言 `population ≤ 期初 + Σheadroom + kCellCount/2 + 1` | **高。** 上界逐格算，对格间分布不作假设——个体被随机游走推进超载格时照样咬得住（全局界 `Σcapacity` 在有移动时不成立，见 §8）。铺得不够满时改由 `parent_ceiling` 决定出生预算，断言恒真——把它误当成 capacity 的 mutant 则必然越界 |
| 10 | `creatures_without_position_never_die_and_never_breed` | 幽灵个体的数量在三季内恒定，且两侧 `population` 的差恰好是幽灵数 | **高。** 差分把「幽灵自己那 5 条」与「它们对世界的影响」分开；给幽灵补上 `Position` 的 mutant 三季全红 |

逐条说明要害：

- **测试 3 是这一轮最要紧的一条。** 决策 3（换代之后统计）唯一的直接守卫。
  它测的是「统计落在正确的时刻」，而这件事一旦回归，报告里的
  `population` 会变成一个静默错位的数字：它含本季将死者、不含新生代，
  不崩不报错。
- **测试 9 借用 `test_environment_systems.cpp` 的算法**（`capacity_base = 40`，
  逐格读真实容量再放 `floor(cap × 0.85)` 个体），不重新设计布点。它守的是
  「世界这个编排层没有把承载力封顶弄坏」。取值不能是 `floor(cap / 2)`——
  那样单格在场数才到容量的一半，`build_sites` 取 `eco_ceiling` 与
  `parent_ceiling = lround(在场数 × offspring_scale)` 的较小者，接管的是
  后者，capacity 这条约束根本没被触到，断言恒真。
- **测试 10 守的是 §5 契约表第二行。** 那个行为是既有的、正确的，但它
  足够反直觉（不死也不繁殖），而 `entities()` 是本模块新开的、调用方
  一定会用到的入口。移动接入之后它仍然成立：缺 `Position` 的实体在
  `MovementSystem` 的收集判据里同样被跳过，与另两个 system 对「谁参与世界
  运转」给出同一个答案。

**刻意不写的测试**：不为「`EnvironmentSystem` 必须先于 `BreedingSystem`」
单写一条。决定执行次序的是 `System::order`（`EnvironmentSystem` 为 0、
`BreedingSystem` 为 1），不是 `scheduler.add()` 的注册顺序——`plan()` 先按
order 稳定排序再贪心分层，调换注册顺序根本不会颠倒批次，注册序只影响 order
相同的 system 谁先占住靠前的批次。假定 order 真的排反了，表现是本季出生数为
零、子代缺席 `report`，此时测试 4 的 `CHECK(report.population > before)`
会红，那条已经覆盖。为同一件事再立一条独立断言，只是多一处需要同步更新的
地方。

**构建**：`CMakeLists.txt` 新增 `world` 静态库与 `world_tests` 可执行目标，
依赖链 `world_tests → world → environment`（后者已 PUBLIC 挂着 genetics 与
ecs）。ASan 的 `foreach` 遍历的是具名列表，必须把 `WORLD_SOURCES` 与
`WORLD_TEST_SOURCES` 一并加入，否则 `-DGAMEV2_ENABLE_ASAN=ON` 的构建会
悄悄漏掉整个世界模块。

---

## 8. 未决事项

- **「季号真的流进了繁殖随机流」这条在 World 层已从「没有守卫」变成
  「守卫不可构造」，因此那条测试被删了。**
  原先它守的是：把同一批初始个体放进两个处于不同季号的世界各结算一季，
  逐条掐掉站点结构、摄食存量、气候三处差异，剩下的自由变量就只有
  `RandomStreams{root_seed_, season_index_}`。删掉 `World::run_season` 里的
  `breeding_.advance_season(season_);` 会让两边都退回季 0 的流，测试立刻变红。
  **`MovementSystem` 接入之后这条路断了，而且绕不过去**：季号现在同时决定
  移动的随机流（`mix_seed(mix_seed(root_seed, season), kMovementPurpose)`）与
  繁殖的随机流，于是季 0 与季 1 的同一批个体落在不同坐标 → 每格在场数不同 →
  `build_sites` 的站点结构与每格 `progeny` 不同 → `make_offspring` 调用次数
  不同。子代基因组的差异于是有了「站点结构不同」这个与季号无关的解释，
  而那正是这条测试当初逐条掐掉的第一项。实测两季的子代数直接不相等。
  要恢复它就得造出两个**移动流相同、繁殖流不同**的世界，而 `run_season` 里
  两者来自同一个 `season_` 成员，任何 World 层的调用序列都做不到。
  **代价要说清楚：`breeding_.advance_season(season_)` 这一行现在完全没有测试
  守卫。** 机制本身在 adapt 层仍被覆盖
  （`test_genetics_systems.cpp::breeding_advance_season_replaces_the_season_index`），
  没有覆盖的是 World 层这个调用点。补法有两条，都不便宜：让 `World` 暴露可控
  的季号注入点（那是给测试开的生产代码口子），或给存档往返测试。**明确否决了
  「钉一个黄金值」**——那会把一个尚未被任何测试钉住的行为变成一碰就碎的快照。
  顺带记一笔 `heterozygosity` 在本套件里剩下的正当职责：它只承担「同一条轨迹
  的逐位复核」。任何形如「它每季都该变」、或拿它当某种漂移探针的断言，都不能
  由它支撑。
- **「并行永不发生、`worker_count` 结构性惰性」这条已经作废。**
  `MovementSystem` 接入前，`World` 排出的 3 个批次全是单 system 批，
  `Scheduler::run` 因此从不向线程池派发；`worker_count_does_not_change_the_result`
  正因如此是个恒绿用例，已删除。
  现在四个 system 排成 `{Movement, Aging}` / `{Environment}` / `{Breeding}`
  三批——**四个 system 三批**这件事本身就说明有一批装着两个，于是并行第一次
  真的发生，`worker_count` 从此有意义。
  第一批能并排的依据是两者访问集确实不相交：`MovementSystem` 写 `Position`、
  只读 `CreaturePhenotype`，`AgingSystem` 读 `CreaturePhenotype`、写
  `Vitality`；两者都只在 `execute` 里就地改写、不做结构变更，而
  `World::get_if` 全程只读（`slot_of` / `find` / `column_at` / `chunk_data`
  都不分配）。
  守卫换成了两条：`world_assembly_plans_into_three_batches_with_one_parallel_pair`
  断言四 system 排成三批，而
  `movement_and_aging_do_not_conflict_so_a_batch_can_run_them_in_parallel`
  直接断言这两个 system 排成一批——后者才是可证伪的那条（实测给
  `AgingSystem::ReadSet` 加上 `const Position`，两条一起变红）。
  **`BreedingSystem::ReadSet` 补上 `const Position` 是这条的前提**：它本来就在
  `execute` 里 `get_if<Position>` 却没声明，于是「移动写、繁殖读」这条冲突表达
  不出去，两者会被排进同一批并发执行，而 `System::order` 只在「本来就会冲突」
  时才等于批次先后（见 `system.hpp` 的 `\warning`）。
- **承载力的全局界不成立，能断言的只有逐格那条。**
  `population_never_exceeds_total_capacity` 原本断言「季末规模 ≤ Σcapacity
  + O(格数)」，靠的是那 256 格封闭且初始均匀。移动打破封闭性之后它就不成立，
  而且成立不了是有原因的：`headroom` 逐格取 `max(0, capacity − occupants)`，
  于是**超载格贡献 0，而它多占的那些个体并不会去扣减别的格的余量**。个体被
  随机游走推进超载格、余量却留在空着的格里时，Σheadroom 就能显著超过
  `Σcapacity − 种群`，总规模因此越过 Σcapacity。实测首季 Σheadroom = 27294
  对 `Σcapacity − 种群` = 17586，季末 112778 对 Σcapacity = 103480。
  这不是 `build_sites` 违约——每一格都守住了自己的 headroom——而是「个体不能被
  强制迁走」这条设定在容量层面的后果。
  该测试已改名为 `population_never_exceeds_the_capacity_left_below_it`，
  断言换成真正成立的那一条：`季末规模 ≤ 季初规模 + Σheadroom + O(格数)`，
  其中 `headroom` 由测试侧的 `total_headroom` 按 `EnvironmentSystem` 的收集判据
  逐字重算。观察时机是安全的：移动在季初跑完，子代落在亲代格内
  （`place_in` 的抖动取 [0, 0.9)，加在整数格坐标上不跨格），因此 flush 之后
  看到的分布正是下一季的余量。
- **`EnvironmentWorld` 补了 `climate()` / `resources()` 的 const 重载。**
  本模块的只读访问器 `World::environment()` 返回 `const EnvironmentWorld&`，
  而原访问器只有非常量版本，导致测试里不得不 `const_cast`——那会把「这个访问器
  不该有 const 版本」的设计问题藏起来。底层 `ClimateField` / `ResourceField`
  本来就有完整 const 重载，补齐是廉价的。非常量重载保留：
  `EnvironmentSystem::execute` 要调 `climate().advance()`。
- **夹具里父子代的气候待遇不对称，是有意的。** `Environment::snapshot` 全程
  无人刷新（`EnvironmentSystem::WriteSet` 只有 `Feeding`），这不是缺陷：
  设计文档 §4.1 明写「环境在个体建立时采样一次并锁定」，三个字段的默认值
  即「无环境影响」。但后果是——手工搭的**父代**走默认值（完全不受气候影响），
  而 `BreedingSystem` 经 `cmd.create` 给**子代**挂上出生格的真实气候，于是子代
  受 `|base_temperature|` 影响、父代不受，落在高温格的子代会全灭。
  曾考虑让 `spawn` 采样所在格的气候来对齐两者，**已否决**：
  `temperature_amplitude = 6.0` 叠上 ±8 的基准温度，偏移量级到 ±14 会让夹具
  里的个体几乎必死，测试没法写。代价是多季测试里种群的下降由「出生格温度」
  驱动而非「当季气候」，读数据时须记住这一点。
- **`AgingSystem` 与 `BreedingSystem` 会对同一个死者各发一次 `cmd.destroy`——已修。**
  两者入队的是不同缓冲，一次 flush 里第一条 destroy 生效，第二条撞上
  `CommandBuffer::flush` 的「目标已失效则拒绝」判据。原先这会报出一条
  **error 级** 的 `ecs.entity_invalid` 假诊断：实体恰好被销毁一次，行为正确，
  错的只是诊断噪音——但它是 error 级，实测一轮 world_tests 能刷出 6076 条，
  足以淹没诊断通道里真正的问题。
  修法落在队列层而不是死亡归属：`destroy` 对已失效的目标改为幂等静默，与
  `World::destroy` 的判法一致（后者对已失效句柄本就直接返回）。这条对**任何**
  两个独立判死的 system 都成立，不是给这一对打的补丁。`add` / `remove` 不享受
  豁免——往一个已经不存在的实体上挂组件一定是 bug。
  命令合并同时改成按**批次**顺序（原为注册顺序），使命令序列与
  `System::order` 决定的执行次序一致。
- **~~一个空掉的格永远不会再被填上，地图因此单调失血~~ ——已由
  `MovementSystem` 修掉，这条留作记录。**
  它是 `sim` 可执行程序（`src/sim/main.cpp`）在移动接入**之前**跑 300 季实测
  出来的：种群从 6979 缓慢滑到 2014，每季出生与死亡基本拉平，滑的不是收支
  而是**可用格数**。`build_sites` 的出生预算取
  `min(eco_ceiling, parent_ceiling)`，而
  `parent_ceiling = lround(here × offspring_scale)`（`src/environment/sites.cpp`）。
  没有亲代就没有站点：`here = 0` 时两项都产出 0，而站点表只由在格个体生成，
  于是一个格一旦清空就永远空着。个体不移动时，地图上没有任何机制能把邻格的
  过剩迁过来，格数只减不增。
  `MovementSystem` 每季把个体按 `Speed` 朝随机方向推进一步，空格因此能被走进
  来的个体重新殖民。实测：同一份 `sim` 配置，移动前 200 季从 6979 滑到 2740
  且仍在下滑；移动后升到 9858 并从第 50 季起稳定持平在 Σcapacity ≈ 9984
  附近，出生 ≈ 死亡 ≈ 1900/季。`H_obs` 也从一路掉到 0.34 变成稳在 0.44。
  注意修掉的只是「格数流失」：**总规模越过 Σcapacity 仍是可能的**，那是另一条
  性质，见上面「承载力的全局界不成立」。
- **这条性质在阶段 2（地形 + 移动重写）之后重新验过一次，仍然成立。**
  它的守法变了：`MovementSystem` 已不是纯随机游走，而是掷硬币决定跟资源存量
  梯度还是随机游走；地形又把地图切成十几个陆块。所以**单次快照测不出来**
  —— 必须看时间序列，否则一次偶然的低读数会被误当成失血。
  `sim` 因此每次运行都打印覆盖率与最挤一格的个体数。实测 `evolve` 跑 200 季：
  覆盖率在上升后趋平，不是在蚀。

  **这条性质经过三次重测，每一次的地图都不同**（全陆地 → 地形 70/30 →
  创始池改造 + 裂变）。种子 20261016、`evolve` 预设：`evolve` 20 季 71.3% →
  200 季 78.0%；`persist` 200 季 87.6%。平台值比无地形时的 87.2% 低，原因
  是水屏障限制移动、存量梯度制造聚集，以及裂变把个体分成十来群之后每群更
  稀疏。细节见 `doc/2026-10-17-game-design.md` §2.1 / §2.2 / §2.4 与 §7.1。

  `sim` 现在每次运行都打印覆盖率与最挤一格的个体数 —— 这是这条性质的常驻
  观测点。**只报末季一个数是不够的**：单调收缩与趋平的单次快照长得一样。
- `threats` 的来源：捕食威胁等级仍由调用方给，环境层不产生；
  `BreedingSystem` 目前恒给 0.0f
- 存档需求出现时，`ClimateField` / `ResourceField` / `LocusTable` /
  `SpeciesRegistry` 的序列化格式，以及 `World` 是否可从存档恢复
- 生态位 spec 交付后，`SeasonReport` 是否要带上生态位占用列

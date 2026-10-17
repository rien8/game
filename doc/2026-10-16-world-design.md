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
- 不改动三个已有模块的语义，只在 `ecs/adapt` 增加两个推进方法
- 批量推进与逐季推进逐位等价，使世界可被分段推进

### 1.2 非目标

本轮明确不做：

- 初始种群生成（调用方经 `entities()` 自行放入）
- 个体在格内的移动（行为系统的活，环境设计文档 §1.2 已排除）
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

```cpp
SeasonReport World::run_season()
{
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

1. **先推进季号，再跑调度器。** 两处的 `season_index_` 都在 `execute` 开头
   消费一次（`climate().advance(season_index_)` 与
   `RandomStreams{root_seed_, season_index_}`），因此推进必须整体发生在
   `scheduler_.run` 之前。中途改会让同一批 system 看到不同的季。
2. **`scheduler_.run` 内部。** 三个 system 的相对先后由各自已声明的
   `order` 与访问集决定，本模块不干预。换代（死亡销毁与子代创建）也在此刻
   生效：`Scheduler::run` 把各 system 的缓冲并进 `cmd` 之后就在末尾
   `cmd.flush(world)`（`src/ecs/scheduler.cpp:144`）。世界在这一步之后才
   处于「本季已结算」的状态。
3. **不要再加一次 `cmd_.flush`。** 它不是无害的重复：`CommandBuffer::flush`
   末尾有 `commands_.clear()`（`src/ecs/command_buffer.cpp:24`），第二次
   flush 面对的是空缓冲、纯属空转。多一行 flush，「统计必须排在换代之后」
   这个约束在代码结构上就看不出来了——只剩这一次 flush 时，位置关系一眼可辨。
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
4. **`worker_count` 只影响并发批次数，不影响结果。** 由测试 9 守住。
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
因此 `worker_count = 1` 与 `worker_count = 4` 应当逐位相同——这是**新断言**，
此前没有测试覆盖过线程数对结果的影响。

---

## 7. 测试策略

`tests/test_world_module.cpp`，共 11 条。末列「实际鉴别力」不是客套，是逐条
写明这条测试今天能抓住什么、抓不住什么——它对着下面「逐条说明要害」与 §8。

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
| 9 | `worker_count_does_not_change_the_result` | worker 1 与 worker 4 跑三季结果逐位相同 | **今天为零。** 并行在该装配下永不发生（实测 `Scheduler::batch_count() == 3`，三个单 system 批），它压根没跑过多线程。留着是给未来的护栏 |
| 10 | `population_never_exceeds_total_capacity` | 跨模块不变量：铺 256 格（步长 16）、每格放 `floor(cap × 0.85)` 个体，`population ≤ Σcapacity + kCells/2 + 1`，连跑三季逐季断言 | **高。** 上限只对这 256 格求和。铺得不够满时改由 `parent_ceiling` 决定出生预算，断言恒真——把它误当成 capacity 的 mutant 则必然越界，实测峰值 6374 对上限 6506 |
| 11 | `creatures_without_position_never_die_and_never_breed` | 幽灵个体的数量在三季内恒定，且两侧 `population` 的差恰好是幽灵数 | **高。** 差分把「幽灵自己那 5 条」与「它们对世界的影响」分开；给幽灵补上 `Position` 的 mutant 三季全红 |

逐条说明要害：

- **测试 3 是这一轮最要紧的一条。** 决策 3（换代之后统计）唯一的直接守卫。
  它测的是「统计落在正确的时刻」，而这件事一旦回归，报告里的
  `population` 会变成一个静默错位的数字：它含本季将死者、不含新生代，
  不崩不报错。
- **测试 10 借用 `test_environment_systems.cpp` 的算法**（`capacity_base = 40`，
  逐格读真实容量再放 `floor(cap × 0.85)` 个体），不重新设计布点。它守的是
  「世界这个编排层没有把承载力封顶弄坏」。取值不能是 `floor(cap / 2)`——
  那样单格在场数才到容量的一半，`build_sites` 取 `eco_ceiling` 与
  `parent_ceiling = lround(在场数 × offspring_scale)` 的较小者，接管的是
  后者，capacity 这条约束根本没被触到，断言恒真。
- **测试 11 守的是 §5 契约表第二行。** 那个行为是既有的、正确的，但它
  足够反直觉（不死也不繁殖），而 `entities()` 是本模块新开的、调用方
  一定会用到的入口。

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

- **「季号真的流进了繁殖随机流」这条在 World 层没有任何测试守卫。**
  `run_seasons_equals_repeated_run_season` 比的是同一二进制里的两个世界，
  任何变异都同时作用在两边、轨迹一起变，逐位相等自然成立——**它验证等价性，
  原理上免疫「两边同时坏」**。其余测试各管一段（季号编号、气候、单季 census），
  都够不着繁殖随机流。已实测确认：删掉 `World::run_season` 里
  `breeding_.advance_season(season_);` 这一行，四个套件仍然全绿。
  机制本身在 adapt 层是覆盖的
  （`test_genetics_systems.cpp::breeding_advance_season_replaces_the_season_index`），
  **未被覆盖的是 World 层的调用点**。补法有两条，都不属于本轮：给存档往返
  测试（同一份存档按不同季号恢复，结果应不同），或在 adapt 层暴露
  `RandomStreams` 的观测口。**明确否决了「钉一个黄金值」**——那会把一个尚未被
  任何测试钉住的行为变成一碰就碎的快照，是 §9 那个 bug 的另一种形式。
- **这个装配下并行永远不会发生，`worker_count` 是结构性惰性的。**
  `World` 排出的 3 个批次全是单 system 批：`EnvironmentSystem` 带
  `mutates_structure` 独占一批；`AgingSystem` 与 `BreedingSystem` 同写
  `Vitality` 被强制分层；`PopulationStatisticsSystem` 根本不在调度器里
  （决策 3 要求它的 census 落在 flush 之后）。因此线程池没有机会并行派发
  任何东西。后果：**`worker_count_does_not_change_the_result` 今天守的是一条
  尚未被违反的性质**，它没有鉴别力可言——测试本身没错，错的是它看起来比实际
  更有力。日后若出现多 system 同批的 system，这条测试才开始真正起作用。
- **别把 `heterozygosity` 当成「每季都不同」的守卫。** 它确实是「删掉
  `breeding_.advance_season` 之后会分叉」的观测量（census / population 不分叉，
  出生被承载力上限顶住），但**分叉不是逐季全中**——实测季 3 两种构建恰好相同。
  它只能被表述成「一个会分叉的观测量」。
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
- **`AgingSystem` 与 `BreedingSystem` 会对同一个死者各发一次 `cmd.destroy`。**
  两者入队的是不同缓冲，`Scheduler::run` 按注册顺序合并后一次 flush：第一条
  destroy 生效，第二条撞上 `CommandBuffer::flush` 的「目标已失效则拒绝」判据，
  报出一条 **error 级** 的 `ecs.entity_invalid` 假诊断。实体恰好被销毁一次，
  行为正确，错的只是诊断噪音——但它是 error 级，会淹没测试输出。
  既有测试从未让这两个 system 在同一个世界上真跑过，是本模块首次把它们装配
  到一起才暴露的。真正的修法要动死亡归属（两个 system 独立判死），属
  `AgingSystem` 的契约变更，另开一轮。调整注册顺序只是让被拒的那一条换成
  另一条，不是修法。
- 个体移动接入后，`Position` 每季的更新时机：必须在 `EnvironmentSystem`
  之前（摄食要用当季坐标），且在 `BreedingSystem` 之前（空间分桶要用
  当季坐标）。届时多半要再加一个 system，届时它的 `order` 排在 0。
- `threats` 的来源：捕食威胁等级仍由调用方给，环境层不产生；
  `BreedingSystem` 目前恒给 0.0f
- 存档需求出现时，`ClimateField` / `ResourceField` / `LocusTable` /
  `SpeciesRegistry` 的序列化格式，以及 `World` 是否可从存档恢复
- 生态位 spec 交付后，`SeasonReport` 是否要带上生态位占用列

#include "test_framework.hpp"

#include "gamev2/world/world.hpp"

#include "gamev2/ecs/world.hpp"
#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// census() 的参数写成 ecs::World&，而本文件在全局作用域：没有这条别名，
// 限定名 ecs 查不到 gamev2::ecs。
namespace ecs = gamev2::ecs;

using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::Environment;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::Vitality;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::genetics::AlleleCall;
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
        // 「neutral」说的是位点的 kind / scale / allele_count 中性，不是
        // 「随机流不用」。switch_rate = 0 时 breeding.cpp 的
        // `uniform_unit(rng) >= locus.switch_rate` 恒成立，mutate 对每个位点
        // 都 early-continue，于是 AlleleSwitch 这条子流一次都不被抽——全体
        // Genome 恒为 Genome{}，杂合度恒为 0。它只是 kPurposeCount 条子流的
        // 一条：RandomStreams 把 season_index 混进全部子流，而 Mating /
        // Crossover / Survival 在 mutate 之外另行抽取，Jitter 则在第 64 行
        // 那次比较里照抽不误。
        //
        // 取一个小的非零值：突变稀疏，但不归零。删掉 World::run_season 里的
        // breeding_.advance_season(season_); 会**改变行为**——第 1 季起繁殖
        // 抽样退回季 0 的种子。真正分叉的观测量是子代基因组（census 与
        // population 在两种构建下逐季相同），由
        // season_number_reaches_the_breeding_random_stream 守住。
        t.loci[i].switch_rate = 0.05f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

/// 子代侧的代谢归零：只经 phenotype_of 生效，手工搭的亲代拿不到。
/// 另一半在 creature_phenotype()，两者缺一不可——见那里的说明。
///
/// 需要「只测编排、不测生存抽签」的测试才能断言确定的数量。Fecundity 取 0.05。
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

/// 表型模板：SenseRange 与 Fecundity 各 0.5，因此摄食 demand 恒为 0.25。
///
/// Metabolism 与 Lifespan 是把 survival_rate 顶到 1 且让个体跨季存续的两半：
/// `steady_genetics()` 的 trait_base 只经 phenotype_of 传给子代，手工搭出来的
/// 亲代走的是这里，两处各管一半。只改 trait_base 的话 burn 仍是
/// Metabolism × BodySize × (1 + Speed) = 2 而 intake 上限只有 0.25，
/// 抽签每季判死 87.5%；只改这里而不改 Lifespan 的话 outlived 的 `age >= lifespan`
/// 又会在第一季把全体清空。
///
/// 能量那一半之外还有 thermal 与 escape 两半：`survival_rate` 是
/// energy × thermal × escape 的乘积（src/genetics/survival.cpp）。
///
/// thermal 要的是 `|Environment::snapshot::temperature|` 小于
/// `thermal_base_width + thermal_resilience_gain × Resilience`——前者是配置项
/// （locus.hpp，默认 1.0f），后者才是 Resilience 这条 trait（默认表型下也为
/// 1.0），宽 2.0。spawn() 给的默认快照 temperature 恒为 0，clamp 后的
/// thermal 才是 1。
///
/// escape 那一半需要**两个**前提，缺一不可：escape_survival 算的是
/// `clamp((Speed − threat_level × BodySize) × escape_responsiveness, 0, 1)`，
/// 要取满 1 既得 threat_level == 0（BodySize 与 Speed 都是 1.0），又得
/// Speed = 1。threat_level == 0 **不是机制保证**，而是 genetics_systems.cpp:185
/// 那句 `threats.push_back(0.0f)` 的硬编码——设计文档 §8 把 threats 的来源
/// 列为未决事项。它一旦变成非 0，下面 report_counts_newborn_creatures 与
/// creatures_without_position_never_die_and_never_breed 里「亲代零死亡」的
/// 前提随即失效。
///
/// **这是 spawn() 那条「不要顺手给真实气候」注释的另一半理由**：一旦父代
/// 开始采样真实气候，thermal 就跟着掉下来。
Phenotype creature_phenotype()
{
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
    p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
    p.traits[trait_index(TraitKind::Metabolism)] = 0.0f;
    p.traits[trait_index(TraitKind::Lifespan)] = 10.0f;
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

    Fixture()
        : world{table, genetics, config, 31}
    {
        // register_species 返回 std::expected 而非指针：不查就吞掉失败，
        // 而这条路径是全部测试造世界的唯一入口。
        const auto reg = world.species().register_species(all_loci());
        CHECK(reg.has_value());
        sid = *reg;
    }

    /// 给满六个组件。这是 World::entities() 的唯一合法用法。
    ///
    /// Environment 给的是设计文档 §4.1 的默认快照，即「无环境影响」：环境在
    /// 个体建立时采样一次并锁定，而这里不采样。父子代因此气候待遇不对称——
    /// 子代由 BreedingSystem 经 cmd.create 挂上出生格的真实气候。这是**有意**
    /// 的，不是漏刷（EnvironmentSystem 的 WriteSet 只有 Feeding，全仓库没有
    /// 任何一处给 snapshot 赋值）。不要「顺手对齐」：采样真实气候会让
    /// temperature_amplitude = 6.0 叠上 ±8 的基准温度，把夹具里的个体逼到
    /// 几乎必死，测试就没法写了。
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
    ///
    /// Environment 同样给设计文档的默认快照（无环境影响），理由与不对称性见
    /// spawn() 的说明。
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

    /// genetics::outlived 判的是 `age >= lifespan`：AgingSystem 把 age 递增到
    /// age_seasons，而 template 的 Lifespan 远大于 1，第一季不成立，故要显式
    /// 设 0 让它恒真。Age 先跑而死亡在 flush 才生效，因此它当季仍参与繁殖与
    /// 存活抽签——这对本模块无影响，只在写测试时别被吓到。
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

TEST(world_runs_an_empty_season)
{
    Fixture f;
    const auto report = f.world.run_season();
    CHECK_EQ(report.season, 0u);
    CHECK_EQ(report.population, 0u);
}

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
    std::vector<EntityId> doomed;
    doomed.reserve(32);
    for (std::size_t i = 0; i < 32; ++i)
    {
        const auto x = static_cast<float>(i % kGridSide) + 0.9f;
        const auto y = static_cast<float>(i / kGridSide) + 0.5f;
        doomed.push_back(f.spawn_doomed(x, y));
    }

    const auto report = f.world.run_season();

    // 逐个查句柄，不经「总数变小」间接推：出生是 128 还是 8，本测试都成立。
    for (const auto e : doomed)
    {
        CHECK(!f.world.entities().is_alive(e));
    }

    // age_seasons 是亲子代的分割线：每个亲代都被 AgingSystem 递增过，子代
    // 则由 cmd.create 挂默认构造的 Vitality，而 AgingSystem 早在它们存在之前
    // 就跑完了。
    std::size_t newborn = 0;
    std::size_t survivor = 0;
    f.world.entities().for_each_entity(
        [&f, &newborn, &survivor](const EntityId e)
        {
            if (f.world.entities().get_if<Vitality>(e)->age_seasons == 0)
            {
                ++newborn;
            }
            else
            {
                ++survivor;
            }
        });

    // 32 个必死者一个都没算进 survivor——它恰好是 seed(32) 的 64。
    CHECK_EQ(survivor, 64u);
    CHECK(newborn > 0);
    CHECK_EQ(report.population, newborn + survivor);
    CHECK_EQ(report.population, f.alive());
}

TEST(report_counts_newborn_creatures)
{
    Fixture f;
    f.seed(32);
    const auto before = f.alive();

    // roomy_config 让每格 headroom 充足，且每格两个体使
    // BreedingSystem 的空间桶（mating_radius_cells = 1）凑得齐两个亲代。
    const auto report = f.world.run_season();

    // 稳态表型下亲代零死亡（Lifespan = 10 跨不过去，survival_rate 恒为 1——
    // energy / thermal / escape 三半各自的前提见 creature_phenotype()，
    // escape 那一半依赖 genetics_systems.cpp:185 的 threat_level 硬编码 0），
    // 因此 population 只会因出生而增，`population > before` 就等价于
    // 「报告把 flush 才创建的子代算在了里面」——若 census 落在换代之前，
    // 它数到的还是那 64 个原始个体，报告会比 before 小。
    CHECK(report.population > before);
}

TEST(season_number_starts_at_zero_and_advances)
{
    Fixture f;
    f.seed(16);
    const auto reports = f.world.run_seasons(3);

    // 季号由 run_season 自增，首季必须是 0——report.season 取的是聚合首字段，
    // 因此这三条 CHECK 钉的是「自增排在 report 构造之后」：把 ++season_ 挪到
    // statistics_.execute 之后、report 构造之前，三条 CHECK 照样全绿，它们对
    // 「自增与统计的先后」没有鉴别力。
    CHECK_EQ(reports.size(), 3u);
    CHECK_EQ(reports[0].season, 0u);
    CHECK_EQ(reports[1].season, 1u);
    CHECK_EQ(reports[2].season, 2u);
}

/// 季号必须真的进入气候场，否则世界停在季 0：所有个体吃到同一份气候，
/// 季节这件事对演化不成立。
TEST(season_index_actually_moves_the_world)
{
    Fixture f;
    f.seed(16);

    // EnvironmentSystem::execute 的第一行把 season_index_ 交给
    // ClimateField::advance，气温因此按 temperature_period 的正弦偏移。
    const auto& env = f.world.environment();

    // 采样格要跨整张图。ClimateField::at 里的季节项 amplitude × sin(phase)
    // 只由 season_ 决定，是加给每一格的同一个加性常数，逐格相位并不存在，
    // 所以多采几格防的不是「相位对称」——那种情形在这里不可能发生。它唯一
    // 能防的是「某一格恰好落在两条曲线的交点上」这种单格退化，多采几格无害。
    // 真正该防的失效是振幅或相位整体不变，而那对任何一格都成立。
    //
    // 浮点舍入在本测试比较的两季上够不着：季 0 的季节项是 6.0 × sin(0) = 0，
    // 季 1 是 6.0 × sin(π/4) ≈ 4.24 °C，而 |base_temperature| ≤ 8 处 float 的
    // ulp 约 9.5e-7，两者差六个量级，没有任何一格会被舍入抹平成相等。真正会
    // 的地方在季 4 与季 8 附近（相位取到 float π 时 sin ≈ −8.7e-8，季节项约
    // −5.2e-7，已在 ulp 量级），而本测试不比较那两季。
    constexpr std::uint16_t sampled[] = {0, 1, 63, 64, 511, 2048, 3000, 4095};

    (void)f.world.run_season();
    std::vector<float> season_zero;
    for (const auto cell : sampled)
    {
        season_zero.push_back(env.climate().at(CellIndex{cell}).temperature);
    }

    (void)f.world.run_season();

    bool moved = false;
    for (std::size_t i = 0; i < std::size(sampled); ++i)
    {
        const float later = env.climate().at(CellIndex{sampled[i]}).temperature;
        if (season_zero[i] != later)
        {
            moved = true;
        }
    }
    CHECK(moved);
}

TEST(run_seasons_equals_repeated_run_season)
{
    // 批量推进必须与逐季推进逐位等价，否则 run_seasons 只是个语法糖，
    // 而「日后可存档」依赖的正是这条等价性。
    //
    // 两个世界必须走完全相同的调用序列：构造、登记物种、seed(16)，
    // 顺序与参数都一样，随机流才逐位对齐。
    //
    // 本测试**不守**「季号真的流进了繁殖随机流」：两个世界来自同一个二进制，
    // 而像删掉 breeding_.advance_season(season_) 那样的变异同时作用在两边、
    // 轨迹一起变，逐位相等照常成立。它验证的是等价性，原理上免疫「两边同时坏」。
    // 这个缺口将来要靠存档往返测试，或 adapt 层暴露 RandomStreams 观测口来补。
    Fixture batched;
    Fixture stepwise;
    batched.seed(16);
    stepwise.seed(16);

    const auto bulk = batched.world.run_seasons(5);
    std::vector<gamev2::world::SeasonReport> one_by_one;
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
        // census 与 heterozygosity 一正一负：前者覆盖存活与出生，后者覆盖
        // 逐位点的等位基因频率汇总——随机流若在任一季错位，两者都会露馅。
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

/// 同种子造出的两个世界，跑同样季数必须逐位相同。
///
/// 设计文档 §6 把这条列为未经验证的新断言，此前没有测试覆盖它。如实交代它的
/// 边界：它与 run_seasons_equals_repeated_run_season 属同一类——都在同一个二进制
/// 里并排跑两个 World，因此同样不免疫「两侧同时坏」（删掉
/// breeding_.advance_season(season_) 会让两份轨迹一起变，逐位相等照常成立）。
/// 与那条的差别只在驱动方式：本测试两侧都走 run_seasons，比较的是同一种推进
/// 方式在两个实例上是否给出同一条轨迹。所以它不是前一条的替代，而是同一性质
/// 在另一条调用路径上的复核。
TEST(equal_seeds_produce_identical_seasons)
{
    Fixture a;
    Fixture b;
    a.seed(32);
    b.seed(32);

    const auto ra = a.world.run_seasons(4);
    const auto rb = b.world.run_seasons(4);

    CHECK_EQ(ra.size(), rb.size());
    for (std::size_t i = 0; i < ra.size() && i < rb.size(); ++i)
    {
        CHECK_EQ(ra[i].population, rb[i].population);
        // 不是第三条物理论证，而是防空转的护栏：下面的循环用 && 收窄下标，
        // 两边 species 不等长时它会静默少比几项，测试照样绿。
        CHECK_EQ(ra[i].species.size(), rb[i].species.size());
        for (std::size_t s = 0;
             s < ra[i].species.size() && s < rb[i].species.size(); ++s)
        {
            // 只比这一个字段就够：population 覆盖存活与出生，杂合度是逐位点
            // H_obs 的**无权**均值（statistics.cpp:101-103），它一动就说明
            // Genome 层面的随机流错位了。另外两个字段不是同一口径的重复——
            // f_coefficient 按 H_exp 加权（:104-105）、effective_population
            // 依赖 Σ(1 − H_exp)（:114-117），位点 A 升、位点 B 降而无权均值
            // 不变时三者的响应未必一致。不比它们是为了让失败面小一些，
            // 不是「加了也不损失覆盖面」。
            CHECK_EQ(ra[i].species[s].heterozygosity,
                     rb[i].species[s].heterozygosity);
        }
    }
}

/// 逐格余量之和 Σ max(0, capacity − occupants)。
///
/// 判据与 EnvironmentSystem 的收集判据逐字相同：缺 Position / CreaturePhenotype
/// / SpeciesTag、或引用未注册物种的实体都不占格，因此 occupants 与 build_sites
/// 读到的 `field.occupants` 是同一份数。
///
/// 它同时是「run_season 之后观察到的坐标 = build_sites 读到的那份」这条前提
/// 的落点：移动在季初跑完，子代则落在亲代格内（place_in 的抖动取 [0, 0.9)，
/// 加在整数格坐标上不跨格），因此 flush 之后看到的分布正是下一季的余量。
double total_headroom(gamev2::world::World& w)
{
    const auto& env = w.environment();
    std::vector<std::uint32_t> occupants(kCellCount, 0);
    w.entities().for_each_entity(
        [&w, &occupants](const EntityId e)
        {
            // get_if 不是 const 成员，因此这里不能用 const 引用接着 entities()。
            const auto* pos = w.entities().get_if<Position>(e);
            const auto* phe = w.entities().get_if<CreaturePhenotype>(e);
            const auto* tag = w.entities().get_if<SpeciesTag>(e);
            if (pos == nullptr || phe == nullptr || tag == nullptr
                || tag->species.value == 0
                || tag->species.value > w.species().species_count())
            {
                return;
            }
            ++occupants[CellIndex::of(pos->x, pos->y).value];
        });

    double sum = 0.0;
    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        sum += std::max(0.0f, env.resources().capacity(cell)
                                - static_cast<float>(occupants[c]));
    }
    return sum;
}

/// 跑三季，每季出生数都不得超过上一季末各格余量之和。
///
/// 被钉住的是 build_sites 的出生预算公式：它取逐格 `max(0, capacity -
/// occupants)` 乘份额再逐格 `lround`（src/environment/sites.cpp），且只在
/// `progeny > 0` 时发站点。于是单季
///
///     出生数 ≤ Σ lround(headroom_c) ≤ Σ headroom_c + kCellCount/2
///
/// 死亡数非负，于是 `季末规模 ≤ 季初规模 + Σheadroom + kCellCount/2`。
///
/// **为什么不能断言「季末规模 ≤ Σcapacity」。** 那条界（少一个 O(格数) 的余量）
/// 在没有移动时成立，那时初始分布均匀、格数不变；接入 MovementSystem 之后它
/// 不成立，而成立不了是有原因的：`headroom` 逐格取 `max(0, ...)`，于是**超载
/// 格贡献 0，而它多占的那些个体并不会去扣减别的格的余量**。个体被随机游走推进
/// 超载格、余量却留在空着的格里时，Σheadroom 就能显著超过 `Σcapacity − 种群`，
/// 总规模因此越过 Σcapacity。实测首季 Σheadroom = 27294 对
/// `Σcapacity − 种群` = 17586，季末 112778 对 Σcapacity = 103480。
/// 这不是 `build_sites` 违约——每一格都守住了自己的 headroom——而是「个体不能
/// 被强制迁走」这条设定在容量层面的后果。真正可断言的只有逐格那一条。
///
/// 铺到接近上限是前提。roomy_config 的 capacity_base = 40 把单格容量顶到
/// [10, 40]；铺得太稀时出生预算改由 parent_ceiling = lround(在场数 ×
/// offspring_scale) 决定（build_sites 取 eco_ceiling 与 parent_ceiling 的
/// 较小者），capacity 这条约束根本没被触到，断言恒真。铺到 0.85 之后
/// eco_ceiling 接手，headroom 归零才压得住出生——那正是要验的机制。
///
/// 取整余量只由逐格 lround 产生，量级是格数而与占位数无关，因此把 headroom
/// 误当成 capacity 的 mutant 必然越界：那样每格一季就多放出约 0.85 倍容量的
/// 子代。
TEST(population_never_exceeds_the_capacity_left_below_it)
{
    Fixture f;

    std::size_t seeded = 0;
    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        const auto n = static_cast<std::size_t>(
            f.world.environment().resources().capacity(cell) * 0.85f);
        const auto x = static_cast<float>(cell.value % kGridSide);
        const auto y = static_cast<float>(cell.value / kGridSide);
        for (std::size_t i = 0; i < n; ++i)
        {
            // 亚格坐标错开，免得整格的人压在同一个浮点坐标上。
            f.spawn(x + 0.1f + static_cast<float>(i % 60) * 0.01f, y + 0.5f);
            ++seeded;
        }
    }
    // 容量读回来全是 0 的话这轮什么也没铺，下面的断言会恒真。
    CHECK(seeded > 0);

    std::size_t previous = f.world.run_season().population;
    for (int round = 0; round < 3; ++round)
    {
        const auto headroom = static_cast<std::size_t>(total_headroom(f.world));
        const auto report = f.world.run_season();
        CHECK(report.population
              <= previous + headroom + kCellCount / 2 + 1);
        previous = report.population;
    }
}

/// 季号必须真的进入繁殖随机流，而不只是气候场。
///
/// 做法是把同一批初始个体放进两个处于不同季号的世界各结算一季，比对两边
/// 的子代基因组：
///
/// - A 先 seed 再跑一季，初始个体在季 0 结算。
/// - B 先在空世界上跑一季（无实体、无站点、不消耗任何随机流，只把季号推到
///   1），再 seed 逐位相同的个体，于是它们在季 1 结算。
///
/// 结算时刻两边仍有三处差异，必须逐条掐掉，否则「基因组不同」就分不清是季号
/// 还是别的东西在起作用：
///
/// - **站点结构**。build_sites 只读 occupants 与 capacity，而 capacity 在
///   EnvironmentWorld 构造时按 base_fertility 定死、不随季节变（src/environment/
///   resource.cpp），occupants 只由个体位置决定。两边的布局逐位相同 ⇒ 格集合
///   与每格 progeny 全同 ⇒ make_offspring 的调用次数全同。下面的 CHECK_EQ 顺带
///   把这层前提钉住：子代数相等正是「站点结构相同」的外部证据。
/// - **摄食存量**。两边的 stock 历史不同（A 的季 0 用的是初始存量，B 的季 1
///   之前空跑了一季只 regrow 不消耗），于是 feeding 不同。但 Metabolism = 0
///   让 burn <= 0，energy_survival 直接返回 1（src/genetics/survival.cpp），
///   feeding 进不了存活率，也就不影响谁活下来、谁进父池。
/// - **气候**。两边的季节相位不同，出生格的 Environment 快照因此不同。它只
///   决定子代身上那个组件与子代自己的 thermal 存活，不参与 make_offspring
///   的抽签。
///
/// 剩下唯一能改变子代基因组的就只有 RandomStreams{root_seed_, season_index_}。
/// 删掉 World::run_season 里的 breeding_.advance_season(season_) 之后两边都
/// 退回季 0 的流，子代基因组逐位相同，本测试立刻变红。
/// 季号流进繁殖随机流这条守卫，在 World 层已经**不可构造**，因此被删。
///
/// 它原先的形状是：把同一批初始个体放进两个处于不同季号的世界各结算一季，
/// 逐条掐掉站点结构、摄食存量、气候三处差异，剩下的自由变量就只有
/// `RandomStreams{root_seed_, season_index_}`。删掉 World::run_season 里的
/// breeding_.advance_season(season_) 之后两边都退回季 0 的流，测试立刻变红。
///
/// 接入 MovementSystem 之后这条路断了，而且断得没法绕：**季号现在同时决定
/// 两条流**。移动的随机流是 mix_seed(mix_seed(root_seed, season),
/// kMovementPurpose)，于是季 0 与季 1 的同一批个体落在不同坐标上 → 每格的
/// 在场数不同 → build_sites 的站点结构与每格 progeny 不同 → make_offspring
/// 的调用次数不同。子代数就此不等（实测两季分别产出不同数量），而子代基因组
/// 的差异于是有了「站点结构不同」这个与季号无关的解释——正是这条测试当初
/// 逐条掐掉的那一项。
///
/// 要恢复它，就得造出两个**移动流相同、繁殖流不同**的世界。而移动流是
/// `World::season_` 的函数、繁殖流也是，`run_season` 里两者来自同一个成员，
/// 因此任何 World 层的调用序列都无法只动其中一个。
///
/// **代价要说清楚：World::run_season 里 breeding_.advance_season(season_)
/// 这一行从今起没有任何测试守卫。** 机制本身仍在 adapt 层被覆盖
/// （test_genetics_systems.cpp::breeding_advance_season_replaces_the_season_index），
/// 没有覆盖的是 World 层这个调用点。要补回来只有两条路，都不便宜：让
/// World 暴露可控的季号注入点（那是给测试开的生产代码口子），或者把移动从
/// World 的默认装配里摘出去（已否决——移动是世界循环的一部分）。
/// 补法未定之前不要「顺手加回来」，它会恒红而不是恒绿。


/// 缺 Position 的幽灵个体既不被环境杀死，也永远不进父池——设计文档 §5
/// 契约表第二行。
///
/// 两个 system 对「谁参与繁殖」给出同一个答案，对「谁参与摄食」却不一样：
///
/// - EnvironmentSystem 的收集判据要求 Position（environment_systems.cpp 的
///   for_each_entity），缺它的实体不进 foragers，因此既不占存量，也不会出现在
///   靶向表 targets() 里。
/// - BreedingSystem 的收集判据只要求 Genome / CreaturePhenotype /
///   Environment / SpeciesTag，不要求 Position，因此幽灵照常进入存活抽签。
///
/// 不繁殖是显式挡下的。收集阶段确实把缺失的坐标压成了 Position{} 占位，
/// 挡住它的是空间分桶那一支的 has_position == 0：占位因此进不了任何半径判定，
/// 否则它会落进 (0, 0) 那一格、凭空成为那一格站点的亲代，而环境层只为有
/// 占位者的格产出站点。
///
/// 不死是数值上的必然。幽灵带的是默认快照（温度 0），表型 Metabolism = 0，
/// 于是 energy_survival 因 burn <= 0 直接返回 1，thermal 因 |0| 返回 1，
/// escape 因威胁恒为 0 返回 1，三者乘积恰为 1；而存活判据是
/// `uniform_unit(rng) < rate`，uniform_unit 取 24 位随机数、最大值
/// 1 - 2^-24，故恒真。AgingSystem 另把 age 逐季递增，而 Lifespan = 10，
/// 三季之内不过寿。
///
/// 「不繁殖」用差分断言守，而不是「总数在涨」那种下界断言：下界的方向与要验
/// 的机制正交——幽灵若真进了父池，出生数只会变多，`>=` 照常成立。差分把
/// 「幽灵自己那 5 条」与「它们对世界的影响」分开：幽灵被 EnvironmentSystem
/// 整个跳过，因此不占格、不消耗存量、不进 targets()，有幽灵那一侧里带
/// Position 的个体拿到的摄食与站点预算与对照侧逐位相同，两侧 population 的
/// 差就只该是幽灵本身。has_position 守卫一旦失效，幽灵挤进父池会改变抽签
/// 与子代预算，差值随即偏离 5——实测给幽灵补上 Position 后三季的差值是
/// 9 / 反向越界 / 9，三季全红。
///
/// 差分之所以逐季恒为 5，靠的是两条**本夹具特有**的性质，都已实测：
///
/// - 出生预算只取决于逐格在场数（build_sites 读 field.occupants），幽灵不占
///   任何格，两侧子代数因此逐季相同：实测对照侧新生儿 128 / 346 / 338。
/// - 每个个体的 survival_rate 都落在 {0, 1}：亲代与幽灵是 1（Metabolism = 0
///   加默认快照温度 0），子代是 0（子代的 Environment 采真实气候，
///   thermal_survival 饱和到 0，实测没有子代跨季）。存活判定于是与「抽到
///   哪个随机数」无关，幽灵多消耗 5 次抽样也改变不了任何一条存活判定。
///
/// 唯一被幽灵多抽的是 Survival 一条子流：RandomStreams 给每个用途一条独立
/// 引擎（random.hpp），Survival 只被 population.cpp 消费，而 Mating /
/// Crossover / Jitter / AlleleSwitch 只被 make_offspring 消费。产生子代的
/// 那四条因此逐位对齐，子代基因组逐位相同——实测两侧同季的
/// `heterozygosity × n × kLocusCount`（逐位点 heterozygous 计数之和）都是 198。
///
/// 两侧 heterozygosity 逐季不同**不是**错位的证据，而是口径差异：幽灵带的
/// 是 Genome{}，逐位点都是纯合子，它照样按 Genome + SpeciesTag 进统计桶，
/// 于是 `stats.h_observed = heterozygous / n`（statistics.cpp:84）的分母多 5、
/// 分子不变。实测分母 192 / 197 之比，正是 heterozygosity 之比的倒数。
///
/// 把这段写在这里，是为了让后来者别拿「两个 Fixture 的统计摘要逐位相同」
/// 当作前提：population 逐季只差 5，但 heterozygosity 必然不同。
///
/// 两个 Fixture 必须走完全相同的调用序列（构造、seed(32)、逐季 run_season），
/// 否则连「同样多的有 Position 个体」这个前提都不成立。
TEST(creatures_without_position_never_die_and_never_breed)
{
    Fixture control;
    Fixture with_ghosts;
    control.seed(32);
    with_ghosts.seed(32);

    std::vector<EntityId> ghosts;
    for (std::size_t i = 0; i < 5; ++i)
    {
        ghosts.push_back(with_ghosts.spawn_without_position());
    }

    auto count_ghosts = [&]
    {
        std::size_t n = 0;
        with_ghosts.world.entities().for_each_entity(
            [&n, &ghosts](const EntityId e)
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

    for (std::uint32_t season = 0; season < 3; ++season)
    {
        const auto a = control.world.run_season();
        const auto b = with_ghosts.world.run_season();
        CHECK_EQ(b.population, a.population + ghosts.size());
    }

    // 「不死」逐个查句柄，不经「总数不变」间接推。
    CHECK_EQ(count_ghosts(), ghosts_before);
}

/// 近交衰退必须真的进到每季的存活判定里，否则 genetics 层那两条测试照样
/// 全绿，而整条机制在世界层是断的。
///
/// 两个世界只差 inbreeding_load，其余逐位相同（含 root_seed 与随机流）。
///
/// **布局是「成对但彼此隔离」**：每两个个体挨在相邻两格（必定落进对方的
/// mating_radius 邻域，构成一个 size = 2 的亲代池），而各对之间隔 10 格，
/// 加上 MovementSystem 每季最多挪 2 格，两季内都凑不���第三个。size = 2 且
/// 适应力全为 1 时 Σw² = 1/2，于是 load = 4 把惩罚压到 0。
///
/// 不隔离成对的话两个池会并成更大的一个，Σw² 掉到 1/4，惩罚只剩 0——
/// 那就恒不红。也不必让池真的只剩一个：那种情况下 make_offspring 直接失败、
/// 一个子代都产不出来，近交系数压根不会被更新。
TEST(inbreeding_reaches_the_survival_lottery)
{
    LocusTable table = neutral_table();
    EnvironmentConfig env = roomy_config();

    GeneticsConfig plain = steady_genetics();
    plain.inbreeding_load = 0.0f;
    GeneticsConfig loaded = plain;
    loaded.inbreeding_load = 4.0f;

    gamev2::world::World control{table, plain, env, 77};
    gamev2::world::World inbred{table, loaded, env, 77};

    const auto control_id = control.species().register_species(all_loci());
    const auto inbred_id = inbred.species().register_species(all_loci());
    CHECK(control_id.has_value());
    CHECK(inbred_id.has_value());

    const auto scatter = [](gamev2::world::World& w, const SpeciesId sid)
    {
        auto& entities = w.entities();
        for (std::size_t k = 0; k < 4; ++k)
        {
            const auto base = static_cast<float>(k * 10);
            for (std::size_t m = 0; m < 2; ++m)
            {
                const auto id = entities.create();
                (void)entities.add<SpeciesTag>(id, SpeciesTag{sid});
                (void)entities.add<Genome>(id, Genome{});
                (void)entities.add<Environment>(id, Environment{});
                (void)entities.add<CreaturePhenotype>(
                    id, CreaturePhenotype{creature_phenotype()});
                (void)entities.add<Vitality>(id, Vitality{});
                (void)entities.add<Position>(
                    id, Position{base + static_cast<float>(m) + 0.5f, 0.5f});
            }
        }
    };
    scatter(control, *control_id);
    scatter(inbred, *inbred_id);

    // 季 0 建立亲代池并把 Σw² = 0.5 存下来；季 1 才吃得到它。
    (void)control.run_season();
    (void)inbred.run_season();
    const auto control_alive = control.run_season().population;
    const auto inbred_alive = inbred.run_season().population;

    CHECK(control_alive > 0);
    CHECK_EQ(inbred_alive, std::size_t{0});
}

/// 近交系数必须**逐物种**存取：整份覆盖会让排在后面的物种抹掉前面的读数，
/// 于是下一季所有物种都吃最后一个物种的值。
///
/// 判据是**两个物种下场不同**：
///
/// - 物种 1 排成「相邻两格一对、各对隔 10 格」，每个 3×3 邻域恰好装下一对，
///   亲代池 size = 2 ⇒ Σw² = 1/2。
/// - 物种 2 铺成 9×9 的密块（每格一个），邻域里 9 个 ⇒ Σw² = 1/9。
///
/// load = 4 时物种 1 的惩罚是 clamp(1 − 2, 0, 1) = 0（全灭），物种 2 是
/// clamp(1 − 4/9) = 0.556（活）。若两者共用一个读数，它们会**同生共死**——
/// 这正是整份覆盖的可观察后果，因此这条断言对 mutant 有鉴别力。
///
/// 两个物种刻意分处地图两端：build_sites 按 (格, 物种) 分组，BreedingSystem
/// 的桶也只收同物种成员，所以它们的亲代池不会互相污染。
TEST(inbreeding_is_kept_per_species)
{
    LocusTable table = neutral_table();
    EnvironmentConfig env = roomy_config();
    GeneticsConfig config = steady_genetics();
    config.inbreeding_load = 4.0f;

    gamev2::world::World world{table, config, env, 77};
    const auto first = world.species().register_species(all_loci());
    const auto second = world.species().register_species(all_loci());
    CHECK(first.has_value());
    CHECK(second.has_value());

    auto& entities = world.entities();
    const auto put = [&entities](const SpeciesId sid, const float x,
                                const float y)
    {
        const auto id = entities.create();
        (void)entities.add<SpeciesTag>(id, SpeciesTag{sid});
        (void)entities.add<Genome>(id, Genome{});
        (void)entities.add<Environment>(id, Environment{});
        (void)entities.add<CreaturePhenotype>(
            id, CreaturePhenotype{creature_phenotype()});
        (void)entities.add<Vitality>(id, Vitality{});
        (void)entities.add<Position>(id, Position{x, y});
    };

    // 物种 1：四对，彼此隔 10 格。
    for (std::size_t k = 0; k < 4; ++k)
    {
        for (std::size_t m = 0; m < 2; ++m)
        {
            put(*first, static_cast<float>(k * 10) + static_cast<float>(m) + 0.5f,
                0.5f);
        }
    }
    // 物种 2：9×9 密块，x/y 各取 50..58。
    for (float x = 50.0f; x < 59.0f; x += 1.0f)
    {
        for (float y = 50.0f; y < 59.0f; y += 1.0f)
        {
            put(*second, x + 0.5f, y + 0.5f);
        }
    }

    // 季 0 建立亲代池并写下各自的 Σw²；季 1 才吃得到。
    (void)world.run_season();
    const auto report = world.run_season();

    CHECK_EQ(report.species.size(), std::size_t{2});
    CHECK_EQ(report.species[0].census, std::uint32_t{0});
    CHECK(report.species[1].census > 0);
}

/// `mating_radius_cells` 变宽 → 亲代池变大 → Σw² 变小 → 近交惩罚变轻。
///
/// 这条守的是**配置耦合**，不是逻辑不变量：`inbreeding_load` 的阈值随它漂，
/// 因为 `Σw²` 的基线就是 1/N，而 N 直接由邻域大小决定。改动邻域半径而不重调
/// 系数，原先「0.5 与 0.7 之间干净分岔」的结论就不再成立。
///
/// 布局是 9×9 的密块、每格一个体，于是邻域里装下的亲代数恰好等于邻域格数：
/// radius 1 是 3×3 = 9 个 ⇒ Σw² = 1/9；radius 4 是 9×9 = 81 个 ⇒ 1/81。
/// load 取 4，惩罚分别是 clamp(1 − 4/9) = 0.556 与 clamp(1 − 4/81) = 0.951。
///
/// 比的是**存活率**而不是最终种群：存活率是 SurvivalInput::inbreeding 那一项
/// 直接写进 Vitality 的读数，同一世界里所有个体共享同一个 F，因此它的差就是
/// 惩罚的差，不掺种群动态的任何其它因素。季 0 还没有惩罚（近交系数当季才写下），
/// 所以必须跑到季 1 才量得到。
TEST(mating_radius_widens_the_pool_and_relieves_inbreeding)
{
    LocusTable table = neutral_table();
    GeneticsConfig genetics = steady_genetics();
    genetics.inbreeding_load = 4.0f;

    EnvironmentConfig narrow_env = roomy_config();
    narrow_env.mating_radius_cells = 1.0f;
    EnvironmentConfig wide_env = narrow_env;
    wide_env.mating_radius_cells = 4.0f;

    gamev2::world::World narrow{table, genetics, narrow_env, 77};
    gamev2::world::World wide{table, genetics, wide_env, 77};

    const auto narrow_id = narrow.species().register_species(all_loci());
    const auto wide_id = wide.species().register_species(all_loci());
    CHECK(narrow_id.has_value());
    CHECK(wide_id.has_value());

    const auto fill = [](gamev2::world::World& w, const SpeciesId sid)
    {
        auto& entities = w.entities();
        for (int x = 0; x < 9; ++x)
        {
            for (int y = 0; y < 9; ++y)
            {
                const auto id = entities.create();
                (void)entities.add<SpeciesTag>(id, SpeciesTag{sid});
                (void)entities.add<Genome>(id, Genome{});
                (void)entities.add<Environment>(id, Environment{});
                (void)entities.add<CreaturePhenotype>(
                    id, CreaturePhenotype{creature_phenotype()});
                (void)entities.add<Vitality>(id, Vitality{});
                (void)entities.add<Position>(
                    id, Position{static_cast<float>(x) + 0.5f,
                                 static_cast<float>(y) + 0.5f});
            }
        }
    };
    fill(narrow, *narrow_id);
    fill(wide, *wide_id);

    (void)narrow.run_season();
    (void)wide.run_season();
    const auto narrow_alive = narrow.run_season().population;
    const auto wide_alive = wide.run_season().population;

    // 存活率是 SurvivalInput::inbreeding 那一项直接写进 Vitality 的读数。
    const auto mean_survival = [](gamev2::world::World& w)
    {
        double sum = 0.0;
        std::size_t n = 0;
        w.entities().for_each_entity(
            [&w, &sum, &n](const EntityId e)
            {
                sum += w.entities().get_if<Vitality>(e)->survival_rate;
                ++n;
            });
        return n > 0 ? sum / static_cast<double>(n) : 0.0;
    };

    // 两个世界都得还有活人才量得到读数；全灭时上面的存活率是 0，断言仍分得开。
    CHECK(narrow_alive > 0);
    CHECK(wide_alive > 0);
    CHECK(mean_survival(wide) > mean_survival(narrow));
    CHECK(wide_alive > narrow_alive);
}

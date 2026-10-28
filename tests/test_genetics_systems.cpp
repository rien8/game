#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/diagnostic.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace gamev2::ecs;
using namespace gamev2::ecs::adapt;
using namespace gamev2::genetics;

namespace {

constexpr std::size_t kSeasons = 32;

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

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

GeneticsConfig default_config()
{
    GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    return c;
}

/// 子代表型由 trait_base 逐项决定（见 adapt_breeding_multi_season 那条），
/// Metabolism 归零与 Speed 取 1 使 survival_rate 恒为 1，Fecundity 取 0.05
/// 则把每季规模倍率钉成确定的 1.05。
GeneticsConfig steady_config()
{
    GeneticsConfig c = default_config();
    c.trait_base[trait_index(TraitKind::Metabolism)] = 0.0f;
    c.trait_base[trait_index(TraitKind::Fecundity)] = 0.05f;
    return c;
}

void set_trait(CreaturePhenotype& p, TraitKind t, float value)
{
    p.phenotype.traits[trait_index(t)] = value;
}

/// 只在本文件用到的组件。给实体补一个它没有的组件，会把它迁往新的
/// archetype，原 archetype 的尾行随后被搬进它腾出的空洞。
struct MigrationProbe
{
    std::uint64_t stamp = 0;
};

struct Harness
{
    LocusTable table{neutral_table()};
    GeneticsConfig config{default_config()};
    SpeciesRegistry species;
    SpeciesId sid{};

    Harness()
    {
        sid = *species.register_species(all_loci());
    }

    EntityId spawn(World& w)
    {
        const EntityId e = w.create();
        (void)w.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)w.add<CreaturePhenotype>(e, CreaturePhenotype{});
        (void)w.add<Environment>(e, Environment{});
        (void)w.add<Vitality>(e, Vitality{});
        (void)w.add<Genome>(e, Genome{});
        return e;
    }
};

/// 按槽位序收集「不是 founders 的实体」的基因组。子代由 cmd.create 在帧末按
/// born 的顺序创建，槽位序即出生序，因此两次结算可以直接逐位对照。
std::vector<Genome> offspring_genomes(World& w,
                                      const std::vector<EntityId>& founders)
{
    std::vector<Genome> out;
    w.for_each_entity([&](const EntityId e)
    {
        if (std::ranges::find(founders, e) != founders.end())
        {
            return;
        }
        const auto* g = w.get_if<Genome>(e);
        if (g != nullptr)
        {
            out.push_back(*g);
        }
    });
    return out;
}

}  // namespace

TEST(adapt_genome_survives_row_migration)
{
    // Genome 有 1200 字节。本文件能编译就说明它的对齐落在列存储的
    // operator new 对齐之内；再走一次行迁移，验证迁移用的移动构造在这个
    // 尺寸上确实把数据带了过去。
    World w;
    Harness h;
    const EntityId first = h.spawn(w);
    const EntityId second = h.spawn(w);

    auto* g = w.get_if<Genome>(second);
    CHECK(g != nullptr);
    g->a[0].allele = 2;
    g->b[kLocusCount - 1].allele = 3;

    // first 迁往新 archetype 后空出自己那行，second 从尾行被搬进这个空洞
    CHECK(w.add<MigrationProbe>(first, MigrationProbe{7}).has_value());

    CHECK(w.is_alive(second));
    const auto* moved = w.get_if<Genome>(second);
    CHECK(moved != nullptr);
    CHECK_EQ(static_cast<int>(moved->a[0].allele), 2);
    CHECK_EQ(static_cast<int>(moved->b[kLocusCount - 1].allele), 3);
}

TEST(adapt_aging_kills_expired_creatures)
{
    World w;
    Harness h;
    const EntityId e = h.spawn(w);

    auto* v = w.get_if<Vitality>(e);
    CHECK(v != nullptr);
    v->age_seasons = 0;

    auto* p = w.get_if<CreaturePhenotype>(e);
    CHECK(p != nullptr);
    set_trait(*p, TraitKind::Lifespan, 0.0f);

    CommandBuffer cmd;
    AgingSystem{}.execute(w, cmd);
    cmd.flush(w);
    CHECK(!w.is_alive(e));
}

TEST(adapt_aging_keeps_living_creatures)
{
    World w;
    Harness h;
    const EntityId e = h.spawn(w);

    auto* p = w.get_if<CreaturePhenotype>(e);
    set_trait(*p, TraitKind::Lifespan, 100.0f);
    auto* v = w.get_if<Vitality>(e);
    v->age_seasons = 3;

    CommandBuffer cmd;
    AgingSystem{}.execute(w, cmd);
    cmd.flush(w);
    CHECK(w.is_alive(e));
    CHECK_EQ(w.get_if<Vitality>(e)->age_seasons, std::uint32_t{4});
}

TEST(adapt_breeding_keeps_survivors_and_adds_offspring_in_fecundity_proportion)
{
    World w;
    Harness h;
    const EntityId a = h.spawn(w);
    const EntityId b = h.spawn(w);
    const EntityId c = h.spawn(w);

    // 表型取成 survival_rate 恰好为 1：Metabolism 归零使能量收支的
    // 分母一并归零而取 1，Speed 取 1 使逃脱余量取满，环境温度偏移为 0
    // 使热胁迫取 1。三个因子相乘为 1，本季抽签必然全部存活，断言不再
    // 依赖种子。SenseRange 留 0，因此 intake 恒 0、Fecundity 改多少都
    // 不影响存活率。
    for (const EntityId e : {a, b, c})
    {
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
    }
    // 三个个体的繁殖力刻意不等：季节适应力是 survival × Fecundity /
    // max_fecundity，1 号拿 1.0 而 0、2 号各拿 0.5。全等时三者都是满值，
    // Vitality::fitness 的断言就成了对默认值的自我确认。
    w.get_if<CreaturePhenotype>(a)->phenotype.traits[trait_index(
        TraitKind::Fecundity)] = 0.5f;
    w.get_if<CreaturePhenotype>(b)->phenotype.traits[trait_index(
        TraitKind::Fecundity)] = 0.5f;
    set_trait(*w.get_if<CreaturePhenotype>(c), TraitKind::Fecundity, 1.0f);

    BreedingSystem sys{h.table, h.species, h.config, 7, 1};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 存活性先钉住，否则下面读 Vitality 拿到的可能是空指针：销毁分支接反
    // （把存活者销毁）会让三个句柄当场失效，解引用就是崩而不是失败报告。
    CHECK(w.is_alive(a));
    CHECK(w.is_alive(b));
    CHECK(w.is_alive(c));
    // 子代数 = round(个体数 × 平均繁殖力) = round(3 × 2/3) = 2。
    // 「一个物种一个子代」的旧行为只会得到 1。
    CHECK_EQ(w.entity_count(), std::size_t{5});
    const auto* va = w.get_if<Vitality>(a);
    const auto* vb = w.get_if<Vitality>(b);
    const auto* vc = w.get_if<Vitality>(c);
    CHECK(va != nullptr && vb != nullptr && vc != nullptr);
    if (va != nullptr && vb != nullptr && vc != nullptr)
    {
        CHECK(va->fitness == 0.5f);
        CHECK(vb->fitness == 0.5f);
        CHECK(vc->fitness == 1.0f);
    }

    std::vector<EntityId> all;
    w.for_each_entity([&](EntityId e) { all.push_back(e); });
    std::vector<EntityId> children;
    for (const EntityId e : all)
    {
        if (e != a && e != b && e != c)
        {
            children.push_back(e);
        }
    }
    CHECK_EQ(children.size(), std::size_t{2});
    for (const EntityId child : children)
    {
        const auto* tag = w.get_if<SpeciesTag>(child);
        const auto* v = w.get_if<Vitality>(child);
        CHECK(tag != nullptr);
        CHECK(v != nullptr);
        CHECK(tag != nullptr && tag->species.value == h.sid.value);
        CHECK(v != nullptr && v->age_seasons == 0);
        CHECK(w.has<Genome>(child));
        CHECK(w.has<CreaturePhenotype>(child));
        CHECK(w.has<Environment>(child));
    }
}

TEST(adapt_breeding_writes_survival_rate_into_vitality)
{
    // Vitality::survival_rate 曾经恒为默认的 1.0：BreedingSystem 只写
    // fitness。取值刻意压到 0.99（Speed 0.99；Metabolism 归零使能量收支取满、
    // Environment 温度偏移 0 使热胁迫取满，三者相乘即 0.99），这样「写进去
    // 了」与「还是默认值 1.0」才分得开——全取 1 的表型只能自我确认默认值。
    //
    // 断言写成「每个存活个体的 survival_rate 都等于 0.99」而不是钉死抽签
    // 结果：0.99 的死亡率下四个个体全灭的概率是 1e-8；反过来，实现一旦漏写，
    // 只要有任何一个个体活下来就必然暴露。
    World w;
    Harness h;
    std::vector<EntityId> founders;
    for (int i = 0; i < 4; ++i)
    {
        const EntityId e = h.spawn(w);
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 0.99f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 1.0f);
        founders.push_back(e);
    }

    BreedingSystem sys{h.table, h.species, h.config, 7, 1};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    std::size_t alive = 0;
    for (const EntityId e : founders)
    {
        if (!w.is_alive(e))
        {
            continue;
        }
        ++alive;
        const auto* v = w.get_if<Vitality>(e);
        CHECK(v != nullptr);
        if (v != nullptr)
        {
            CHECK(v->survival_rate == 0.99f);
        }
    }
    CHECK(alive > 0);
}

TEST(adapt_aging_and_breeding_never_share_a_scheduler_batch)
{
    // BreedingSystem 自 settle 抄回 season_fitness / survival_rates 起也写
    // Vitality，与 AgingSystem 撞上同一个写集，Scheduler 的冲突检测必然把
    // 二者分进不同批次。这条用例保护的是「不得并行」这一半——批次之间的
    // 先后不是 WriteSet 能表达的声明，只能写进 doxygen 并由调用点负责
    // （见 genetics_systems.hpp 里 BreedingSystem 的说明）。
    Harness h;
    AgingSystem aging;
    BreedingSystem breeding{h.table, h.species, h.config, 7, 1};
    Scheduler sched{0};
    sched.add(aging);
    sched.add(breeding);
    sched.plan();

    CHECK_EQ(sched.batch_count(), std::size_t{2});
}

TEST(adapt_breeding_skips_entities_whose_species_is_not_registered)
{
    World w;
    Harness h;
    const EntityId stray = h.spawn(w);
    // SpeciesTag 默认构造出来的 value 是 0，而物种 id 从 1 起。
    w.get_if<SpeciesTag>(stray)->species = SpeciesId{0};

    std::vector<std::string_view> codes;
    w.set_diagnostic_handler(
        [&codes](const Diagnostic& d) { codes.push_back(d.code); });

    BreedingSystem sys{h.table, h.species, h.config, 7, 1};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 统计 system 对同一个 id 是「跳过」，繁殖 system 也必须跳过：留在这
    // 里的实体 survived 恒为 0，若照常提交销毁就成了静默吃实体。
    CHECK(w.is_alive(stray));
    CHECK_EQ(codes.size(), std::size_t{1});
    CHECK_EQ(codes[0], std::string_view{"GENETICS_UNKNOWN_SPECIES"});

    // 两个 system 对非法 id 必须给出同一个答案，否则面板上的普查数与
    // 实际存活的实体数会悄悄分叉。
    PopulationStatisticsSystem stats{h.species};
    CommandBuffer stat_cmd;
    stats.execute(w, stat_cmd);
    stat_cmd.flush(w);
    CHECK_EQ(stats.statistics().size(), std::size_t{1});
    CHECK_EQ(stats.statistics()[0].census, std::uint32_t{0});
}

TEST(adapt_breeding_multi_season_keeps_population_and_isolates_species)
{
    World w;
    Harness h;
    const SpeciesId other = *h.species.register_species(all_loci());

    // 子代表型完全由 trait_base 决定：neutral_table 的位点全部指向
    // BodySize 且加性为 0，express 原样返回 trait_base，于是亲代与子代
    // 逐项同值。Metabolism 归零 + Speed 取 1 让 survival_rate 恒为 1，
    // 存活抽签必然全中；Fecundity 取 0.05 则把每季规模倍率钉成确定的
    // 1.05，32 季约 4.76 倍——所有断言都不依赖种子。
    const GeneticsConfig config = steady_config();

    const auto spawn = [&](SpeciesId sid)
    {
        const EntityId e = w.create();
        (void)w.add<SpeciesTag>(e, SpeciesTag{sid});
        CreaturePhenotype cp{};
        cp.phenotype.traits = config.trait_base;
        (void)w.add<CreaturePhenotype>(e, cp);
        (void)w.add<Environment>(e, Environment{});
        (void)w.add<Vitality>(e, Vitality{});
        (void)w.add<Genome>(e, Genome{});
        return e;
    };

    constexpr std::size_t kAlpha = 100;
    constexpr std::size_t kBeta = 20;
    for (std::size_t i = 0; i < kAlpha; ++i)
    {
        (void)spawn(h.sid);
    }
    for (std::size_t i = 0; i < kBeta; ++i)
    {
        (void)spawn(other);
    }

    // 一个确定性判死的个体：Lifespan 归零时 outlived(p, 0) 恒真，与抽签无关。
    // 它是本条用例里「销毁分支被真正走到」的凭证——三个个体全部存活的
    // fixture 只能钉住极性（销毁存活者会被抓），钉不住必要性。
    const EntityId doomed = spawn(h.sid);
    w.get_if<CreaturePhenotype>(doomed)->phenotype.traits[trait_index(
        TraitKind::Lifespan)] = 0.0f;

    for (std::uint64_t season = 1; season <= kSeasons; ++season)
    {
        BreedingSystem sys{h.table, h.species, config, 7, season};
        CommandBuffer cmd;
        sys.execute(w, cmd);
        cmd.flush(w);
    }

    CHECK(!w.is_alive(doomed));

    std::size_t alpha = 0;
    std::size_t beta = 0;
    std::size_t tagged = 0;
    w.for_each_entity([&](EntityId e)
    {
        const auto* tag = w.get_if<SpeciesTag>(e);
        if (tag == nullptr)
        {
            return;
        }
        ++tagged;
        if (tag->species.value == h.sid.value)
        {
            ++alpha;
        }
        else if (tag->species.value == other.value)
        {
            ++beta;
        }
    });
    // 每个带标签的个体都必须是这两个物种之一：子代不会凭空长出第三个物种。
    CHECK_EQ(tagged, w.entity_count());
    // 老行为（每池每季一个子代）在这 32 季里只能攒到 kAlpha + 32 = 132，
    // 远够不到 3 倍；漏掉上限或把存活数再乘一次则是 2^k 量级。
    CHECK(alpha > 3 * kAlpha);
    CHECK(alpha < 8 * kAlpha);
    // 两个物种各自按自己的规模走同一条曲线，互不串种。
    CHECK(beta > 3 * kBeta);
    CHECK(beta < 8 * kBeta);
}

TEST(adapt_command_buffer_create_reports_new_entity)
{
    World w;
    EntityId e{};
    CommandBuffer cmd;
    cmd.create(e);
    CHECK(!static_cast<bool>(e));
    cmd.flush(w);
    CHECK(static_cast<bool>(e));
    CHECK(w.is_alive(e));
}

TEST(adapt_population_statistics_reports_per_species)
{
    World w;
    Harness h;
    h.spawn(w);
    h.spawn(w);
    h.spawn(w);

    PopulationStatisticsSystem sys{h.species};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    CHECK_EQ(sys.statistics().size(), std::size_t{1});
    CHECK_EQ(sys.statistics()[0].census, std::uint32_t{3});
    CHECK_EQ(sys.statistics()[0].loci.size(), kLocusCount);
}

TEST(adapt_breeding_uses_environment_targets_when_provided)
{
    World w;
    Harness h;
    std::vector<EntityId> founders;
    for (int i = 0; i < 12; ++i)
    {
        const auto e = h.spawn(w);
        founders.push_back(e);
        (void)w.add<Position>(e, Position{static_cast<float>(i % 4),
                                          static_cast<float>(i / 4)});
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.05f);
    }

    EnvironmentWorld env{31, gamev2::environment::EnvironmentConfig{}};
    EnvironmentSystem environment{env, h.species, 0};

    CommandBuffer env_cmd;
    environment.execute(w, env_cmd);
    env_cmd.flush(w);

    BreedingSystem sys{h.table, h.species, h.config, 7, 0,
                       &environment.targets(), 1.0f};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 环境侧至少给出了一个站点，且预算被承载力封顶后仍有出生。
    CHECK(!environment.targets().empty());
    CHECK(w.entity_count() > 12);

    // 每个子代都必须带着 Position 出生。缺了它，下一季的 EnvironmentSystem
    // 在第一次 for_each_entity 里就会把它跳过——不出站点、不写 Feeding、
    // 不占格，于是没有人再给它繁殖预算：整条链在第一季之后就静默断了。
    // 12 个创始个体各有实体可查，这里按「不是创始个体」筛出子代。
    std::vector<EntityId> newborns;
    w.for_each_entity([&](const EntityId e)
    {
        const auto born_after = std::ranges::none_of(
            founders, [e](const EntityId f) { return f == e; });
        if (born_after)
        {
            newborns.push_back(e);
        }
    });
    CHECK(!newborns.empty());
    if (newborns.empty())
    {
        return;
    }
    const auto side = static_cast<float>(gamev2::environment::kGridSide);
    for (const EntityId e : newborns)
    {
        const auto* pos = w.get_if<Position>(e);
        CHECK(pos != nullptr);
        if (pos == nullptr)
        {
            continue;
        }
        CHECK(pos->x >= 0.0f && pos->x < side);
        CHECK(pos->y >= 0.0f && pos->y < side);
    }

    // 上面那两条对「落在哪一格」毫无约束力：未接入时的兜底是
    // place_in(CellIndex{}, i)，落点 x, y ∈ [0, 0.9)，同样在 [0, 64) 内，
    // 把 birth_cells 整个丢掉、恒定落格 0 也照样全过。下面两条才是钉住
    // Step 8 的那一条语义。
    std::vector<std::uint16_t> published;
    for (const auto& target : environment.targets())
    {
        published.push_back(target.cell.value);
    }
    CHECK(!published.empty());
    if (published.empty())
    {
        return;
    }
    // 条件二：至少有一个子代不落在 (0, 0)。恒落格 0 的 mutant 死在这里。
    // 创始个体铺在 (i%4, i/4)，覆盖 12 个格，站点不只在 (0, 0)。
    std::size_t away_from_origin = 0;
    for (const EntityId e : newborns)
    {
        const auto* pos = w.get_if<Position>(e);
        if (pos == nullptr)
        {
            continue;
        }
        const auto cell = gamev2::environment::CellIndex::of(pos->x, pos->y);
        // 条件一：子代必须落在环境侧发过站点的格子里。
        CHECK(std::ranges::find(published, cell.value) != published.end());
        if (cell.value != 0)
        {
            ++away_from_origin;
        }
    }
    CHECK(away_from_origin > 0);
}

TEST(adapt_breeding_keeps_positionless_entities_out_of_every_bucket)
{
    // 缺 Position 的个体既不出站点（EnvironmentSystem 整趟跳过它），也不该进
    // 任何父池：两个 system 对「谁参与繁殖」必须给出同一个答案，设计文档 §8.1
    // 也是这么写的。
    //
    // 夹具把带坐标的个体摆在 (0, 0) 那一格：把缺失压成 Position{} 的写法恰好
    // 落进同一格，因此这里复现的正是缺陷本身，换任何别的格子都复现不出来。
    // 容量抬到 40（同 test_environment_sites 的做法）保证该格 headroom 恒 >= 8，
    // 站点必发——于是「零子代」只可能来自桶被剔空，不会是因为整条链没跑起来。
    gamev2::environment::EnvironmentConfig config{};
    config.capacity_base = 40.0f;

    // placed 个个体带 Position，第 placed + 1 个个体身上没有。返回本季子代数。
    const auto offspring_count = [&](const std::size_t placed)
    {
        World w;
        Harness h;
        std::vector<EntityId> founders;

        for (std::size_t i = 0; i < placed + 1; ++i)
        {
            const auto e = h.spawn(w);
            founders.push_back(e);
            if (i < placed)
            {
                (void)w.add<Position>(e, Position{0.5f, 0.6f});
            }
            // burn = Metabolism 0.12 × BodySize 1 × (1 + Speed 1) = 0.24，
            // intake = SenseRange 1 × Fecundity 2 × feeding（富格下 1）= 2，
            // 比值被 clamp 取 1 ⇒ survival_rate 恒为 1，个体逐个必然存活，
            // 判定与抽签无关——夹具因此是确定的。
            auto* p = w.get_if<CreaturePhenotype>(e);
            set_trait(*p, TraitKind::SenseRange, 1.0f);
            set_trait(*p, TraitKind::Fecundity, 2.0f);
            set_trait(*p, TraitKind::Metabolism, 0.12f);
            set_trait(*p, TraitKind::BodySize, 1.0f);
            set_trait(*p, TraitKind::Speed, 1.0f);
            set_trait(*p, TraitKind::Lifespan, 5.0f);
        }

        EnvironmentWorld env{31, config};
        EnvironmentSystem environment{env, h.species, 0};
        CommandBuffer env_cmd;
        environment.execute(w, env_cmd);
        env_cmd.flush(w);

        BreedingSystem sys{h.table, h.species, h.config, 7, 0,
                           &environment.targets(), 1.0f};
        CommandBuffer cmd;
        sys.execute(w, cmd);
        cmd.flush(w);

        // 站点确实发得出来：EnvironmentSystem 看得见带 Position 的个体，
        // 且该格有余量。不成立时下面两条断言都会失去意义，所以直接报失败。
        CHECK(!environment.targets().empty());
        return offspring_genomes(w, founders).size();
    };

    // 对照组：两个带 Position 的个体同在半径内，桶凑够亲代，站点产子代。
    // 先钉住夹具有出生能力，下面那条的零子代才不是恒真。
    CHECK(offspring_count(2) > 0);
    // 第三个个体缺 Position。它没被计入 (0, 0) 格站点的父池，因此那个桶里
    // 只剩一个亲代，被 bucket.size() < 2 剔掉——它既不产子代，也不被算作
    // 任何站点的亲代。压成 Position{} 的写法会让它进桶并凭空产出一窝子代。
    CHECK_EQ(offspring_count(1), std::size_t{0});
}

TEST(adapt_breeding_keeps_outlived_parents_out_of_the_pool)
{
    // 寿尽是硬判定：AgingSystem 的 destroy 经 CommandBuffer 排队到帧末，
    // BreedingSystem 在同一帧里仍看得见那个个体。不把他排出父池，本季已
    // 寿尽的亲代就会继续繁殖——这正是「先跑 Aging 再繁殖」要避免的事。
    gamev2::environment::EnvironmentConfig config{};
    config.capacity_base = 40.0f;

    const auto offspring_count = [&](const bool expire_one,
                                     const bool use_targets)
    {
        World w;
        Harness h;
        std::vector<EntityId> founders;
        for (int i = 0; i < 2; ++i)
        {
            const auto e = h.spawn(w);
            founders.push_back(e);
            (void)w.add<Position>(e, Position{0.5f, 0.6f});
            auto* p = w.get_if<CreaturePhenotype>(e);
            set_trait(*p, TraitKind::SenseRange, 1.0f);
            set_trait(*p, TraitKind::Fecundity, 2.0f);
            set_trait(*p, TraitKind::Metabolism, 0.12f);
            set_trait(*p, TraitKind::BodySize, 1.0f);
            set_trait(*p, TraitKind::Speed, 1.0f);
            set_trait(*p, TraitKind::Lifespan, 5.0f);
        }
        if (expire_one)
        {
            // 与 outlived(phenotype, age) 逐字同一判据：age >= Lifespan。
            w.get_if<Vitality>(founders[1])->age_seasons = 5;
        }

        EnvironmentWorld env{31, config};
        EnvironmentSystem environment{env, h.species, 0};
        CommandBuffer env_cmd;
        environment.execute(w, env_cmd);
        env_cmd.flush(w);
        CHECK(!environment.targets().empty());

        CommandBuffer cmd;
        if (use_targets)
        {
            BreedingSystem sys{h.table, h.species, h.config, 7, 0,
                               &environment.targets(), 1.0f};
            sys.execute(w, cmd);
            // create 把 newborn_ 元素的地址交给了队列：system 必须先于
            // flush 活着，否则 flush 写的是已经释放的 vector 缓冲区。
            cmd.flush(w);
        }
        else
        {
            BreedingSystem sys{h.table, h.species, h.config, 7, 0};
            sys.execute(w, cmd);
            cmd.flush(w);
        }
        return offspring_genomes(w, founders).size();
    };

    // 对照组：两个都能生育，两条路径都该有子代，下面的零子代才不是恒真。
    CHECK(offspring_count(false, true) > 0);
    CHECK(offspring_count(false, false) > 0);
    // 只剩一个能生育的亲代：凑不成一对，两条路径都必须零子代。
    CHECK_EQ(offspring_count(true, true), std::size_t{0});
    CHECK_EQ(offspring_count(true, false), std::size_t{0});
}

TEST(adapt_breeding_does_not_turn_zero_fecundity_into_one_child)
{
    // progeny_for == 0 时旧路径仍发站点，而 settle 为兼容旧调用方把 0
    // 按 1 处理——零繁殖力的物种于是每季凭空产一胎。亲代取成必然存活
    // 的表型（Metabolism 0 / Speed 1），零子代才只可能来自预算为零。
    World w;
    Harness h;
    for (int i = 0; i < 2; ++i)
    {
        const EntityId e = h.spawn(w);
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.0f);
    }

    BreedingSystem sys{h.table, h.species, h.config, 7, 0};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    CHECK_EQ(w.entity_count(), std::size_t{2});
}

TEST(adapt_breeding_without_environment_still_works)
{
    World w;
    Harness h;
    for (int i = 0; i < 4; ++i)
    {
        const auto e = h.spawn(w);
        auto* p = w.get_if<CreaturePhenotype>(e);
        set_trait(*p, TraitKind::Speed, 1.0f);
        set_trait(*p, TraitKind::Lifespan, 5.0f);
        set_trait(*p, TraitKind::Fecundity, 0.5f);
    }

    BreedingSystem sys{h.table, h.species, h.config, 7, 0};
    CommandBuffer cmd;
    sys.execute(w, cmd);
    cmd.flush(w);

    // 降级路径必须与本轮之前逐位相同：仍然按 progeny_for 出子代。
    CHECK(w.entity_count() > 4);
}

TEST(adapt_breeding_ignores_feeding_when_environment_is_not_wired)
{
    // 验收标准 8：未接入环境层时 settle 与 BreedingSystem 的行为必须与本轮
    // 之前逐位相同。对照组就是「本轮之前」的精确复现——那时的代码从不读
    // Feeding，因此个体身上有没有 Feeding 组件对它没有任何区别。
    //
    // 触发条件很普通：EnvironmentSystem 跑过一次（写下了真的 Feeding）之后，
    // 调用方仍然用 5 参构造函数。此时若把收集来的 feedings 无条件交给
    // settle，settle 的判据 feedings.size() == genomes.size() 照样命中，它就会
    // 去读那些组件，于是 energy_survival 里
    // intake = SenseRange × Fecundity × feeding 改写 survival_rate，存活抽签、
    // 活下来的父池、子代基因组逐层跟着变。随机数消费次数不变，所以只有把子代
    // 基因组本身拿来比才看得见——本文件其余用例都不跑 EnvironmentSystem，
    // 因此对这条改动完全无感。
    Harness h;
    gamev2::environment::EnvironmentConfig scarce{};
    // want = 0.35 × 2 = 0.7，而存量上限 0.16 × base_fertility ∈ [0.04, 0.16)
    // 恒小于它，于是 got 恒取满存量，feeding 逐个都远低于 want。
    //
    // 0.16 不是随手挑的：burn = Metabolism 0.12 × BodySize 1 × (1 + Speed 1)
    // = 0.24，intake = SenseRange 1 × Fecundity 2 × feeding，所以
    // survival_rate = min(feeding / 0.12, 1)。要让夹具真的压低存活率，
    // 每个个体的 feeding 都必须严格小于 0.12。本配置实测 12 个落在
    // [0.0224, 0.0712]，最大的一档离 0.12 尚有 1.69 倍余量——而下面那条
    // 夹具断言用的正是同一个 0.12。抬到 0.5 时最大的一档是 0.2224，个体里
    // 已有一部分 survival_rate 回到 1；抬到 0.86 时 12 个全体回到 1，
    // mutant 于是被静默解除：两边全体必然存活，谁死谁活再无差别。
    scarce.capacity_base = 0.16f;

    const auto seed = [&](World& w, std::vector<EntityId>& founders)
    {
        for (int i = 0; i < 12; ++i)
        {
            const auto e = h.spawn(w);
            founders.push_back(e);
            (void)w.add<Position>(e, Position{static_cast<float>(i % 4),
                                              static_cast<float>(i / 4)});
            auto* p = w.get_if<CreaturePhenotype>(e);
            // burn = Metabolism 0.12 × BodySize 1 × (1 + Speed 1) = 0.24。
            // 没有 Feeding 时 intake = SenseRange 1 × Fecundity 2 = 2，比值
            // 2 / 0.24 ≈ 8.3 被 clamp 取 1，thermal 与 escape 各取 1，于是
            // survival_rate 恒为 1、12 个个体逐个必然存活，判定与种子无关。
            //
            // 有 Feeding 时 intake 还要乘上那个落在 [0.022, 0.071] 的值，
            // 比值落到 0.19 ~ 0.59：实测 12 个里活 4 个，活下来的父池换了
            // 一批，而两边仍都产出满额子代。mutant 因此被「子代基因组逐位
            // 不同」抓住（实测 20 处不一致），而不是被「子代数不同」一把抓住
            // ——后者只说明规模变了，两边的 progeny 都仍是 24。
            //
            // 0.24 是量出来的：rate 与 demand 无关，因为 want = 0.35 × demand
            // 恒大于整格存量，got 恒取满存量，于是 feeding ≈ G / (0.35 × demand)，
            // demand 在分子上一约就掉了，只剩 burn 留在分母上。
            set_trait(*p, TraitKind::SenseRange, 1.0f);
            set_trait(*p, TraitKind::Fecundity, 2.0f);
            set_trait(*p, TraitKind::Metabolism, 0.12f);
            set_trait(*p, TraitKind::BodySize, 1.0f);
            set_trait(*p, TraitKind::Speed, 1.0f);
            set_trait(*p, TraitKind::Lifespan, 5.0f);
            // 每个个体四段等位互不相同。子代由抽中的两个亲代重组而来，亲代
            // 集合一变子代逐位就变；全体 Genome{} 相同时选谁都一样，「抽签
            // 结果」这条链在测试里根本不存在，这条用例就成了恒真。
            auto* g = w.get_if<Genome>(e);
            g->a[0].allele = static_cast<AlleleId>(i % 2);
            g->a[1].allele = static_cast<AlleleId>((i / 2) % 2);
            g->b[0].allele = static_cast<AlleleId>((i / 4) % 2);
            g->b[1].allele = static_cast<AlleleId>((i / 8) % 2);
        }
    };

    World fed;
    std::vector<EntityId> fed_founders;
    seed(fed, fed_founders);

    World plain;
    std::vector<EntityId> plain_founders;
    seed(plain, plain_founders);

    EnvironmentWorld env{71, scarce};
    EnvironmentSystem environment{env, h.species, 0};
    CommandBuffer env_cmd;
    environment.execute(fed, env_cmd);
    env_cmd.flush(fed);

    // 先钉住夹具：真的写下了 Feeding，且 12 个都落在 survival_rate < 1 的
    // 区间里。任一不成立，下面那条相等就退化成恒真——所以这里报的是失败
    // 而不是静默跳过。界取 0.12 而不是 1.0：它是 intake/burn 归 1 的那个
    // 点，跨过去个体就全体免疫 feeding，mutant 随之失效。
    std::size_t written = 0;
    std::size_t hungry = 0;
    fed.for_each_entity([&](const EntityId e)
    {
        const auto* f = fed.get_if<Feeding>(e);
        if (f == nullptr)
        {
            return;
        }
        ++written;
        if (f->satisfaction < 0.12f)
        {
            ++hungry;
        }
    });
    CHECK_EQ(written, std::size_t{12});
    CHECK_EQ(hungry, std::size_t{12});

    // 两个世界走同一条 5 参降级路，同一个种子。唯一的差别就是 fed 身上多出
    // 的那批 Feeding 组件。
    BreedingSystem sys_fed{h.table, h.species, h.config, 7, 1};
    CommandBuffer cmd_fed;
    sys_fed.execute(fed, cmd_fed);
    cmd_fed.flush(fed);

    BreedingSystem sys_plain{h.table, h.species, h.config, 7, 1};
    CommandBuffer cmd_plain;
    sys_plain.execute(plain, cmd_plain);
    cmd_plain.flush(plain);

    const auto a = offspring_genomes(fed, fed_founders);
    const auto b = offspring_genomes(plain, plain_founders);

    // 对照组必须有子代，否则下面那条循环一次都不跑，整条用例是空的。
    CHECK(!b.empty());
    // 先逐位比内容再比长度：长度不等时只比公共前缀，既不越界（CHECK_EQ
    // 非致命，按 b.size() 索引 a 会越界），又不会因为长度先炸就漏掉内容
    // 本身已经不同这一事实。
    const auto common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i)
    {
        CHECK(a[i] == b[i]);
    }
    CHECK_EQ(a.size(), b.size());
}

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
    // 两边都成立。因此 dress 对 wa / wb 各以同一个 i 顺序调用一次，两边
    // 得到逐位相同的 Genome 与 Phenotype。
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
        // 每个个体的三段等位互不相同：i ∈ [0, 6) 落在 a[0] / a[1] / b[0]
        // 三个二进制位上，i 递增时至少有一段翻转。子代由抽中的两个亲代重组
        // 而来，亲代集合一变子代逐位就变；全体 Genome{} 相同时选谁都一样，
        // 抽签结果这条链在测试里根本不存在，这条用例就成了恒真。
        auto* g = w.get_if<Genome>(e);
        g->a[0].allele = static_cast<AlleleId>(i % 2);
        g->a[1].allele = static_cast<AlleleId>((i / 2) % 2);
        g->b[0].allele = static_cast<AlleleId>((i / 4) % 2);
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

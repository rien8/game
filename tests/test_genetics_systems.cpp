#include "gamev2/ecs/adapt/genetics_systems.hpp"

#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/diagnostic.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/world.hpp"

#include "test_framework.hpp"

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

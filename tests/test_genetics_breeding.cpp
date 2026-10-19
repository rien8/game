#include "gamev2/genetics/breeding.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

using namespace gamev2::genetics;

namespace {

/// 构造全体位点取值相同的单倍型，值即 tag。x 全为 0、y 全为 1，
/// 于是「某位点来自哪一亲代」可以只比对 allele 就读出来。
Haplotype tagged_haplotype(AlleleId tag)
{
    Haplotype h{};
    for (auto& call : h)
    {
        call.allele = tag;
        call.jitter = 0;
    }
    return h;
}

}  // namespace

TEST(gamete_without_crossover_takes_one_parent_whole)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{5};

    for (int i = 0; i < 200; ++i)
    {
        const auto g = make_gamete(x, y, 0, rng);
        const AlleleId src = g[100].allele;
        for (std::size_t k = 0; k < kLocusCount; ++k)
        {
            CHECK_EQ(g[k].allele, src);
        }
    }
}

TEST(gamete_alleles_always_come_from_a_parent)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{6};
    for (int i = 0; i < 500; ++i)
    {
        const auto g = make_gamete(x, y, 4, rng);
        for (std::size_t k = 0; k < kLocusCount; ++k)
        {
            CHECK(g[k].allele == 0 || g[k].allele == 1);
        }
    }
}

TEST(gamete_produces_alternating_segments)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{8};

    bool saw_transition = false;
    for (int trial = 0; trial < 200 && !saw_transition; ++trial)
    {
        const auto g = make_gamete(x, y, 6, rng);
        for (std::size_t k = 1; k < kLocusCount; ++k)
        {
            if (g[k].allele != g[k - 1].allele)
            {
                saw_transition = true;
                break;
            }
        }
    }
    CHECK(saw_transition);
}

TEST(gamete_source_switch_count_bounded_by_crossovers)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{9};

    // 切换次数至多为交叉点数。重复落在同一位点，或落在末位点之后，
    // 都会浪费交叉点，因此这是上界而非等式。
    for (int trial = 0; trial < 200; ++trial)
    {
        const auto g = make_gamete(x, y, 3, rng);
        std::size_t switches = 0;
        for (std::size_t k = 1; k < kLocusCount; ++k)
        {
            if (g[k].allele != g[k - 1].allele)
            {
                ++switches;
            }
        }
        CHECK(switches <= 3);
    }
}

TEST(gamete_crossover_count_above_max_is_clamped)
{
    const auto x = tagged_haplotype(0);
    const auto y = tagged_haplotype(1);
    auto rng = std::mt19937_64{29};

    // 20 个交叉点落进 300 个位点，去重后期望仍接近 19 个，远高于上界 8，
    // 所以夹取一旦失效，下面这个断言每轮都会失败。
    for (int trial = 0; trial < 100; ++trial)
    {
        const auto g = make_gamete(x, y, 20, rng);
        std::size_t switches = 0;
        for (std::size_t k = 0; k < kLocusCount; ++k)
        {
            CHECK(g[k].allele == 0 || g[k].allele == 1);
            if (k > 0 && g[k].allele != g[k - 1].allele)
            {
                ++switches;
            }
        }
        CHECK(switches <= kMaxCrossovers);
    }
}

TEST(mutate_respects_zero_switch_rate)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    auto genome = Genome{};
    auto rng = RandomStreams{3, 0};
    mutate(genome.a, table, config, rng);

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        CHECK_EQ(genome.a[i].allele, AlleleId{0});
        CHECK_EQ(genome.a[i].jitter, std::int8_t{0});
    }
}

TEST(mutate_jitter_accumulates_and_clamps)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;  // 每代都突变
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 0.0f;    // 全部走 jitter 微调
    config.jitter_step = 2;

    auto genome = Genome{};
    for (std::size_t season = 0; season < 1000; ++season)
    {
        auto rng = RandomStreams{11, season};
        mutate(genome.a, table, config, rng);
        const std::int8_t j = genome.a[0].jitter;
        CHECK(j >= -2);
        CHECK(j <= 2);
    }
}

TEST(mutate_switches_alleles_at_configured_rate)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.5f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;  // 突变即替换
    config.jitter_step = 0;

    constexpr std::size_t kTrials = 20000;
    std::size_t switched = 0;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        auto genome = Genome{};
        auto rng = RandomStreams{13, season};
        mutate(genome.a, table, config, rng);
        if (genome.a[0].allele != 0)
        {
            ++switched;
        }
    }
    // 实测替换率应落在设定值 0.5 附近，容差 0.03。
    const double rate = static_cast<double>(switched) / kTrials;
    CHECK(rate > 0.47);
    CHECK(rate < 0.53);
}

TEST(mutate_structural_locus_replaces_allele_regardless_of_switch_share)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Structural;
        table.loci[i].allele_count = 3;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;
    }
    auto config = GeneticsConfig{};
    // allele_switch_share 取 0：Structural 一旦误走 Trait 路径，替换分支永不
    // 触发，退化成只动 jitter，下面两条断言同时失败——这正是区分两条路径的
    // 判据。所有现有测试的位点都是 Trait，Structural 分支原本零覆盖。
    config.allele_switch_share = 0.0f;
    config.jitter_step = 5;

    bool saw_first = false;
    bool saw_second = false;
    for (std::size_t season = 0; season < 2000; ++season)
    {
        auto genome = Genome{};
        auto rng = RandomStreams{19, season};
        mutate(genome.a, table, config, rng);
        const AlleleId got = genome.a[0].allele;
        // switch_rate 为 1，每季必然替换，故 allele 必不等于初值 0。
        CHECK(got != 0);
        CHECK(got < 3);
        saw_first = saw_first || got == 1;
        saw_second = saw_second || got == 2;
        // jitter 对 Structural 无意义。
        CHECK_EQ(genome.a[0].jitter, std::int8_t{0});
    }
    // 均匀替换意味着两个替代等位基因都应被取到。
    CHECK(saw_first);
    CHECK(saw_second);
}

TEST(mutate_jitter_actually_moves_and_reaches_both_bounds)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 0.0f;
    config.jitter_step = 2;

    // 「step 恒为 0」同样满足 -2 <= j <= 2，夹取断言区分不了，只能看分布。
    bool saw_nonzero = false;
    bool saw_lower = false;
    bool saw_upper = false;
    auto genome = Genome{};
    // genome 跨季保留：jitter 每季在已有值上累加，两端各需一步踩到底。
    for (std::size_t season = 0; season < 2000; ++season)
    {
        auto rng = RandomStreams{23, season};
        mutate(genome.a, table, config, rng);
        const std::int8_t j = genome.a[0].jitter;
        saw_nonzero = saw_nonzero || j != 0;
        saw_lower = saw_lower || j == -2;
        saw_upper = saw_upper || j == 2;
    }
    CHECK(saw_nonzero);
    CHECK(saw_lower);
    CHECK(saw_upper);
}

TEST(mutate_picks_other_allele_across_the_edges_of_the_allele_range)
{
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;

    struct Case
    {
        AlleleId current;
        std::uint8_t allele_count;
    };
    // count 2 让 span 为 1、offset 恒 0，current 0 只能走 else 分支；
    // count 4 是 kMaxAlleles，原有测试从未出现过，current 2 落在 span 中部，
    // 两条比较分支都能取到。
    const Case cases[] = {{0, 2}, {2, 4}};

    for (const Case& c : cases)
    {
        auto table = LocusTable{};
        for (std::size_t i = 0; i < kLocusCount; ++i)
        {
            table.loci[i].kind = LocusKind::Trait;
            table.loci[i].allele_count = c.allele_count;
            table.loci[i].scale = 1.0f;
            table.loci[i].switch_rate = 1.0f;
        }

        bool seen[kMaxAlleles] = {};
        for (std::size_t season = 0; season < 2000; ++season)
        {
            auto genome = Genome{};
            for (auto& call : genome.a)
            {
                call.allele = c.current;
            }
            auto rng = RandomStreams{31, season};
            mutate(genome.a, table, config, rng);
            const AlleleId got = genome.a[0].allele;
            CHECK(got != c.current);
            CHECK(got < c.allele_count);
            seen[got] = true;
        }
        // 均匀替换：恰好取遍其余 allele_count - 1 个，且一个都没漏。
        std::size_t distinct = 0;
        for (const bool hit : seen)
        {
            distinct += hit ? 1 : 0;
        }
        CHECK(!seen[c.current]);
        CHECK_EQ(distinct, static_cast<std::size_t>(c.allele_count) - 1);
    }
}

TEST(mutate_never_keeps_the_same_allele_on_switch)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 3;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;

    // 每季从 allele 1 重新起步。断言的是「一次替换不保留原等位基因」，
    // 若把上一季的结果留给下一季，断言就变成了「一条三状态随机游走永不走回
    // 起点」——那既不是本模块要保证的性质，也必然为假：游走的平稳分布是均匀的。
    for (std::size_t season = 0; season < 2000; ++season)
    {
        auto genome = Genome{};
        for (auto& call : genome.a)
        {
            call.allele = 1;
        }
        auto rng = RandomStreams{17, season};
        mutate(genome.a, table, config, rng);
        CHECK(genome.a[0].allele != 1);
    }
}

namespace {

struct PoolBuilder
{
    std::vector<Parent> items;

    void add(const Genome& g, float f)
    {
        items.push_back(Parent{items.size(), &g, f});
    }

    [[nodiscard]] ParentPool pool() const
    {
        return ParentPool{items, SpeciesId{1}};
    }
};

/// 数出子代单倍型沿位点方向的段数（来源改变次数 + 1）。
///
/// \pre 亲代的两条单倍型在每个位点上取值不同，否则来源读不出来。
[[nodiscard]] std::size_t segment_count(const Haplotype& child,
                                        const Genome& parent)
{
    std::size_t segments = 1;
    bool from_b = child[0].allele != parent.a[0].allele;
    for (std::size_t i = 1; i < kLocusCount; ++i)
    {
        const bool now_b = child[i].allele != parent.a[i].allele;
        segments += now_b != from_b;
        from_b = now_b;
    }
    return segments;
}

}  // namespace

TEST(offspring_rejects_empty_pool)
{
    PoolBuilder b;
    auto rng = RandomStreams{1, 0};
    const auto table = LocusTable{};
    const auto config = GeneticsConfig{};
    const auto r = make_offspring(b.pool(), table, config, rng);
    CHECK(!r.has_value());
    CHECK(r.error() == BreedingError::EmptyPool);
}

TEST(offspring_rejects_single_parent_pool)
{
    PoolBuilder b;
    const Genome g{};
    b.add(g, 1.0f);
    auto rng = RandomStreams{1, 0};
    const auto table = LocusTable{};
    const auto config = GeneticsConfig{};
    const auto r = make_offspring(b.pool(), table, config, rng);
    CHECK(!r.has_value());
    CHECK(r.error() == BreedingError::PoolTooSmall);
}

TEST(offspring_prefers_parents_with_higher_fitness)
{
    // 高适应亲代占绝对权重时，合格后代必须主要由它贡献。计数方式是
    // 整条单倍型相同——亲代为纯合且互不相同时，后代单倍型可唯一归因。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome star{};
    for (auto& c : star.a)
    {
        c.allele = 1;
    }
    for (auto& c : star.b)
    {
        c.allele = 1;
    }
    Genome weak{};
    for (auto& c : weak.a)
    {
        c.allele = 0;
    }
    for (auto& c : weak.b)
    {
        c.allele = 0;
    }

    PoolBuilder b;
    b.add(weak, 1.0f);
    b.add(star, 999.0f);

    std::size_t from_star = 0;
    constexpr std::size_t kTrials = 4000;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        auto rng = RandomStreams{23, season};
        const auto child = make_offspring(b.pool(), table, config, rng);
        CHECK(child.has_value());
        if (child->a[0].allele == 1 && child->b[0].allele == 1)
        {
            ++from_star;
        }
    }
    const double rate = static_cast<double>(from_star) / kTrials;
    CHECK(rate > 0.95);
}

TEST(offspring_haplotypes_are_each_drawn_from_a_parent)
{
    // 纯合双亲的后代必然是纯合，但两份单倍型各自独立抽自亲代池。
    // 只抽一个亲代让两路共用它（自交）时，(0,1) 与 (1,0) 永不出现。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome p0{};
    Genome p1{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        p0.a[i].allele = 0;
        p0.b[i].allele = 0;
        p1.a[i].allele = 1;
        p1.b[i].allele = 1;
    }

    PoolBuilder b;
    b.add(p0, 1.0f);
    b.add(p1, 1.0f);

    // 断言只依赖组合集合，不依赖池内顺序：抽到 p1 在先、p0 在后是完全合法
    // 的有性生殖结果，等权下四种组合各占 1/4。
    std::size_t seen[2][2] = {};
    constexpr std::size_t kTrials = 500;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        auto rng = RandomStreams{29, season};
        const auto child = make_offspring(b.pool(), table, config, rng);
        CHECK(child.has_value());
        CHECK(child->a[0].allele < 2);
        CHECK(child->b[0].allele < 2);
        ++seen[child->a[0].allele][child->b[0].allele];
    }
    const std::size_t counts[4] = {seen[0][0], seen[0][1], seen[1][0],
                                   seen[1][1]};
    for (const std::size_t n : counts)
    {
        CHECK(n > kTrials / 10);
        CHECK(n < kTrials / 2);
    }
}

TEST(offspring_never_lets_a_negative_weight_break_the_cdf)
{
    // 每个亲代的两条单倍型都写成自己的编号，子代来源可直接读出。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 3;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome tagged[3]{};
    for (std::size_t p = 0; p < 3; ++p)
    {
        for (std::size_t i = 0; i < kLocusCount; ++i)
        {
            tagged[p].a[i].allele = static_cast<AlleleId>(p);
            tagged[p].b[i].allele = static_cast<AlleleId>(p);
        }
    }

    constexpr std::size_t kTrials = 500;
    constexpr std::size_t kDraws = 2 * kTrials;  // 每季两条单倍型

    {
        // 全零权重：等概率退化。若这个分支不存在，CDF 全零会让 upper_bound
        // 落到表尾并被夹到最后一个亲代，整季只由 pool.back() 繁殖。
        PoolBuilder b;
        b.add(tagged[0], 0.0f);
        b.add(tagged[1], 0.0f);
        b.add(tagged[2], 0.0f);

        std::size_t drawn[3] = {};
        for (std::size_t season = 0; season < kTrials; ++season)
        {
            auto rng = RandomStreams{37, season};
            const auto child = make_offspring(b.pool(), table, config, rng);
            CHECK(child.has_value());
            CHECK(child->a[0].allele < 3);
            CHECK(child->b[0].allele < 3);
            ++drawn[child->a[0].allele];
            ++drawn[child->b[0].allele];
        }
        for (const std::size_t n : drawn)
        {
            const double rate = static_cast<double>(n) / kDraws;
            CHECK(rate > 0.20);
            CHECK(rate < 0.47);
        }
    }

    {
        // fitness() 在 max_fecundity <= 0 的退化路径上原样返回 survival，
        // 并未截断，所以父池里可能出现负权重。抽样前必须自行截为 0：
        // 不截的话 CDF 非单调，upper_bound「区间已排序」的前置条件被破坏。
        PoolBuilder b;
        b.add(tagged[0], 1.0f);
        b.add(tagged[1], 2.0f);
        b.add(tagged[2], -100.0f);

        std::size_t drawn[3] = {};
        for (std::size_t season = 0; season < kTrials; ++season)
        {
            auto rng = RandomStreams{41, season};
            const auto child = make_offspring(b.pool(), table, config, rng);
            CHECK(child.has_value());
            CHECK(child->a[0].allele < 3);
            CHECK(child->b[0].allele < 3);
            ++drawn[child->a[0].allele];
            ++drawn[child->b[0].allele];
        }
        CHECK_EQ(drawn[2], std::size_t{0});
        const double rate = static_cast<double>(drawn[0]) / kDraws;
        CHECK(rate > 0.26);
        CHECK(rate < 0.41);
    }
}

TEST(offspring_mutates_both_haplotypes)
{
    // switch_rate = 1 且只走替换分支时，每条单倍型的每个位点都必然被换掉。
    // 本任务其余测试一律用 switch_rate = 0 做归因，突变是否接上了只有这里能
    // 区分：漏掉对其中一条单倍型施加突变，它就整条保持亲代值。
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].kind = LocusKind::Trait;
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 1.0f;
    }
    auto config = GeneticsConfig{};
    config.allele_switch_share = 1.0f;
    config.jitter_step = 0;

    const Genome zero{};  // 全体位点 allele = 0

    PoolBuilder b;
    b.add(zero, 1.0f);
    b.add(zero, 1.0f);

    auto rng = RandomStreams{43, 0};
    const auto child = make_offspring(b.pool(), table, config, rng);
    CHECK(child.has_value());
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        CHECK(child->a[i].allele != 0);
        CHECK(child->b[i].allele != 0);
    }
}

TEST(offspring_degrades_to_uniform_when_the_weight_total_is_not_usable)
{
    // std::max(NaN, 0.0f) 返回 NaN——实现是 a < b ? b : a，NaN < 0.0f 为假，
    // 于是取回第一个参数。累加出的 total 跟着变成 NaN，而 `total <= 0.0f`
    // 在 NaN 下同样是假，守卫会被整条绕过。多个大权重累加溢出成 inf 时同理。
    // 两种情形里 target 都是 NaN 或 inf，upper_bound 返回 end()，idx 被夹到
    // size-1：整季 100% 的子代来自 pool.back()，且零报错。
    constexpr std::size_t kPoolSize = 4;
    constexpr std::size_t kTrials = 500;
    constexpr std::size_t kDraws = 2 * kTrials;  // 每季两条单倍型

    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 4;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    // 每个亲代的两条单倍型都写成自己的编号，子代来源可直接读出。
    Genome tagged[kPoolSize]{};
    for (std::size_t p = 0; p < kPoolSize; ++p)
    {
        for (std::size_t i = 0; i < kLocusCount; ++i)
        {
            tagged[p].a[i].allele = static_cast<AlleleId>(p);
            tagged[p].b[i].allele = static_cast<AlleleId>(p);
        }
    }

    const auto expect_uniform = [&](const float* fitness)
    {
        PoolBuilder b;
        for (std::size_t p = 0; p < kPoolSize; ++p)
        {
            b.add(tagged[p], fitness[p]);
        }

        std::size_t drawn[kPoolSize] = {};
        for (std::size_t season = 0; season < kTrials; ++season)
        {
            auto rng = RandomStreams{47, season};
            const auto child = make_offspring(b.pool(), table, config, rng);
            CHECK(child.has_value());
            CHECK(child->a[0].allele < kPoolSize);
            CHECK(child->b[0].allele < kPoolSize);
            ++drawn[child->a[0].allele];
            ++drawn[child->b[0].allele];
        }
        // 等权退化下每个亲代期望拿到 1/4，即 0.25（σ=0.014），断言区间取
        // (0.15, 0.40) 留出 7σ 以上余量；守卫失效时这里是 1.0 与 0.0。
        for (std::size_t p = 0; p < kPoolSize; ++p)
        {
            const double rate = static_cast<double>(drawn[p]) / kDraws;
            CHECK(rate > 0.15);
            CHECK(rate < 0.40);
        }
    };

    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float all_nan[kPoolSize] = {nan, nan, nan, nan};
        expect_uniform(all_nan);
    }
    {
        // 3 × 1e38 之和仍在 FLT_MAX 之内，第 4 个才把 running 推成 inf。
        const float overflow[kPoolSize] = {1e38f, 1e38f, 1e38f, 1e38f};
        expect_uniform(overflow);
    }
}

TEST(offspring_recombines_heterozygous_parents_and_uses_the_mating_stream)
{
    // 两个亲代各自的两条单倍型在每个位点上互补，两位亲代取的等位基因区间又不
    // 相交（p0 用 0/1，p1 用 2/3）。子代每个位点的等位基因于是都能反推出
    // 「哪个亲代的哪一条单倍型」——亲代内重组、交叉点数、父代抽样走哪条
    // 随机流，三处此前零覆盖的接线一旦被改坏，断言立刻失败。
    constexpr std::size_t kPoolSize = 2;
    constexpr std::size_t kTrials = 400;

    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 4;
        table.loci[i].scale = 1.0f;
        table.loci[i].switch_rate = 0.0f;
    }
    auto config = GeneticsConfig{};
    config.jitter_step = 0;

    Genome parents[kPoolSize]{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        const auto bit = static_cast<AlleleId>(i % 2);
        parents[0].a[i].allele = bit;
        parents[0].b[i].allele = static_cast<AlleleId>(1 - bit);
        parents[1].a[i].allele = static_cast<AlleleId>(2 + bit);
        parents[1].b[i].allele = static_cast<AlleleId>(3 - bit);
    }

    PoolBuilder b;
    // 权重全 0 走等概率退化分支，Mating 流上恰好被消耗两次抽取。重放它即可
    // 断言「亲代抽自 Mating 而非 Crossover」：两条流独立播种，改抽 Crossover
    // 只会让重放值与实际值大面积不等。
    b.add(parents[0], 0.0f);
    b.add(parents[1], 0.0f);

    bool saw_recombined_a = false;
    bool saw_recombined_b = false;
    std::size_t stream_mismatch = 0;
    for (std::size_t season = 0; season < kTrials; ++season)
    {
        RandomStreams probe{53, season};
        auto& mating = probe.stream(RandomPurpose::Mating);
        const std::size_t expect_i = uniform_index(mating, kPoolSize);
        const std::size_t expect_j = uniform_index(mating, kPoolSize);

        auto rng = RandomStreams{53, season};
        const auto child = make_offspring(b.pool(), table, config, rng);
        CHECK(child.has_value());

        // p0 的两位亲代都只产出 {0,1}，p1 都只产出 {2,3}，故首个位点就足以
        // 定出各自抽中的是哪一位亲代。
        const auto got_i = child->a[0].allele >= 2 ? std::size_t{1}
                                                  : std::size_t{0};
        const auto got_j = child->b[0].allele >= 2 ? std::size_t{1}
                                                  : std::size_t{0};
        stream_mismatch += (got_i != expect_i) + (got_j != expect_j);
        saw_recombined_a =
            saw_recombined_a || segment_count(child->a, parents[got_i]) > 1;
        saw_recombined_b =
            saw_recombined_b || segment_count(child->b, parents[got_j]) > 1;
    }
    CHECK_EQ(stream_mismatch, std::size_t{0});
    // 交叉点数恒为 0、或 make_gamete 被改成只取一条单倍型时，任一条子代
    // 单倍型都只会有一段。单次交叉点数为 0 的概率是 1/9，400 季全中的
    // 概率是 (1/9)^400。
    CHECK(saw_recombined_a);
    CHECK(saw_recombined_b);
}

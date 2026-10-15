#include "gamev2/genetics/expression.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

using namespace gamev2::genetics;

namespace {

LocusDef make_trait_locus(float v0, float v1, std::uint8_t r0,
                          std::uint8_t r1, float h)
{
    LocusDef l{};
    l.kind = LocusKind::Trait;
    l.allele_count = 2;
    l.scale = 1.0f;
    l.dominance = h;
    l.switch_rate = 0.0f;
    l.alleles[0].additive = v0;
    l.alleles[0].dom_rank = r0;
    l.alleles[1].additive = v1;
    l.alleles[1].dom_rank = r1;
    return l;
}

LocusDef make_structural_locus(std::uint8_t s0, std::uint8_t r0,
                               std::uint8_t s1, std::uint8_t r1)
{
    LocusDef l{};
    l.kind = LocusKind::Structural;
    l.allele_count = 2;
    l.alleles[0].state = s0;
    l.alleles[0].dom_rank = r0;
    l.alleles[1].state = s1;
    l.alleles[1].dom_rank = r1;
    return l;
}

GeneticsConfig no_jitter()
{
    GeneticsConfig c{};
    c.jitter_unit = 0.0f;
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

/// LocusDef::target 是 uint8_t，而 trait_index 返回 size_t；测试里两处都要用，
/// 收一个转换点好过散落 static_cast。
std::uint8_t axis(TraitKind t)
{
    return static_cast<std::uint8_t>(trait_index(t));
}

}  // namespace

TEST(trait_homozygote_is_allele_value)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 0.0f);
    const auto v = trait_value(l, 0, 0, 0, 0, no_jitter());
    CHECK(v == 1.0f);
}

TEST(trait_additive_heterozygote_is_midpoint)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 0.0f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == 0.0f);
}

TEST(trait_complete_dominance_matches_homozygote)
{
    // 高等级等位基因为 1（值 -1），低等级为 0（值 1）。
    // h = 1 时杂合子应与显性纯合子同值，即 -1。
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 1.0f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == -1.0f);
}

TEST(trait_dominance_symmetric_in_argument_order)
{
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 1.0f);
    const auto v = trait_value(l, 1, 0, 0, 0, no_jitter());
    CHECK(v == -1.0f);
}

TEST(trait_overdominance_beats_both_homozygotes)
{
    // 超显让杂合子越过显性纯合子，因此它必须同时高于两个纯合子：
    // 这要求显性方是取值更高的那一个，即 rank 0 高于 rank 1。
    const auto l = make_trait_locus(1.0f, -1.0f, 1, 0, 1.5f);
    const auto het = trait_value(l, 0, 0, 1, 0, no_jitter());
    const auto homo0 = trait_value(l, 0, 0, 0, 0, no_jitter());
    const auto homo1 = trait_value(l, 1, 0, 1, 0, no_jitter());
    CHECK(het > homo0);
    CHECK(het > homo1);
}

TEST(trait_equal_rank_is_pure_additive)
{
    // 等级相同意味着共显，杂合子必须回到中点，与 h 无关。
    const auto l = make_trait_locus(1.0f, -1.0f, 3, 3, 1.5f);
    const auto v = trait_value(l, 0, 0, 1, 0, no_jitter());
    CHECK(v == 0.0f);
}

TEST(trait_jitter_shifts_additive_value)
{
    // 两次调用传同一个等位基因下标，只有 jitter 不同。h = 0 时取值就是中点，
    // 中点把 jitter 位移吸收一半——纯合子也不例外，这正是「加性」的可观测面。
    const auto l = make_trait_locus(1.0f, -1.0f, 0, 1, 0.0f);
    GeneticsConfig c{};
    c.jitter_unit = 0.1f;
    const auto base = trait_value(l, 0, 0, 0, 0, c);
    const auto jittered = trait_value(l, 0, 3, 0, 0, c);
    CHECK(jittered == base + 0.15f);
}

TEST(structural_homozygote_uses_own_state)
{
    const auto l = make_structural_locus(4, 0, 2, 1);
    CHECK_EQ(structural_state(l, 0, 0), std::uint8_t{4});
    CHECK_EQ(structural_state(l, 1, 1), std::uint8_t{2});
}

TEST(structural_heterozygote_uses_dominant_state)
{
    const auto l = make_structural_locus(4, 1, 2, 0);
    CHECK_EQ(structural_state(l, 0, 1), std::uint8_t{4});
    CHECK_EQ(structural_state(l, 1, 0), std::uint8_t{4});
}

TEST(phenotype_environment_changes_trait_axes)
{
    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        table.loci[i].allele_count = 2;
        table.loci[i].scale = 0.0f;  // 纯环境项，隔离 g(E)
    }
    GeneticsConfig config{};
    config.trait_base.fill(1.0f);
    config.trait_min.fill(-100.0f);
    config.trait_max.fill(100.0f);

    const auto mild = EnvironmentSnapshot{0.0f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{5.0f, 0.2f, 0.1f};

    const auto a = phenotype_of(Genome{}, table, all_loci(), mild, config);
    const auto b = phenotype_of(Genome{}, table, all_loci(), harsh, config);

    CHECK(a.traits[trait_index(TraitKind::Metabolism)]
          < b.traits[trait_index(TraitKind::Metabolism)]);
    CHECK(a.traits[trait_index(TraitKind::Fecundity)]
          > b.traits[trait_index(TraitKind::Fecundity)]);
    CHECK(a.traits[trait_index(TraitKind::SenseRange)]
          > b.traits[trait_index(TraitKind::SenseRange)]);
}

TEST(phenotype_environment_never_touches_structure)
{
    // 四个槽位各铺一组互不相同的结构态：只比「两环境相等」的话，槽位路由
    // 写错也会通过——串位只有靠逐字段的期望值才暴露得出来。
    struct SlotSpec
    {
        StructuralSlot slot;
        std::uint8_t state;
    };
    const SlotSpec specs[] = {
        {StructuralSlot::BodySegments, 5},
        {StructuralSlot::LimbPairs, 6},
        {StructuralSlot::Eyes, 7},
        {StructuralSlot::Flags, 10},
    };

    auto table = LocusTable{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        const SlotSpec& s = specs[i % 4];
        table.loci[i].kind = LocusKind::Structural;
        table.loci[i].allele_count = 2;
        table.loci[i].target = static_cast<std::uint8_t>(s.slot);
        table.loci[i].alleles[0].state = s.state;
        table.loci[i].alleles[0].dom_rank = 1;
        table.loci[i].alleles[1].state = static_cast<std::uint8_t>(s.state - 1);
        table.loci[i].alleles[1].dom_rank = 0;
    }
    GeneticsConfig config{};
    config.trait_base.fill(1.0f);
    config.trait_min.fill(-100.0f);
    config.trait_max.fill(100.0f);

    const auto mild = EnvironmentSnapshot{0.0f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{9.0f, 0.0f, 0.0f};

    const auto a = phenotype_of(Genome{}, table, all_loci(), mild, config);
    const auto b = phenotype_of(Genome{}, table, all_loci(), harsh, config);

    CHECK_EQ(a.body_segments, b.body_segments);
    CHECK_EQ(a.limb_pairs, b.limb_pairs);
    CHECK_EQ(a.eyes, b.eyes);
    CHECK_EQ(a.flags, b.flags);

    CHECK_EQ(a.body_segments, std::uint8_t{5});
    CHECK_EQ(a.limb_pairs, std::uint8_t{6});
    CHECK_EQ(a.eyes, std::uint8_t{7});
    CHECK_EQ(a.flags, std::uint8_t{10});
    CHECK_EQ(b.body_segments, std::uint8_t{5});
    CHECK_EQ(b.limb_pairs, std::uint8_t{6});
    CHECK_EQ(b.eyes, std::uint8_t{7});
    CHECK_EQ(b.flags, std::uint8_t{10});
}

TEST(express_accumulates_loci_into_their_own_axes)
{
    // 四个位点、三条轴，h 全为 0 且基因型纯合，于是每个位点的取值就是该
    // 等位基因的 additive，期望值可以逐轴手算。Speed 轴放两个位点，用来
    // 区分「累加」与「后写入者胜」。
    auto table = LocusTable{};
    table.loci[0] = make_trait_locus(1.5f, -1.5f, 0, 1, 0.0f);
    table.loci[0].target = axis(TraitKind::BodySize);
    table.loci[0].scale = 2.0f;
    table.loci[1] = make_trait_locus(-2.0f, 2.0f, 0, 1, 0.0f);
    table.loci[1].target = axis(TraitKind::Speed);
    table.loci[1].scale = 0.5f;
    table.loci[2] = make_trait_locus(0.25f, -0.25f, 0, 1, 0.0f);
    table.loci[2].target = axis(TraitKind::Speed);
    // 0.5 * (-2.0) 与 5.0 * 0.25 相加得 +1.25，不是 0：两项必须都落到
    // Speed 轴上。若 scale 取 4.0，两项恰好抵消成 0，期望值等于
    // trait_base[Speed]，与「两个位点都被跳过」不可区分。
    table.loci[2].scale = 5.0f;
    table.loci[3] = make_trait_locus(3.0f, -3.0f, 0, 1, 0.0f);
    table.loci[3].target = axis(TraitKind::Strength);
    table.loci[3].scale = -1.5f;

    SpeciesMask mask{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        mask.active.set(i);
    }

    GeneticsConfig config{};
    config.trait_base[axis(TraitKind::BodySize)] = 10.0f;
    config.trait_base[axis(TraitKind::Speed)] = 20.0f;
    config.trait_base[axis(TraitKind::Strength)] = 30.0f;
    config.trait_min.fill(-1000.0f);
    config.trait_max.fill(1000.0f);

    const auto p = express(Genome{}, table, mask, config);

    // 10 + 2.0 * 1.5
    CHECK(p.traits[axis(TraitKind::BodySize)] == 13.0f);
    // 20 + 0.5 * (-2.0) + 5.0 * 0.25
    CHECK(p.traits[axis(TraitKind::Speed)] == 20.25f);
    // 30 + (-1.5) * 3.0
    CHECK(p.traits[axis(TraitKind::Strength)] == 25.5f);
    // 没有位点指向的轴必须原样停在 trait_base 上。
    CHECK(p.traits[axis(TraitKind::Lifespan)] == 0.0f);
}

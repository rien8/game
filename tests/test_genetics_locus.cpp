#include "gamev2/genetics/locus.hpp"

#include "test_framework.hpp"

#include <type_traits>

using namespace gamev2::genetics;

namespace {

/// 全部位点都是默认值的空表，尚未填入任何等位基因。
LocusTable blank_table()
{
    return LocusTable{};
}

/// 把前 count 个位点填成一组合法的数量性状位点，剩余位点保持 allele_count 为 0。
void fill_trait_loci(LocusTable& table, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        auto& l = table.loci[i];
        l.kind = LocusKind::Trait;
        l.allele_count = 2;
        l.target = 0;
        l.scale = 1.0f;
        l.dominance = 0.0f;
        l.switch_rate = 0.01f;
        l.alleles[0].additive = 1.0f;
        l.alleles[0].dom_rank = 0;
        l.alleles[1].additive = -1.0f;
        l.alleles[1].dom_rank = 1;
    }
}

/// 给每条性状轴一个非退化的区间。GeneticsConfig{} 的界全是 0，validate 会把
/// 它判成退化区间，所以想测到别的错误就必须先给出合法区间。
GeneticsConfig ranged_config()
{
    GeneticsConfig cfg{};
    cfg.trait_min.fill(-1.0f);
    cfg.trait_max.fill(1.0f);
    return cfg;
}

}  // namespace

TEST(locus_table_size_is_fixed)
{
    static_assert(kLocusCount == 300);
    static_assert(kMaxAlleles == 4);
    static_assert(sizeof(LocusTable) >= sizeof(LocusDef) * kLocusCount);
    static_assert(std::is_trivially_copyable_v<LocusTable>);
    CHECK_EQ(blank_table().loci.size(), kLocusCount);
}

TEST(locus_rejects_allele_count_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].allele_count = 1;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::AlleleCountOutOfRange);
}

TEST(locus_rejects_allele_count_above_max)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].allele_count = static_cast<std::uint8_t>(kMaxAlleles + 1);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::AlleleCountOutOfRange);
}

TEST(locus_rejects_duplicate_rank_on_structural)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].kind = LocusKind::Structural;
    table.loci[0].alleles[0].dom_rank = 1;
    table.loci[0].alleles[1].dom_rank = 1;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::DuplicateRank);
}

TEST(locus_rejects_trait_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].target = static_cast<std::uint8_t>(kTraitCount);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::TraitOutOfRange);
}

TEST(locus_rejects_slot_out_of_range)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].kind = LocusKind::Structural;
    table.loci[0].target =
        static_cast<std::uint8_t>(StructuralSlot::kSlotCount);
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::SlotOutOfRange);
}

TEST(locus_rejects_negative_dominance)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].dominance = -0.5f;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::NegativeDominance);
}

TEST(locus_rejects_bad_switch_rate)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    table.loci[0].switch_rate = 1.5f;
    const auto r = validate(table, GeneticsConfig{});
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::BadSwitchRate);
}

TEST(locus_rejects_too_many_overdominant)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    for (std::size_t i = 0; i < 3; ++i)
    {
        table.loci[i].dominance = 1.5f;
    }
    GeneticsConfig cfg{};
    cfg.max_overdominant_loci = 2;
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::TooManyOverdominant);
}

TEST(locus_rejects_equal_trait_bounds)
{
    // 复现真正的故障形状：Lifespan 的界被配成 0/0，表达层 clamp 后每个个体
    // 的 Lifespan 恒为 0，outlived(p, 0) 恒真，全员判死、亲代池恒空、
    // 整季零子代且一路不报错。相等即非法，不只是「看起来不好」。
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    auto cfg = ranged_config();
    const std::size_t t = trait_index(TraitKind::Lifespan);
    cfg.trait_min[t] = 0.0f;
    cfg.trait_max[t] = 0.0f;
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::BadTraitRange);
}

TEST(locus_rejects_one_equal_trait_range_among_valid_ones)
{
    // 非法区间落在中间的 Strength 上：靠前七条合法区间不能把校验掩盖掉，
    // 否则循环若提前收敛就会漏检。
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    auto cfg = ranged_config();
    const std::size_t t = trait_index(TraitKind::Strength);
    cfg.trait_max[t] = cfg.trait_min[t];
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::BadTraitRange);
}

TEST(locus_rejects_inverted_trait_bounds)
{
    // min > max 与 min == max 是两种不同的配置错误：前者的 clamp 区间自相
    // 矛盾，后者把性状钉成常量。两条都要拦。
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    auto cfg = ranged_config();
    const std::size_t t = trait_index(TraitKind::Speed);
    cfg.trait_min[t] = 5.0f;
    cfg.trait_max[t] = -5.0f;
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::BadTraitRange);
}

TEST(locus_rejects_negative_jitter_step)
{
    // breeding 以 [−j, j] 为突变步长区间：j = −1 时下界 1 大于上界 −1，
    // std::clamp 的前置条件被破坏（UB），必须在配置期拦下。
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    auto cfg = ranged_config();
    cfg.jitter_step = -1;
    const auto r = validate(table, cfg);
    CHECK(!r.has_value());
    CHECK(r.error() == LocusConfigError::NegativeJitterStep);
}

TEST(locus_accepts_a_well_formed_table)
{
    auto table = blank_table();
    fill_trait_loci(table, kLocusCount);
    // 必须显式给出合法区间：全零的 GeneticsConfig 会被新加的退化区间校验
    // 判掉，用例想验证的是「合法表 + 合法配置」一起通过。
    auto cfg = ranged_config();
    cfg.max_overdominant_loci = 8;
    for (std::size_t i = 0; i < cfg.max_overdominant_loci; ++i)
    {
        table.loci[i].dominance = 1.5f;
    }
    const auto r = validate(table, cfg);
    CHECK(r.has_value());
}

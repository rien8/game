#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "gamev2/world/params.hpp"
#include "gamev2/world/world.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::Stone;
using gamev2::world::VeinRank;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::domain_grade_of;
using gamev2::world::effective_concentration;
using gamev2::world::element_of_stone;
using gamev2::world::is_dead_scar;
using gamev2::world::is_fivefold;
using gamev2::world::is_hetero;
using gamev2::world::kDomainGradeCount;
using gamev2::world::kElementCount;
using gamev2::world::kLandTypeCount;
using gamev2::world::kScarKindCount;
using gamev2::world::vein_targets;

// constexpr 不是装饰：这条链必须真能用于常量求值。若实现里混进了
// 非 constexpr 的调用（如 std::lround），下面几条会直接编译失败，
// 而不是等到下游写 static_assert 时才炸。
static_assert(gamev2::world::compute_spirit_tide(8914).cycle == 8u);
static_assert(gamev2::world::compute_spirit_tide(8914).year_in_phase == 113u);
static_assert(gamev2::world::compute_spirit_tide(8001).peak == 110u);
static_assert(gamev2::world::compute_spirit_tide(9001).peak == 99u);
static_assert(gamev2::world::compute_spirit_tide(9300).year_in_phase == 299u);

TEST(enum_counts_match_their_declarations)
{
    CHECK_EQ(static_cast<std::size_t>(Element::poison) + 1, kElementCount);
    CHECK_EQ(static_cast<std::size_t>(DomainGrade::blessed) + 1,
             kDomainGradeCount);
    CHECK_EQ(static_cast<std::size_t>(LandType::mundane) + 1, kLandTypeCount);
    CHECK_EQ(static_cast<std::size_t>(Scar::depleted_eye) + 1, kScarKindCount);
    CHECK_EQ(static_cast<std::size_t>(VeinRank::dragon), 4u);
}

TEST(fivefold_and_hetero_partition_the_elements)
{
    // 五行：金木水火土。异质：雷冰风光暗音毒。none 两者都不是。
    // 两个谓词必须恰好覆盖除 none 之外的全部属性，否则污染扣减会漏算。
    std::size_t covered = 0;
    for (std::size_t i = 1; i < kElementCount; ++i)
    {
        const auto e = static_cast<Element>(i);
        const bool five = is_fivefold(e);
        const bool hetero = is_hetero(e);
        CHECK(!(five && hetero));
        if (five || hetero)
        {
            ++covered;
        }
    }
    CHECK_EQ(covered, kElementCount - 1);
    CHECK(!is_fivefold(Element::none));
    CHECK(!is_hetero(Element::none));
}

TEST(dead_scar_covers_exactly_the_three_death_causes)
{
    CHECK(is_dead_scar(Scar::sinkhole));
    CHECK(is_dead_scar(Scar::tribulation));
    CHECK(is_dead_scar(Scar::depleted_eye));
    CHECK(!is_dead_scar(Scar::none));
    // 浊域不是死地：它是浓度高但混乱，与「灵气枯竭」是两回事。
    CHECK(!is_dead_scar(Scar::turbid));
}

TEST(vein_targets_scale_with_area_and_cap_at_the_world_totals)
{
    // 默认预设 256x256 必须正好是设定给的 1 / 9 / 40。
    const auto full = vein_targets(256u * 256u);
    CHECK_EQ(full.dragon, 1u);
    CHECK_EQ(full.main, 9u);
    CHECK_EQ(full.branch, 40u);

    // 超过参考面积后封顶：天下只有九条主脉、约四十条支脉。
    const auto huge = vein_targets(512u * 512u);
    CHECK_EQ(huge.main, 9u);
    CHECK_EQ(huge.branch, 40u);

    // 小图幅按面积线性缩放。
    const auto small = vein_targets(128u * 128u);
    CHECK_EQ(small.main, 9u * 128u * 128u / (256u * 256u));
    CHECK(small.dragon == 1u);
}

TEST(domain_grade_is_monotone_in_concentration)
{
    // 分等必须随浓度单调不降：更高的浓度不能拿到更低的分等。
    DomainGrade prev = DomainGrade::barren;
    for (std::uint32_t c = 0; c <= 255; ++c)
    {
        const auto g = domain_grade_of(static_cast<std::uint8_t>(c));
        CHECK(static_cast<std::size_t>(g) >= static_cast<std::size_t>(prev));
        prev = g;
    }
    CHECK(domain_grade_of(0) == DomainGrade::barren);
    CHECK(domain_grade_of(255) == DomainGrade::blessed);
}

TEST(effective_concentration_applies_suppression)
{
    CHECK_EQ(effective_concentration(200, 0), 200u);
    CHECK_EQ(effective_concentration(200, 255), 0u);
    // 200 * (255 - 128) / 255 = 99.6 -> 99。截断而非四舍五入，
    // 因此这里是 99 不是 100。
    CHECK_EQ(effective_concentration(200, 128), 99u);
    CHECK_EQ(effective_concentration(255, 128), 127u);
}

TEST(stone_maps_to_a_fivefold_or_hetero_element)
{
    // 每一个岩性都要有属性，否则地脉走到那里就断属性。
    for (std::size_t i = 0; i <= static_cast<std::size_t>(Stone::igneous_intrusive);
         ++i)
    {
        const auto e = element_of_stone(static_cast<Stone>(i));
        CHECK(is_fivefold(e) || is_hetero(e));
    }
}

TEST(params_reject_year_zero)
{
    WorldParams p;
    CHECK(p.is_valid());
    CHECK_EQ(p.spirit_year, 8914u);
    // 灵历 0 会让 (year - 1) 下溢，必须拦下。
    p.spirit_year = 0;
    CHECK(!p.is_valid());
}

TEST(world_carries_the_spirit_layer_containers)
{
    WorldParams p;
    p.width = 16;
    p.height = 12;
    p.z_levels = 3;

    World w(p);
    CHECK_EQ(w.ley_strength.size(), 192u);
    CHECK_EQ(w.ley_depth.size(), 192u);
    CHECK_EQ(w.ley_element.size(), 192u);
    CHECK_EQ(w.vein_rank.size(), 192u);
    CHECK_EQ(w.vein_id.size(), 192u);
    CHECK_EQ(w.eye_flow.size(), 192u);
    CHECK_EQ(w.domain_concentration.size(), 192u);
    CHECK_EQ(w.domain_grade.size(), 192u);
    CHECK_EQ(w.scar_kind.size(), 192u);
    CHECK_EQ(w.spirit_suppression.size(), 192u);
    CHECK_EQ(w.land_type.size(), 192u);
    CHECK_EQ(w.eye_purity.size(), 192u);
    CHECK_EQ(w.cave_heaven.size(), 192u);
    CHECK(w.ley_channels.empty());
    CHECK(w.ley_nodes.empty());
    CHECK(w.veins.empty());
    CHECK(w.eyes.empty());
    CHECK(w.scars.empty());
    CHECK(w.cave_heavens.empty());
    // 默认态：凡地、无疤、无洞天、浓度为零。
    CHECK(w.land_type.at(0) == LandType::mundane);
    CHECK(w.scar_kind.at(0) == Scar::none);
    CHECK(!w.cave_heaven.at(0));
    CHECK_EQ(w.domain_concentration.at(0), 0u);
}

TEST(contemporary_year_matches_the_setting)
{
    // 设定 2.6 与 9：当代是灵历 8914 年，谷底期第一百一十三年，
    // 距下一轮灵潮尚有八十七年。五个数必须同时对上。
    const auto tide = gamev2::world::compute_spirit_tide(8914);
    CHECK_EQ(tide.year, 8914u);
    CHECK_EQ(tide.cycle, 8u);  // 索引从 0 起，8 即第九轮
    CHECK(tide.phase == gamev2::world::TidePhase::trough);
    CHECK_EQ(tide.year_in_phase, 113u);
    CHECK_EQ(tide.years_to_next_tide, 87u);
}

TEST(tide_loss_reproduces_the_setting_figure)
{
    // 设定 2.6：「天机阁测得每轮损耗约一成——九轮之后，下一轮灵潮的
    // 峰值不足元潮时期的四成」。0.9^9 = 0.387 < 0.4。
    //
    // 注意轮次语义：第九轮的峰值是 0.9^8，设定说的「不足四成」指的是
    // 下一轮，也就是 0.9^9。两个指数差一，写错哪一个都会红。
    const auto ninth = gamev2::world::compute_spirit_tide(8001);
    CHECK_EQ(ninth.peak, 110u);  // 0.9^8 = 0.43046721 -> 109.77 -> 110

    const auto tenth = gamev2::world::compute_spirit_tide(9001);
    CHECK_EQ(tenth.peak, 99u);   // 0.9^9 = 0.387420489 -> 98.79 -> 99
    CHECK(tenth.peak < 102u);    // 0.4 * 255 = 102
}

TEST(phase_boundaries_land_on_the_setting_years)
{
    // 灵潮期 300 年、衰退期 500 年、谷底期 200 年，合计千年。
    // 第九轮灵潮期是灵历 8001—8301，衰退期 8301—8801，谷底期 8801—9001。
    CHECK(gamev2::world::compute_spirit_tide(8001).phase
          == gamev2::world::TidePhase::tide);
    CHECK(gamev2::world::compute_spirit_tide(8300).phase
          == gamev2::world::TidePhase::tide);
    CHECK(gamev2::world::compute_spirit_tide(8301).phase
          == gamev2::world::TidePhase::decline);
    CHECK(gamev2::world::compute_spirit_tide(8800).phase
          == gamev2::world::TidePhase::decline);
    CHECK(gamev2::world::compute_spirit_tide(8801).phase
          == gamev2::world::TidePhase::trough);
    CHECK(gamev2::world::compute_spirit_tide(9000).phase
          == gamev2::world::TidePhase::trough);
    CHECK(gamev2::world::compute_spirit_tide(9001).phase
          == gamev2::world::TidePhase::tide);
}

TEST(peak_year_of_the_tide_phase_reaches_the_peak_ratio)
{
    // 灵潮期的最后一年（灵历 9300）应当正好落在本轮峰值上。
    // 9301 已经是衰退期的第一年——阶段边界年归后一个阶段。
    const auto peak = gamev2::world::compute_spirit_tide(9300);
    CHECK(peak.phase == gamev2::world::TidePhase::tide);
    CHECK_EQ(peak.year_in_phase, 299u);
    CHECK_EQ(peak.level, peak.peak);
}

TEST(trough_is_low_but_not_empty)
{
    // 谷底期不是绝对零——设定说「灵脉半死」，不是死透。
    const auto trough = gamev2::world::compute_spirit_tide(8914);
    CHECK(trough.level > 0u);
    CHECK(trough.level < trough.peak);
}

TEST(the_stage_writes_the_same_state_as_the_pure_function)
{
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 1;
    p.spirit_year = 8914;

    World w(p);
    gamev2::world::make_spirit_tide(w);

    const auto expected = gamev2::world::compute_spirit_tide(8914);
    CHECK_EQ(w.spirit_tide.year, expected.year);
    CHECK_EQ(w.spirit_tide.cycle, expected.cycle);
    CHECK(w.spirit_tide.phase == expected.phase);
    CHECK_EQ(w.spirit_tide.year_in_phase, expected.year_in_phase);
    CHECK_EQ(w.spirit_tide.years_to_next_tide, expected.years_to_next_tide);
    CHECK_EQ(w.spirit_tide.level, expected.level);
    CHECK_EQ(w.spirit_tide.peak, expected.peak);
}

TEST(the_year_boundaries_of_the_tide_cycle)
{
    // 合法年份的下界。灵历元年是元潮，峰值满格 255，潮位按相位
    // 走到第 1/300 处。
    const auto first = gamev2::world::compute_spirit_tide(1);
    CHECK_EQ(first.cycle, 0u);
    CHECK_EQ(first.peak, 255u);
    CHECK_EQ(first.level, 77u);
    CHECK(first.phase == gamev2::world::TidePhase::tide);
    CHECK_EQ(first.years_to_next_tide, 1000u);

    // 第 19 轮之后损耗封顶：9^20 起放不进 uint64，比例停在 0.9^19。
    // 封顶是已知的模型边界，不是精度问题——把它钉住，免得以后有人
    // 以为「再往后也在正常衰减」。
    const auto nineteenth = gamev2::world::compute_spirit_tide(19001);
    const auto twentieth = gamev2::world::compute_spirit_tide(20001);
    CHECK_EQ(nineteenth.peak, 34u);   // 0.9^19 = 0.13508 -> 34
    CHECK_EQ(twentieth.peak, 34u);    // 封顶，与第十九轮同值
    CHECK_EQ(twentieth.cycle, 20u);
}

}  // namespace

#include "gamev2/game/session.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::ecs::Diagnostic;
using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::Environment;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::Vitality;
using gamev2::environment::kGridSide;
using gamev2::game::GameConfig;
using gamev2::game::GameSession;
using gamev2::genetics::Genome;
using gamev2::genetics::LocusKind;
using gamev2::genetics::Phenotype;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kLocusCount;
using gamev2::genetics::trait_index;

/// GameSession 的 table_ 是默认构造的 LocusTable{}，全部位点 allele_count = 0。
/// 而 World 持有它一份 const 引用，所以铺个体之前必须先把它填成合法的位点表：
/// 子代的表型由 BreedingSystem 经 phenotype_of 现算，读的就是这份表。
/// 填它的入口是 GameSession::locus_table()——那条接缝本来是给阶段 2/3 铺初始
/// 个体用的，这里只是提前用到。
void fill_table(GameSession& session)
{
    auto& table = session.locus_table();
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        auto& locus = table.loci[i];
        locus.kind = LocusKind::Trait;
        locus.allele_count = 2;
        locus.target = static_cast<std::uint8_t>(i % gamev2::genetics::kTraitCount);
        locus.scale = 1.0f;
        locus.switch_rate = 0.05f;
        locus.alleles[1].dom_rank = 1;
    }
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

SpeciesId register_species(GameSession& session)
{
    const auto reg = session.world().species().register_species(all_loci());
    // register_species 返回 std::expected 而非指针：不查就吞掉失败，而这是
    // 本文件造多物种世界的唯一入口。
    CHECK(reg.has_value());
    return *reg;
}

/// 活得过季末的表型模板。四条约束叠起来让 survival_rate 恒为 1，census 因此
/// 只由铺了多少决定，测试不跟随机数赛跑：
///
/// - Metabolism = 0 → energy_survival 走 `burn <= 0` 的 1.0 短路；
/// - Environment 给默认快照（temperature 恒 0）→ thermal 恒 1；
/// - Speed = 1 且 threats 恒 0 → escape 取满；
/// - Lifespan = 100 → outlived 长期为假（判据是 `age >= lifespan`）。
///
/// SenseRange / Fecundity 各 0.5，于是摄食 demand 恒 0.25，够 build_sites 算
/// 出一个非零需求，子代预算那半条路因此仍被走到。
Phenotype resident()
{
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
    p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
    p.traits[trait_index(TraitKind::Metabolism)] = 0.0f;
    p.traits[trait_index(TraitKind::Lifespan)] = 100.0f;
    return p;
}

/// 铺 count 个常驻个体，第 i 个落在第 (first_cell + i) 格。两个物种给不同的
/// first_cell，于是不共格——共格会让 build_sites 的 headroom 在两物种间分摊，
/// 出生数变成竞争的结果，测试就没必要为它操心。
///
/// **刻意不钉 `water_fraction = 0`**：生产代码 `src/game/session.cpp` 的
/// `make_environment()` 用模块默认 0.70 —— 游戏就该有地形，钉 0 等于把游戏
/// 变成一块没有水的平地。代价是这里按固定格坐标（cells 0..63，即 y = 0 整行）
/// 播种，约 70% 的个体开局落在水格上，被 MovementSystem 的 `nearest_standable`
/// 静默搬到最近的陆地。断言只看 census 与 victory、与位置无关，所以测试仍然
/// 绿 —— 但它依赖的是「位置无关」这条性质，不是「地形不影响这里」。哪���
/// 种法或断言一旦引入位置，这条会碎。
void seed_species(GameSession& session, const SpeciesId sid,
                  const std::size_t first_cell, const std::size_t count)
{
    auto& entities = session.world().entities();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto c = first_cell + i;
        const auto x = static_cast<float>(c % kGridSide) + 0.5f;
        const auto y = static_cast<float>(c / kGridSide) + 0.5f;
        const EntityId e = entities.create();
        (void)entities.add<Position>(e, Position{x, y});
        (void)entities.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)entities.add<Environment>(e, Environment{});
        (void)entities.add<Genome>(e, Genome{});
        (void)entities.add<Vitality>(e, Vitality{});
        (void)entities.add<CreaturePhenotype>(e, CreaturePhenotype{resident()});
    }
}

/// 逐个销毁带该 SpeciesTag 的实体，含往年 flush 出来的子代——不销毁子代的话
/// 母种没了仍有同族后代，并存数不会真的掉回目标以下。
void cull(GameSession& session, const SpeciesId sid)
{
    auto& entities = session.world().entities();
    std::vector<EntityId> doomed;
    entities.for_each_entity(
        [&](const EntityId e)
        {
            const auto* tag = entities.get_if<SpeciesTag>(e);
            if (tag != nullptr && tag->species == sid)
            {
                doomed.push_back(e);
            }
        });
    for (const auto e : doomed)
    {
        entities.destroy(e);
    }
}

/// report.species 的下标是 SpeciesId::value - 1。判据来自季末读数而不是私有
/// 计数，因此上面两条断言不是自证的。
std::size_t census_of(const GameSession& session, const SpeciesId sid)
{
    const auto index = static_cast<std::size_t>(sid.value) - 1;
    const auto& species = session.scroll().report.species;
    return index < species.size() ? species[index].census : 0;
}

TEST(a_session_starts_on_season_zero_with_no_creatures)
{
    GameSession session{GameConfig{}};
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{0});
    CHECK_EQ(session.scroll().report.population, std::size_t{0});
    CHECK_EQ(session.tick_in_season(), std::uint32_t{0});
    CHECK(!session.scroll().victory);
}

TEST(a_session_advances_a_whole_season_per_advance_one_season)
{
    // 两个粒度必须分开，名字就是它们的唯一区别：advance_one_substep 走一
    // 步，advance_one_season 走完一季。混用会让渲染层一帧跑掉一整季。
    GameSession session{GameConfig{}};
    session.advance_one_season();
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{1});
    CHECK_EQ(session.scroll().report.season, std::uint32_t{0});
    CHECK_EQ(session.tick_in_season(), std::uint32_t{0});
}

TEST(advancing_one_substep_does_not_finish_a_season)
{
    GameSession session{GameConfig{}};
    session.advance_one_substep();
    CHECK_EQ(session.tick_in_season(), std::uint32_t{1});
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{0});
}

TEST(advancing_one_season_completes_a_season_that_already_started)
{
    // 渲染层的真实形状：一季 8 帧，先播 3 帧再决定补齐本季。这条补齐循环是
    // advance_one_season 唯一一条此前没有任何测试走过的路径。
    GameSession session{GameConfig{}};
    std::size_t incomplete = 0;
    session.world().entities().set_diagnostic_handler(
        [&](const Diagnostic& d)
        {
            if (d.code == "WORLD_INCOMPLETE_SEASON")
            {
                ++incomplete;
            }
        });

    for (std::uint32_t i = 0; i < 3; ++i)
    {
        session.advance_one_substep();
    }
    CHECK_EQ(session.tick_in_season(), std::uint32_t{3});
    // 季中途 scroll 还没刷新，仍是开局那份。
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{0});

    session.advance_one_season();

    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{1});
    CHECK_EQ(session.scroll().report.season, std::uint32_t{0});
    CHECK_EQ(session.tick_in_season(), std::uint32_t{0});
    // 补齐循环的条件若被误写成 `<=`，这里会多跑一步：第 9 次 step_once 让
    // tick_ 停在 9 而不是回到 0，season_report 的 `tick_ != steps_per_season`
    // 于是报出这条诊断，而摄食累加器与归一化分母也跟着错位。末尾那条
    // tick_in_season() == 0 抓不住它——season_report 结尾无条件把 tick_ 清 0。
    CHECK_EQ(incomplete, std::size_t{0});
}

TEST(victory_needs_the_species_count_and_the_full_streak)
{
    // refresh_scroll 是私有的，而它只在并存数达标时才动计数——空世界永远走
    // 不到那条分支。所以这里经 session.world() 铺出两个真物种，而不是断言
    // 开局的默认值（任何返回默认值的实现都能过那种断言）。
    GameConfig config{};
    config.target_species = 2;
    config.target_seasons = 3;
    // 默认 20 是给真局的门槛；这里只关心「达标 / 不达标」这个判据本身，
    // 把门槛压到 1 于是断言不依赖铺多少个体。
    config.min_viable = 1;
    GameSession session{config};
    fill_table(session);
    const auto first = register_species(session);
    const auto second = register_species(session);
    seed_species(session, first, 0, 32);
    seed_species(session, second, 32, 32);

    // 还没跑过一季：季末读数是空的，并存数 0 < target_species，计数停在 0。
    CHECK(!session.scroll().victory);
    CHECK_EQ(session.scroll().seasons_to_victory, std::uint32_t{3});

    // 三季达标：seasons_to_victory 逐季递减，第三季才判赢。
    for (std::uint32_t expected = 2; expected >= 1; --expected)
    {
        session.advance_one_season();
        // 断言前置：并存数真的达标了。这一条不能省，否则上面那个递减可能
        // 只是因为并存数压根没达标、计数一直停在 0。
        CHECK(census_of(session, first) >= config.min_viable);
        CHECK(census_of(session, second) >= config.min_viable);
        CHECK(!session.scroll().victory);
        CHECK_EQ(session.scroll().seasons_to_victory, std::uint32_t{expected});
    }

    session.advance_one_season();
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{3});
    CHECK(session.scroll().victory);
    CHECK_EQ(session.scroll().seasons_to_victory, std::uint32_t{0});

    // 「维持」比「达到」更难：第二个物种灭掉之后计数归零重来。
    cull(session, second);
    session.advance_one_season();
    CHECK_EQ(session.scroll().seasons_elapsed, std::uint32_t{4});
    CHECK(census_of(session, first) >= config.min_viable);
    CHECK_EQ(census_of(session, second), std::size_t{0});
    CHECK(!session.scroll().victory);
    CHECK_EQ(session.scroll().seasons_to_victory, std::uint32_t{3});
}

}  // namespace

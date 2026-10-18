#include "gamev2/game/session.hpp"

#include <span>

namespace gamev2::game {

namespace {

gamev2::genetics::GeneticsConfig make_genetics()
{
    gamev2::genetics::GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    c.thermal_base_width = 18.0f;
    c.escape_responsiveness = 1.5f;
    return c;
}

gamev2::environment::EnvironmentConfig make_environment()
{
    gamev2::environment::EnvironmentConfig c{};
    // 9800 个体没法全画。压到 1/3 上下，靠密度换可视性——见
    // doc/2026-10-17-game-design.md §10 末段。
    c.capacity_base = 1.3f;
    return c;
}

}  // namespace

GameSession::GameSession(GameConfig config)
    : config_{config},
      table_{},
      world_{table_, make_genetics(), make_environment(), config.root_seed}
{
    scroll_.seasons_to_victory = config_.target_seasons;
}

void GameSession::advance_one_substep()
{
    world_.step_once();
}

void GameSession::advance_one_season()
{
    while (world_.tick_in_season() < world_.steps())
    {
        world_.step_once();
    }
    // 上一季的报告留在这儿，只为取「刚灭绝那支的归零前规模」—— 本季报告里
    // 它的 census 已经是 0 了。
    previous_ = scroll_.report;
    scroll_.report = world_.season_report();
    // +1 不是凑数：report.season 是刚结算完的那一季（0 基），seasons_elapsed
    // 是世界此刻所在的季，也是胜利进度条的分子。
    scroll_.seasons_elapsed = scroll_.report.season + 1;

    const auto [born, lost] = world_.speciation_report();
    scroll_.born.clear();
    for (const auto& b : born)
    {
        scroll_.born.push_back(
            BornSpecies{b.id, b.parent, b.founders, b.anchor});
    }
    scroll_.lost.clear();
    for (const auto id : lost)
    {
        // SeasonReport::species 的下标是 SpeciesId::value - 1，直接换算。
        const auto idx = static_cast<std::size_t>(id.value) - 1;
        const auto before = idx < previous_.species.size()
                                ? previous_.species[idx].census
                                : 0;
        scroll_.lost.push_back(LostSpecies{id, before});
    }

    refresh_scroll();
}

void GameSession::refresh_scroll() noexcept
{
    std::size_t coexisting = 0;
    for (const auto& s : scroll_.report.species)
    {
        if (s.census >= config_.min_viable)
        {
            ++coexisting;
        }
    }

    if (coexisting >= config_.target_species)
    {
        ++coexist_seasons_;
    }
    else
    {
        // 中途把物种数压回目标以下，计数归零重来——「维持」比「达到」更难。
        coexist_seasons_ = 0;
    }

    scroll_.victory = coexist_seasons_ >= config_.target_seasons;
    scroll_.seasons_to_victory =
        scroll_.victory ? 0
                        : config_.target_seasons - coexist_seasons_;
}

std::size_t GameSession::paint_terrain(
    const std::span<const gamev2::environment::CellIndex> cells)
{
    return world_.queue_terrain_edit(cells, false);
}

std::size_t GameSession::raise_land(
    const std::span<const gamev2::environment::CellIndex> cells)
{
    return world_.queue_terrain_edit(cells, true);
}

std::size_t GameSession::undo_terrain(
    const std::span<const gamev2::environment::CellIndex> cells)
{
    return world_.undo_terrain(cells);
}

std::size_t GameSession::paint_climate(
    const std::span<const gamev2::environment::CellIndex> cells, const float delta)
{
    return world_.queue_climate_edit(cells, delta);
}

void GameSession::cull(const gamev2::genetics::SpeciesId species,
                       const gamev2::genetics::TraitKind trait, const float lo,
                       const float hi)
{
    world_.queue_cull(species, trait, lo, hi);
}

}  // namespace gamev2::game

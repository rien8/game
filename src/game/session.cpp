#include "gamev2/game/session.hpp"

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
    scroll_.report = world_.season_report();
    // +1 不是凑数：report.season 是刚结算完的那一季（0 基），seasons_elapsed
    // 是世界此刻所在的季，也是胜利进度条的分子。
    scroll_.seasons_elapsed = scroll_.report.season + 1;
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

}  // namespace gamev2::game

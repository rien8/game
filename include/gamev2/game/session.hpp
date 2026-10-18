/// @file session.hpp
/// @brief 游戏层：持有 world::World，提供 tick 级推进与季末读数。
///
/// 本层只做编排与读数，不含任何模拟逻辑，也不认识渲染库——渲染层要的量
/// 全部由这里的只读访问器提供。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.4。
#pragma once

#include "gamev2/genetics/locus.hpp"
#include "gamev2/world/world.hpp"

#include <cstddef>
#include <cstdint>

namespace gamev2::game {

/// 一局的配置。玩家干预工具（画屏障 / 气候笔刷 / 育种所）在阶段 4 才接进来。
struct GameConfig
{
    std::uint64_t root_seed = 20261017;
    /// 胜利所需的并存物种数下限。
    std::size_t target_species = 3;
    /// 达到 target_species 后还需持续的季数。
    std::uint32_t target_seasons = 20;
    /// census 低于此值不算「存活」，因此不计入并存物种数。
    std::size_t min_viable = 20;
};

/// 抄经人读的那一卷。SeasonReport 之上只多一样东西：是否已经抄完。
struct Scroll
{
    /// 已完整结算的季数。
    ///
    /// 刻意不叫 season：report.season 是刚结算那一季的 0 基季号，因此
    /// seasons_elapsed == report.season + 1。两个都叫 season 而含义差一，
    /// 会让任何按 scroll().season 去对齐 report 的下标静默错一季。
    ///
    /// 这条不变式**只在第一次结算之后成立**：开局两者都是 0，那时
    /// seasons_elapsed == report.season + 1 不成立（0 != 1）。
    std::uint32_t seasons_elapsed = 0;
    gamev2::world::SeasonReport report;
    bool victory = false;
    /// 距离胜利还差几季。已达标且尚未走满 target_seasons 时逐季递减。
    std::uint32_t seasons_to_victory = 0;
};

class GameSession
{
public:
    explicit GameSession(GameConfig config);

    /// 位点表。构造世界要用，且必须活得比本对象久——World 持有它的引用。
    [[nodiscard]] gamev2::genetics::LocusTable& locus_table() noexcept
    {
        return table_;
    }

    /// 铺初始个体用。本层不生成初始种群，理由同 world::World。
    [[nodiscard]] gamev2::world::World& world() noexcept
    {
        return world_;
    }

    /// 推进一个季内子步。渲染层每帧调一次。
    void advance_one_substep();

    /// 跑完整一季：把剩下的子步补齐，季末结算，刷新 scroll_。
    void advance_one_season();

    /// 一季有几个季内子步，渲染层据此决定「补齐本季」前还能再播几帧。
    /// 转发自 World 是为了让它不必穿透可写的 world() 去问同一个数。
    [[nodiscard]] std::uint32_t steps() const noexcept
    {
        return world_.steps();
    }

    /// 只在 advance_one_season() 里刷新。季中途读到的是**上一季**的读数：
    /// 季中的实体表还在变，而 report 的 census 要跨过换代 flush 才有意义。
    [[nodiscard]] const Scroll& scroll() const noexcept
    {
        return scroll_;
    }

    [[nodiscard]] std::uint32_t tick_in_season() const noexcept
    {
        return world_.tick_in_season();
    }

private:
    /// 只读地重算并存物种数与胜利进度。不改世界。
    void refresh_scroll() noexcept;

    GameConfig config_;
    gamev2::genetics::LocusTable table_;
    gamev2::world::World world_;
    Scroll scroll_;
    std::uint32_t coexist_seasons_ = 0;
};

}  // namespace gamev2::game

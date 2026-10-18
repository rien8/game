/// @file session.hpp
/// @brief 游戏层：持有 world::World，提供 tick 级推进与季末读数。
///
/// 本层只做编排与读数，不含任何模拟逻辑，也不认识渲染库——渲染层要的量
/// 全部由这里的只读访问器提供。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.4。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/species.hpp"
#include "gamev2/world/world.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::game {

/// 一局的配置。玩家干预工具（画屏障 / 气候笔刷 / 育种所）在阶段 4 才接进来。
struct GameConfig
{
    std::uint64_t root_seed = 20261017;
    /// 胜利所需的并存物种数下限。
    std::size_t target_species = 3;
    /// 达到 target_species 后还需持续的季数。
    ///
    /// 取 50 而不是 20：实测裂变**会级联** —— 一旦拆开，每个新物种的
    /// 「其余全部」变小，剩下的小陆块相对自己物种的 divergence 立刻变大，
    /// 于是继续裂。`evolve` 在 min_founder = 40 下第 10 季就有 6 个物种，
    /// target_seasons = 20 会让胜利落在第 40 季，200 季的局有 80% 用不上。
    /// 把节奏调在胜利条件上而不是裂变门槛上，两件事各归各的位置 ——
    /// min_founder 管「不许裂出错物种」，target_seasons 管「这局玩多久」。
    std::uint32_t target_seasons = 50;
    /// census 低于此值不算「存活」，因此不计入并存物种数。
    ///
    /// 取 60 而不是 20：裂变门槛 40 之下，那 11 块有主陆块里 60~178 个体
    /// 的全都够格，于是小岛一开局就各自成种，胜利条件几乎立刻满足。
    /// 60 把「一块只有几十个个体的陆块」排除在并存之外，胜利因此落在
    /// 第 ~90 季（实测）。
    std::size_t min_viable = 60;
};

/// 本局裂出来的物种。谱系是抄经人唯一能读懂的因果链：不带母物种，玩家看到
/// 「物种 4 出现了」只会当成噪声。`anchor` 让他知道新物种落在哪一块地。
struct BornSpecies
{
    gamev2::genetics::SpeciesId id;
    gamev2::genetics::SpeciesId parent;
    std::size_t founders;
    gamev2::environment::CellIndex anchor;
};

/// 本局 census 归零的物种。`last_census` 是它**归零那一季之前**的规模。
struct LostSpecies
{
    gamev2::genetics::SpeciesId id;
    std::uint32_t last_census;
};

/// 抄经人读的那一卷。SeasonReport 之上只多两样东西：是否已经抄完，
/// 以及这一季谁诞生、谁灭绝。
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
    /// 本局新出现的物种。玩家靠这个知道自己的干预有没有生效。
    std::vector<BornSpecies> born;
    /// 本局 census 归零的物种。玩家靠这个知道哪一支灭了。
    std::vector<LostSpecies> lost;
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

    /// 上一季的报告。**只为取「刚灭绝那支的归零前规模」而存在** —— 本季
    /// 报告里它的 census 已经是 0，拿不到灭绝前的数字。
    gamev2::world::SeasonReport previous_;

    GameConfig config_;
    gamev2::genetics::LocusTable table_;
    gamev2::world::World world_;
    Scroll scroll_;
    std::uint32_t coexist_seasons_ = 0;
};

}  // namespace gamev2::game

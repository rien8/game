/// @file movement_system.hpp
/// @brief 行为系统：每季把个体按其 Speed 性状朝一个随机方向推进一步。
///
/// 它存在的理由是 build_sites 的一个结构性后果：出生预算取
/// min(eco_ceiling, parent_ceiling)，而 parent_ceiling =
/// lround(here × offspring_scale)——没有亲代就没有站点，于是一个空掉的格
/// 永远不会再被填上。个体不动，地图上的可用面积就只减不增，世界必然单调
/// 收缩。见 doc/2026-10-16-world-design.md §8。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include <cstdint>
#include <tuple>

namespace gamev2::ecs::adapt {

/// 随机游走。扩散能力由 Speed 性状决定，因而是可遗传的——跑得快的个体
/// 先殖民空出来的邻格。
///
/// 只在 execute 里就地改写 Position，不做任何结构变更，因此不需要
/// mutates_structure。
class MovementSystem final : public System
{
public:
    /// 排在 EnvironmentSystem 之前——摄食要按当季坐标分格，与 BreedingSystem
    /// 的先后同理。二者的冲突都由访问集自动成立：本 system 写 Position，
    /// 对方读 Position。**不要只靠这一行表达先后**——System::order 只在两个
    /// system 本来就会冲突时才等于批次先后，而这两条冲突是真的。
    static constexpr int order = -1;

    /// 只读 CreaturePhenotype 是为了取 Speed 定步长。
    using ReadSet = std::tuple<const CreaturePhenotype>;
    using WriteSet = std::tuple<Position>;

    MovementSystem(std::uint64_t root_seed, std::uint32_t season_index)
        : root_seed_{root_seed}, season_index_{season_index}
    {
    }

    /// 改写本 system 消费的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：execute 在开头把 root_seed_ 与
    ///       season_index_ 混成随机流，之后改它不会有效果。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = season_index;
    }

    void execute(World& world, CommandBuffer& cmd) override;

private:
    std::uint64_t root_seed_;
    std::uint32_t season_index_;
};

}  // namespace gamev2::ecs::adapt

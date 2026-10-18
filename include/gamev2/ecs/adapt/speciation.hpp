/// @file speciation.hpp
/// @brief 物种形成：按 4 邻域连通分量切分每个物种，逐块判条件后裂变。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.3 与 §5。
#pragma once

// SpeciesTag 与 Genome 的 using 别名都在 genetics_systems.hpp 里，不在
// components.hpp —— 后者只有 Position / CreaturePhenotype / Environment。
#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {

/// 本季裂出来的物种。`anchor` 是那个分量的中位数格 —— 抄经人要知道新物种
/// 出现在地图的哪一块，而不只是「物种 4 诞生了」。
struct BornSpecies
{
    gamev2::genetics::SpeciesId id;
    gamev2::genetics::SpeciesId parent;
    std::size_t founders;
    gamev2::environment::CellIndex anchor;
};

/// **不进调度器**：它读刚 flush 出来的实体表，而调度器批次的时序管不到那里。
/// `World::season_report` 在换代 flush 之后、统计之前直接 execute 它，否则
/// 季末报告会把刚裂出来的物种连同它的全部个体一起漏掉。
///
/// 写集只有 `SpeciesTag`，且经 `get_if` 拿**可变指针原地改值**，因此不触发
/// archetype 迁移，**不需要 `mutates_structure`**。
class SpeciationSystem final : public System
{
public:
    /// \pre `species` 必须与 `env` 活的一样久，且必须是**非常量**引用：
    ///   登记新物种要调 `register_species`。阶段 1/2 之前所有 system 拿的都是
    ///   const 引用，因为物种彼时是构造期登记的静态标签。
    ///
    /// 地形与配置**从 `env` 取而不从 `ecs::World` 取**：作为 `ecs::System`
    /// 本类拿到的是 `ecs::World`，它没有环境层字段。这与 `EnvironmentSystem`
    /// 持有 `EnvironmentWorld&` 是同一个理由。
    SpeciationSystem(gamev2::genetics::SpeciesRegistry& species,
                     const EnvironmentWorld& env);

    using ReadSet = std::tuple<const Genome, const SpeciesTag, const Position>;
    using WriteSet = std::tuple<SpeciesTag>;

    void execute(World& world, CommandBuffer& cmd) override;

    /// 本季裂出来的物种。生命周期归调用方管 —— `World` 把它抄进 Scroll。
    [[nodiscard]] const std::vector<BornSpecies>& born() const noexcept
    {
        return born_;
    }

    /// 本季一个个体都不剩的物种。
    [[nodiscard]] const std::vector<gamev2::genetics::SpeciesId>& lost()
        const noexcept
    {
        return lost_;
    }

private:
    /// 一个「已满足前几条、正在攒连续季数」的分量。**纯派生态**：不进任何
    /// ECS 组件。它不进任何存档契约，因为它每季由当前世界重算。
    struct Pending
    {
        gamev2::genetics::SpeciesId species;
        gamev2::environment::CellIndex anchor;
        std::uint32_t seasons = 1;
    };

    gamev2::genetics::SpeciesRegistry& species_;
    const EnvironmentWorld& env_;
    std::vector<BornSpecies> born_;
    std::vector<gamev2::genetics::SpeciesId> lost_;
    std::vector<Pending> pending_;
};

}  // namespace gamev2::ecs::adapt

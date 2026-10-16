/// @file environment_systems.hpp
/// @brief 环境模块的 ECS 适配层：推进气候、跑完季内摄食、算承载力预算，
///        并把逐个体的 Feeding 与逐站点的 BreedingTarget 交给 BreedingSystem。
///
/// 跨 system 传递的一律是 ECS 组件或按物种分组后的站点表，不传下标数组：
/// 两个 system 各自 for_each_entity 一遍，遍历顺序是各自的实现细节。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/sites.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {

/// 气候场与资源场。整局唯一一份，不进 ECS——它是单一实例的场景状态，
/// 放进组件只会让「每个实体各有一份气候」这个错误写法看起来合法。
class EnvironmentWorld
{
public:
    EnvironmentWorld(std::uint64_t root_seed,
                     const gamev2::environment::EnvironmentConfig& config);

    [[nodiscard]] gamev2::environment::ClimateField& climate() noexcept;
    [[nodiscard]] gamev2::environment::ResourceField& resources() noexcept;
    [[nodiscard]] const gamev2::environment::EnvironmentConfig& config()
        const noexcept;

private:
    gamev2::environment::EnvironmentConfig config_;
    gamev2::environment::ClimateField climate_;
    gamev2::environment::ResourceField resources_;
};

/// **必须排在 BreedingSystem 之前**。与 AgingSystem 必须排在 BreedingSystem
/// 之前是同一类约束：WriteSet 只能让调度器分批，表达不了批次之间的先后。
/// 顺序反了不会崩，只会让本季出生数为零。
class EnvironmentSystem final : public System
{
public:
    EnvironmentSystem(EnvironmentWorld& env, std::uint32_t season_index)
        : env_{env}, season_index_{season_index}
    {
    }

    using ReadSet = std::tuple<const Position, const CreaturePhenotype,
                               const SpeciesTag>;
    using WriteSet = std::tuple<Feeding>;

    void execute(World& world, CommandBuffer& cmd) override;

    /// 传给 BreedingSystem 的站点表。环境层只说「哪个格、哪个物种、
    /// 该产几个」，空间分桶与 Parent::index 的翻译留在 adapt 层。
    [[nodiscard]] const std::vector<BreedingTarget>& targets() const noexcept
    {
        return targets_;
    }

private:
    EnvironmentWorld& env_;
    std::uint32_t season_index_;
    std::vector<BreedingTarget> targets_;
};

}  // namespace gamev2::ecs::adapt

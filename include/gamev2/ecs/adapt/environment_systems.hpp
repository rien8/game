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

    /// World::environment() 只给 const 引用，所以「只读暴露气候场与资源场」
    /// 这条承诺需要真的能只读。底层 ClimateField / ResourceField 本就全有 const
    /// 成员，这里补的只是同一件事的 const 通道，非常量重载仍是 EnvironmentSystem
    /// 推进气候与资源时走的路径。
    [[nodiscard]] const gamev2::environment::ClimateField& climate()
        const noexcept;
    [[nodiscard]] const gamev2::environment::ResourceField& resources()
        const noexcept;

private:
    gamev2::environment::EnvironmentConfig config_;
    gamev2::environment::ClimateField climate_;
    gamev2::environment::ResourceField resources_;
};

/// 逐个体写 Feeding，逐站点出 BreedingTarget。必须先于 BreedingSystem 跑完。
/// WriteSet / ReadSet 只保证二者不同批，批次之间的先后由 System::order 表达；
/// 两者都改掉时本 system 会落在 BreedingSystem 之后，读到上一季的站点表，
/// 那只会让本季出生数按上一季结算。
///
/// Feeding 的首次写入是 `World::add`，即 archetype 迁移。它与另一 system 对
/// 同一批 slots_ 的无锁遍历并发相撞就是 use-after-free，而访问集表达不了这个
/// 风险——挡它的是本类的 `mutates_structure`，它让本 system 独占一个批次。
///
/// 持 SpeciesRegistry 是为了与 BreedingSystem 对「谁参与繁殖」给出同一个答案：
/// 未注册的物种 id 若被本 system 放进来，它会占位、吃光本该属于真实物种的存量
/// 并出现在 targets() 里，而 BreedingSystem 会把它整个跳过——一个只消耗不繁殖
/// 的幽灵物种。
class EnvironmentSystem final : public System
{
public:
    EnvironmentSystem(EnvironmentWorld& env,
                      const gamev2::genetics::SpeciesRegistry& species,
                      std::uint32_t season_index)
        : env_{env}, species_{species}, season_index_{season_index}
    {
    }

    /// 首次见到的个体走 World::add，即 archetype 迁移：它会重写 slots_ 里的
    /// archetype/chunk/row 并可能重分配 storage_ 的列缓冲。读集表达不了这件事
    /// （plan 的判据里读/读不算冲突，两个访问集不相交的只读 system 会被放进
    /// 同一批并行派发），因此只能由这个标记让它独占批次。
    static constexpr bool mutates_structure = true;

    /// 排在 BreedingSystem 之前。该标记已保证本 system 独占一个批次，
    /// order 再把这一批钉在前面：BreedingSystem 经 targets_ 指针读本 system
    /// 的产物，若它落在更早的批次，读到的是上一季的站点表与不存在的
    /// Feeding 组件，整季饥荒静默失效。二者没有任何共读写集，顺序完全靠
    /// 这两行声明。
    static constexpr int order = 0;

    using ReadSet = std::tuple<const Position, const CreaturePhenotype,
                               const SpeciesTag>;
    using WriteSet = std::tuple<Feeding>;

    void execute(World& world, CommandBuffer& cmd) override;

    /// 改写本 system 消费的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：本类在 execute 的第一行就把
    ///       season_index_ 交给 climate().advance()，之后改它不会有效果。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = season_index;
    }

    /// 传给 BreedingSystem 的站点表。环境层只说「哪个格、哪个物种、
    /// 该产几个」，空间分桶与 Parent::index 的翻译留在 adapt 层。
    [[nodiscard]] const std::vector<BreedingTarget>& targets() const noexcept
    {
        return targets_;
    }

private:
    EnvironmentWorld& env_;
    const gamev2::genetics::SpeciesRegistry& species_;
    std::uint32_t season_index_;
    std::vector<BreedingTarget> targets_;
};

}  // namespace gamev2::ecs::adapt

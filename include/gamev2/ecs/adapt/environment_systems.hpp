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
#include "gamev2/environment/terrain.hpp"

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

    /// 只读地形。**不给可变重载**：玩家改写地形属阶段 4，届时再补。
    /// MovementSystem 与 World::environment() 走的就是这一条。
    [[nodiscard]] const gamev2::environment::TerrainField& terrain()
        const noexcept;

private:
    gamev2::environment::EnvironmentConfig config_;
    gamev2::environment::TerrainField terrain_;
    gamev2::environment::ClimateField climate_;
    gamev2::environment::ResourceField resources_;
};

/// 逐个体写 Feeding，逐站点出 BreedingTarget。必须先于 BreedingSystem 跑完。
///
/// **自 tick 化起，World 不再把本 system 装进调度器**：那 8 个子步要由
/// World 逐步驱动，跨 tick 的位置关系调度器表达不了。ReadSet / WriteSet 与
/// `order` 在这条路径上都不参与——与 BreedingSystem 那份注释同理——实际
/// 次序由 `World::step_once`（季首跑 begin_season，其后每 tick 一步 step）
/// 与 `World::season_report`（finish 之后才 execute breeding）的显式序列
/// 钉死。
///
/// 下面三段讲的都是**为什么这些声明仍然保留**：别的装配下
/// `Scheduler::plan` 还要读它们，不要因为「本条路径不用」就顺手删掉。
///
/// 第一，`WriteSet` / `ReadSet` 只保证二者不同批，批次之间的先后由
/// `System::order` 表达；两者都改掉时本 system 会落在 BreedingSystem 之后，
/// 读到上一季的站点表，那只会让本季出生数按上一季结算。
///
/// 第二，Feeding 的首次写入是 `World::add`，即 archetype 迁移。它与另一
/// system 对同一批 slots_ 的无锁遍历并发相撞就是 use-after-free，而访问集
/// 表达不了这个风险——挡它的是本类的 `mutates_structure`，它让本 system 独占
/// 一个批次。
///
/// 第三，持 SpeciesRegistry 是为了与 BreedingSystem 对「谁参与繁殖」给出
/// 同一个答案：未注册的物种 id 若被本 system 放进来，它会占位、吃光本该属于
/// 真实物种的存量并出现在 targets() 里，而 BreedingSystem 会把它整个跳过
/// ——一个只消耗不繁殖的幽灵物种。
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

    /// 季首：推进气候、收集摄食输入、点一次格内人数。step 与 finish 的
    /// 全部输入都在这里备齐。
    ///
    /// 三段必须按 begin → step×steps_per_season → finish 的次序调用。
    /// 跨过 begin 直接 step 会拿空的 foragers 跑完一季，跨过 finish
    /// 直接让调用方结算则整季没有 Feeding——两者都不崩，只是让饥荒与
    /// 出生预算静默失效，因此次序写在这里而不靠运行时拦截。
    void begin_season(World& world);

    /// 跑一个季内子步。次数由调用方决定，等于 steps_per_season。
    /// \pre begin_season 已被调用。
    void step();

    /// 季末：把累计满意度写回 Feeding，按格与物种算子代预算填 targets_。
    /// \pre 本季的 step 已被调用 steps_per_season 次。
    void finish(World& world);

    /// 改写本 system 消费的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 begin_season 之前：本类在 begin_season 的第一行就把
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

    // begin_season 备齐、finish 消费。留在成员里而不是每季构造，是为了让
    // step() 无参：它每季被调用 steps_per_season 次，任何一次分配都会被
    // 乘上这个倍数。
    std::vector<EntityId> handles_;
    std::vector<gamev2::environment::Forager> foragers_;
    std::vector<std::uint32_t> species_keys_;
    gamev2::environment::SeasonFeeder feeder_{env_.config()};
};

}  // namespace gamev2::ecs::adapt

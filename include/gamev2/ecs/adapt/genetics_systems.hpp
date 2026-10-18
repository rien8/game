/// @file genetics_systems.hpp
/// @brief 遗传模块的 ECS 适配层：定义相关组件，并把繁殖、存活、统计装配成 System。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/population.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::ecs::adapt {

using gamev2::genetics::Genome;

struct SpeciesTag
{
    gamev2::genetics::SpeciesId species;
};

struct CreaturePhenotype
{
    gamev2::genetics::Phenotype phenotype;
};

struct Environment
{
    gamev2::environment::EnvironmentSnapshot snapshot;
};

struct Vitality
{
    /// 本季各项生理约束存活率之积，用 BreedingSystem 以 settle 的
    /// survival_rates 抄进存活个体。恒为 1.0 的默认值会让「这个季节过得
    /// 艰难」在面板上完全不可见，而它恰恰是玩家最需要看到的一条。
    float survival_rate = 1.0f;
    /// 本季的相对适应力，用 BreedingSystem 以 settle 的 season_fitness 抄进
    /// 存活个体。它是 UI 唯一能拿到的、经过生存率与繁殖力加权的选择依据，
    /// 恒为 1.0 的默认值会让面板上的「适应度」列变成装饰。
    float fitness = 1.0f;
    std::uint32_t age_seasons = 0;
};

/// 年龄递增 + 寿命硬判定。寿命是确定死亡，不是概率约束，
/// 因此不走 genetics::survival_rate。
///
/// 与 BreedingSystem 的先后自 tick 化起由 `World::season_report` 的显式
/// 序列保证，不再由调度器分层：本 system 仍留在调度器里、季首那一批就跑完，
/// 而 BreedingSystem 不进调度器，改由 World 在 EnvironmentSystem::finish
/// 之后直接 execute。
class AgingSystem final : public System
{
public:
    /// 本 system 写 Vitality、BreedingSystem 也写，二者访问集冲突——**能否
    /// 同批由访问集决定，不由 order 决定**。World 如今不再把 BreedingSystem
    /// 装进调度器，「必须排在 BreedingSystem 之前」这条约束由
    /// World::season_report 的显式序列兑现，order 在这条路径上不参与。
    ///
    /// 但不要因此删掉这个声明：若 World 改回把两者都装进调度器，访问集仍是
    /// 它们能否同批的唯一依据，order 只能决定哪一批在前。
    static constexpr int order = 0;

    /// 读集就是本 system 真的读到的那些组件：收集遍历取出 CreaturePhenotype
    /// 判寿命，Vitality 改年龄。它与 EnvironmentSystem 的读集毫无交集
    /// （本类 CreaturePhenotype + Vitality，对方 Position + CreaturePhenotype
    /// + SpeciesTag + Feeding），两个只读 system 在 plan 的判据下不冲突——
    /// 二者曾被塞进同一批并行派发，而 EnvironmentSystem 的 World::add 会迁移
    /// archetype、与本类对 slots_ 的无锁遍历撞成 use-after-free。这个形状现在
    /// 由 EnvironmentSystem::mutates_structure 挡住，不必在这里占位声明一个
    /// 读不到的组件。
    using ReadSet = std::tuple<const CreaturePhenotype>;
    using WriteSet = std::tuple<Vitality>;

    void execute(World& world, CommandBuffer& cmd) override;
};

/// 繁殖季结算。子代实体经 CommandBuffer 在帧末创建，
/// 整季换代因此是一次原子提交。
///
/// **必须排在 AgingSystem 之后**。settle 把 `Lifespan <= 0` 的个体判死，
/// 而 settle 收到的 age 恒为 0（SeasonInput 不携带年龄，递增由 AgingSystem
/// 负责）：Age 先跑，本季过寿的个体带着真实年龄进结算并当场出局；反过来
/// 则用上一季的年龄判定，等于让寿命判定整整慢一季。反序唯一可察觉的后果是
/// 一季的偏差，正因为它不崩不报错，这条先后才只能由一处显式序列显式钉死。
/// 自 tick 化起那一处是 World::season_report（EnvironmentSystem 必须排在它
/// 之前，否则 BreedingSystem 读到的是上一季的站点表与不存在的 Feeding，
/// 整季饥荒静默失效）；本类的 order 与 WriteSet 在这条路径上都不参与。
class BreedingSystem final : public System
{
public:
    /// 原有构造函数：不接入环境层，父池退化为「一物种一池 + progeny_for」，
    /// 行为与接入环境层之前逐位相同。
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index}
    {
    }

    /// 接入环境层：父池改用 targets 给的站点，feeding 取自 Feeding 组件，
    /// 新生个体落在其站点的格内。`targets` 为 nullptr 时行为与上一个
    /// 构造函数完全一致。
    ///
    /// \pre targets 若非空，其生命周期必须覆盖本 system 的下一次 execute——
    ///       指向的是 EnvironmentSystem::targets() 那张表，而后者每季 execute
    ///       都会 clear() 再重建。
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index,
                   const std::vector<BreedingTarget>* targets,
                   float mating_radius_cells)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index},
          targets_{targets}, mating_radius_cells_{mating_radius_cells}
    {
    }

    /// 改写本 season 抽签所用的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：execute 只在收集完所有个体之后
    ///       构造一次 RandomStreams{root_seed_, season_index_}，之后就再也不读
    ///       season_index_，因此 execute 期间改它完全无效。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = static_cast<std::uint64_t>(season_index);
    }

    /// 只读并拷贝出组件；Vitality 只被就地改写，结构变更全走队列。
    ///
    /// `const Feeding` 是一条真的读：收集遍历逐个体取出它，取到的是喂给
    /// settle 的那份 feedings——注意只在接入环境层时（targets_ != nullptr）
    /// 这份取值才真的进 settle，未接入时给的是空 span，取值被 settle 的
    /// 1.0f 降级分支顶掉。与 EnvironmentSystem 的 WriteSet 之间它同时构成
    /// 一条真的读写冲突，但那一条并不是结构变更风险的挡法——那个形状由
    /// EnvironmentSystem::mutates_structure 独占批次来兜。
    /// `const Position` 是一条真的读：空间分桶逐个体取出它算所在格。
    /// 它以前漏在这里，于是 MovementSystem 写 Position 与本 system 读
    /// Position 这条冲突表达不出去——访问集不相交的两个 system 会被排进
    /// 同一批并行执行，而 System::order 只在「本来就会冲突」时才等于批次
    /// 先后（见 system.hpp 的 \warning）。补上它，「移动先于繁殖」才成立。
    using ReadSet = std::tuple<const Genome, const CreaturePhenotype,
                               const Environment, const SpeciesTag,
                               const Feeding, const Position>;
    using WriteSet = std::tuple<Vitality>;

    /// 排在 AgingSystem、EnvironmentSystem 与 MovementSystem 之后。与前两者
    /// 的冲突分别由 WriteSet（都写 Vitality）与 EnvironmentSystem::
    /// mutates_structure 兜底，与 MovementSystem 的由 Position 的读写兜底，
    /// order 负责让本 system 落进更晚的那一批。
    ///
    /// 在此之前这条约束只写在注释里，靠 plan() 恰好按注册顺序分层成立：
    /// 按 Breeding、Environment、Aging 的顺序注册，plan 会排出
    /// batch{Breeding} → batch{Environment}，Breeding 于是读到上一季的
    /// targets() 和尚不存在的 Feeding 组件，整季饥荒静默失效且不报错。
    static constexpr int order = 1;

    /// \pre 本缓冲器必须在本次 execute 返回后才 flush：newborn_ 是
    /// cmd.create 的回写目标，下一轮 execute 会覆盖它。
    void execute(World& world, CommandBuffer& cmd) override;

private:
    const gamev2::genetics::LocusTable& table_;
    const gamev2::genetics::SpeciesRegistry& species_;
    const gamev2::genetics::GeneticsConfig& config_;
    std::uint64_t root_seed_;
    std::uint64_t season_index_;

    /// 逐物种的近交系数，下标 = SpeciesId::value - 1。每季由 settle 从**该
    /// 物种**的亲代池算出的 offspring_inbreeding 按下标写回，**不需要调用方
    /// 喂**——它来自 breeding 自己的亲代池，而走 statistics 的 f_coefficient
    /// 那条路会被地图的生态结构污染（见 genetics/survival.hpp 的
    /// SurvivalInput::inbreeding）。
    std::vector<float> inbreeding_;

    /// nullptr 表示未接入环境层。指针而非引用：调用方可能这一季还没有
    /// 环境系统跑过，那时给一个空引用比给 nullptr 更难查。
    const std::vector<BreedingTarget>* targets_ = nullptr;
    /// 「父池」的空间半径，单位为格，交给 adapt 层做分桶——Parent::index
    /// 必须是本物种数组内的下标，那个数组由 execute 切出，环境层无从知道
    /// 它的形状。
    float mating_radius_cells_ = 0.0f;

    /// cmd.create 的回写目标。execute 返回后栈上的局部量已经失效，而
    /// flush 发生在 execute 之后，新句柄只能寄存在成员里。
    std::vector<EntityId> newborn_;
};

/// 群体统计。结果由调用方取走——系统不往 World 之外写全局状态，
/// 统计结果的生命周期归调用方管。
class PopulationStatisticsSystem final : public System
{
public:
    explicit PopulationStatisticsSystem(
        const gamev2::genetics::SpeciesRegistry& species)
        : species_{species}
    {
    }

    using ReadSet = std::tuple<const Genome, const SpeciesTag>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer& cmd) override;

    [[nodiscard]] const std::vector<gamev2::genetics::SpeciesStatistics>&
    statistics() const noexcept
    {
        return stats_;
    }

private:
    const gamev2::genetics::SpeciesRegistry& species_;
    std::vector<gamev2::genetics::SpeciesStatistics> stats_;
};

}  // namespace gamev2::ecs::adapt

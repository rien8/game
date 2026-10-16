/// @file genetics_systems.hpp
/// @brief 遗传模块的 ECS 适配层：定义相关组件，并把繁殖、存活、统计装配成 System。
#pragma once

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
/// 与 BreedingSystem 的先后要求见 BreedingSystem 的说明：两者同写
/// Vitality，调度器会强制分层，但批次之间的先后不是它能表达的。
class AgingSystem final : public System
{
public:
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
/// 一季的偏差，正因为它不崩不报错，才只能靠这份文档守住。
///
/// WriteSet 已经表达了「不得并行」——两者都写 Vitality，Scheduler 的冲突
/// 检测必然把二者分进不同批次。它表达不了的是「先后」：批次之间的次序由
/// plan() 的贪心分层决定，不是声明式的顺序契约（要它得另设一个 before /
/// after 声明，目前只有一个这样的约束，不值得为它改 Scheduler 的接口）。
class BreedingSystem final : public System
{
public:
    BreedingSystem(const gamev2::genetics::LocusTable& table,
                   const gamev2::genetics::SpeciesRegistry& species,
                   const gamev2::genetics::GeneticsConfig& config,
                   std::uint64_t root_seed, std::uint64_t season_index)
        : table_{table}, species_{species}, config_{config},
          root_seed_{root_seed}, season_index_{season_index}
    {
    }

    /// 只读并拷贝出组件；Vitality 只被就地改写，结构变更全走队列。
    using ReadSet = std::tuple<const Genome, const CreaturePhenotype,
                               const Environment, const SpeciesTag>;
    using WriteSet = std::tuple<Vitality>;

    /// \pre 本缓冲器必须在本次 execute 返回后才 flush：newborn_ 是
    /// cmd.create 的回写目标，下一轮 execute 会覆盖它。
    void execute(World& world, CommandBuffer& cmd) override;

private:
    const gamev2::genetics::LocusTable& table_;
    const gamev2::genetics::SpeciesRegistry& species_;
    const gamev2::genetics::GeneticsConfig& config_;
    std::uint64_t root_seed_;
    std::uint64_t season_index_;

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

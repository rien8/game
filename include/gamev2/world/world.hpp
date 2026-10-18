/// @file world.hpp
/// @brief 世界模块：把 ecs / genetics / environment 装配成一个会自己按季
///        推进的整体，并给出与真实存续状态一致的季末报告。
///
/// 一季的时序只有这一处实现。三个前序模块各自用 System::order 注释守住的
/// 先后约束，在这里第一次被真正执行一次。
#pragma once

#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/adapt/movement_system.hpp"
#include "gamev2/ecs/adapt/speciation.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/config.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/species.hpp"
#include "gamev2/genetics/statistics.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::world {

/// 一次季节结算的结果。
struct SeasonReport
{
    std::uint32_t season = 0;
    /// 各物种 census 之和，即本季存活总数。判据与统计 system 相同：
    /// 缺 Genome 或引用未注册物种的实体不在其中。
    std::size_t population = 0;
    /// 下标 = SpeciesId::value - 1。拷贝而非引用：统计 system 的成员缓冲
    /// 会被下一季 clear() 重写，而报告要活得比下一季久。拷贝代价与物种数
    /// 成正比而与个体数无关——AlleleStats 是逐位点的定长统计。
    std::vector<gamev2::genetics::SpeciesStatistics> species;
};

/// 装配并驱动整个世界。构造完即可放进初始个体并按季推进。
///
/// 本类不生成初始种群：调用方本来就必须知道怎么造 Genome 与 Phenotype，
/// 把它藏在这里只会多一层无法验证的隐式行为。
class World
{
public:
    /// \pre table 的生命周期必须覆盖本对象——它是唯一被引用的外部数据。
    /// \pre worker_count >= 1。
    ///
    /// 四个 special member 全部删除：Scheduler::entries_ 里存的是指向
    /// environment_ / aging_ / breeding_ 的裸 System*，而它们各自都持有引用
    /// 成员（environment_ 指向 env_，breeding_ 指向 genetics_config_ 与调用方
    /// 的 table / species）——移动一次就会留下一堆指向旧对象的指针。
    /// （今天 ecs::World 不可移动已隐式挡掉了移动，但那是传递来的保证，
    /// 不是本类自己声明的。）
    World(const gamev2::genetics::LocusTable& table,
          gamev2::genetics::GeneticsConfig genetics_config,
          gamev2::environment::EnvironmentConfig environment_config,
          std::uint64_t root_seed,
          std::size_t worker_count = 1);

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    /// \pre 全部物种必须在第一次 run_season 之前登记完，之后不再登记。
    ///       未注册的 id 会让对应实体被 EnvironmentSystem 与
    ///       BreedingSystem 各自跳过，而后者报一条诊断。
    gamev2::genetics::SpeciesRegistry& species() noexcept
    {
        return species_;
    }

    /// 初始个体由调用方经这里放进世界。
    ///
    /// 一个参与世界运转的个体必须带齐六个组件：Genome、CreaturePhenotype、
    /// Environment、SpeciesTag、Position、Vitality。缺其中任何一个的后果见
    /// 实现处与设计文档 §5，其中最反直觉的是缺 Position——该个体参与存活
    /// 抽签，但不被摄食、不进任何父池，于是永不繁殖也永不被环境杀死。
    gamev2::ecs::World& entities() noexcept
    {
        return entities_;
    }

    /// 只读暴露气候场与资源场。测试要读真实承载力，UI 要画地图。
    const gamev2::ecs::adapt::EnvironmentWorld& environment() const noexcept
    {
        return env_;
    }

    /// 推进一季并返回季末报告。报告的 census 落在 flush 之后，因此等于
    /// 本季真实的存活存续数（死亡已生效、新生代已计入）。
    ///
    /// \pre 不可重入，不可多线程并发调用。
    [[nodiscard]] SeasonReport run_season();

    /// 推进一个季内子步。一季 = config.steps_per_season 次。
    ///
    /// 第一次调用顺带跑完「季首」那一步：推进三处季号、跑 MovementSystem
    /// 与 AgingSystem、推进气候、收集摄食输入。之后每次只跑一步摄食。
    ///
    /// \pre 不可重入，不可多线程并发调用（同 run_season）。
    void step_once();

    /// 季末结算并返回报告：算子代预算、繁殖、换代、统计。返回后世界停在
    /// 下一季的开头，tick_in_season() 归零。
    ///
    /// \pre 本季已被 step_once 推进 config.steps_per_season 次。少跑会
    ///      让摄食累加器少累一轮、季末 feeding 偏高——不崩，只是静默偏松，
    ///      因此这里报一条 WORLD_INCOMPLETE_SEASON 诊断而非拦下。
    [[nodiscard]] SeasonReport season_report();

    /// 本季已推进的子步数。0 表示「季末刚过或尚未开始」，两种状态对调用方
    /// 是同一个——渲染层看到 0 就该准备进入下一季。
    [[nodiscard]] std::uint32_t tick_in_season() const noexcept
    {
        return tick_;
    }

    /// 一季有几个季内子步。GameSession 用它把一季补齐。
    [[nodiscard]] std::uint32_t steps() const noexcept
    {
        return env_.config().steps_per_season;
    }

    /// 上一季的裂变与灭绝事件。**只在 `season_report()` 之后有效**：开局两者
    /// 都是空。
    [[nodiscard]] std::pair<
        std::vector<gamev2::ecs::adapt::BornSpecies>,
        std::vector<gamev2::genetics::SpeciesId>>
    speciation_report() const noexcept
    {
        return {born_, lost_};
    }

    /// 上一季的季末报告。给需要把报告与 `speciation_report()` 对齐的调用方
    /// ——`run_season()` 只返回报告本身，而裂变事件得单独取，两者不同源时
    /// 会静默错一季。
    [[nodiscard]] SeasonReport last_season() const noexcept
    {
        return last_;
    }

    /// 逐季推进 count 次。与调用 count 次 run_season() 逐位等价——
    /// 这条等价性是可测的，也是日后加存档的前提。
    [[nodiscard]] std::vector<SeasonReport> run_seasons(std::uint32_t count);

private:
    /// 季首：推进三处季号，跑移动与老化那一批，再收集摄食输入。
    /// 只由 step_once 在 tick_ == 0 时调用。
    void open_season();

    gamev2::genetics::GeneticsConfig genetics_config_;
    gamev2::genetics::SpeciesRegistry species_;
    std::uint64_t root_seed_;
    std::uint32_t season_ = 0;
    std::uint32_t tick_ = 0;

    gamev2::ecs::World entities_;
    gamev2::ecs::CommandBuffer cmd_;
    gamev2::ecs::Scheduler scheduler_;

    // 声明顺序即依赖顺序：breeding_ 的构造参数要取
    // &environment_.targets()，而 env_ 又是 environment_ 的引用对象。
    // 逆序析构恰好把这三条依赖倒过来。
    //
    // breeding_ 必须收 genetics_config_ 这个成员而不是构造函数的按值形参：
    // BreedingSystem 把配置存成 const 引用，形参一返回引用就悬垂，而它要到
    // 每季 execute 里的 settle 才被读。
    //
    // movement_ 只收标量（root_seed 与季号），不持有任何引用，因此它在声明
    // 顺序里的位置不影响正确性；放这里只是为了与它在调度器中的注册顺序对齐。
    gamev2::ecs::adapt::EnvironmentWorld env_;
    gamev2::ecs::adapt::MovementSystem movement_;
    gamev2::ecs::adapt::EnvironmentSystem environment_;
    gamev2::ecs::adapt::AgingSystem aging_;
    gamev2::ecs::adapt::BreedingSystem breeding_;
    // 声明在 statistics_ 之后是为了让两者的相对位置一眼可见；**实际次序由
    // season_report 的语句顺序钉死，不由声明顺序** —— 裂变必须排在统计之前，
    // 否则季末报告会把刚裂出来的物种连同它的全部个体一起漏掉。
    gamev2::ecs::adapt::SpeciationSystem speciation_;
    gamev2::ecs::adapt::PopulationStatisticsSystem statistics_;

    // 拷贝而非引用：SpeciationSystem 每季 clear() 再重建，而读数要活得比
    // 下一季久。
    std::vector<gamev2::ecs::adapt::BornSpecies> born_;
    std::vector<gamev2::genetics::SpeciesId> lost_;
    SeasonReport last_;
};

}  // namespace gamev2::world

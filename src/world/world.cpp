#include "gamev2/world/world.hpp"

#include <string>

namespace gamev2::world {

using gamev2::ecs::Diagnostic;
using gamev2::ecs::DiagnosticLevel;

World::World(const gamev2::genetics::LocusTable& table,
             gamev2::genetics::GeneticsConfig genetics_config,
             gamev2::environment::EnvironmentConfig environment_config,
             const std::uint64_t root_seed,
             const std::size_t worker_count)
    : genetics_config_{genetics_config},
      root_seed_{root_seed},
      entities_{},
      cmd_{},
      scheduler_{worker_count},
      env_{root_seed, environment_config},
      movement_{root_seed, 0, env_.resources(), env_.terrain(),
                environment_config.gradient_weight},
      environment_{env_, species_, 0},
      aging_{},
      // table 不落成成员：构造函数的 const 引用形参绑的就是调用方的对象，
      // 与 genetics_config_ 那份按值形参不同，不会构造一返回即悬垂。
      breeding_{table, species_, genetics_config_, root_seed, 0,
                &environment_.targets(),
                environment_config.mating_radius_cells},
      speciation_{species_, env_},
      statistics_{species_}
{
    // 调度器现在只装 MovementSystem 与 AgingSystem。两者访问集不相交
    // （movement_system.hpp 的 ReadSet/WriteSet 说明了为什么），因此排成
    // 一批并发执行——这是 World 层唯一的并发点，也是 tick 化之后仍然
    // 保留的那一处。
    //
    // EnvironmentSystem 与 BreedingSystem 刻意不进：前者的 8 个子步要由
    // World 逐步驱动（跨 tick 的位置关系调度器表达不了），后者必须在
    // EnvironmentSystem::finish 之后、换代 flush 之前跑，与
    // PopulationStatisticsSystem 是同一条理由——直接 execute，位置由 World
    // 的显式序列钉死。
    scheduler_.add(movement_);
    scheduler_.add(aging_);
    scheduler_.plan();

    // statistics_ 刻意不进调度器：它与另三个 system 的访问集相交（都碰
    // SpeciesTag），注册进去既省不下任何并行，又只能在 cmd.flush 之前
    // 跑到——而 BreedingSystem 的死亡销毁与子代创建都在 flush 才生效，
    // 于是 census 会含本季将死者、不含刚出生的子代。
    //
    // speciation_ 同样不进，理由更硬：它读的是**刚 flush 出来的**实体表，
    // 而调度器的批次边界在 flush 之前，因此没有任何一种注册方式能让它
    // 排在换代之后、统计之前。
}

void World::step_once()
{
    if (tick_ == 0)
    {
        open_season();
    }
    environment_.step();
    ++tick_;
}

void World::open_season()
{
    // 三处 season_index_ 都在各自消费它的入口一次性使用（移动的随机流、
    // climate().advance、RandomStreams），因此推进必须整体发生在它们各自
    // 读之前。
    movement_.advance_season(season_);
    environment_.advance_season(season_);
    breeding_.advance_season(season_);

    // 季首这一批：移动与老化排在摄食之前，与 EnvironmentSystem 的
    // order = 0 那条约束同源——摄食要按当季坐标分格。
    scheduler_.run(entities_, cmd_);

    environment_.begin_season(entities_);
}

SeasonReport World::season_report()
{
    if (tick_ != env_.config().steps_per_season)
    {
        entities_.report(Diagnostic{
            DiagnosticLevel::error, "WORLD_INCOMPLETE_SEASON",
            "season_report called after " + std::to_string(tick_)
                + " of "
                + std::to_string(env_.config().steps_per_season)
                + " substeps"});
    }

    environment_.finish(entities_);

    // BreedingSystem 经 targets_ 指针读 EnvironmentSystem 的产物，因此它
    // 必须排在 finish 之后。
    breeding_.execute(entities_, cmd_);

    // 换代在这里生效。调度器在它自己的 run 末尾 flush 一次，但此刻不在
    // run 内部——breeding 直接 execute 入的是 cmd_，需要 World 自己冲。
    cmd_.flush(entities_);

    // 裂变排在换代之后、统计之前：它消费的就是这张刚 flush 出来的表，
    // 而排在统计之后会让季末报告把刚裂出来的物种连同它的全部个体漏掉。
    speciation_.execute(entities_, cmd_);
    cmd_.flush(entities_);
    born_ = speciation_.born();
    lost_ = speciation_.lost();

    // 统计落在换代之后，因此 census 含刚出生的子代、不含本季将死者。
    statistics_.execute(entities_, cmd_);

    last_ = SeasonReport{season_, 0, statistics_.statistics()};
    for (const auto& s : last_.species)
    {
        last_.population += s.census;
    }
    ++season_;
    tick_ = 0;
    return last_;
}

SeasonReport World::run_season()
{
    for (std::uint32_t i = 0; i < env_.config().steps_per_season; ++i)
    {
        step_once();
    }
    return season_report();
}

std::vector<SeasonReport> World::run_seasons(const std::uint32_t count)
{
    std::vector<SeasonReport> reports;
    reports.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        reports.push_back(run_season());
    }
    return reports;
}

}  // namespace gamev2::world

#include "gamev2/world/world.hpp"

namespace gamev2::world {

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
      movement_{root_seed, 0},
      environment_{env_, species_, 0},
      aging_{},
      // table 不落成成员：构造函数的 const 引用形参绑的就是调用方的对象，
      // 与 genetics_config_ 那份按值形参不同，不会构造一返回即悬垂。
      breeding_{table, species_, genetics_config_, root_seed, 0,
                &environment_.targets(),
                environment_config.mating_radius_cells},
      statistics_{species_}
{
    // 注册顺序不参与调度顺序：plan() 先按 System::order 稳定排序再贪心分层
    // （MovementSystem 为 -1，EnvironmentSystem 与 AgingSystem 为 0，
    // BreedingSystem 为 1）。这里只决定同 order 的 system 谁先占住靠前的批次。
    scheduler_.add(movement_);
    scheduler_.add(environment_);
    scheduler_.add(aging_);
    scheduler_.add(breeding_);
    scheduler_.plan();

    // statistics_ 刻意不进调度器：它与另三个 system 的访问集相交（都碰
    // SpeciesTag），注册进去既省不下任何并行，又只能在 cmd.flush 之前
    // 跑到——而 BreedingSystem 的死亡销毁与子代创建都在 flush 才生效，
    // 于是 census 会含本季将死者、不含刚出生的子代。
}

SeasonReport World::run_season()
{
    // 三处 season_index_ 都在各自 execute 的开头消费一次，因此推进必须
    // 整体发生在 scheduler_.run 之前。
    movement_.advance_season(season_);
    environment_.advance_season(season_);
    breeding_.advance_season(season_);

    scheduler_.run(entities_, cmd_);

    // 换代（死亡销毁与子代创建）归 Scheduler::run：它把各 system 的缓冲并进
    // cmd 之后就在末尾 cmd.flush(world)（src/ecs/scheduler.cpp）。**此处不得
    // 再加一次 flush。** 它不是无害的重复：多一行 flush，「统计落在换代之前」
    // 这个回归在代码结构上就看不出来了——后续的变异验证会删掉这行、得到
    // 全绿，然后误判成测试没有鉴别力。真正的约束是下面这行 statistics_ 必须
    // 排在 scheduler_.run 之后：census 要含刚出生的子代、不含本季将死者。
    statistics_.execute(entities_, cmd_);

    SeasonReport report{season_, 0, statistics_.statistics()};
    for (const auto& s : report.species)
    {
        report.population += s.census;
    }
    ++season_;
    return report;
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

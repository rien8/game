/// @file setup.cpp
/// @brief 众生层：从当代格局建实体、回读名册。
#include "gamev2/life/interface.hpp"
// 私有头与本文件同目录，不在也不该在 life 目标的包含路径里。
#include "generate.hpp"
#include "systems.hpp"
#include "gamev2/ecs/scheduler.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace gamev2::life {

Life::Life(const gamev2::world::World& world,
           const gamev2::faction::EraReport& era,
           LifeDiagnostics& diagnostics)
    : world_(world),
      era_(era),
      diagnostics_(diagnostics)
{
}

void Life::seed()
{
    // 当代格局是众生的出身地：没有当代（台账为空）就没有可初始化的
    // 众生。这是退化不是错误，由诊断报出（设计 §7）。
    if (era_.veins.empty())
    {
        ++diagnostics_.no_contemporary_state;
        return;
    }

    const std::vector<PersonInit> people =
        generate_population(world_.params().seed);

    souls_.reserve(people.size());
    for (std::uint32_t i = 0; i < people.size(); ++i)
    {
        const PersonInit& p = people[i];
        const gamev2::ecs::EntityId e = ecs_world_.create();
        souls_.push_back(e);
        (void)ecs_world_.add<SoulId>(e, SoulId{i, p.name, p.sect});
        (void)ecs_world_.add<Cultivation>(e, Cultivation{p.realm});
        (void)ecs_world_.add<Aptitude>(e, p.aptitude);
        (void)ecs_world_.add<Temper>(e, p.temper);
        (void)ecs_world_.add<Mortal>(e, Mortal{era_.now - p.age, p.age});
        (void)ecs_world_.add<Office>(e, Office{p.office});
    }

    // 名册从实体回读，不从 PersonInit 抄：这样「组件真的挂上了」是
    // 被名册测试顺手钉住的事实（照 faction collect() 的先例）。
    roster_.year = era_.now;
    roster_.entries.reserve(souls_.size());
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = ecs_world_.get_if<SoulId>(e);
        const auto* cult = ecs_world_.get_if<Cultivation>(e);
        const auto* apt = ecs_world_.get_if<Aptitude>(e);
        const auto* temper = ecs_world_.get_if<Temper>(e);
        const auto* mortal = ecs_world_.get_if<Mortal>(e);
        const auto* office = ecs_world_.get_if<Office>(e);
        // 这六件都是本函数亲手挂上的：缺任何一件是内部错误，不是要
        // 兜住的运行时情形。
        assert(id != nullptr && cult != nullptr && apt != nullptr
               && temper != nullptr && mortal != nullptr
               && office != nullptr);

        RosterEntry entry;
        entry.soul = id->index;
        entry.name = id->name;
        entry.sect = id->sect;
        entry.office = office->kind;
        entry.realm = cult->realm;
        entry.age = mortal->age;
        entry.temper = *temper;
        entry.aptitude = *apt;
        roster_.entries.push_back(entry);
    }

    // 时钟从当代起算：开局即 8914，run_years 每年推一格。
    clock_ = ecs_world_.create();
    (void)ecs_world_.add<LifeClock>(clock_, LifeClock{era_.now});
    // 新生序号从初始人口总数起：初始队列的 index 全部小于它。
    next_index_ = static_cast<std::uint32_t>(roster_.entries.size());
}

std::uint32_t Life::year()
{
    auto* clock = ecs_world_.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // 时钟是 seed 亲手挂的：缺了是内部错误
    return clock->year;
}

void Life::run_years(std::uint32_t n)
{
    // 注册顺序即执行顺序（Scheduler::plan 按注册顺序贪心分层，冲突者
    // 落后一批）：Clock 写年，另三个读它（各自读集里都有 const
    // LifeClock）；Aging 写 Mortal，另两个不碰。串行是刻意的：本层
    // 规模下并行零收益，each_parallel 的行为又依赖批内位置。
    ecs::Scheduler scheduler(1);
    ClockSystem clock(clock_);
    AgingSystem aging(souls_);
    BirthSystem birth(world_.params().seed, clock_, souls_, next_index_);
    CultivateSystem cultivate(world_.params().seed, clock_, souls_);
    scheduler.add(clock);
    scheduler.add(aging);
    scheduler.add(birth);
    scheduler.add(cultivate);
    scheduler.plan();

    ecs::CommandBuffer cmd;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        scheduler.run(ecs_world_, cmd);
    }
}

}  // namespace gamev2::life

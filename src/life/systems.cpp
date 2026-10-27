/// @file systems.cpp
/// @brief 众生层：年循环的四个 system。
#include "systems.hpp"
// 私有头与本文件同目录，不在也不该在 life 目标的包含路径里。
#include "generate.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

namespace gamev2::life {

ClockSystem::ClockSystem(gamev2::ecs::EntityId clock) : clock_(clock)
{
}

void ClockSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer&)
{
    auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // seed 亲手挂的：缺了是内部错误
    ++clock->year;
}

AgingSystem::AgingSystem(
    const std::vector<gamev2::ecs::EntityId>& souls)
    : souls_(souls)
{
}

void AgingSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    // 遍历序号表而非 view.each：销毁需要 EntityId，而 each 只给组件
    // 引用。空 get_if = 死者留下的空位，跳过。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* mortal = world.get_if<Mortal>(e);
        const auto* cult = world.get_if<Cultivation>(e);
        if (mortal == nullptr || cult == nullptr)
        {
            continue;
        }
        ++mortal->age;
        // 寿元上限只查表不存表：kRealmLifespan 是设定 §5.1 的逐字
        // 落地、唯一权威（刀 1 的 Mortal 注释）。
        const auto realm = static_cast<std::size_t>(cult->realm);
        if (mortal->age >= kRealmLifespan[realm])
        {
            cmd.destroy(e);
        }
    }
}

BirthSystem::BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                         std::vector<gamev2::ecs::EntityId>& souls,
                         std::uint32_t& next_index)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      next_index_(next_index)
{
}

void BirthSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);

    // 各宗在册数：序号表里活着且有宗籍的。空 get_if = 已死。
    std::array<std::uint32_t, 7> alive{};
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        if (id == nullptr)
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(id->sect);
        if (sect < alive.size())
        {
            ++alive[sect];
        }
    }

    auto recruit = [&](history::Sect sect, std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            // 序号从 next_index_ 起单调：初始队列的 index 全部小于它，
            // 新老按号可分（差异与补齐 §6）。姓名/属性与 make_person
            // 同一套派生。
            const std::uint32_t index = next_index_++;
            const PersonInit p =
                make_person(seed_, index, sect, Realm::yinqi);
            const gamev2::ecs::EntityId e = world.create();
            souls_.push_back(e);
            (void)cmd.add<SoulId>(e,
                                  SoulId{index, p.name, p.sex, p.sect});
            (void)cmd.add<Cultivation>(e, Cultivation{p.realm, 0u});
            (void)cmd.add<Aptitude>(e, p.aptitude);
            (void)cmd.add<Temper>(e, p.temper);
            (void)cmd.add<Mortal>(
                e, Mortal{clock->year - kMinAge, kMinAge});
            // 新入门是外门：晋升是刀 4 的事。
            (void)cmd.add<Office>(
                e, Office{sect == history::Sect::unowned
                              ? OfficeKind::none
                          : OfficeKind::outer});
        }
    };

    // 宗门年收徒：max(1, 在册 / 除数)。标定值，锚点见常数注释。
    for (std::size_t s = 0; s < alive.size(); ++s)
    {
        const std::uint32_t n = alive[s] / kSectRecruitDivisor;
        recruit(static_cast<history::Sect>(s), n > 0u ? n : 1u);
    }

    // 散修每十年多一批（设定 §8.3）。年份取模而非计数器：重放即
    // 存档（设计 §4.4），口径不依赖「跑没跑过上一个十年」。
    if (clock->year % kFreeBatchYears == 0u)
    {
        recruit(history::Sect::unowned, kFreeBatch);
    }
}

CultivateSystem::CultivateSystem(
    std::uint64_t seed, gamev2::ecs::EntityId clock,
    const std::vector<gamev2::ecs::EntityId>& souls)
    : seed_(seed), clock_(clock), souls_(souls)
{
}

void CultivateSystem::execute(gamev2::ecs::World& world,
                              gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* cult = world.get_if<Cultivation>(e);
        const auto* apt = world.get_if<Aptitude>(e);
        if (cult == nullptr || apt == nullptr)
        {
            continue;  // 死者留下的空位
        }
        if (cult->realm == Realm::dacheng)
        {
            continue;  // 大乘不再进（kRealmYears 的哨兵 0）
        }
        ++cult->progress;
        const auto realm = static_cast<std::size_t>(cult->realm);
        if (cult->progress < kRealmYears[realm])
        {
            continue;
        }
        // 刀 2 假设供给充足：设定 §5.1 的资源门槛不记账，记账是刀 3
        // 的 Purse。这里只有年资 + 悟性两轴，常数是标定值。
        std::int32_t chance =
            static_cast<std::int32_t>(kBreakthroughBasePermille);
        chance += (static_cast<std::int32_t>(apt->comprehension) - 5)
                  * static_cast<std::int32_t>(
                        kBreakthroughPerAptitudePermille);
        chance = std::clamp(
            chance, static_cast<std::int32_t>(kBreakthroughMinPermille),
            static_cast<std::int32_t>(kBreakthroughMaxPermille));

        // 掷运按 (seed, 流号, 年, 实体槽号) 无状态派生：调整遍历次序
        // 不得换一个世界（设计 §4.1）。
        const auto stream =
            static_cast<std::uint32_t>(world::Stream::life);
        world::Pcg32 rng(world::stage_seed(
            world::stage_seed(world::stage_seed(seed_, stream),
                              clock->year),
            e.index()));
        if (static_cast<std::int32_t>(rng.below(1000u)) < chance)
        {
            cult->realm = static_cast<Realm>(realm + 1u);
            cult->progress = 0u;
        }
        // 失败：年资保留，下一年再试。
    }
}

}  // namespace gamev2::life

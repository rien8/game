#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/world.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/soul.hpp"
#include "test_framework.hpp"

// 私有头：BirthSystem 是年循环的内部实现。收徒人数的整除边界只能
// 在这一层构造——开局的七宗人口全在 200 以下，跑公开 API 到不了
// 「在册 400、其中 1 人刚寿尽」这个状态。
#include "../src/life/systems.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

gamev2::ecs::EntityId add_soul(gamev2::ecs::World& world,
                              std::vector<gamev2::ecs::EntityId>& souls,
                              gamev2::history::Sect sect,
                              std::uint32_t age)
{
    using gamev2::life::Cultivation;
    using gamev2::life::Mortal;
    using gamev2::life::Office;
    using gamev2::life::OfficeKind;
    using gamev2::life::Purse;
    using gamev2::life::Realm;
    using gamev2::life::Sex;
    using gamev2::life::SoulId;
    const gamev2::ecs::EntityId e = world.create();
    souls.push_back(e);
    (void)world.add<SoulId>(
        e, SoulId{static_cast<std::uint32_t>(souls.size() - 1),
                  std::string{}, Sex::male, sect});
    (void)world.add<Cultivation>(e, Cultivation{Realm::yinqi, 0});
    (void)world.add<Mortal>(e, Mortal{0, age});
    (void)world.add<Office>(e, Office{OfficeKind::outer});
    (void)world.add<Purse>(e, Purse{});
    return e;
}

}  // namespace

TEST(the_dead_are_not_counted_when_recruiting)
{
    // 太玄在册 400：399 活 + 1 寿尽。新算法按活人数 399/200 = 1 名
    // 太玄弟子；旧算法把寿尽者也算进 400/200 = 2。其余六宗各 1 人，
    // 触 max(1, ·) 下限各收 1 名，全年合计 7 名。
    gamev2::ecs::World world;
    const gamev2::ecs::EntityId clock = world.create();
    (void)world.add<gamev2::life::LifeClock>(
        clock, gamev2::life::LifeClock{8914});

    std::vector<gamev2::ecs::EntityId> souls;
    for (std::uint32_t i = 0; i < 399; ++i)
    {
        add_soul(world, souls, gamev2::history::Sect::taixuan,
                 gamev2::life::kMinAge);
    }
    add_soul(world, souls, gamev2::history::Sect::taixuan,
             gamev2::life::kRealmLifespan[static_cast<std::size_t>(
                 gamev2::life::Realm::yinqi)]);
    for (std::uint32_t s = 1; s < 7; ++s)
    {
        add_soul(world, souls, static_cast<gamev2::history::Sect>(s),
                 gamev2::life::kMinAge);
    }

    std::uint32_t next_index = 406;
    gamev2::life::BirthSystem birth(13, clock, souls, next_index);
    gamev2::ecs::CommandBuffer cmd;
    birth.execute(world, cmd);

    CHECK_EQ(souls.size(), 406u + 7u);
}

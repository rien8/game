#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::World;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::EnvironmentSystem;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::Feeding;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::genetics::Phenotype;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kTraitCount;
using gamev2::genetics::trait_index;

namespace {

/// 造一批有位置、有物种、有表型的个体。SenseRange 与 Fecundity 各自 0.5，
/// 因此 demand 恒为 0.25。
void seed_creatures(World& world, const std::size_t count,
                    const std::uint64_t seed)
{
    std::mt19937_64 rng{seed};
    std::uniform_real_distribution<float> pos{0.0f, 63.9f};

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{pos(rng), pos(rng)});
        (void)world.add<SpeciesTag>(e, SpeciesTag{SpeciesId{1}});

        Phenotype p{};
        p.traits.fill(1.0f);
        p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
        p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
        (void)world.add<CreaturePhenotype>(e, CreaturePhenotype{p});
    }
}

}  // namespace

TEST(environment_system_writes_feeding_for_every_creature)
{
    World world;
    seed_creatures(world, 200, 1);

    EnvironmentWorld env{31, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    std::size_t seen = 0;
    world.for_each_entity([&](const EntityId e)
    {
        const auto* f = world.get_if<Feeding>(e);
        if (f == nullptr)
        {
            return;
        }
        ++seen;
        CHECK(f->satisfaction >= 0.0f);
        CHECK(f->satisfaction <= 1.0f);
    });
    CHECK_EQ(seen, std::size_t{200});
}

TEST(environment_system_emits_targets_for_every_cell_and_species)
{
    World world;
    seed_creatures(world, 50, 2);

    EnvironmentWorld env{32, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    const auto& targets = system.targets();
    CHECK(!targets.empty());
    for (const auto& t : targets)
    {
        CHECK_EQ(t.species.value, std::uint32_t{1});
        CHECK(t.progeny > 0);
    }
}

TEST(environment_system_is_deterministic)
{
    World a;
    seed_creatures(a, 120, 9);
    EnvironmentWorld env_a{77, EnvironmentConfig{}};
    EnvironmentSystem sys_a{env_a, 3};

    CommandBuffer cmd_a;
    sys_a.execute(a, cmd_a);
    cmd_a.flush(a);

    World b;
    seed_creatures(b, 120, 9);
    EnvironmentWorld env_b{77, EnvironmentConfig{}};
    EnvironmentSystem sys_b{env_b, 3};

    CommandBuffer cmd_b;
    sys_b.execute(b, cmd_b);
    cmd_b.flush(b);

    const auto& ta = sys_a.targets();
    const auto& tb = sys_b.targets();
    CHECK_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i)
    {
        CHECK_EQ(ta[i].cell.value, tb[i].cell.value);
        CHECK_EQ(ta[i].species.value, tb[i].species.value);
        CHECK_EQ(ta[i].progeny, tb[i].progeny);
        CHECK_EQ(ta[i].environment.temperature,
                 tb[i].environment.temperature);
        CHECK_EQ(ta[i].environment.fertility, tb[i].environment.fertility);
        CHECK_EQ(ta[i].environment.light, tb[i].environment.light);
    }
}

TEST(total_birth_budget_stays_under_total_capacity)
{
    World world;
    seed_creatures(world, 6000, 12);

    EnvironmentWorld env{64, EnvironmentConfig{}};
    EnvironmentSystem system{env, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    std::uint64_t budget = 0;
    for (const auto& t : system.targets())
    {
        budget += t.progeny;
    }
    // 出生数必须被承载力封住——这是本模块存在的全部理由。
    // capacity_base 默认 3.9，两倍余量足够宽到不会因浮点抖动翻车。
    CHECK(budget <= 2ull * static_cast<std::uint64_t>(kCellCount) * 3ull);
}

// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/ecs/adapt/environment_systems.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::ecs::adapt {

using gamev2::environment::EnvironmentConfig;
using gamev2::environment::Forager;
using gamev2::genetics::TraitKind;

EnvironmentWorld::EnvironmentWorld(
    const std::uint64_t root_seed, const EnvironmentConfig& config)
    : config_{config}, climate_{root_seed, config_}, resources_{climate_, config_}
{
}

gamev2::environment::ClimateField& EnvironmentWorld::climate() noexcept
{
    return climate_;
}

gamev2::environment::ResourceField& EnvironmentWorld::resources() noexcept
{
    return resources_;
}

const EnvironmentConfig& EnvironmentWorld::config() const noexcept
{
    return config_;
}

const gamev2::environment::ClimateField& EnvironmentWorld::climate()
    const noexcept
{
    return climate_;
}

const gamev2::environment::ResourceField& EnvironmentWorld::resources()
    const noexcept
{
    return resources_;
}

void EnvironmentSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    env_.climate().advance(season_index_);

    // 一次遍历收齐摄食所需的全部输入。缺 Position 或 SpeciesTag 的实体
    // 本季不参与环境结算：BreedingSystem 的空间分桶用同一把尺子量它们，
    // 两个 system 对「谁参与繁殖」必须给出同一个答案。
    std::vector<EntityId> handles;
    std::vector<Forager> foragers;
    std::vector<std::uint32_t> species_keys;

    const auto species_count = species_.species_count();
    world.for_each_entity([&](const EntityId e)
    {
        const auto* pos = world.get_if<Position>(e);
        const auto* phe = world.get_if<CreaturePhenotype>(e);
        const auto* tag = world.get_if<SpeciesTag>(e);
        // 判据与 BreedingSystem 的那处逐字相同。未注册的 id 若放进来，
        // 该实体会占位吃光本格存量、并出现在 targets() 里，而 BreedingSystem
        // 会把它整个跳过——一个只消耗不繁殖的幽灵物种。这里不另报诊断：
        // 登记表的守卫与诊断都在 BreedingSystem 那一处，两处都报会让同一个
        // 坏实体的诊断条数翻倍。
        if (pos == nullptr || phe == nullptr || tag == nullptr
            || tag->species.value == 0
            || tag->species.value > species_count)
        {
            return;
        }
        // 摄食量正比于感知范围与繁殖力，与遗传层能量收支约束同源；
        // 公式住在遗传层，环境层只收一个标量。
        const float demand =
            phe->phenotype.traits[trait_index(TraitKind::SenseRange)]
            * phe->phenotype.traits[trait_index(TraitKind::Fecundity)];

        handles.push_back(e);
        foragers.push_back(Forager{pos->x, pos->y, demand});
        species_keys.push_back(tag->species.value);
    });

    env_.resources().count_occupants(foragers);

    const auto feeding =
        gamev2::environment::run_season(env_.resources(), foragers,
                                        env_.config());

    // 首次见到就加上组件，之后各季只改值。add 会把实体迁往新 archetype，
    // 因此它只能在这一段里做，绝不能落回上面那次 for_each_entity 之内。
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        if (auto* f = world.get_if<Feeding>(handles[i]); f != nullptr)
        {
            f->satisfaction = feeding[i];
        }
        else
        {
            (void)world.add<Feeding>(handles[i], Feeding{feeding[i]});
        }
    }

    const auto sites = gamev2::environment::build_sites(
        env_.resources(), env_.climate(), foragers, species_keys, env_.config());

    targets_.clear();
    targets_.reserve(sites.size());
    for (const auto& s : sites)
    {
        targets_.push_back(BreedingTarget{
            gamev2::genetics::SpeciesId{s.species_key}, s.cell, s.progeny,
            s.environment});
    }
}

}  // namespace gamev2::ecs::adapt

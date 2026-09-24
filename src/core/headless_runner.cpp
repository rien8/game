// src/core/headless_runner.cpp
#include "core/headless_runner.hpp"

#include "core/ecs/snapshot.hpp"
#include "core/evolution.hpp"
#include "world/world_generator.hpp"

#include <format>
#include <iostream>
#include <random>
#include <span>
#include <string_view>

namespace game {

namespace {

constexpr std::string_view kEventEliteBorn        = "EliteBorn";
constexpr std::string_view kEventBossEvolved      = "BossEvolved";
constexpr std::string_view kEventEnvironmentShift = "EnvironmentShift";

auto random_seed() -> std::uint64_t {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

auto event_type_name(EventType t) -> std::string_view {
    switch (t) {
        case EventType::EliteBorn:        return kEventEliteBorn;
        case EventType::BossEvolved:      return kEventBossEvolved;
        case EventType::EnvironmentShift: return kEventEnvironmentShift;
    }
    return "Unknown";
}

auto mean_creature_energy(std::span<const ecs::CreatureSnapshot> creatures)
    -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) sum += c.energy;
    return sum / static_cast<float>(creatures.size());
}

auto print_periodic(Simulation& sim) -> void {
    std::cout << std::format(
        "[tick {:>5}] pop={:<4} mean_e={:.2f} world_e={:.0f}\n",
        sim.tick_count(), sim.creatures().size(),
        mean_creature_energy(sim.creatures()), sim.world_energy());
    for (const auto& e : sim.events()) {
        std::cout << std::format("         {}: {}\n",
                                  event_type_name(e.type), e.description);
    }
}

auto count_events_by_type(const std::vector<Event>& events)
    -> std::map<std::string, std::size_t> {
    std::map<std::string, std::size_t> counts;
    for (const auto& e : events) {
        counts[std::string{event_type_name(e.type)}] += 1;
    }
    return counts;
}

}  // namespace

auto HeadlessRunner::run(const Config& cfg) -> Summary {
    Config effective = cfg;
    if (effective.seed == 0) {
        effective.seed = random_seed();
    }

    World world = WorldGenerator(effective.map_w, effective.map_h)
                      .generate(effective.seed);
    Simulation sim(std::move(world), effective.seed, effective.sim_params);

    Summary summary;
    summary.seed = effective.seed;
    summary.initial_population = sim.creatures().size();

    float energy_accum = 0.0f;
    std::size_t sample_count = 0;
    std::uint64_t next_log_tick = effective.log_period;

    while (sim.tick_count() < effective.ticks) {
        sim.advance(effective.sim_params.tick_dt, 1.0f);

        if (sim.tick_count() >= next_log_tick) {
            energy_accum += mean_creature_energy(sim.creatures());
            sample_count += 1;
            print_periodic(sim);
            next_log_tick += effective.log_period;
        }
    }

    summary.total_ticks        = sim.tick_count();
    summary.final_population   = sim.creatures().size();
    summary.mean_energy        = sample_count > 0
                                     ? energy_accum / static_cast<float>(sample_count)
                                     : 0.0f;
    summary.final_world_energy = sim.world_energy();
    summary.events_by_type     = count_events_by_type(sim.events());
    return summary;
}

}  // namespace game

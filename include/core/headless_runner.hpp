// include/core/headless_runner.hpp
#pragma once

#include "core/simulation.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace game {

// Drives a Simulation without SDL: deterministic fixed-tick batch run with
// periodic stdout logging and a final aggregated Summary. See
// docs/superpowers/specs/2026-09-24-text-only-run-mode-design.md.
class HeadlessRunner {
public:
    struct Config {
        std::uint64_t       seed = 0;          // 0 means "random_seed()"
        std::size_t         ticks = 1000;
        std::size_t         log_period = 60;
        std::size_t         map_w = 160;
        std::size_t         map_h = 120;
        Simulation::Params  sim_params{};
    };

    struct Summary {
        std::uint64_t       total_ticks = 0;
        std::size_t         initial_population = 0;
        std::size_t         final_population = 0;
        float               mean_energy = 0.0f;        // time-averaged sample mean
        float               final_world_energy = 0.0f;
        std::map<std::string, std::size_t> events_by_type;
        std::uint64_t       seed = 0;
    };

    [[nodiscard]] auto run(const Config& cfg) -> Summary;
};

}  // namespace game

// include/core/ecs/systems.hpp
#pragma once

#include "core/ecs/concepts.hpp"
#include "core/ecs/registry.hpp"

#include <cstdint>
#include <random>

namespace game {

class World;
class EvolutionEngine;

// BehaviorSystem 持有自己的 RNG（与原 Simulation::behavior_rng_ 同 seed 派生），
// 这样 RNG 序列与重构前完全一致，行为不变。
class CreatureBehaviorSystem {
public:
    CreatureBehaviorSystem(World& world, EvolutionEngine& evo,
                           std::uint64_t seed, std::uint64_t& next_id);

    auto update(ecs::Registry& r, std::uint64_t tick) -> void;

private:
    World&           world_;
    EvolutionEngine& evo_;
    std::uint64_t&   next_id_;
    std::mt19937_64  rng_;
};

}  // namespace game
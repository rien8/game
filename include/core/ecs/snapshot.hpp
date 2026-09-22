// include/core/ecs/snapshot.hpp
#pragma once

#include "core/ecs/components.hpp"
#include "core/gene.hpp"

#include <cstdint>
#include <string_view>

namespace game::ecs {

// 扁平结构，给 renderer 一行一动用。
// name 指向 registry 内 std::string（Simulation 持 registry 全程，string 不悬空）。
struct CreatureSnapshot {
    std::uint64_t      id        = 0;
    std::string_view   name;
    Traits             gene;
    Position           pos;
    float              hunger = 0.0f;
    float              energy = 0.0f;
    std::uint32_t      age     = 0;
    float              fitness = 0.0f;
    bool               is_elite = false;
    bool               is_boss  = false;
};

}  // namespace game::ecs

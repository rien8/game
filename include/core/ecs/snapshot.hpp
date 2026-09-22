// include/core/ecs/snapshot.hpp
#pragma once

#include "core/ecs/components.hpp"
#include "core/gene.hpp"

#include <cstdint>
#include <string>

namespace game::ecs {

// 扁平结构，给 renderer 一行一动用。
// Simulation 持 registry 全程，所以这里直接 own 一份 std::string。
struct CreatureSnapshot {
    std::uint64_t      id        = 0;
    std::string        name;
    Traits             gene;
    Position           pos;
    float              hunger = 0.0f;
    float              energy = 0.0f;
    std::uint32_t      age     = 0;
    float              fitness = 0.0f;
    bool               is_elite = false;
    bool               is_boss  = false;
    // Task 9 占位字段：让 evolution.cpp / tile_renderer.cpp 能编译。
    // 真值在 Task 11 由 Simulation::creatures() 实时从 registry 拼装。
    std::uint32_t      elite_age = 0;
    bool               dead      = false;
};

}  // namespace game::ecs

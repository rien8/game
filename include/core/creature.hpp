#pragma once

#include "core/ecs/snapshot.hpp"
#include "core/gene.hpp"

#include <random>
#include <string>

namespace game {

// 保留旧类型名 → 新快照的 alias。renderer / main.cpp 不动。
using Creature = ecs::CreatureSnapshot;

// inline 函数（不再有 decide_and_act）
inline void recover_energy(Creature& c) {
    if (c.hunger > 0.5f) {
        c.energy = std::min(1.0f, c.energy + 0.05f);
    } else {
        c.energy = std::max(0.0f, c.energy - 0.05f);
    }
}

auto make_name(const Traits& gene) -> std::string;

}  // namespace game

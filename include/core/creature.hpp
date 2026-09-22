#pragma once

#include "core/ecs/components.hpp"
#include "core/ecs/snapshot.hpp"
#include "core/gene.hpp"

#include <random>
#include <string>

namespace game {

// 保留旧类型名 → 新快照的 alias。renderer / main.cpp 不动。
using Creature = ecs::CreatureSnapshot;

// 直接作用于组件（BehaviorSystem 路径）
inline auto recover_energy(ecs::Vitals& v) -> void {
    if (v.hunger > 0.5f) {
        v.energy = std::min(1.0f, v.energy + 0.05f);
    } else {
        v.energy = std::max(0.0f, v.energy - 0.05f);
    }
}

// 保留对 Creature (= CreatureSnapshot) 的兼容（任何外部代码用了就还在）
inline auto recover_energy(Creature& c) -> void {
    ecs::Vitals v{.hunger = c.hunger, .energy = c.energy, .age = c.age, .dead = false};
    recover_energy(v);
    c.energy = v.energy;
}

auto make_name(const Traits& gene) -> std::string;

}  // namespace game
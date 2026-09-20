#pragma once

#include "core/gene.hpp"
#include "core/position.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace game {

class World;
class EvolutionEngine;

struct Creature {
    std::uint64_t id = 0;          // 全局唯一
    Traits gene;
    std::string name;

    Position pos{0, 0};
    float hunger = 1.0f;            // 0=临界  1=饱
    float energy = 1.0f;            // 0=虚脱  1=精力充沛
    std::uint32_t age = 0;          // tick 数
    bool dead = false;              // 本 tick 内行为机判定死亡，由 Simulation 收割

    float fitness = 0.0f;
    bool is_elite = false;
    bool is_boss = false;
    std::uint32_t elite_age = 0;    // 连续保持精英的 tick 数
};

// 每 tick 调用一次：消耗饥饿/能量 → 死亡/吃/移动/繁殖/随机游走。
void decide_and_act(Creature& self, World& world,
                    std::vector<Creature>& births,
                    EvolutionEngine& evo,
                    std::mt19937_64& rng,
                    std::uint64_t& next_id);

void recover_energy(Creature& c);

// 基因哈希 → 音节组合名字，如 "鳞·噬渊者"。
auto make_name(const Traits& gene) -> std::string;

}  // namespace game

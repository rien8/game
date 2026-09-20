#pragma once

#include "core/gene.hpp"

#include <cstddef>
#include <string>

namespace game {

// 生物个体。
struct Creature {
    Traits gene;
    float fitness = 0.0f;
    std::size_t elite_age = 0;  // 连续保持精英身份的代数
    bool is_elite = false;
    bool is_boss = false;
    std::string name;
};

// 基因哈希 → 音节组合名字，如 "鳞·噬渊者"。
auto make_name(const Traits& gene) -> std::string;

}  // namespace game

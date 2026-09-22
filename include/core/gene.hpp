#pragma once

#include <array>
#include <cstddef>
#include <random>

namespace game {

// 基因型：4 个属性（size / speed / attack / defense），范围 [0,1]。
struct Traits {
    using is_component = void;     // 兼容 ECS Component concept（Task 9）
    std::array<float, 4> values{};
};

// 高斯变异：每个属性加噪声，clamp 到 [0,1]。
void mutate(Traits& traits, float strength, std::mt19937_64& rng);

// 均匀交叉：逐属性随机取亲本。
auto crossover(const Traits& a, const Traits& b, std::mt19937_64& rng) -> Traits;

// 欧氏距离。
auto distance(const Traits& a, const Traits& b) -> float;

// 适应度：越接近 optimum 越适应，范围 (0,1]。
auto fitness(const Traits& gene, const Traits& optimum) -> float;

}  // namespace game

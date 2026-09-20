#include "core/gene.hpp"

#include <algorithm>
#include <cmath>

namespace game {

void mutate(Traits& traits, float strength, std::mt19937_64& rng) {
    std::normal_distribution<float> dist{0.0f, strength};
    for (auto& value : traits.values) {
        value = std::clamp(value + dist(rng), 0.0f, 1.0f);
    }
}

auto crossover(const Traits& a, const Traits& b, std::mt19937_64& rng) -> Traits {
    Traits child;
    std::uniform_int_distribution<int> coin{0, 1};
    for (std::size_t i = 0; i < child.values.size(); ++i) {
        child.values[i] = coin(rng) ? a.values[i] : b.values[i];
    }
    return child;
}

auto distance(const Traits& a, const Traits& b) -> float {
    float sum = 0.0f;
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        const float d = a.values[i] - b.values[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

auto fitness(const Traits& gene, const Traits& optimum) -> float {
    return 1.0f / (1.0f + distance(gene, optimum));
}

}  // namespace game

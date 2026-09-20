#include "core/evolution.hpp"

#include "core/gene.hpp"        // 提供 free function mutate / crossover / distance / fitness
#include "pcg/seed.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace game {

namespace {

constexpr std::uint32_t kEvolutionSalt = 0xE701u;

}  // namespace

EvolutionEngine::EvolutionEngine(Params params, std::uint64_t seed)
    : params_(params), rng_(pcg::derive_seed(seed, kEvolutionSalt)) {}

auto EvolutionEngine::mean_fitness(std::span<const Creature> creatures) const -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) sum += c.fitness;
    return sum / static_cast<float>(creatures.size());
}

auto EvolutionEngine::stddev_fitness(std::span<const Creature> creatures, float mean) const
    -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) {
        const float d = c.fitness - mean;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<float>(creatures.size()));
}

auto EvolutionEngine::evaluate_fitness(std::span<Creature> creatures,
                                       const Traits& optimum) -> void {
    for (auto& c : creatures) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < c.gene.values.size(); ++i) {
            const float d = c.gene.values[i] - optimum.values[i];
            sum += d * d;
        }
        c.fitness = 1.0f / (1.0f + std::sqrt(sum));
    }
}

auto EvolutionEngine::detect_elite_boss(std::span<Creature> creatures,
                                        std::vector<Event>& events,
                                        std::uint64_t current_tick) -> void {
    const float m = mean_fitness(creatures);
    const float sd = stddev_fitness(creatures, m);
    const float threshold = m + params_.elite_threshold_sigma * sd;

    for (auto& c : creatures) {
        if (c.fitness > threshold) {
            if (!c.is_elite) {
                c.is_elite = true;
                events.push_back({EventType::EliteBorn, current_tick,
                    std::format("精英诞生: {} (fitness={:.3f})", c.name, c.fitness)});
            }
            ++c.elite_age;
            if (c.elite_age >= params_.boss_streak_ticks && !c.is_boss) {
                c.is_boss = true;
                events.push_back({EventType::BossEvolved, current_tick,
                    std::format("BOSS 蜕变: {} (fitness={:.3f})", c.name, c.fitness)});
            }
        } else if (c.is_elite) {
            c.is_elite = false;
            c.elite_age = 0;
        }
    }
}

auto EvolutionEngine::reproduce(const Creature& parent) -> Creature {
    Creature child;
    child.gene = parent.gene;          // 克隆
    mutate(child.gene, params_.mutation_strength, rng_);  // 变异
    child.name = make_name(child.gene);
    return child;
}

auto EvolutionEngine::drift_environment(Traits& optimum,
                                        std::uint64_t tick,
                                        std::uint64_t epoch_length,
                                        std::vector<Event>& events) -> void {
    if (epoch_length > 0 && tick % epoch_length == 0) {
        std::uniform_real_distribution<float> shift{0.0f, 1.0f};
        for (auto& v : optimum.values) v = shift(rng_);
        events.push_back({EventType::EnvironmentShift, tick,
            std::format("环境剧变! 世界规则重构 (tick {})", tick)});
    } else {
        constexpr float kDrift = 0.02f;
        std::uniform_real_distribution<float> noise{-kDrift, kDrift};
        for (auto& v : optimum.values) {
            v = std::clamp(v + noise(rng_), 0.0f, 1.0f);
        }
    }
}

}  // namespace game

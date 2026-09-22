#include "core/evolution.hpp"

#include "core/creature.hpp"     // make_name 声明
#include "core/ecs/components.hpp"
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

auto EvolutionEngine::mean_fitness(ecs::Registry& r) const -> float {
    float sum = 0.0f;
    std::size_t n = 0;
    for (auto e : r.view<ecs::Reproduction>()) {
        sum += r.get<ecs::Reproduction>(e).fitness;
        ++n;
    }
    return n == 0 ? 0.0f : sum / static_cast<float>(n);
}

auto EvolutionEngine::stddev_fitness(ecs::Registry& r, float mean) const -> float {
    float sum = 0.0f;
    std::size_t n = 0;
    for (auto e : r.view<ecs::Reproduction>()) {
        const float d = r.get<ecs::Reproduction>(e).fitness - mean;
        sum += d * d;
        ++n;
    }
    return n == 0 ? 0.0f : std::sqrt(sum / static_cast<float>(n));
}

auto EvolutionEngine::evaluate_fitness(ecs::Registry& r,
                                       const Traits& optimum) -> void {
    for (auto e : r.view<ecs::Traits, ecs::Reproduction>()) {
        const auto& t = r.get<ecs::Traits>(e);
        float sum = 0.0f;
        for (std::size_t i = 0; i < t.values.size(); ++i) {
            const float d = t.values[i] - optimum.values[i];
            sum += d * d;
        }
        r.get<ecs::Reproduction>(e).fitness = 1.0f / (1.0f + std::sqrt(sum));
    }
}

auto EvolutionEngine::detect_elite_boss(ecs::Registry& r,
                                        std::vector<Event>& events,
                                        std::uint64_t current_tick) -> void {
    const float m = mean_fitness(r);
    const float sd = stddev_fitness(r, m);
    const float threshold = m + params_.elite_threshold_sigma * sd;

    for (auto e : r.view<ecs::Identity, ecs::Reproduction>()) {
        auto& id = r.get<ecs::Identity>(e);
        const float fit = r.get<ecs::Reproduction>(e).fitness;
        const std::string name = r.try_get<ecs::Name>(e)
            ? std::string{r.get<ecs::Name>(e).value} : std::string{"?"};

        if (fit > threshold) {
            if (!id.is_elite) {
                id.is_elite = true;
                events.push_back({EventType::EliteBorn, current_tick,
                    std::format("精英诞生: {} (fitness={:.3f})", name, fit)});
            }
            ++id.elite_age;
            if (id.elite_age >= params_.boss_streak_ticks && !id.is_boss) {
                id.is_boss = true;
                events.push_back({EventType::BossEvolved, current_tick,
                    std::format("BOSS 蜕变: {} (fitness={:.3f})", name, fit)});
            }
        } else if (id.is_elite) {
            id.is_elite = false;
            id.elite_age = 0;
        }
    }
}

auto EvolutionEngine::reproduce(ecs::Registry& r, ecs::Entity parent,
                                 std::uint64_t& next_id) -> ecs::Entity {
    const auto& parent_traits = r.get<ecs::Traits>(parent);
    auto child = r.create();
    ecs::Traits child_traits = parent_traits;
    mutate(child_traits, params_.mutation_strength, rng_);
    r.emplace<ecs::Traits>(child, child_traits);
    r.emplace<ecs::Reproduction>(child);
    r.emplace<ecs::Name>(child, ecs::Name{.value = make_name(child_traits)});
    // Identity 在这里赋值：避免 caller 忘记导致 child 落到 slot 0 与
    // 其它无 Identity 的 entity 冲突。
    r.emplace<ecs::Identity>(child, ecs::Identity{.id = next_id++});
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
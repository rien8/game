#include "core/evolution.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <utility>

namespace game {

namespace {

constexpr std::uint32_t kRngSalt = 0xE701u;

}  // namespace

EvolutionEngine::EvolutionEngine(Params params, std::uint64_t seed)
    : params_(params), rng_(pcg::derive_seed(seed, kRngSalt)) {
    population_.reserve(params.population_size);

    std::uniform_real_distribution<float> init_dist{0.0f, 1.0f};
    for (std::size_t i = 0; i < params.population_size; ++i) {
        Creature creature;
        for (auto& value : creature.gene.values) {
            value = init_dist(rng_);
        }
        creature.name = make_name(creature.gene);
        population_.push_back(std::move(creature));
    }
    for (auto& value : optimum_.values) {
        value = init_dist(rng_);
    }
}

auto EvolutionEngine::mean() const -> float {
    float sum = 0.0f;
    for (const auto& c : population_) {
        sum += c.fitness;
    }
    return population_.empty() ? 0.0f : sum / static_cast<float>(population_.size());
}

auto EvolutionEngine::stddev(float mean_value) const -> float {
    if (population_.empty()) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (const auto& c : population_) {
        const float d = c.fitness - mean_value;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<float>(population_.size()));
}

auto EvolutionEngine::select_parent(float total_fitness) -> const Creature& {
    std::uniform_real_distribution<float> dist{0.0f, total_fitness};
    float r = dist(rng_);
    for (const auto& c : population_) {
        r -= c.fitness;
        if (r <= 0.0f) {
            return c;
        }
    }
    return population_.back();
}

auto EvolutionEngine::step() -> StepReport {
    ++generation_;

    StepReport report;
    report.generation = generation_;

    // 1. 计算 fitness。
    for (auto& c : population_) {
        c.fitness = fitness(c.gene, optimum_);
    }

    // 统计。
    const float mean_fitness = mean();
    const float sd = stddev(mean_fitness);
    const float elite_threshold = mean_fitness + params_.elite_threshold_sigma * sd;

    report.population_size = population_.size();
    report.avg_fitness = mean_fitness;

    float max_fitness = 0.0f;
    for (const auto& c : population_) {
        max_fitness = std::max(max_fitness, c.fitness);
    }
    report.max_fitness = max_fitness;

    // 2. 精英怪标记 + 3. Boss 蜕变判定。
    std::size_t elite_count = 0;
    std::size_t boss_count = 0;
    for (auto& c : population_) {
        if (c.fitness > elite_threshold) {
            ++elite_count;
            if (!c.is_elite) {
                c.is_elite = true;
                report.events.push_back({EventType::EliteBorn,
                    std::format("精英诞生: {} (fitness={:.3f})", c.name, c.fitness)});
            }
            ++c.elite_age;
            if (c.elite_age >= params_.boss_streak && !c.is_boss) {
                c.is_boss = true;
                report.events.push_back({EventType::BossEvolved,
                    std::format("BOSS 蜕变: {} (fitness={:.3f})", c.name, c.fitness)});
            }
        } else if (c.is_elite) {
            // 失去精英身份，计数归零（Boss 身份一旦获得不退化）。
            c.is_elite = false;
            c.elite_age = 0;
        }
        if (c.is_boss) {
            ++boss_count;
        }
    }
    report.elite_count = elite_count;
    report.boss_count = boss_count;

    // 4. 环境漂移 + 周期性剧变。
    if (generation_ % params_.epoch_length == 0) {
        std::uniform_real_distribution<float> shift_dist{0.0f, 1.0f};
        for (auto& value : optimum_.values) {
            value = shift_dist(rng_);
        }
        report.events.push_back({EventType::EnvironmentShift,
            std::format("环境剧变! 世界规则重构 (gen {})", generation_)});
    } else {
        std::uniform_real_distribution<float> drift_dist{-params_.optimum_drift,
                                                         params_.optimum_drift};
        for (auto& value : optimum_.values) {
            value = std::clamp(value + drift_dist(rng_), 0.0f, 1.0f);
        }
    }

    // 5. 世界能量积累。
    world_energy_ += mean_fitness * 10.0f;
    report.world_energy = world_energy_;

    // 6. 繁殖。
    const float total_fitness = std::accumulate(
        population_.begin(), population_.end(), 0.0f,
        [](float acc, const Creature& c) { return acc + c.fitness; });

    std::vector<Creature> next;
    next.reserve(params_.population_size);

    // 精英 / Boss 跨代存活；精英继续小幅进化，Boss 形态稳定。
    for (const auto& c : population_) {
        if (c.is_elite || c.is_boss) {
            Creature survivor = c;
            if (!c.is_boss) {
                mutate(survivor.gene, params_.mutation_strength * 0.5f, rng_);
            }
            next.push_back(std::move(survivor));
        }
    }

    // 其余按选择 + 交叉 + 变异产生新一代。
    std::bernoulli_distribution mutate_coin{params_.mutation_rate};
    while (next.size() < params_.population_size) {
        const Creature& parent_a = select_parent(total_fitness);
        const Creature& parent_b = select_parent(total_fitness);
        Traits child_gene = crossover(parent_a.gene, parent_b.gene, rng_);
        if (mutate_coin(rng_)) {
            mutate(child_gene, params_.mutation_strength, rng_);
        }
        Creature child;
        child.gene = child_gene;
        child.name = make_name(child_gene);
        next.push_back(std::move(child));
    }

    population_ = std::move(next);

    return report;
}

}  // namespace game

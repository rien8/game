#pragma once

#include "core/creature.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace game {

enum class EventType {
    EliteBorn,
    BossEvolved,
    EnvironmentShift,
};

struct EvolutionEvent {
    EventType type;
    std::string description;
};

// 一步演化后的统计与事件。
struct StepReport {
    std::size_t generation = 0;
    std::size_t population_size = 0;
    float avg_fitness = 0.0f;
    float max_fitness = 0.0f;
    std::size_t elite_count = 0;
    std::size_t boss_count = 0;
    float world_energy = 0.0f;
    std::vector<EvolutionEvent> events;
};

// 抽象适应度演化引擎。
class EvolutionEngine {
public:
    struct Params {
        std::size_t population_size = 100;
        float mutation_rate = 0.1f;
        float mutation_strength = 0.1f;
        float elite_threshold_sigma = 2.5f;  // 精英怪：fitness > mean + 2.5σ
        std::size_t boss_streak = 3;         // 连续保持精英 N 代 → Boss
        float optimum_drift = 0.02f;         // 每代环境漂移幅度
        std::size_t epoch_length = 20;       // 每 N 代发生一次环境剧变
    };

    EvolutionEngine(Params params, std::uint64_t seed);

    [[nodiscard]] auto step() -> StepReport;

    [[nodiscard]] auto world_energy() const noexcept -> float { return world_energy_; }

private:
    [[nodiscard]] auto mean() const -> float;
    [[nodiscard]] auto stddev(float mean_value) const -> float;
    [[nodiscard]] auto select_parent(float total_fitness) -> const Creature&;

    Params params_;
    Traits optimum_;
    std::vector<Creature> population_;
    float world_energy_ = 0.0f;
    std::size_t generation_ = 0;
    std::mt19937_64 rng_;
};

}  // namespace game

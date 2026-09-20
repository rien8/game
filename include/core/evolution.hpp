#pragma once

#include "core/creature.hpp"
#include "core/gene.hpp"

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace game {

enum class EventType {
    EliteBorn,
    BossEvolved,
    EnvironmentShift,
};

struct Event {
    EventType type;
    std::uint64_t tick = 0;
    std::string description;
};

class EvolutionEngine {
public:
    struct Params {
        float mutation_rate = 0.1f;
        float mutation_strength = 0.1f;
        float elite_threshold_sigma = 2.5f;
        std::uint32_t boss_streak_ticks = 30;
    };

    EvolutionEngine(Params params, std::uint64_t seed);

    // ① 计算所有生物的 fitness：1 / (1 + distance(gene, optimum))
    auto evaluate_fitness(std::span<Creature> creatures,
                          const Traits& optimum) -> void;

    // ② 标记精英与 Boss；elite_age 按 tick 累加；触发时入 events
    auto detect_elite_boss(std::span<Creature> creatures,
                           std::vector<Event>& events,
                           std::uint64_t current_tick) -> void;

    // ③ 无性繁殖：单亲克隆 + 变异 + 新名字；vitals 重置由 caller 处理
    [[nodiscard]] auto reproduce(const Creature& parent) -> Creature;

    // ④ optimum 每 tick 小幅漂移；每 epoch_length tick 一次剧变
    auto drift_environment(Traits& optimum,
                           std::uint64_t tick,
                           std::uint64_t epoch_length,
                           std::vector<Event>& events) -> void;

    [[nodiscard]] auto mean_fitness(std::span<const Creature>) const -> float;
    [[nodiscard]] auto stddev_fitness(std::span<const Creature>, float mean) const -> float;

private:
    Params params_;
    std::mt19937_64 rng_;
};

}  // namespace game

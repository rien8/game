#pragma once

#include "core/evolution.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace game {

// accumulator 模式的时钟；speed=0 即暂停。
class SimulationClock {
public:
    void configure(float tick_dt, std::size_t max_ticks_per_frame);

    // 由 Simulation::advance 调用：real_dt × speed 累积；超 tick_dt 就消费一次 tick_fn。
    void advance(float real_dt, float speed, auto&& tick_fn);

    [[nodiscard]] auto tick_dt() const noexcept -> float { return tick_dt_; }

private:
    float tick_dt_ = 0.1f;
    std::size_t max_ticks_per_frame_ = 8;
    float accumulator_ = 0.0f;
};

// 时间流向世界模拟。
class Simulation {
public:
    struct Params {
        std::size_t initial_population = 30;
        std::size_t max_population = 200;
        std::uint64_t epoch_length = 200;
        float tick_dt = 0.1f;
        std::size_t max_ticks_per_frame = 8;
        EvolutionEngine::Params evolution{};
    };

    Simulation(World world, std::uint64_t seed, Params params = {});

    // 主入口。
    auto advance(float real_dt, float speed) -> void;

    // 快照给 renderer / 调试。
    [[nodiscard]] auto world() const -> const World& { return world_; }
    [[nodiscard]] auto creatures() const -> std::span<const Creature> { return creatures_; }
    [[nodiscard]] auto events() const -> const std::vector<Event>& { return events_; }
    [[nodiscard]] auto world_energy() const noexcept -> float { return world_energy_; }
    [[nodiscard]] auto tick_count() const noexcept -> std::uint64_t { return tick_; }
    [[nodiscard]] auto tick_dt() const noexcept -> float { return clock_.tick_dt(); }
    [[nodiscard]] auto speed() const noexcept -> float { return last_speed_; }

private:
    auto tick() -> void;

    Params params_;
    World world_;
    Traits optimum_{};
    std::vector<Creature> creatures_;
    std::vector<Event> events_;
    std::vector<Creature> pending_births_;
    EvolutionEngine evolution_;
    SimulationClock clock_;
    std::uint64_t tick_ = 0;
    float world_energy_ = 0.0f;
    float last_speed_ = 1.0f;
    std::uint64_t next_id_ = 1;
    std::mt19937_64 behavior_rng_;
};

}  // namespace game

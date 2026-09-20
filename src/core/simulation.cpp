#include "core/simulation.hpp"

#include "core/creature.hpp"
#include "world/world_generator.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <format>

namespace game {

namespace {

constexpr std::uint32_t kBehaviorSalt = 0xBE11u;
constexpr std::uint32_t kOptimumSalt = 0x0E70u;
constexpr std::size_t kMaxEventsKept = 64;

}  // namespace

void SimulationClock::configure(float tick_dt, std::size_t max_ticks_per_frame) {
    tick_dt_ = tick_dt;
    max_ticks_per_frame_ = max_ticks_per_frame;
    accumulator_ = 0.0f;
}

void SimulationClock::advance(float real_dt, float speed, auto&& tick_fn) {
    accumulator_ += real_dt * speed;
    std::size_t consumed = 0;
    while (accumulator_ >= tick_dt_ && consumed < max_ticks_per_frame_) {
        tick_fn();
        accumulator_ -= tick_dt_;
        ++consumed;
    }
}

Simulation::Simulation(World world, std::uint64_t seed, Params params)
    : params_(params),
      world_(std::move(world)),
      evolution_(params.evolution, seed),
      behavior_rng_(pcg::derive_seed(seed, kBehaviorSalt)) {
    clock_.configure(params_.tick_dt, params_.max_ticks_per_frame);

    // 初始化 optimum。
    {
        auto rng = std::mt19937_64{pcg::derive_seed(seed, kOptimumSalt)};
        std::uniform_real_distribution<float> dist{0.0f, 1.0f};
        for (auto& v : optimum_.values) v = dist(rng);
    }

    // 初始生物放在陆地上。
    creatures_ = populate_initial_creatures(
        world_, params_.initial_population, seed);
    next_id_ = creatures_.size() + 1;
}

auto Simulation::advance(float real_dt, float speed) -> void {
    last_speed_ = speed;
    clock_.advance(real_dt, speed, [this] { tick(); });
}

auto Simulation::tick() -> void {
    ++tick_;

    // 1. 世界 biomass 重生
    world_.regrow_biomass();

    // 2. 环境漂移 / 剧变
    evolution_.drift_environment(optimum_, tick_, params_.epoch_length, events_);

    // 3. 生物行为循环
    pending_births_.clear();
    for (auto& c : creatures_) {
        decide_and_act(c, world_, pending_births_,
                       evolution_, behavior_rng_, next_id_);
    }

    // 4. 收割死亡
    std::erase_if(creatures_, [](const Creature& c) { return c.dead; });

    // 5. 收纳新生（带 max_population 上限）
    if (creatures_.size() < params_.max_population) {
        const std::size_t room = params_.max_population - creatures_.size();
        const std::size_t take = std::min(room, pending_births_.size());
        for (std::size_t i = 0; i < take; ++i) {
            creatures_.push_back(std::move(pending_births_[i]));
        }
    }

    // 6. 演化层
    evolution_.evaluate_fitness(creatures_, optimum_);
    evolution_.detect_elite_boss(creatures_, events_, tick_);

    // 7. 世界能量
    world_energy_ += world_.total_biomass() * 0.01f;

    // 8. 修剪事件队列
    if (events_.size() > kMaxEventsKept) {
        const std::size_t drop = events_.size() - kMaxEventsKept;
        events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(drop));
    }

    // 9. 容灾：所有生物饿死 → 用 5 只初始生物重启种群（避免世界彻底死掉）
    if (creatures_.empty()) {
        auto fresh = populate_initial_creatures(world_, 5, tick_);
        next_id_ = 1;
        for (auto& c : fresh) {
            c.id = next_id_++;
            creatures_.push_back(std::move(c));
        }
    }
}

}  // namespace game

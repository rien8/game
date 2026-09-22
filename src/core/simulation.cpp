#include "core/simulation.hpp"

#include "core/creature.hpp"
#include "world/world_generator.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <format>
#include <ranges>

namespace game {

namespace {

constexpr std::uint32_t kOptimumSalt = 0x0E70u;

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
      behavior_system_(world_, evolution_, seed, next_id_) {
    clock_.configure(params_.tick_dt, params_.max_ticks_per_frame);

    // optimum 初始化（不变）
    {
        auto rng = std::mt19937_64{pcg::derive_seed(seed, kOptimumSalt)};
        std::uniform_real_distribution<float> dist{0.0f, 1.0f};
        for (auto& v : optimum_.values) v = dist(rng);
    }

    // 初始生物：直接进 registry
    populate_initial_creatures(registry_, world_,
                                params_.initial_population, seed, next_id_);

    snapshot_cache_.clear();
}

auto Simulation::advance(float real_dt, float speed) -> void {
    last_speed_ = speed;
    clock_.advance(real_dt, speed, [this] { tick(); });
}

auto Simulation::tick() -> void {
    ++tick_;

    // 1. world biomass 重生
    world_.regrow_biomass();

    // 2. 环境漂移 / 剧变（optimum 是世界状态，由 Simulation 拥有）
    evolution_.drift_environment(optimum_, tick_, params_.epoch_length, events_);

    // 3. 计算 fitness 并检测精英/Boss（直接对 registry 操作）
    evolution_.evaluate_fitness(registry_, optimum_);
    evolution_.detect_elite_boss(registry_, events_, tick_);

    // 4. 行为循环：用 BehaviorSystem
    behavior_system_.update(registry_, tick_);
}

auto Simulation::creatures() -> std::span<const ecs::CreatureSnapshot> {
    // 实时组装：按 Identity::id 排序
    snapshot_cache_.clear();
    for (auto e : registry_.view<ecs::Identity>()) {
        const auto& id = registry_.get<ecs::Identity>(e);
        ecs::CreatureSnapshot s;
        s.id = id.id;
        // 其余字段暂时 default；Task 11 补齐
        snapshot_cache_.push_back(s);
    }
    std::ranges::sort(snapshot_cache_, {}, [](const auto& s) { return s.id; });
    return snapshot_cache_;
}

}  // namespace game

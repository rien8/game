#include "core/simulation.hpp"

#include "core/creature.hpp"
#include "world/world_generator.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <vector>

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

    // 5. 收割死亡：BehaviorSystem 只置 vitals.dead=true；这里实际销毁 entity。
    //    注：Registry::destroy 只翻 alive_ + bump generation，并不擦除各组件
    //    storage 的行；SparseSet 迭代时会通过 contains() 跳过已死 entity。
    //    长会话下 storage 会无界增长 —— 本 spec 范围内可接受，按需后续再清理。
    std::vector<ecs::Entity> to_die;
    for (auto e : registry_.view<ecs::Vitals>()) {
        if (registry_.get<ecs::Vitals>(e).dead) to_die.push_back(e);
    }
    for (auto e : to_die) registry_.destroy(e);
}

auto Simulation::creatures() -> std::span<const ecs::CreatureSnapshot> {
    // 实时组装：从 registry 的各组件取值填进快照，按 Identity::id 排序。
    // 注意 CreatureSnapshot::name 是 std::string（非 string_view），
    // 每 tick 重新拷贝约 200 个 string；当前规模下可接受。
    snapshot_cache_.clear();
    for (auto e : registry_.view<ecs::Identity, ecs::Position, ecs::Vitals,
                                 ecs::Reproduction, ecs::Name, ecs::Traits>()) {
        const auto& id  = registry_.get<ecs::Identity>(e);
        const auto& pos = registry_.get<ecs::Position>(e);
        const auto& vit = registry_.get<ecs::Vitals>(e);
        const auto& rep = registry_.get<ecs::Reproduction>(e);
        const auto& nam = registry_.get<ecs::Name>(e);
        const auto& trt = registry_.get<ecs::Traits>(e);
        snapshot_cache_.push_back(ecs::CreatureSnapshot{
            .id        = id.id,
            .name      = nam.value,
            .gene      = trt,
            .pos       = pos,
            .hunger    = vit.hunger,
            .energy    = vit.energy,
            .age       = vit.age,
            .fitness   = rep.fitness,
            .is_elite  = id.is_elite,
            .is_boss   = id.is_boss,
            .elite_age = id.elite_age,
            .dead      = vit.dead,
        });
    }
    std::ranges::sort(snapshot_cache_, {}, [](const auto& s) { return s.id; });
    return snapshot_cache_;
}

}  // namespace game

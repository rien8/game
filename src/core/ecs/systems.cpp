// src/core/ecs/systems.cpp
#include "core/ecs/systems.hpp"

#include "core/creature.hpp"
#include "core/ecs/components.hpp"
#include "core/evolution.hpp"
#include "world/world.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace game {

namespace {

constexpr std::uint32_t kBehaviorSalt = 0xBE11u;
constexpr float         kHungerRate       = 0.02f;
constexpr std::uint32_t kMaxAge           = 200;
constexpr std::uint32_t kMatingAge        = 20;
constexpr std::uint32_t kMateInterval     = 50;

}  // namespace

CreatureBehaviorSystem::CreatureBehaviorSystem(World& world, EvolutionEngine& evo,
                                               std::uint64_t seed,
                                               std::uint64_t& next_id)
    : world_(world),
      evo_(evo),
      next_id_(next_id),
      rng_(pcg::derive_seed(seed, kBehaviorSalt)) {}

auto CreatureBehaviorSystem::update(ecs::Registry& r, std::uint64_t /*tick*/) -> void {
    using namespace ecs;

    for (Entity e : r.view<Position, Traits, Vitals, Identity>()) {
        auto& pos    = r.get<Position>(e);
        auto& vitals = r.get<Vitals>(e);

        vitals.age += 1;
        vitals.hunger -= kHungerRate;
        recover_energy(vitals);

        if (vitals.hunger <= 0.0f || vitals.age > kMaxAge) {
            vitals.dead = true;
            continue;
        }

        if (pos.x >= world_.width() || pos.y >= world_.height()) continue;
        auto& here = world_.at(pos.x, pos.y);

        // 1) 吃草
        if (here.biomass > 0.1f && vitals.hunger < 1.0f) {
            const float want = 1.0f - vitals.hunger;
            const float eat  = std::min(here.biomass * 0.5f, want);
            here.biomass = std::max(0.0f, here.biomass - eat * 0.4f);
            vitals.hunger = std::min(1.0f, vitals.hunger + eat * 0.3f);
            vitals.energy = std::min(1.0f, vitals.energy + 0.05f);
            continue;
        }

        // 2) 找食物（限于陆地）
        if (vitals.hunger < 0.5f) {
            Position best = pos;
            float    best_b = here.biomass;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const auto nx = static_cast<std::int64_t>(pos.x) + dx;
                    const auto ny = static_cast<std::int64_t>(pos.y) + dy;
                    if (nx < 0 || ny < 0
                        || static_cast<std::size_t>(nx) >= world_.width()
                        || static_cast<std::size_t>(ny) >= world_.height()) continue;
                    if (!world_.is_land(static_cast<std::size_t>(nx),
                                        static_cast<std::size_t>(ny))) continue;
                    const float b = world_.at(static_cast<std::size_t>(nx),
                                              static_cast<std::size_t>(ny)).biomass;
                    if (b > best_b) {
                        best_b = b;
                        best   = {static_cast<std::uint16_t>(nx),
                                  static_cast<std::uint16_t>(ny)};
                    }
                }
            }
            pos = best;
            continue;
        }

        // 3) 繁殖（Task 10 版：直接克隆 traits + 命名；Task 11 改为走 evo_.reproduce）
        if (vitals.hunger > 0.7f && vitals.energy > 0.5f
            && vitals.age >= kMatingAge
            && (vitals.age % kMateInterval == 0)) {
            auto& parent_traits = r.get<Traits>(e);
            auto  child = r.create();
            r.emplace<Position>(child, pos);
            r.emplace<Traits>(child, parent_traits);
            r.emplace<Vitals>(child, Vitals{.hunger = 1.0f, .energy = 0.8f,
                                            .age = 0, .dead = false});
            r.emplace<Identity>(child, Identity{.id = next_id_++});
            r.emplace<Name>(child, Name{.value = make_name(parent_traits)});
            r.emplace<Reproduction>(child);
            vitals.energy = std::max(0.0f, vitals.energy - 0.3f);
            continue;
        }

        // 4) 随机游走
        std::uniform_int_distribution<int> dir(0, 7);
        for (int retry = 0; retry < 4; ++retry) {
            const int d  = dir(rng_);
            const int dx = (d % 3) - 1;
            const int dy = (d / 3) - 1;
            const auto nx = static_cast<std::int64_t>(pos.x) + dx;
            const auto ny = static_cast<std::int64_t>(pos.y) + dy;
            if (nx < 0 || ny < 0
                || static_cast<std::size_t>(nx) >= world_.width()
                || static_cast<std::size_t>(ny) >= world_.height()) continue;
            if (!world_.is_land(static_cast<std::size_t>(nx),
                                static_cast<std::size_t>(ny))) continue;
            pos = {static_cast<std::uint16_t>(nx), static_cast<std::uint16_t>(ny)};
            break;
        }
    }
}

}  // namespace game
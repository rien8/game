#include "core/creature.hpp"

#include "core/evolution.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <random>

namespace game {

namespace {

constexpr float kHungerRate = 0.02f;
constexpr std::uint32_t kMaxAge = 200;
constexpr std::uint32_t kMatingAge = 20;
constexpr std::uint32_t kMateInterval = 50;

}  // namespace

void recover_energy(Creature& c) {
    if (c.hunger > 0.5f) {
        c.energy = std::min(1.0f, c.energy + 0.05f);
    } else {
        c.energy = std::max(0.0f, c.energy - 0.05f);
    }
}

void decide_and_act(Creature& self, World& world,
                    std::vector<Creature>& births,
                    EvolutionEngine& evo,
                    std::mt19937_64& rng,
                    std::uint64_t& next_id) {
    self.age += 1;
    self.hunger -= kHungerRate;
    recover_energy(self);

    if (self.hunger <= 0.0f || self.age > kMaxAge) {
        self.dead = true;
        return;
    }

    auto& here = world.at(self.pos.x, self.pos.y);

    // 1) 脚下有食物 → 吃（原地）
    if (here.biomass > 0.1f && self.hunger < 1.0f) {
        const float want = 1.0f - self.hunger;
        const float eat_amount = std::min(here.biomass * 0.5f, want);
        here.biomass = std::max(0.0f, here.biomass - eat_amount * 0.4f);
        self.hunger = std::min(1.0f, self.hunger + eat_amount * 0.3f);
        self.energy = std::min(1.0f, self.energy + 0.05f);
        return;
    }

    // 2) 饿 → 扫 8 邻居找最高 biomass 移动 1 步（限陆地）
    if (self.hunger < 0.5f) {
        Position best = self.pos;
        float best_b = here.biomass;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const std::size_t nx = static_cast<std::size_t>(
                    static_cast<std::int64_t>(self.pos.x) + dx);
                const std::size_t ny = static_cast<std::size_t>(
                    static_cast<std::int64_t>(self.pos.y) + dy);
                if (nx >= world.width() || ny >= world.height()) continue;
                if (!world.is_land(nx, ny)) continue;
                const float b = world.at(nx, ny).biomass;
                if (b > best_b) { best_b = b; best = {nx, ny}; }
            }
        }
        self.pos = best;
        return;
    }

    // 3) 饱 + 能繁殖 → 无性繁殖（每 kMateInterval tick 一次）
    if (self.hunger > 0.7f && self.energy > 0.5f
        && self.age >= kMatingAge
        && (self.age % kMateInterval == 0)) {
        Creature child = evo.reproduce(self);
        child.id = next_id++;
        child.pos = self.pos;
        child.age = 0;
        child.hunger = 1.0f;
        child.energy = 0.8f;
        child.dead = false;
        births.push_back(std::move(child));
        self.energy = std::max(0.0f, self.energy - 0.3f);
        return;
    }

    // 4) 随机游走 1 步（限陆地，重试 4 次）
    std::uniform_int_distribution<int> dir(0, 7);
    for (int retry = 0; retry < 4; ++retry) {
        const int d = dir(rng);
        const int dx = (d % 3) - 1;
        const int dy = (d / 3) - 1;
        const std::size_t nx = static_cast<std::size_t>(
            static_cast<std::int64_t>(self.pos.x) + dx);
        const std::size_t ny = static_cast<std::size_t>(
            static_cast<std::int64_t>(self.pos.y) + dy);
        if (nx >= world.width() || ny >= world.height()) continue;
        if (!world.is_land(nx, ny)) continue;
        self.pos = {nx, ny};
        return;
    }
}

auto make_name(const Traits& gene) -> std::string {
    constexpr std::array<std::string_view, 12> kPrefixes = {
        "鳞", "焰", "霜", "暗", "星", "血", "岩", "风", "雷", "影", "冥", "翠",
    };
    constexpr std::array<std::string_view, 12> kSuffixes = {
        "噬渊者", "织梦者", "守誓者", "裂空者", "焚天者", "葬海者",
        "司命者", "逐日者", "镇岳者", "踏云者", "噬神者", "冥河者",
    };

    std::uint32_t hash = 0x811C9DC5u;
    for (const float value : gene.values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        hash = (hash ^ bits) * 0x01000193u;
    }
    const std::size_t prefix = hash % kPrefixes.size();
    const std::size_t suffix = (hash >> 8) % kSuffixes.size();
    return std::string{kPrefixes[prefix]} + "·" + std::string{kSuffixes[suffix]};
}

}  // namespace game

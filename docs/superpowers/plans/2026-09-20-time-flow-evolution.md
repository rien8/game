# Time-Flow Evolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the turn-based `EvolutionEngine` into a tick-based, time-flow simulation where creatures live on the map with position/hunger/energy and an SDL3 GUI main loop drives the world forward.

**Architecture:** Three-layer separation — `Simulation` is the sole clock owner; `EvolutionEngine` degrades into per-tick rule methods called by `Simulation::tick()`; `World` + `Creature` are pure data with no time concept. main.cpp becomes an SDL3 event loop feeding real `dt` into `Simulation::advance(real_dt, speed)`.

**Tech Stack:** C++23, SDL3 (window/renderer/texture + `SDL_RenderDebugText`), clang-cl, Ninja, CMake 3.25+, vcpkg. No unit-test framework (project has none; verification is build + console + HUD + manual observation).

## Global Constraints

These constraints apply to every task. Copy-pasted verbatim from the spec / CLAUDE.md.

- **Platform:** Windows-only; paths use `E:\gamev2` form; runner is PowerShell Core (`pwsh`).
- **C++ standard:** C++23 (`CMAKE_CXX_STANDARD 23`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF`).
- **Compiler flags:** `-Wall -Wextra -Wpedantic` (Debug adds `-g -O0`, Release `-O2 -DNDEBUG`).
- **Warnings as errors:** `/W4 /WX` (clang-cl); ASCII/UTF-8 compile flag is set globally.
- **Naming:** types PascalCase, functions camelCase, variables snake_case, private members `_` suffix.
- **Memory:** `std::unique_ptr` / `std::shared_ptr`; no raw `new`/`delete`; RAII for all resources.
- **Strings:** `std::string_view` for read-only, `std::string` for owned; no C-string functions.
- **Errors:** prefer `std::expected`; templates use concepts.
- **Header style:** `#pragma once`; public headers in `include/`; impl in `src/`; no `using namespace` in headers.
- **Build entry point:** `pwsh E:/gamev2/build.ps1` (configures + builds with Ninja/clang-cl/vcpkg). Run with `pwsh E:/gamev2/build.ps1 run` to launch.
- **Source layout:**
  - `include/core/{gene,creature,evolution,simulation,position}.hpp`
  - `src/core/{gene,creature,evolution,simulation}.cpp`
  - `include/world/{world,biome,world_generator}.hpp`
  - `src/world/{world,world_generator}.cpp`
  - `include/render/tile_renderer.hpp`
  - `src/render/tile_renderer.cpp`
  - `src/main.cpp`
- **RNG:** `std::mt19937_64`; seed derived via `pcg::derive_seed(seed, salt)`. Each major component holds its own RNG (different salt) for independence.
- **No git:** project is not a git repo — `commit` steps are omitted; build outputs go to `E:/gamev2/build/`.

---

## File-Change Map

| File | Change |
|---|---|
| `include/core/position.hpp` | **Create** — `Position` struct |
| `include/core/creature.hpp` | Modify — add `pos/hunger/energy/age/dead/id`; declare `decide_and_act` + `recover_energy` |
| `src/core/creature.cpp` | Modify — implement `decide_and_act`, `recover_energy` |
| `include/core/evolution.hpp` | Modify — replace `step()` with 4 methods (`evaluate_fitness`, `detect_elite_boss`, `reproduce`, `drift_environment`) + utility methods |
| `src/core/evolution.cpp` | Modify — implement new API |
| `include/world/world.hpp` | Modify — add `biomass/biomass_target/biomass_regrowth` on `Tile`; add `regrow_biomass()`, `is_land()`, `total_biomass()` on `World` |
| `src/world/world.cpp` | Modify — implement new methods; add biome → biomass lookup table |
| `src/world/world_generator.cpp` | Modify — write biomass target/regrowth per tile; add `populate_creatures()` helper |
| `include/core/simulation.hpp` | **Create** — `Event` enum + struct, `SimulationClock`, `Simulation` |
| `src/core/simulation.cpp` | **Create** — `Simulation::tick`, `Simulation::advance` |
| `include/render/tile_renderer.hpp` | Modify — add `render_creatures`, `render_hud`, `gene_color`, `raw_renderer` |
| `src/render/tile_renderer.cpp` | Modify — implement new methods |
| `src/main.cpp` | Rewrite — SDL3 event loop with pause/speed/quit |
| `CMakeLists.txt` | Modify — add `src/core/simulation.cpp` to executable |

---

## Task 1: Position POD

**Files:**
- Create: `E:/gamev2/include/core/position.hpp`

**Why first:** every later task references `Position`. Zero deps.

**Produces:** `game::Position` — `{x, y}` of `std::uint32_t`, with equality operator and bounds-checked constructors.

- [ ] **Step 1: Create `include/core/position.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <utility>

namespace game {

// 二维坐标（无符号）。用于 Creature 在地图上的位置。
struct Position {
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    constexpr Position() noexcept = default;
    constexpr Position(std::uint32_t x_, std::uint32_t y_) noexcept : x{x_}, y{y_} {}

    [[nodiscard]] constexpr auto operator==(const Position&) const noexcept -> bool = default;
};

}  // namespace game
```

- [ ] **Step 2: Verify build still works**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK. (Position header is not yet referenced — sanity-check that adding it didn't break anything.)

---

## Task 2: Tile biomass extension (struct only)

**Files:**
- Modify: `E:/gamev2/include/world/world.hpp`

**Why:** before implementing behavior, both Creature and World need to know what "biomass" means.

**Produces:** `Tile` now carries `biomass`, `biomass_target`, `biomass_regrowth`. No behavior yet.

- [ ] **Step 1: Extend `Tile` struct in `include/world/world.hpp`**

Replace the existing `Tile` struct (lines 11-16) with:

```cpp
struct Tile {
    float elevation = 0.0f;    // 海拔 [0, 1]
    float temperature = 0.0f;  // 温度 [0, 1]
    float moisture = 0.0f;     // 湿度 [0, 1]
    Biome biome = Biome::DeepOcean;
    float biomass = 0.0f;            // 当前食物量 [0, 1]
    float biomass_target = 0.0f;     // 该 biome 的"满载"基线（生成时由 WorldGenerator 写入）
    float biomass_regrowth = 0.0f;   // 每 tick 重生率（生成时写入）
};
```

- [ ] **Step 2: Verify build**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK. World still default-constructs tiles; biomass stays at 0 until Task 4.

---

## Task 3: World biomass behavior (`regrow_biomass`, `is_land`, `total_biomass`)

**Files:**
- Modify: `E:/gamev2/include/world/world.hpp` — add to public API
- Modify: `E:/gamev2/src/world/world.cpp` — implement

**Produces:** `World::regrow_biomass()` updates every tile's `biomass` toward `biomass_target` by `biomass_regrowth` per call, clamped to `[0, 1]`. `is_land(x,y)` returns true when biome is not DeepOcean / Ocean. `total_biomass()` sums current biomass.

- [ ] **Step 1: Extend `World` API in `include/world/world.hpp`**

After the existing `height()` getter (line 27), add:

```cpp
    // 每 tick 调用一次：每块 tile 的 biomass 向 biomass_target 逼近。
    auto regrow_biomass() -> void;

    // 陆地块判定（DeepOcean / Ocean 视为非陆地）。
    [[nodiscard]] auto is_land(std::size_t x, std::size_t y) const -> bool;

    // 当前世界总生物量（用于世界能量累积）。
    [[nodiscard]] auto total_biomass() const -> float;
```

- [ ] **Step 2: Implement new methods in `src/world/world.cpp`**

Append before the closing `}  // namespace game`:

```cpp
auto World::regrow_biomass() -> void {
    for (auto& tile : tiles_) {
        if (tile.biomass_regrowth <= 0.0f) continue;
        const float diff = tile.biomass_target - tile.biomass;
        tile.biomass += diff * tile.biomass_regrowth;
        if (tile.biomass < 0.0f) tile.biomass = 0.0f;
        if (tile.biomass > 1.0f) tile.biomass = 1.0f;
    }
}

auto World::is_land(std::size_t x, std::size_t y) const -> bool {
    const Biome b = tiles_[y * width_ + x].biome;
    return b != Biome::DeepOcean && b != Biome::Ocean;
}

auto World::total_biomass() const -> float {
    float total = 0.0f;
    for (const auto& tile : tiles_) {
        total += tile.biomass;
    }
    return total;
}
```

- [ ] **Step 3: Verify build**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK. Behavior not yet exercised — Task 4 wires biomass into the generator.

---

## Task 4: WorldGenerator fills biomass table per biome

**Files:**
- Modify: `E:/gamev2/src/world/world_generator.cpp` — write `biomass_target` and `biomass_regrowth` per tile based on biome

**Produces:** after generation, every tile has non-zero `biomass_target` and `biomass_regrowth` matching the spec table.

- [ ] **Step 1: Add biome → biomass lookup helper in `src/world/world_generator.cpp`**

Insert this anonymous-namespace block above the `classify_biome` function (around line 35):

```cpp
struct BiomassProfile {
    float target;
    float regrowth;
};

constexpr auto biomass_for(Biome b) -> BiomassProfile {
    switch (b) {
        case Biome::DeepOcean:  return {0.0f, 0.0f};
        case Biome::Ocean:      return {0.0f, 0.0f};
        case Biome::Beach:      return {0.3f, 0.10f};
        case Biome::Grassland:  return {0.7f, 0.15f};
        case Biome::Forest:     return {0.9f, 0.10f};
        case Biome::Rainforest: return {1.0f, 0.10f};
        case Biome::Desert:     return {0.1f, 0.03f};
        case Biome::Mountain:   return {0.1f, 0.03f};
        case Biome::Snowcap:    return {0.1f, 0.03f};
        case Biome::Swamp:      return {0.6f, 0.08f};
        case Biome::Tundra:     return {0.2f, 0.05f};
    }
    return {0.0f, 0.0f};
}
```

- [ ] **Step 2: Write biomass per tile inside the generation loop**

In `WorldGenerator::generate`, inside the `for y / for x` body, after `tile.biome = classify_biome(...)` (around line 110), add:

```cpp
            const auto profile = biomass_for(tile.biome);
            tile.biomass_target = profile.target;
            tile.biomass_regrowth = profile.regrowth;
            tile.biomass = profile.target;  // 初始即满载
```

- [ ] **Step 3: Verify build and biomass values**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK.

Then verify biomass is wired correctly via a temporary 1-line probe (insert, build, run, remove). Insert in `src/world/world_generator.cpp` at the end of `generate()` just before `return world;`:

```cpp
    float t = 0.0f;
    for (const auto& tile : world.tiles_) t += tile.biomass;
    std::fprintf(stderr, "[probe] total_biomass=%f\n", t);
```

Build, run (existing main still prints generations), confirm stderr shows a non-zero value, **then remove the probe line and rebuild**.

---

## Task 5: Creature struct extension (no behavior yet)

**Files:**
- Modify: `E:/gamev2/include/core/creature.hpp`

**Produces:** `Creature` carries `id`, `pos`, `hunger`, `energy`, `age`, `dead` flags. Adds forward declaration of `decide_and_act` / `recover_energy` (full impl in next task).

- [ ] **Step 1: Extend Creature struct**

Replace the entire `include/core/creature.hpp` content with:

```cpp
#pragma once

#include "core/gene.hpp"
#include "core/position.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace game {

class World;
class EvolutionEngine;

struct Creature {
    std::uint64_t id = 0;          // 全局唯一
    Traits gene;
    std::string name;

    Position pos{0, 0};
    float hunger = 1.0f;            // 0=临界  1=饱
    float energy = 1.0f;            // 0=虚脱  1=精力充沛
    std::uint32_t age = 0;          // tick 数
    bool dead = false;              // 本 tick 内行为机判定死亡，由 Simulation 收割

    float fitness = 0.0f;
    bool is_elite = false;
    bool is_boss = false;
    std::uint32_t elite_age = 0;    // 连续保持精英的 tick 数
};

// 每 tick 调用一次：消耗饥饿/能量 → 死亡/吃/移动/繁殖/随机游走。
void decide_and_act(Creature& self, World& world,
                    std::vector<Creature>& births,
                    EvolutionEngine& evo,
                    std::mt19937_64& rng,
                    std::uint64_t& next_id);

void recover_energy(Creature& c);

// 基因哈希 → 音节组合名字，如 "鳞·噬渊者"。
auto make_name(const Traits& gene) -> std::string;

}  // namespace game
```

- [ ] **Step 2: Verify build still compiles (existing main.cpp)**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK. Existing `EvolutionEngine::step()` still uses the old `Creature` (which now has new members with defaults — must compile).

If the old `make_name(creature.gene)` call still works, the build passes; if any initializer in `evolution.cpp` overwrites a member we added with a default, fix that initializer.

---

## Task 6: implement `recover_energy` + `decide_and_act`

**Files:**
- Modify: `E:/gamev2/src/core/creature.cpp` — implement the two new functions

**Produces:** Behavior machine as specified in spec §5.2. Single parent, double-crossover → single-parent clone + mutate. Eating on current tile first; if still hungry and on low-biomass tile, scan neighbors; else if satiated + mature + on a `kMateInterval` tick, reproduce; else random walk (up to 4 retries, fails on water).

- [ ] **Step 1: Replace `src/core/creature.cpp` content**

Replace the entire file with:

```cpp
#include "core/creature.hpp"

#include "core/evolution.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <array>
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
```

- [ ] **Step 2: Verify build (will fail because `EvolutionEngine::reproduce(Creature)` doesn't exist yet)**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build FAILS with `no member named 'reproduce' in 'EvolutionEngine'`. This is correct — Task 7 adds it.

---

## Task 7: EvolutionEngine refactor (split `step()` into 4 methods + utilities)

**Files:**
- Modify: `E:/gamev2/include/core/evolution.hpp` — replace interface
- Modify: `E:/gamev2/src/core/evolution.cpp` — implement new API

**Produces:** EvolutionEngine exposes `evaluate_fitness`, `detect_elite_boss`, `reproduce(parent)`, `drift_environment`, plus mean/stddev utilities. Internal RNG retained. `step()` is removed.

- [ ] **Step 1: Replace `include/core/evolution.hpp` content**

Replace the entire header with:

```cpp
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
```

- [ ] **Step 2: Replace `src/core/evolution.cpp` content**

Replace the entire file with:

```cpp
#include "core/evolution.hpp"

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

void mutate(Traits& traits, float strength, std::mt19937_64& rng) {
    std::normal_distribution<float> dist{0.0f, strength};
    for (auto& value : traits.values) {
        value = std::clamp(value + dist(rng), 0.0f, 1.0f);
    }
}

auto EvolutionEngine::mean_fitness(std::span<const Creature> creatures) const -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) sum += c.fitness;
    return sum / static_cast<float>(creatures.size());
}

auto EvolutionEngine::stddev_fitness(std::span<const Creature> creatures, float mean) const
    -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) {
        const float d = c.fitness - mean;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<float>(creatures.size()));
}

auto EvolutionEngine::evaluate_fitness(std::span<Creature> creatures,
                                       const Traits& optimum) -> void {
    for (auto& c : creatures) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < c.gene.values.size(); ++i) {
            const float d = c.gene.values[i] - optimum.values[i];
            sum += d * d;
        }
        c.fitness = 1.0f / (1.0f + std::sqrt(sum));
    }
}

auto EvolutionEngine::detect_elite_boss(std::span<Creature> creatures,
                                        std::vector<Event>& events,
                                        std::uint64_t current_tick) -> void {
    const float m = mean_fitness(creatures);
    const float sd = stddev_fitness(creatures, m);
    const float threshold = m + params_.elite_threshold_sigma * sd;

    for (auto& c : creatures) {
        if (c.fitness > threshold) {
            if (!c.is_elite) {
                c.is_elite = true;
                events.push_back({EventType::EliteBorn, current_tick,
                    std::format("精英诞生: {} (fitness={:.3f})", c.name, c.fitness)});
            }
            ++c.elite_age;
            if (c.elite_age >= params_.boss_streak_ticks && !c.is_boss) {
                c.is_boss = true;
                events.push_back({EventType::BossEvolved, current_tick,
                    std::format("BOSS 蜕变: {} (fitness={:.3f})", c.name, c.fitness)});
            }
        } else if (c.is_elite) {
            c.is_elite = false;
            c.elite_age = 0;
        }
    }
}

auto EvolutionEngine::reproduce(const Creature& parent) -> Creature {
    Creature child;
    child.gene = parent.gene;          // 克隆
    mutate(child.gene, params_.mutation_strength, rng_);  // 变异
    child.name = make_name(child.gene);
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
```

- [ ] **Step 3: Verify build (expected to fail only at main.cpp)**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build FAILS with an error in `src/main.cpp` (it still calls the removed `EvolutionEngine::step()`). All other files compile cleanly. Task 11 rewrites `main.cpp`.

---

## Task 8: WorldGenerator initial population

**Files:**
- Modify: `E:/gamev2/src/world/world_generator.cpp` — add `populate_creatures()` helper invoked by `generate()`

**Produces:** `WorldGenerator::generate(seed)` returns a `World` whose `creatures` (initially empty) have been prepared. Actually the generator only returns `World`; creatures are owned by `Simulation`. So this task adds a **standalone helper** `populate_initial_creatures(world, count) -> std::vector<Creature>` that callers (the upcoming `Simulation` constructor) can use.

- [ ] **Step 1: Add `populate_initial_creatures` helper**

First, add two includes to the top of `src/world/world_generator.cpp` (next to the existing `#include "pcg/seed.hpp"`):

```cpp
#include "core/creature.hpp"
#include "core/gene.hpp"
```

Then, append the new function just before the final `}  // namespace game` at the bottom of the file:

```cpp
namespace {
constexpr std::uint32_t kInitialCreatureSalt = 0x1C7Eu;
}  // namespace

auto populate_initial_creatures(const World& world,
                                std::size_t count,
                                std::uint64_t seed) -> std::vector<Creature> {
    std::vector<Creature> out;
    out.reserve(count);

    // 收集所有陆地坐标。
    std::vector<Position> land_tiles;
    for (std::size_t y = 0; y < world.height(); ++y) {
        for (std::size_t x = 0; x < world.width(); ++x) {
            if (world.is_land(x, y)) land_tiles.push_back({x, y});
        }
    }
    if (land_tiles.empty()) return out;

    auto rng = std::mt19937_64{pcg::derive_seed(seed, kInitialCreatureSalt)};
    std::uniform_int_distribution<std::size_t> tile_pick(0, land_tiles.size() - 1);
    std::uniform_real_distribution<float> gene_pick(0.0f, 1.0f);

    for (std::size_t i = 0; i < count; ++i) {
        const Position& p = land_tiles[tile_pick(rng)];
        Traits gene;
        for (auto& v : gene.values) v = gene_pick(rng);
        Creature c;
        c.id = i + 1;
        c.pos = p;
        c.gene = gene;
        c.name = make_name(gene);
        c.hunger = 1.0f;
        c.energy = 1.0f;
        out.push_back(std::move(c));
    }
    return out;
}
```

- [ ] **Step 2: Verify build**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build FAILS only in main.cpp (still calling old `step()`). The helper compiles.

---

## Task 9: Simulation header (`Event`, `SimulationClock`, `Simulation`)

**Files:**
- Create: `E:/gamev2/include/core/simulation.hpp`

**Produces:** the only time owner. Owns World, Events queue, EvolutionEngine, SimulationClock, two RNG streams.

- [ ] **Step 1: Create `include/core/simulation.hpp`**

```cpp
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
```

- [ ] **Step 2: Verify (no impl file yet — only header)**

Header included from main.cpp won't compile yet (impl missing). Defer build verification to Task 8 step 2 / Task 10.

---

## Task 10: Simulation implementation + CMakeLists update

**Files:**
- Create: `E:/gamev2/src/core/simulation.cpp`
- Modify: `E:/gamev2/CMakeLists.txt` — add `src/core/simulation.cpp` to executable

**Produces:** the `tick()` orchestration exactly matching the spec pseudocode.

- [ ] **Step 1: Update CMakeLists.txt**

In `CMakeLists.txt`, in the `add_executable(gamev2 ...)` list (around lines 18-26), add `src/core/simulation.cpp`:

```cmake
add_executable(gamev2
    src/main.cpp
    src/core/gene.cpp
    src/core/creature.cpp
    src/core/evolution.cpp
    src/core/simulation.cpp
    src/world/world.cpp
    src/world/world_generator.cpp
    src/render/tile_renderer.cpp
)
```

- [ ] **Step 2: Create `src/core/simulation.cpp`**

```cpp
#include "core/simulation.hpp"

#include "core/creature.hpp"
#include "world/world_generator.hpp"

#include <pcg/seed.hpp>

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
```

- [ ] **Step 3: Verify build (will fail in main.cpp)**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build FAILS only at `src/main.cpp` calling old `EvolutionEngine::step()`. The Simulation object file compiles cleanly.

---

## Task 11: TileRenderer extension (`render_creatures`, `render_hud`, `gene_color`)

**Files:**
- Modify: `E:/gamev2/include/render/tile_renderer.hpp`
- Modify: `E:/gamev2/src/render/tile_renderer.cpp`

**Produces:** renderer can draw creatures (2×2 colored pixel; 3×3 with yellow border for elite; 4×4 with red border + 1-char name for boss) on top of the world, plus a HUD string with tick/pop/energy/paused state.

- [ ] **Step 1: Extend `include/render/tile_renderer.hpp`**

After the existing `render(...)` declaration (line 32), add:

```cpp
    void render_creatures(std::span<const Creature> creatures);
    void render_hud(std::uint64_t tick, std::size_t pop, float energy,
                    bool paused, float speed);

    [[nodiscard]] auto raw_renderer() noexcept -> SDL_Renderer* { return renderer_.get(); }
```

Add at the top (after `#pragma once` / existing includes):

```cpp
#include <span>
#include "core/creature.hpp"
```

- [ ] **Step 2: Extend `src/render/tile_renderer.cpp`**

Add at top, after existing includes:

```cpp
#include "core/creature.hpp"
#include <bit>
#include <span>
```

Add anonymous-namespace helpers (next to `biome_color`):

```cpp
auto gene_color(const Traits& gene) -> std::uint32_t {
    // 用与 make_name 同样的 FNV-1a 哈希 → RGB。
    std::uint32_t hash = 0x811C9DC5u;
    for (const float v : gene.values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
        hash = (hash ^ bits) * 0x01000193u;
    }
    const auto r = static_cast<std::uint8_t>((hash >> 0) & 0xFFu);
    const auto g = static_cast<std::uint8_t>((hash >> 8) & 0xFFu);
    const auto b = static_cast<std::uint8_t>((hash >> 16) & 0xFFu);
    return pack_rgba(r, g, b);
}

auto draw_creature_dot(SDL_Renderer* r, std::int32_t cx, std::int32_t cy,
                       std::int32_t half, std::uint32_t fill,
                       std::uint32_t border) -> void {
    SDL_SetRenderDrawColor(r,
        (fill >> 0) & 0xFFu, (fill >> 8) & 0xFFu, (fill >> 16) & 0xFFu, 0xFFu);
    SDL_FRect fill_rect{
        static_cast<float>(cx - half),
        static_cast<float>(cy - half),
        static_cast<float>(half * 2 + 1),
        static_cast<float>(half * 2 + 1)
    };
    SDL_RenderFillRect(r, &fill_rect);
    if (border != 0) {
        SDL_SetRenderDrawColor(r,
            (border >> 0) & 0xFFu, (border >> 8) & 0xFFu, (border >> 16) & 0xFFu, 0xFFu);
        SDL_FRect b{
            static_cast<float>(cx - half - 1),
            static_cast<float>(cy - half - 1),
            static_cast<float>(half * 2 + 3),
            static_cast<float>(half * 2 + 3)
        };
        SDL_RenderRect(r, &b);
    }
}
```

Add the new methods at the bottom of the file:

```cpp
void TileRenderer::render_creatures(std::span<const Creature> creatures) {
    constexpr std::uint32_t kEliteBorder = (0xFFu << 0) | (0xE0u << 8) | (0x00u << 16);  // 黄
    constexpr std::uint32_t kBossBorder  = (0x40u << 0) | (0x40u << 8) | (0xFFu << 16);  // 红
    auto* r = renderer_.get();
    for (const auto& c : creatures) {
        if (c.dead) continue;
        const std::int32_t cx = static_cast<std::int32_t>(c.pos.x);
        const std::int32_t cy = static_cast<std::int32_t>(c.pos.y);
        std::int32_t half = 1;
        std::uint32_t border = 0;
        if (c.is_elite) { half = 1; border = kEliteBorder; }
        if (c.is_boss)  { half = 2; border = kBossBorder; }
        draw_creature_dot(r, cx, cy, half, gene_color(c.gene), border);
        if (c.is_boss && !c.name.empty()) {
            SDL_RenderDebugTextFormat(r,
                static_cast<float>(cx + 3),
                static_cast<float>(cy - 6),
                "%.1s", c.name.c_str());
        }
    }
}

void TileRenderer::render_hud(std::uint64_t tick, std::size_t pop, float energy,
                              bool paused, float speed) {
    auto* r = renderer_.get();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0xC0u);  // 半透明黑底
    SDL_FRect bg{2.0f, 2.0f, 220.0f, 60.0f};
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 220, 220, 220, 0xFFu);
    SDL_RenderDebugTextFormat(r, 6.0f, 4.0f,
        "tick=%llu pop=%zu E=%.0f %s x%.1f",
        static_cast<unsigned long long>(tick), pop, energy,
        paused ? "PAUSE" : "RUN", speed);
}
```

- [ ] **Step 3: Verify build (will fail at main.cpp only)**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build FAILS only at `src/main.cpp`. Renderer code compiles.

---

## Task 12: SDL3 main loop rewrite

**Files:**
- Modify: `E:/gamev2/src/main.cpp` — replace CLI loop with SDL3 event loop

**Produces:** window opens, world renders, creatures appear as dots, SPACE pauses, 1-4 changes speed, ESC quits.

- [ ] **Step 1: Replace `src/main.cpp`**

```cpp
#include "core/simulation.hpp"
#include "render/tile_renderer.hpp"
#include "world/world_generator.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <format>
#include <iostream>
#include <random>
#include <string_view>

namespace {

auto random_seed() -> std::uint64_t {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

auto parse_seed(int argc, char** argv) -> std::uint64_t {
    std::uint64_t seed = random_seed();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
        }
    }
    return seed;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    SetConsoleOutputCP(CP_UTF8);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << std::format("SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }

    constexpr std::size_t kMapW = 80;
    constexpr std::size_t kMapH = 60;

    const std::uint64_t seed = parse_seed(argc, argv);
    World world = WorldGenerator(kMapW, kMapH).generate(seed);
    Simulation sim(std::move(world), seed);

    auto renderer = TileRenderer::create(kMapW, kMapH, "World Will — Time-Flow");
    if (!renderer) {
        std::cerr << std::format("TileRenderer::create failed: {}\n", renderer.error());
        SDL_Quit();
        return 1;
    }

    bool running = true;
    bool paused = false;
    float speed = 1.0f;
    Uint64 last = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        case SDLK_SPACE:
                            paused = !paused;
                            break;
                        case SDLK_1: speed = 0.5f; break;
                        case SDLK_2: speed = 1.0f; break;
                        case SDLK_3: speed = 2.0f; break;
                        case SDLK_4: speed = 4.0f; break;
                    }
                    break;
            }
        }

        Uint64 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.25f) dt = 0.25f;  // 防止大间隔导致 accumulator 失控

        sim.advance(paused ? 0.0f : dt, speed);

        renderer->render(sim.world());
        renderer->render_creatures(sim.creatures());
        renderer->render_hud(sim.tick_count(), sim.creatures().size(),
                             sim.world_energy(), paused, speed);
        SDL_RenderPresent(renderer->raw_renderer());

        // 控制台日志（每 60 tick 打印一次，避免刷屏）
        static std::uint64_t last_log_tick = 0;
        if (sim.tick_count() - last_log_tick >= 60) {
            last_log_tick = sim.tick_count();
            std::cout << std::format(
                "[tick {:>5}] pop={:<4} energy={:.0f} speed={:.1f}x {}\n",
                sim.tick_count(), sim.creatures().size(), sim.world_energy(),
                speed, paused ? "PAUSED" : "");
            for (const auto& e : sim.events()) {
                std::cout << "         " << e.description << '\n';
            }
        }

        SDL_Delay(16);  // ~60 FPS 上限
    }

    SDL_Quit();
    return 0;
}
```

- [ ] **Step 2: Build**

Run:
```powershell
pwsh E:/gamev2/build.ps1
```
Expected: build OK (warnings allowed, but `/WX` means they must be addressed if any appear).

- [ ] **Step 3: Manual verification — run**

Run:
```powershell
pwsh E:/gamev2/build.ps1 run
```
Then in the window verify:
1. **Map visible**: colored tiles matching biomes (blue water, green grass, etc.).
2. **Creatures visible**: dozens of colored 2×2 dots scattered on land.
3. **Movement**: dots shift position every fraction of a second.
4. **HUD readable**: top-left shows `tick=N pop=N E=N ... x1.0` (or PAUSE).
5. **Pause**: press SPACE → HUD shows PAUSE, creatures freeze.
6. **Speed**: press 3 → speed becomes 2.0x; creatures move visibly faster.
7. **Quit**: press ESC → window closes; program exits 0.

If the build fails with `/WX` due to a warning, fix the warning (e.g. unused parameter, sign-conversion) and rebuild.

If creatures never move: most likely `decide_and_act` never runs because creatures are all on full tiles and don't search. Verify in console that `pop` is non-zero and `tick` is increasing.

---

## Task 13: Final smoke test + sanity checks

**Files:** none (verification only)

**Purpose:** Confirm spec §11 manual validation scenarios all hold.

- [ ] **Step 1: 60-second run**

Run:
```powershell
pwsh E:/gamev2/build.ps1 run
```
Leave for 60 seconds. Check:
1. **No crash** — process still running.
2. **Population churn** — `pop` count fluctuates (some die, some are born).
3. **Console events** — at least one `精英诞生` or `BOSS 蜕变` line printed within 60s.
4. **Window responsive** — pause/speed/quit all still work.

- [ ] **Step 2: Pause stability**

Press SPACE; wait 1 sec; press SPACE again. Verify `pop` value before pause = `pop` value after resume (no ticks consumed during pause).

- [ ] **Step 3: Speed verification**

Press 4 (4x speed). Verify HUD shows `x4.0` and creatures visibly move ~4× faster.

- [ ] **Step 4: Cleanup — leave build clean**

Close the program. Confirm no zombie processes in Task Manager.

---

## Spec Coverage Check

| Spec section | Implementation task(s) |
|---|---|
| §3 Time model (clock, params) | Task 9 (SimulationClock), Task 10 (advance) |
| §4 World extension (Tile biomass, regrow, is_land, total_biomass) | Task 2, Task 3, Task 4 |
| §5 Creature extension (pos/hunger/energy/age/dead/id, decide_and_act) | Task 5, Task 6 |
| §6 EvolutionEngine refactor (4 methods + utilities) | Task 7 |
| §7 Simulation main loop (tick orchestration) | Task 9 (header), Task 10 (impl) |
| §7.3 decide_and_act placement | Task 6 |
| §8 Renderer extension (render_creatures, render_hud, gene_color) | Task 11 |
| §9 main.cpp SDL3 loop | Task 12 |
| §10 file structure | All tasks (file paths per task headers) |
| §11 testing strategy | Tasks 4, 12, 13 (build + manual observation) |
| §12 risks (max_pop cap, accumulator cap, RNG streams, event trim) | Task 10 (max_pop, trim); Task 9 (max_ticks_per_frame); Task 10 (separate behavior_rng_); Task 12 (dt cap) |

All spec requirements are covered. No gaps.
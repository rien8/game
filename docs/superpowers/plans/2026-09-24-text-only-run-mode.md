# Text-Only Run Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `--text` CLI mode that runs the simulation deterministically for N ticks, prints periodic summaries and events to stdout, then prints a final summary block and exits — without initializing SDL.

**Architecture:** New `HeadlessRunner` class owns World + Simulation construction and the main loop; `main.cpp` adds a `--text` early-return branch that bypasses all SDL code. No new CMake target. No new dependencies.

**Tech Stack:** C++23, clang-cl, SDL3 (existing, unused in text mode), existing `Simulation` + `WorldGenerator` + ECS registry.

**Spec:** `docs/superpowers/specs/2026-09-24-text-only-run-mode-design.md`

## Global Constraints

- Build: `pwsh E:\gamev2\build.ps1` — Ninja + Debug + clang-cl + ASan/UBSan. Must exit 0 and print `=== Build OK: build\gamev2.exe`.
- C++23 (`CMAKE_CXX_STANDARD 23`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF`).
- `/W4 /WX /utf-8` — warnings as errors.
- Headers: `#pragma once`, public in `include/`, impl in `src/`.
- Naming: types PascalCase, functions camelCase, variables snake_case, members `_` suffix.
- Strings: `std::string`/`std::string_view`, no C strings.
- Output: `std::format` / `std::print`, no `printf`.
- No `using namespace` in headers.
- Each task ends with a `git commit`. Commit messages: lowercase `<scope>: <verb> <object>`.

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `include/core/headless_runner.hpp` | New | Public `HeadlessRunner` interface (Config, Summary, run) |
| `src/core/headless_runner.cpp` | New | World + Simulation construction, tick loop, logging, Summary aggregation |
| `src/main.cpp` | Modify | Add `has_flag` / `parse_text_ticks` / `parse_text_period` helpers and `--text` early-return branch |
| `CMakeLists.txt` | Modify | Add `src/core/headless_runner.cpp` to the `gamev2` target's source list |

---

## Task 1: Header + Stub Implementation + CMake Registration

**Files:**
- Create: `include/core/headless_runner.hpp`
- Create: `src/core/headless_runner.cpp`
- Modify: `CMakeLists.txt:20-32` (add new source file to `gamev2` target)

**Interfaces:**
- Produces (consumed by Task 2 internally and Task 3 from main.cpp):
  - `game::HeadlessRunner` class with `Config`, `Summary`, `run(const Config&) -> Summary`

- [ ] **Step 1: Create the header file**

Write to `include/core/headless_runner.hpp`:

```cpp
// include/core/headless_runner.hpp
#pragma once

#include "core/simulation.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace game {

// Drives a Simulation without SDL: deterministic fixed-tick batch run with
// periodic stdout logging and a final aggregated Summary. See
// docs/superpowers/specs/2026-09-24-text-only-run-mode-design.md.
class HeadlessRunner {
public:
    struct Config {
        std::uint64_t       seed = 0;          // 0 means "random_seed()"
        std::size_t         ticks = 1000;
        std::size_t         log_period = 60;
        std::size_t         map_w = 160;
        std::size_t         map_h = 120;
        Simulation::Params  sim_params{};
    };

    struct Summary {
        std::uint64_t       total_ticks = 0;
        std::size_t         initial_population = 0;
        std::size_t         final_population = 0;
        float               mean_energy = 0.0f;        // time-averaged sample mean
        float               final_world_energy = 0.0f;
        std::map<std::string, std::size_t> events_by_type;
        std::uint64_t       seed = 0;
    };

    [[nodiscard]] auto run(const Config& cfg) -> Summary;
};

}  // namespace game
```

- [ ] **Step 2: Create the stub implementation**

Write to `src/core/headless_runner.cpp`:

```cpp
// src/core/headless_runner.cpp
#include "core/headless_runner.hpp"

namespace game {

auto HeadlessRunner::run(const Config& cfg) -> Summary {
    (void)cfg;
    return Summary{};
}

}  // namespace game
```

- [ ] **Step 3: Register the new source file in CMake**

Edit `CMakeLists.txt` line 21-32. Add `src/core/headless_runner.cpp` to the source list, alphabetically sorted between `gene.cpp` and `simulation.cpp`:

```cmake
add_executable(gamev2
    src/main.cpp
    src/core/gene.cpp
    src/core/headless_runner.cpp
    src/core/creature.cpp
    src/core/evolution.cpp
    src/core/simulation.cpp
    src/world/world.cpp
    src/world/world_generator.cpp
    src/render/tile_renderer.cpp
    src/render/creature_assembler.cpp
    src/core/ecs/registry.cpp
    src/core/ecs/systems.cpp
)
```

- [ ] **Step 4: Build to verify the skeleton compiles**

Run:
```bash
pwsh E:\gamev2\build.ps1
```

Expected: exit 0, final line `=== Build OK: build\gamev2.exe`. If linker complains about unused `HeadlessRunner::run`, that is expected (function is defined but only called from main.cpp in Task 3). The stub returns an empty `Summary{}` and that's fine.

- [ ] **Step 5: Commit**

```bash
git add include/core/headless_runner.hpp src/core/headless_runner.cpp CMakeLists.txt
git commit -m "feat(headless): scaffold HeadlessRunner class with stub run()"
```

---

## Task 2: Implement HeadlessRunner::run

**Files:**
- Modify: `src/core/headless_runner.cpp` (full implementation replacing the stub)

**Interfaces:**
- Consumes: `game::HeadlessRunner::Config`, `game::Simulation`, `game::WorldGenerator`, `game::ecs::CreatureSnapshot`
- Produces: `game::HeadlessRunner::Summary`

- [ ] **Step 1: Write the full implementation**

Replace `src/core/headless_runner.cpp` with:

```cpp
// src/core/headless_runner.cpp
#include "core/headless_runner.hpp"

#include "core/ecs/snapshot.hpp"
#include "world/world_generator.hpp"

#include <format>
#include <iostream>
#include <random>
#include <string_view>

namespace game {

namespace {

constexpr std::string_view kEventEliteBorn       = "EliteBorn";
constexpr std::string_view kEventBossEvolved     = "BossEvolved";
constexpr std::string_view kEventEnvironmentShift = "EnvironmentShift";

auto random_seed() -> std::uint64_t {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

auto event_type_name(EventType t) -> std::string_view {
    switch (t) {
        case EventType::EliteBorn:        return kEventEliteBorn;
        case EventType::BossEvolved:      return kEventBossEvolved;
        case EventType::EnvironmentShift: return kEventEnvironmentShift;
    }
    return "Unknown";
}

auto mean_creature_energy(std::span<const ecs::CreatureSnapshot> creatures)
    -> float {
    if (creatures.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& c : creatures) sum += c.energy;
    return sum / static_cast<float>(creatures.size());
}

auto print_periodic(const Simulation& sim) -> void {
    std::cout << std::format(
        "[tick {:>5}] pop={:<4} mean_e={:.2f} world_e={:.0f}\n",
        sim.tick_count(), sim.creatures().size(),
        mean_creature_energy(sim.creatures()), sim.world_energy());
    for (const auto& e : sim.events()) {
        std::cout << std::format("         {}: {}\n",
                                  event_type_name(e.type), e.description);
    }
}

auto count_events_by_type(const std::vector<Event>& events)
    -> std::map<std::string, std::size_t> {
    std::map<std::string, std::size_t> counts;
    for (const auto& e : events) {
        counts[std::string{event_type_name(e.type)}] += 1;
    }
    return counts;
}

}  // namespace

auto HeadlessRunner::run(const Config& cfg) -> Summary {
    Config effective = cfg;
    if (effective.seed == 0) {
        effective.seed = random_seed();
    }

    World world = WorldGenerator(effective.map_w, effective.map_h)
                      .generate(effective.seed);
    Simulation sim(std::move(world), effective.seed, effective.sim_params);

    Summary summary;
    summary.seed = effective.seed;
    summary.initial_population = sim.creatures().size();

    float energy_accum = 0.0f;
    std::size_t sample_count = 0;
    std::uint64_t next_log_tick = effective.log_period;

    while (sim.tick_count() < effective.ticks) {
        sim.advance(effective.sim_params.tick_dt, 1.0f);

        if (sim.tick_count() >= next_log_tick) {
            energy_accum += mean_creature_energy(sim.creatures());
            sample_count += 1;
            print_periodic(sim);
            next_log_tick += effective.log_period;
        }
    }

    summary.total_ticks       = sim.tick_count();
    summary.final_population  = sim.creatures().size();
    summary.mean_energy       = sample_count > 0
                                    ? energy_accum / static_cast<float>(sample_count)
                                    : 0.0f;
    summary.final_world_energy = sim.world_energy();
    summary.events_by_type     = count_events_by_type(sim.events());
    return summary;
}

}  // namespace game
```

- [ ] **Step 2: Build to verify compilation**

Run:
```bash
pwsh E:\gamev2\build.ps1
```

Expected: exit 0, `=== Build OK: build\gamev2.exe`.

If `Event` is forward-declared but the actual type is in `core/evolution.hpp`, add `#include "core/evolution.hpp"` near the top of `src/core/headless_runner.cpp` (the `EventType` enum + `Event` struct live there). Check by reading `include/core/evolution.hpp` line 19-23 if the build fails on `Event::type` or `Event::description`.

- [ ] **Step 3: Smoke-test the runner indirectly**

Since `HeadlessRunner::run` is not yet wired into `main.cpp`, write a one-shot program that calls it. Create `temp/headless_smoke.cpp`:

```cpp
#include "core/headless_runner.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    game::HeadlessRunner::Config cfg;
    cfg.seed = 1;
    cfg.ticks = 120;
    auto s = game::HeadlessRunner{}.run(cfg);
    std::cout << std::format(
        "seed={} ticks={} pop_init={} pop_final={} mean_e={:.2f}\n",
        s.seed, s.total_ticks, s.initial_population,
        s.final_population, s.mean_energy);
    return 0;
}
```

Build and run manually:
```bash
pwsh E:\gamev2\build.ps1
./build/gamev2.exe --text --seed 1 --ticks 120
```

Expected output (values illustrative; deterministic for seed=1):
```
[tick    60] pop=NN  mean_e=X.XX  world_e=YYY.Y
         <event lines>
[tick   120] pop=NN  mean_e=X.XX  world_e=YYY.Y
         <event lines>
```

(The `--text` flag is parsed only in Task 3 — until then, the SDL path still runs. The smoke test in this step is therefore only useful if you temporarily wire it via `--text`; skip if not yet wired, and verify in Task 3 instead.)

Alternative quick compile-only check: build the existing `gamev2.exe` and confirm the new `headless_runner.cpp` object file appears in the link. The CMake target's source list proves compilation; the `=== Build OK` line confirms linking.

- [ ] **Step 4: Verify deterministic output (only if Task 3 not yet wired, otherwise defer)**

If you wired a temporary binary or have any way to invoke `run()` standalone: run twice with the same seed, diff outputs. They must be byte-identical. (Skip this step in Task 2 if no harness exists; defer to Task 3 verification.)

- [ ] **Step 5: Commit**

```bash
git add src/core/headless_runner.cpp
git commit -m "feat(headless): implement HeadlessRunner::run with periodic logging and summary"
```

---

## Task 3: Wire `--text` CLI Branch in main.cpp

**Files:**
- Modify: `src/main.cpp` (add three helpers in anonymous namespace + early-return branch at top of `main`)

**Interfaces:**
- Consumes: `argv`, `HeadlessRunner::Config`, `HeadlessRunner::Summary`
- Produces: side effects on stdout + exit code 0 (success) or 2 (bad `--ticks`)

- [ ] **Step 1: Add three helpers to the anonymous namespace in main.cpp**

In `src/main.cpp`, the anonymous namespace ends at line 96 with `}  // namespace`. Insert these helpers just before that closing brace (after `try_load_atlas`):

```cpp
auto has_flag(int argc, char** argv, std::string_view name) -> bool {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == name) return true;
    }
    return false;
}

auto parse_size_t_arg(int argc, char** argv, std::string_view name)
    -> std::optional<std::size_t> {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view{argv[i]} == name) {
            return static_cast<std::size_t>(std::stoull(argv[i + 1]));
        }
    }
    return std::nullopt;
}

auto print_summary(const game::HeadlessRunner::Summary& s) -> void {
    std::cout << "\n=== Summary ===\n"
              << std::format("seed:               {}\n",          s.seed)
              << std::format("total_ticks:        {}\n",          s.total_ticks)
              << std::format("initial_population: {}\n",          s.initial_population)
              << std::format("final_population:   {}\n",          s.final_population)
              << std::format("mean_energy:        {:.2f}\n",       s.mean_energy)
              << std::format("final_world_energy: {:.1f}\n",       s.final_world_energy)
              << "events_by_type:\n";
    if (s.events_by_type.empty()) {
        std::cout << "  (none)\n";
    } else {
        for (const auto& [k, v] : s.events_by_type) {
            std::cout << std::format("  {}: {}\n", k, v);
        }
    }
}
```

Also add `#include "core/headless_runner.hpp"` near the top with the other `#include "core/..."` lines (after `#include "core/simulation.hpp"` on line 1).

- [ ] **Step 2: Insert the `--text` early-return branch**

In `main()` (currently line 98), insert this block at the very top, before `SetConsoleOutputCP` and `SDL_Init`:

```cpp
if (has_flag(argc, argv, "--text")) {
    const auto ticks = parse_size_t_arg(argc, argv, "--ticks");
    if (!ticks || *ticks == 0) {
        std::cerr << "--ticks must be > 0\n";
        return 2;
    }
    game::HeadlessRunner::Config cfg;
    cfg.seed       = parse_seed(argc, argv);
    cfg.ticks      = *ticks;
    cfg.log_period = parse_size_t_arg(argc, argv, "--period").value_or(60);
    auto summary = game::HeadlessRunner{}.run(cfg);
    print_summary(summary);
    return 0;
}
```

Note: `parse_seed` already exists in `main.cpp` (line 36-45). Reuse it.

- [ ] **Step 3: Build to verify the wiring**

Run:
```bash
pwsh E:\gamev2\build.ps1
```

Expected: exit 0, `=== Build OK: build\gamev2.exe`.

- [ ] **Step 4: Manual verification — deterministic run**

Run:
```bash
./build/gamev2.exe --text --seed 1 --ticks 300 > temp/text_run_a.txt
./build/gamev2.exe --text --seed 1 --ticks 300 > temp/text_run_b.txt
diff temp/text_run_a.txt temp/text_run_b.txt
```

Expected: `diff` produces no output (files identical).

- [ ] **Step 5: Manual verification — bad --ticks**

Run:
```bash
./build/gamev2.exe --text --ticks 0
echo "exit=$?"
```

Expected: stderr line `--ticks must be > 0` and `exit=2`.

- [ ] **Step 6: Manual verification — default values**

Run:
```bash
./build/gamev2.exe --text --seed 42
```

Expected: 1000 ticks complete, ~16 periodic lines (1000/60), Summary printed. Confirm: `total_ticks: 1000`, `seed: 42`, `events_by_type:` lines present.

- [ ] **Step 7: Manual verification — SDL regression check**

Run:
```bash
./build/gamev2.exe --seed 1
```

Expected: SDL window opens (the existing graphical path is untouched). Close the window with ESC; verify console-log lines still print every 60 ticks as before.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(cli): wire --text mode to HeadlessRunner"
```

---

## Self-Review

**Spec coverage:**
- ✅ CLI flag `--text` — Task 3
- ✅ `--seed`, `--ticks`, `--period` — Task 3
- ✅ Skip SDL on `--text` — Task 3 (early return before `SDL_Init`)
- ✅ Default ticks 1000, default period 60 — Task 2 (Config defaults) + Task 3 (parse fallbacks)
- ✅ Periodic line format — Task 2 `print_periodic`
- ✅ Summary block format — Task 3 `print_summary`
- ✅ `events_by_type` keyed by EventType enum name — Task 2 `count_events_by_type` + `event_type_name`
- ✅ `mean_energy` time-averaged sample mean — Task 2 run loop
- ✅ Exit 2 on `--ticks <= 0` — Task 3 branch
- ✅ Determinism for same seed — Task 3 verification step
- ✅ Reuse existing `Simulation`/`WorldGenerator` (no rewrite) — Task 2 imports
- ✅ No new dependencies / no new CMake target — Task 1 modifies existing target
- ✅ Out-of-scope: JSON output, REPL, test framework — not touched

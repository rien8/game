# Text-Only Run Mode — Design

## Goal

Add a `--text` CLI flag that runs the simulation deterministically for a fixed number of ticks, prints periodic summaries and events to stdout, then prints a final summary block and exits — without initializing SDL or creating a window.

## Motivation

Currently the only way to observe game mechanics is the SDL window. This is unsuitable for:

- Quick demos on machines without a display
- Understanding evolution dynamics over many ticks
- Future headless tooling (CI, regression snapshots, AI training)

A text-only mode addresses all three by reusing the existing `Simulation` (which already has no SDL dependency) behind a thin runner class.

## CLI

```
gamev2 --text [--seed N] [--ticks N] [--period N]
```

- `--text` — required, switches to text-only mode (no SDL)
- `--seed N` — optional, deterministic seed (default: `random_device`)
- `--ticks N` — optional, total ticks to run (default: 1000). Must be `> 0`, otherwise `return 2`
- `--period N` — optional, log every N ticks (default: 60)

When `--text` is set, `main` skips `SDL_Init`, `TileRenderer::create`, atlas loading, camera, and the SDL event loop entirely.

## Architecture

```
CLI argv
  ↓
main.cpp: parse --text flag
  ↓ (--text present)
HeadlessRunner::run(Config) -> Summary
  ↓
  - construct World via WorldGenerator(seed)
  - construct Simulation (no SDL dep)
  - loop: sim.advance(tick_dt, 1.0) until sim.tick_count() >= ticks
  - each `period` ticks: print periodic line + drain sim.events() to stdout
  - accumulate energy sample mean + event counts
  - return Summary
  ↓
main.cpp: print Summary block to stdout, return 0
```

`HeadlessRunner` does not include any SDL headers and does not call any SDL function. `Simulation` already satisfies this constraint (its rendering concern is handled at the `main.cpp` layer).

## Component: HeadlessRunner

File: `include/core/headless_runner.hpp`, `src/core/headless_runner.cpp`.

```cpp
namespace game {

class HeadlessRunner {
public:
    struct Config {
        std::uint64_t seed = 0;            // 0 = random_device
        std::size_t   ticks = 1000;
        std::size_t   log_period = 60;
        std::size_t   map_w = 160;
        std::size_t   map_h = 120;
        Simulation::Params sim_params{};
    };

    struct Summary {
        std::uint64_t total_ticks = 0;
        std::size_t   initial_population = 0;
        std::size_t   final_population = 0;
        float         mean_energy = 0.0f;        // time-averaged sample mean
        float         final_world_energy = 0.0f;
        std::map<std::string, std::size_t> events_by_type;
        std::uint64_t seed = 0;
    };

    [[nodiscard]] auto run(const Config& cfg) -> Summary;
};

} // namespace game
```

### Run loop

```
run(cfg):
  if cfg.seed == 0: cfg.seed = random_seed()
  World world = WorldGenerator(cfg.map_w, cfg.map_h).generate(cfg.seed)
  Simulation sim(std::move(world), cfg.seed, cfg.sim_params)
  initial_pop = sim.creatures().size()

  energy_accum = 0.0
  sample_count = 0
  next_log_tick = cfg.log_period

  while sim.tick_count() < cfg.ticks:
    sim.advance(cfg.sim_params.tick_dt, 1.0f)
    if sim.tick_count() >= next_log_tick:
      energy_accum += mean(creatures' energies)
      sample_count += 1
      print_periodic(sim)
      next_log_tick += cfg.log_period

  return Summary{ ... }
```

`mean_energy` is the time-averaged sample mean across `log_period` checkpoints. Not weighted by population; sampled mean-of-means.

### Periodic output format

```
[tick   60] pop=42  mean_e=1.42  world_e=987.0
         EliteBorn: <description>
         EnvironmentShift: <description>
```

The first line matches the existing `main.cpp` console-log format (currently `[tick N] pop=K energy=X.X speed=Y.Yx PAUSED`) but is adapted for text-mode: no `speed`/`paused` fields, and `energy` becomes the per-creature mean.

Event lines use the existing `Event::description` field, prefixed by the enum name stringified.

### Summary output

```
=== Summary ===
seed:               1234567890123
total_ticks:        1000
initial_population: 30
final_population:   87
mean_energy:        1.23
final_world_energy: 542.1
events_by_type:
  EliteBorn:        3
  BossEvolved:      1
  EnvironmentShift: 4
```

`events_by_type` keys are `EventType` enum names: `EliteBorn`, `BossEvolved`, `EnvironmentShift`. Keys with zero events are omitted.

## main.cpp Changes

Add three small helpers and one branch:

```cpp
auto has_flag(int argc, char** argv, std::string_view name) -> bool;
auto parse_text_ticks(int argc, char** argv) -> std::optional<std::size_t>;
auto parse_text_period(int argc, char** argv) -> std::optional<std::size_t>;
```

At the top of `main`, before `SDL_Init`:

```cpp
if (has_flag(argc, argv, "--text")) {
    const auto ticks = parse_text_ticks(argc, argv);
    if (!ticks || *ticks == 0) {
        std::cerr << "--ticks must be > 0\n";
        return 2;
    }
    HeadlessRunner::Config cfg{
        .seed = parse_seed(argc, argv),
        .ticks = *ticks,
        .log_period = parse_text_period(argc, argv).value_or(60),
    };
    auto summary = HeadlessRunner{}.run(cfg);
    print_summary(summary);
    return 0;
}
```

The SDL block below is unchanged.

## Error Handling

- `--ticks 0` or negative → `stderr` message + `return 2`
- `--ticks NaN` (non-numeric) → `std::stoull` throws; let it propagate to `std::terminate` (matches existing `parse_seed` behavior)
- `--period` defaults to 60 if omitted; if invalid, same as `--ticks`
- `WorldGenerator::generate` / `Simulation::advance` errors: not currently reported via `std::expected`; runner lets them propagate (no new error surface added)

## Testing

Project has no existing test infrastructure (no `tests/`, no Catch2/doctest dependency). Spec defers test scaffolding to a follow-up — current scope is functional verification via manual runs:

1. `pwsh build.ps1 run -- --text --seed 1 --ticks 100` — expect 2 periodic lines + Summary
2. `pwsh build.ps1 run -- --text --seed 1 --ticks 100` twice — byte-identical output (determinism)
3. `pwsh build.ps1 run -- --text --ticks 0` — exit code 2, stderr message
4. `pwsh build.ps1 run -- --seed 1` (no `--text`) — SDL window opens as before (regression)

Test scaffolding (a `tests/` directory with a Catch2 dependency) is explicitly out of scope for this design. A future spec can introduce it.

## Out of Scope

- JSON / structured output (`--json` flag)
- Replay / log saving
- Interactive REPL
- Test framework setup
- Modifying the existing SDL console-log line (kept for parity, not changed)

## Build

No new CMake target needed. New files:

- `include/core/headless_runner.hpp`
- `src/core/headless_runner.cpp`

Both added to the existing `gamev2` library target. `main.cpp` is modified in place.

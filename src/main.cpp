#include "core/evolution.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

}  // namespace

auto main(int argc, char** argv) -> int {
    // Windows 控制台默认按系统代码页（GBK）解码，切换为 UTF-8 以正确显示中文。
    SetConsoleOutputCP(CP_UTF8);

    std::uint64_t seed = random_seed();
    std::size_t generations = 200;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--generations" && i + 1 < argc) {
            generations = static_cast<std::size_t>(std::stoull(argv[++i]));
        }
    }

    std::cout << std::format("=== 世界演化 seed={} generations={} ===\n", seed, generations);

    game::EvolutionEngine engine{{}, seed};
    for (std::size_t i = 0; i < generations; ++i) {
        const auto report = engine.step();
        std::cout << std::format(
            "[gen {:>4}] pop={:<4} fit(avg={:.3f} max={:.3f}) elite={} boss={} energy={:.0f}\n",
            report.generation, report.population_size, report.avg_fitness,
            report.max_fitness, report.elite_count, report.boss_count, report.world_energy);
        for (const auto& event : report.events) {
            std::cout << "         " << event.description << '\n';
        }
    }

    return 0;
}

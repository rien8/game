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
    game::World world = game::WorldGenerator(kMapW, kMapH).generate(seed);
    game::Simulation sim(std::move(world), seed, game::Simulation::Params{});

    auto renderer = game::TileRenderer::create(kMapW, kMapH, "World Will — Time-Flow");
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

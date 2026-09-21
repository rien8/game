#include "core/simulation.hpp"
#include "render/creature_assembler.hpp"
#include "render/tile_renderer.hpp"
#include "world/world_generator.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kManifestPath = "assets/parts_processed/manifest.json";
constexpr std::string_view kAssetRoot    = "assets/parts_processed";

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

auto parse_screenshot(int argc, char** argv) -> std::optional<std::string> {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--screenshot" && i + 1 < argc) {
            return std::string{argv[++i]};
        }
    }
    return std::nullopt;
}

// 加载 manifest + atlas。
// 任何一步失败都返回 nullopt，让 caller 退回 dot 渲染（不黑屏）。
// 注意：assembler 必须持有 atlas 的引用/指针，所以 assembler 单独放 main 里
// 跟 atlas 同寿命。SpriteAssets 合包方案会让 std::optional 重新分配时把
// atlas 移到新位置，assembler 的旧引用失效 → 段错误。
auto try_load_atlas(SDL_Renderer* renderer)
    -> std::optional<std::pair<game::PartManifest, game::PartAtlas>> {
    auto m = game::load_manifest(kManifestPath);
    if (!m) {
        std::cerr << std::format(
            "[sprite] manifest load failed: {} — falling back to dot rendering\n",
            m.error());
        return std::nullopt;
    }
    auto atlas = game::PartAtlas::load(renderer, *m, kAssetRoot);
    if (!atlas) {
        std::cerr << std::format(
            "[sprite] atlas load failed: {} — falling back to dot rendering\n",
            atlas.error());
        return std::nullopt;
    }
    std::size_t part_count = 0;
    for (const auto& c : m->categories) part_count += c.parts.size();
    std::cerr << std::format(
        "[sprite] loaded {} categories, {} total parts\n",
        m->categories.size(), part_count);
    return std::pair{std::move(*m), std::move(*atlas)};
}

}  // namespace

auto main(int argc, char** argv) -> int {
    SetConsoleOutputCP(CP_UTF8);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << std::format("SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }
    // SDL3_image v3 没有 IMG_Init：格式支持靠 vcpkg feature 编译时启用。

    // 大地图：4x 面积。max_ticks_per_frame=8 让 sim 不至于掉帧太狠。
    constexpr std::size_t kMapW = 160;
    constexpr std::size_t kMapH = 120;

    const std::uint64_t seed = parse_seed(argc, argv);
    const auto screenshot_path = parse_screenshot(argc, argv);
    game::World world = game::WorldGenerator(kMapW, kMapH).generate(seed);
    game::Simulation sim(std::move(world), seed, game::Simulation::Params{});

    auto renderer = game::TileRenderer::create(kMapW, kMapH, "World Will — Time-Flow");
    if (!renderer) {
        std::cerr << std::format("TileRenderer::create failed: {}\n", renderer.error());
        SDL_Quit();
        return 1;
    }

    // atlas 放 main 的栈上，地址稳定；assembler 在它之后构造，
    // 持有 atlas 引用，整个 main 生命周期有效。
    auto atlas_pair = try_load_atlas(renderer->raw_renderer());
    std::unique_ptr<game::CreatureAssembler> assembler;
    if (atlas_pair) {
        assembler = std::make_unique<game::CreatureAssembler>(atlas_pair->second);
        renderer->set_assembler(assembler.get());
    }

    // 相机：默认居中，初始 px_per_tile=8（160×120 地图在 1280×960 里差不多填满，
    // 留点空间看 HUD）
    game::Camera camera{
        .tile_cx      = static_cast<float>(kMapW) / 2.0f,
        .tile_cy      = static_cast<float>(kMapH) / 2.0f,
        .px_per_tile  = 8,
    };
    auto reset_camera = [&]{
        camera.tile_cx     = static_cast<float>(kMapW) / 2.0f;
        camera.tile_cy     = static_cast<float>(kMapH) / 2.0f;
        camera.px_per_tile = 8;
    };

    // 把相机限制在地图范围内（地图比视口小时居中；缩放过大时也夹住）
    auto clamp_camera = [&]{
        const int p = std::max(1, camera.px_per_tile);
        const float vis_w = static_cast<float>(renderer->window_w()) / p;
        const float vis_h = static_cast<float>(renderer->window_h()) / p;
        const float min_cx = vis_w * 0.5f;
        const float max_cx = static_cast<float>(kMapW) - vis_w * 0.5f;
        const float min_cy = vis_h * 0.5f;
        const float max_cy = static_cast<float>(kMapH) - vis_h * 0.5f;
        if (min_cx > max_cx) {
            camera.tile_cx = static_cast<float>(kMapW) / 2.0f;
        } else {
            camera.tile_cx = std::clamp(camera.tile_cx, min_cx, max_cx);
        }
        if (min_cy > max_cy) {
            camera.tile_cy = static_cast<float>(kMapH) / 2.0f;
        } else {
            camera.tile_cy = std::clamp(camera.tile_cy, min_cy, max_cy);
        }
    };

    // 鼠标位置 → 世界 tile 坐标
    auto screen_to_tile = [&](int sx, int sy) -> std::pair<float, float> {
        const float win_cx = renderer->window_w() * 0.5f;
        const float win_cy = renderer->window_h() * 0.5f;
        const int p = std::max(1, camera.px_per_tile);
        return {
            camera.tile_cx + (static_cast<float>(sx) - win_cx) / p,
            camera.tile_cy + (static_cast<float>(sy) - win_cy) / p,
        };
    };

    // 以鼠标位置为中心缩放
    auto zoom_at_mouse = [&](int mx, int my, int wheel_y, bool fast) {
        const float step = fast ? 1.5f : 1.2f;
        const float factor = (wheel_y > 0) ? step : (1.0f / step);
        const int new_p = std::clamp(
            static_cast<int>(static_cast<float>(camera.px_per_tile) * factor),
            2, 64);
        if (new_p == camera.px_per_tile) return;

        const auto [old_tx, old_ty] = screen_to_tile(mx, my);
        camera.px_per_tile = new_p;
        // 反推：缩放后让 old_tx/ty 仍落在 (mx, my)
        const auto [new_tx, new_ty] = screen_to_tile(mx, my);
        camera.tile_cx += old_tx - new_tx;
        camera.tile_cy += old_ty - new_ty;
    };

    bool running = true;
    bool paused = false;
    float speed = 1.0f;
    Uint64 last = SDL_GetTicks();
    int frame_count = 0;

    // LMB 拖动 pan
    bool dragging = false;
    int last_mouse_x = 0;
    int last_mouse_y = 0;

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
                        case SDLK_R:
                            reset_camera();
                            break;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = true;
                        last_mouse_x = event.button.x;
                        last_mouse_y = event.button.y;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = false;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (dragging) {
                        const int dx = event.motion.x - last_mouse_x;
                        const int dy = event.motion.y - last_mouse_y;
                        last_mouse_x = event.motion.x;
                        last_mouse_y = event.motion.y;
                        const int p = std::max(1, camera.px_per_tile);
                        camera.tile_cx -= static_cast<float>(dx) / p;
                        camera.tile_cy -= static_cast<float>(dy) / p;
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    float mx = 0.0f, my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    const SDL_Keymod mods = SDL_GetModState();
                    const bool fast = (mods & SDL_KMOD_SHIFT) != 0;
                    zoom_at_mouse(static_cast<int>(mx), static_cast<int>(my),
                                  event.wheel.y, fast);
                    break;
                }
            }
        }

        // WASD 平移（连续，每帧按持有时间位移）
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const int p = std::max(1, camera.px_per_tile);
            // 速度：每帧 0.5 个 tile，按 zoom 缩放（zoom 越大，1 tile 越大，要走得更快才跟得上视觉）
            float pan = 0.5f * static_cast<float>(p) / 8.0f;
            // shift 加持 = 2x
            if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) pan *= 2.0f;
            Uint64 now2 = SDL_GetTicks();
            float dt = static_cast<float>(now2 - last) / 1000.0f;
            if (dt > 0.25f) dt = 0.25f;
            pan *= 60.0f * dt;  // 归一化到 60fps
            if (keys[SDL_SCANCODE_W]) camera.tile_cy -= pan;
            if (keys[SDL_SCANCODE_S]) camera.tile_cy += pan;
            if (keys[SDL_SCANCODE_A]) camera.tile_cx -= pan;
            if (keys[SDL_SCANCODE_D]) camera.tile_cx += pan;
        }

        Uint64 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.25f) dt = 0.25f;

        sim.advance(paused ? 0.0f : dt, speed);

        clamp_camera();
        renderer->set_camera(camera);
        renderer->render(sim.world());
        renderer->render_creatures(sim.creatures());
        renderer->render_minimap(camera);
        renderer->render_hud(sim.tick_count(), sim.creatures().size(),
                             sim.world_energy(), paused, speed);
        SDL_RenderPresent(renderer->raw_renderer());

        // --screenshot 模式：跑 60 帧后截图退出
        if (screenshot_path && frame_count >= 60) {
            SDL_Surface* surf = SDL_RenderReadPixels(
                renderer->raw_renderer(), nullptr);
            if (surf != nullptr) {
                if (SDL_SavePNG(surf, screenshot_path->c_str())) {
                    std::cerr << std::format("saved screenshot: {}\n", *screenshot_path);
                } else {
                    std::cerr << std::format("SDL_SavePNG failed: {}\n", SDL_GetError());
                }
                SDL_DestroySurface(surf);
            } else {
                std::cerr << std::format("SDL_RenderReadPixels failed: {}\n", SDL_GetError());
            }
            running = false;
            continue;
        }
        frame_count++;

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

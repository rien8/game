#pragma once

#include "world/world.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/creature.hpp"
#include "render/creature_assembler.hpp"

namespace game {

// 平移 + 缩放状态。所有字段在屏幕坐标系里都是世界 tile 单位的浮点数。
// tile_cx/tile_cy 是屏幕中心对应的世界 tile 坐标（不是左上角）。
// px_per_tile 同时也是 zoom level（值大 = 放大）。
struct Camera {
    float tile_cx = 0.0f;
    float tile_cy = 0.0f;
    float px_per_tile = 16.0f;
};

// SDL3 tile 地图渲染器。只封装 window/renderer/texture；
// SDL 全局生命周期（SDL_Init/SDL_Quit）由调用方管理。
class TileRenderer {
public:
    [[nodiscard]] static auto create(std::size_t tile_width,
                                     std::size_t tile_height,
                                     std::string_view title)
        -> std::expected<TileRenderer, std::string>;

    TileRenderer(TileRenderer&&) noexcept = default;
    auto operator=(TileRenderer&&) noexcept -> TileRenderer& = default;
    TileRenderer(const TileRenderer&) = delete;
    auto operator=(const TileRenderer&) -> TileRenderer& = delete;
    ~TileRenderer() = default;

    void render(const World& world);
    void render_creatures(std::span<const Creature> creatures);

    // 右下角小地图：显示整个世界 + 当前视口框。
    // minimap 纹理首次调用时按 1 像素 = 1 tile 构建并缓存（世界不变就只建一次）。
    void render_minimap(const Camera& cam);

    // 注入生物合成器；之后 render_creatures 用 sprite 替代 dot。
    // 传 nullptr 切回 dot 模式（兜底，资源缺失时不黑屏）。
    void set_assembler(const CreatureAssembler* a) noexcept { assembler_ = a; }
    [[nodiscard]] auto assembler() const noexcept -> const CreatureAssembler* { return assembler_; }

    // 相机：每帧 set 一次，render()/render_creatures() 都会用。
    void set_camera(const Camera& cam) noexcept { camera_ = cam; }
    [[nodiscard]] auto camera() const noexcept -> const Camera& { return camera_; }

    // 窗口像素尺寸（render target）。
    [[nodiscard]] auto window_w() const noexcept -> int { return window_w_; }
    [[nodiscard]] auto window_h() const noexcept -> int { return window_h_; }

    void render_hud(std::uint64_t tick, std::size_t pop, float energy,
                    bool paused, float speed);

    [[nodiscard]] auto raw_renderer() noexcept -> SDL_Renderer* { return renderer_.get(); }

    [[nodiscard]] auto window() noexcept -> SDL_Window* { return window_.get(); }

private:
    struct WindowDeleter {
        void operator()(SDL_Window* p) const noexcept { SDL_DestroyWindow(p); }
    };
    struct RendererDeleter {
        void operator()(SDL_Renderer* p) const noexcept { SDL_DestroyRenderer(p); }
    };
    struct TextureDeleter {
        void operator()(SDL_Texture* p) const noexcept { SDL_DestroyTexture(p); }
    };

    TileRenderer(std::unique_ptr<SDL_Window, WindowDeleter> window,
                 std::unique_ptr<SDL_Renderer, RendererDeleter> renderer,
                 std::unique_ptr<SDL_Texture, TextureDeleter> texture,
                 std::size_t width,
                 std::size_t height,
                 int window_w,
                 int window_h);

    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    std::unique_ptr<SDL_Texture, TextureDeleter> minimap_texture_;
    std::size_t width_;
    std::size_t height_;
    int window_w_;
    int window_h_;
    std::vector<std::uint32_t> pixels_;
    const CreatureAssembler* assembler_ = nullptr;
    Camera camera_{};
};

}  // namespace game

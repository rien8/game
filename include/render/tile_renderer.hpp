#pragma once

#include "world/world.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game {

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
                 std::size_t height);

    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    std::size_t width_;
    std::size_t height_;
    std::vector<std::uint32_t> pixels_;
};

}  // namespace game

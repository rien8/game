#include "render/tile_renderer.hpp"

#include <format>

namespace game {

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 960;

// 打包 RGBA 到 uint32，匹配 SDL_PIXELFORMAT_RGBA32（小端内存布局 R,G,B,A）。
constexpr auto pack_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) -> std::uint32_t {
    return (static_cast<std::uint32_t>(0xFF) << 24) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(r);
}

auto biome_color(Biome biome) -> std::uint32_t {
    switch (biome) {
        case Biome::DeepOcean:  return pack_rgba(25, 55, 115);
        case Biome::Ocean:      return pack_rgba(35, 75, 145);
        case Biome::Beach:      return pack_rgba(210, 190, 130);
        case Biome::Grassland:  return pack_rgba(120, 165, 70);
        case Biome::Forest:     return pack_rgba(45, 110, 50);
        case Biome::Rainforest: return pack_rgba(25, 90, 45);
        case Biome::Desert:     return pack_rgba(215, 185, 110);
        case Biome::Swamp:      return pack_rgba(65, 90, 55);
        case Biome::Tundra:     return pack_rgba(165, 180, 170);
        case Biome::Mountain:   return pack_rgba(130, 125, 120);
        case Biome::Snowcap:    return pack_rgba(235, 240, 245);
    }
    return pack_rgba(0, 0, 0);
}

}  // namespace

auto TileRenderer::create(std::size_t tile_width,
                          std::size_t tile_height,
                          std::string_view title)
    -> std::expected<TileRenderer, std::string> {
    SDL_Window* window = SDL_CreateWindow(
        std::string{title}.c_str(), kWindowWidth, kWindowHeight, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        return std::unexpected(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        return std::unexpected(std::format("SDL_CreateRenderer failed: {}", SDL_GetError()));
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(tile_width), static_cast<int>(tile_height));
    if (texture == nullptr) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return std::unexpected(std::format("SDL_CreateTexture failed: {}", SDL_GetError()));
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    return TileRenderer{
        std::unique_ptr<SDL_Window, WindowDeleter>{window},
        std::unique_ptr<SDL_Renderer, RendererDeleter>{renderer},
        std::unique_ptr<SDL_Texture, TextureDeleter>{texture},
        tile_width,
        tile_height,
    };
}

TileRenderer::TileRenderer(std::unique_ptr<SDL_Window, WindowDeleter> window,
                           std::unique_ptr<SDL_Renderer, RendererDeleter> renderer,
                           std::unique_ptr<SDL_Texture, TextureDeleter> texture,
                           std::size_t width,
                           std::size_t height)
    : window_(std::move(window)),
      renderer_(std::move(renderer)),
      texture_(std::move(texture)),
      width_(width),
      height_(height),
      pixels_(width * height) {}

void TileRenderer::render(const World& world) {
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            pixels_[y * width_ + x] = biome_color(world.at(x, y).biome);
        }
    }

    SDL_UpdateTexture(texture_.get(), nullptr, pixels_.data(),
                      static_cast<int>(width_ * sizeof(std::uint32_t)));
    SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr);
    SDL_RenderPresent(renderer_.get());
}

}  // namespace game

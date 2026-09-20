#include "render/tile_renderer.hpp"

#include <bit>
#include <format>
#include <span>

#include "core/creature.hpp"

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

    // 用 tile 坐标系作为逻辑呈现尺寸；SDL 自动拉伸到窗口。
    // 这样 render_creatures 用 tile 坐标直接画点，不用自己换算缩放。
    SDL_SetRenderLogicalPresentation(renderer,
        static_cast<int>(tile_width), static_cast<int>(tile_height),
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

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
    SDL_RenderClear(renderer_.get());  // 清 back buffer，让 logical 背景变黑
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            pixels_[y * width_ + x] = biome_color(world.at(x, y).biome);
        }
    }

    SDL_UpdateTexture(texture_.get(), nullptr, pixels_.data(),
                      static_cast<int>(width_ * sizeof(std::uint32_t)));
    SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr);
    // 不在这里 SDL_RenderPresent — main.cpp 在画完生物 + HUD 后统一 present。
}

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
            // SDL_RenderDebugText 只支持 ASCII；用单个 'B' 标识 boss 而非多字节中文。
            SDL_RenderDebugTextFormat(r,
                static_cast<float>(cx + 3),
                static_cast<float>(cy - 6),
                "B");
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

}  // namespace game

#include "render/tile_renderer.hpp"

#include <bit>
#include <format>
#include <optional>
#include <span>

#include "core/creature.hpp"
#include "render/creature_assembler.hpp"

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
                       std::int32_t size, std::uint32_t fill,
                       std::uint32_t border) -> void {
    SDL_SetRenderDrawColor(r,
        (fill >> 0) & 0xFFu, (fill >> 8) & 0xFFu, (fill >> 16) & 0xFFu, 0xFFu);
    const std::int32_t half = size / 2;
    SDL_FRect fill_rect{
        static_cast<float>(cx - half),
        static_cast<float>(cy - half),
        static_cast<float>(size),
        static_cast<float>(size)
    };
    SDL_RenderFillRect(r, &fill_rect);
    if (border != 0) {
        SDL_SetRenderDrawColor(r,
            (border >> 0) & 0xFFu, (border >> 8) & 0xFFu, (border >> 16) & 0xFFu, 0xFFu);
        SDL_FRect b{
            static_cast<float>(cx - half - 1),
            static_cast<float>(cy - half - 1),
            static_cast<float>(size + 2),
            static_cast<float>(size + 2)
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

    // 不开 logical presentation：直接按窗口像素渲染，下一个 tick=窗口像素 / tile 像素。
    // 这样 render_creatures 用 c.pos.x * kPxPerTile 算屏幕坐标。

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
        kWindowWidth,
        kWindowHeight,
    };
}

TileRenderer::TileRenderer(std::unique_ptr<SDL_Window, WindowDeleter> window,
                           std::unique_ptr<SDL_Renderer, RendererDeleter> renderer,
                           std::unique_ptr<SDL_Texture, TextureDeleter> texture,
                           std::size_t width,
                           std::size_t height,
                           int window_w,
                           int window_h)
    : window_(std::move(window)),
      renderer_(std::move(renderer)),
      texture_(std::move(texture)),
      width_(width),
      height_(height),
      window_w_(window_w),
      window_h_(window_h),
      pixels_(width * height) {}

void TileRenderer::render(const World& world) {
    SDL_RenderClear(renderer_.get());
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            pixels_[y * width_ + x] = biome_color(world.at(x, y).biome);
        }
    }
    SDL_UpdateTexture(texture_.get(), nullptr, pixels_.data(),
                      static_cast<int>(width_ * sizeof(std::uint32_t)));

    // 视口 = 屏幕中心 ± 半个可见 tile 数。源 / 目标 rect 按相机缩放。
    // 注意 px_per_tile = 0 是未初始化状态，兜底为 1 防除零。
    const int p = std::max(1, camera_.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / static_cast<float>(p);
    const float vis_h = static_cast<float>(window_h_) / static_cast<float>(p);
    const SDL_FRect src{
        camera_.tile_cx - vis_w * 0.5f,
        camera_.tile_cy - vis_h * 0.5f,
        vis_w,
        vis_h,
    };
    const SDL_FRect dst{0.0f, 0.0f,
                        static_cast<float>(window_w_),
                        static_cast<float>(window_h_)};
    SDL_RenderTexture(renderer_.get(), texture_.get(), &src, &dst);
    // 不在这里 SDL_RenderPresent — main.cpp 在画完生物 + HUD 后统一 present。
}

void TileRenderer::render_creatures(std::span<const Creature> creatures) {
    constexpr std::uint32_t kEliteBorder = (0xFFu << 0) | (0xE0u << 8) | (0x00u << 16);  // 黄
    constexpr std::uint32_t kBossBorder  = (0x40u << 0) | (0x40u << 8) | (0xFFu << 16);  // 红
    auto* r = renderer_.get();

    // 相机：屏幕中心对应的世界 tile + 像素每 tile（缩放）
    const int p = std::max(1, camera_.px_per_tile);
    const float cam_cx = camera_.tile_cx;
    const float cam_cy = camera_.tile_cy;
    const int win_cx = window_w_ / 2;
    const int win_cy = window_h_ / 2;

    // sprite 设计画布 → 窗口像素：tile px_per_tile 越大 sprite 也越大
    constexpr int kSpriteCanvasPxBase = 64;
    const int kSpriteCanvasPx = std::max(8, kSpriteCanvasPxBase * p / 16);

    for (const auto& c : creatures) {
        if (c.dead) continue;

        // tile 中心 → 屏幕像素
        const std::int32_t cx = static_cast<std::int32_t>(
            (static_cast<float>(c.pos.x) + 0.5f - cam_cx) * static_cast<float>(p))
            + win_cx;
        const std::int32_t cy = static_cast<std::int32_t>(
            (static_cast<float>(c.pos.y) + 0.5f - cam_cy) * static_cast<float>(p))
            + win_cy;

        if (assembler_ != nullptr) {
            std::optional<SDL_Color> tint;
            if (c.is_boss) {
                tint = SDL_Color{0xFFu, 0x60u, 0x60u, 0xFFu};
            } else if (c.is_elite) {
                tint = SDL_Color{0xFFu, 0xE0u, 0x60u, 0xFFu};
            }
            const AppearanceGene gene = derive_appearance(c.gene);
            assembler_->render(r, gene, cx, cy, kSpriteCanvasPx, tint);

            if (c.is_boss && !c.name.empty()) {
                SDL_RenderDebugText(r,
                    static_cast<float>(cx + kSpriteCanvasPx / 2 + 2),
                    static_cast<float>(cy - kSpriteCanvasPx / 2 - 2),
                    "B");
            }
            continue;
        }

        // dot 兜底模式
        std::int32_t size = std::max(2, p / 4);
        std::uint32_t border = 0;
        if (c.is_elite) { size = std::max(2, p / 2); border = kEliteBorder; }
        if (c.is_boss)  { size = std::max(2, p * 3 / 4); border = kBossBorder; }
        draw_creature_dot(r, cx, cy, size, gene_color(c.gene), border);
        if (c.is_boss && !c.name.empty()) {
            SDL_RenderDebugText(r,
                static_cast<float>(cx + 6),
                static_cast<float>(cy - p / 2 - 2),
                "B");
        }
    }
}

void TileRenderer::render_minimap(const Camera& cam) {
    // 懒构建：第一次调用时按 world tile = 1 pixel 烧一张小地图纹理。
    // 世界静态 → 只构建一次。
    if (minimap_texture_ == nullptr) {
        SDL_Texture* mm = SDL_CreateTexture(
            renderer_.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            static_cast<int>(width_), static_cast<int>(height_));
        if (mm == nullptr) return;
        SDL_SetTextureScaleMode(mm, SDL_SCALEMODE_NEAREST);

        std::vector<std::uint32_t> mm_pixels(width_ * height_);
        mm_pixels = pixels_;
        SDL_UpdateTexture(mm, nullptr, mm_pixels.data(),
                          static_cast<int>(width_ * sizeof(std::uint32_t)));
        minimap_texture_.reset(mm);
    }

    // minimap 放在右下角 8px 边距，宽 200px，按世界长宽比缩高
    constexpr int kMmW = 200;
    const int kMmH = static_cast<int>(kMmW * static_cast<int>(height_) / static_cast<int>(width_));
    const int mm_x = window_w_ - kMmW - 8;
    const int mm_y = window_h_ - kMmH - 8;

    // 缩放绘制 minimap 纹理
    const SDL_FRect mm_dst{
        static_cast<float>(mm_x), static_cast<float>(mm_y),
        static_cast<float>(kMmW), static_cast<float>(kMmH)
    };
    SDL_RenderTexture(renderer_.get(), minimap_texture_.get(), nullptr, &mm_dst);

    // 白色边框 — 用 4 条 line 而不是 RenderRect，避开 SDL3 浮点 rect 右边/下边偶发漏画的 bug
    SDL_SetRenderDrawColor(renderer_.get(), 0xFFu, 0xFFu, 0xFFu, 0xFFu);
    const float fx = static_cast<float>(mm_x);
    const float fy = static_cast<float>(mm_y);
    const float fw = static_cast<float>(kMmW);
    const float fh = static_cast<float>(kMmH);
    SDL_RenderLine(renderer_.get(), fx,             fy,             fx + fw,     fy          );
    SDL_RenderLine(renderer_.get(), fx,             fy + fh - 1.0f, fx + fw,     fy + fh - 1.0f);
    SDL_RenderLine(renderer_.get(), fx,             fy,             fx,          fy + fh - 1.0f);
    SDL_RenderLine(renderer_.get(), fx + fw - 1.0f, fy,             fx + fw - 1.0f, fy + fh - 1.0f);

    // 当前视口在 minimap 上的矩形
    const int p = std::max(1, cam.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / static_cast<float>(p);
    const float vis_h = static_cast<float>(window_h_) / static_cast<float>(p);
    // 如果视口覆盖 >= 95% 的地图，画一个内框也看不出区别 — 跳过
    if (vis_w >= static_cast<float>(width_) * 0.95f &&
        vis_h >= static_cast<float>(height_) * 0.95f) {
        return;
    }
    const float sx = static_cast<float>(kMmW) / static_cast<float>(width_);
    const float sy = static_cast<float>(kMmH) / static_cast<float>(height_);
    const float vx = (cam.tile_cx - vis_w * 0.5f) * sx;
    const float vy = (cam.tile_cy - vis_h * 0.5f) * sy;
    const float vw = vis_w * sx;
    const float vh = vis_h * sy;
    const SDL_FRect vp{
        static_cast<float>(mm_x) + vx,
        static_cast<float>(mm_y) + vy,
        vw, vh
    };
    // 用亮黄边框更显眼
    SDL_SetRenderDrawColor(renderer_.get(), 0xFFu, 0xE0u, 0x40u, 0xFFu);
    SDL_RenderRect(renderer_.get(), &vp);
}

void TileRenderer::render_hud(std::uint64_t tick, std::size_t pop, float energy,
                              bool paused, float speed) {
    auto* r = renderer_.get();
    // HUD 用窗口像素坐标（与 tile 系统无关）
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0xC0u);  // 半透明黑底
    SDL_FRect bg{8.0f, 8.0f, 280.0f, 56.0f};
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 220, 220, 220, 0xFFu);
    SDL_RenderDebugTextFormat(r, 14.0f, 12.0f,
        "tick=%llu pop=%zu E=%.0f %s x%.1f",
        static_cast<unsigned long long>(tick), pop, energy,
        paused ? "PAUSE" : "RUN", speed);
}

}  // namespace game

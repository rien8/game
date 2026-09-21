#include "render/creature_assembler.hpp"

#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <fstream>
#include <utility>

namespace game {

namespace {

// nlohmann/json 没有 enum 的 from_json；手写转换。
auto parse_attach_side(const std::string& s) -> std::optional<AttachSide> {
    if (s == "top")    return AttachSide::Top;
    if (s == "bottom") return AttachSide::Bottom;
    if (s == "left")   return AttachSide::Left;
    if (s == "right")  return AttachSide::Right;
    return std::nullopt;
}

auto require_int(const nlohmann::json& j, std::string_view key) -> int {
    return j.at(key).get<int>();
}

auto require_array4(const nlohmann::json& j, std::string_view key)
    -> std::array<int, 4> {
    const auto& a = j.at(key);
    if (!a.is_array() || a.size() != 4) {
        throw std::runtime_error(std::format("'{}' must be a 4-element array", key));
    }
    return {a[0].get<int>(), a[1].get<int>(), a[2].get<int>(), a[3].get<int>()};
}

auto optional_array2(const nlohmann::json& j, std::string_view key)
    -> std::optional<std::array<int, 2>> {
    if (!j.contains(key)) return std::nullopt;
    const auto& a = j.at(key);
    if (!a.is_array() || a.size() != 2) {
        throw std::runtime_error(std::format("'{}' must be a 2-element array", key));
    }
    return std::array{a[0].get<int>(), a[1].get<int>()};
}

auto category_from_json(const nlohmann::json& j, std::string_view name) -> Category {
    Category c;
    c.name = std::string{name};
    c.z_order = require_int(j, "zOrder");
    c.parts.reserve(j.at("parts").size());
    for (const auto& p : j.at("parts")) {
        const auto bb = require_array4(p, "bbox");
        c.parts.push_back(PartEntry{
            .id   = p.at("id").get<std::string>(),
            .file = p.at("file").get<std::string>(),
            .bbox = SDL_Rect{bb[0], bb[1], bb[2], bb[3]},
        });
    }
    if (j.contains("attachSide")) {
        c.attach_side = parse_attach_side(j.at("attachSide").get<std::string>());
    }
    if (auto p = optional_array2(j, "attachPoint")) {
        c.attach_point = AttachPoint{(*p)[0], (*p)[1]};
    }
    if (auto p = optional_array2(j, "attachFront")) {
        c.attach_front = AttachPoint{(*p)[0], (*p)[1]};
    }
    if (auto p = optional_array2(j, "attachBack")) {
        c.attach_back = AttachPoint{(*p)[0], (*p)[1]};
    }
    return c;
}

}  // namespace

// ---------- Manifest loader --------------------------------------------------

auto load_manifest(std::string_view path) -> std::expected<PartManifest, std::string> {
    std::ifstream f{std::string{path}};
    if (!f) {
        return std::unexpected(std::format("cannot open manifest: {}", path));
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        return std::unexpected(std::format("manifest JSON parse error: {}", e.what()));
    }

    PartManifest m;
    try {
        m.canvas_size     = require_int(j, "canvasSize");
        m.white_threshold = require_int(j, "whiteThresh");
        const auto& cats  = j.at("categories");
        m.categories.reserve(cats.size());
        for (auto it = cats.begin(); it != cats.end(); ++it) {
            m.categories.push_back(category_from_json(it.value(), it.key()));
        }
    } catch (const std::exception& e) {
        return std::unexpected(std::format("manifest schema error: {}", e.what()));
    }

    return m;
}

// ---------- PartAtlas --------------------------------------------------------

PartAtlas::PartAtlas(PartAtlas&& other) noexcept
    : by_category_(std::move(other.by_category_)),
      category_meta_(std::move(other.category_meta_)),
      renderer_(other.renderer_) {
    other.renderer_ = nullptr;
}

auto PartAtlas::operator=(PartAtlas&& other) noexcept -> PartAtlas& {
    if (this != &other) {
        destroy_textures();
        by_category_   = std::move(other.by_category_);
        category_meta_ = std::move(other.category_meta_);
        renderer_      = other.renderer_;
        other.renderer_ = nullptr;
    }
    return *this;
}

PartAtlas::~PartAtlas() {
    destroy_textures();
}

void PartAtlas::destroy_textures() noexcept {
    for (auto& [name, entries] : by_category_) {
        for (auto& e : entries) {
            if (e.texture != nullptr) {
                SDL_DestroyTexture(e.texture);
                e.texture = nullptr;
            }
        }
    }
}

auto PartAtlas::load(SDL_Renderer* renderer,
                      const PartManifest& manifest,
                      std::string_view asset_root)
    -> std::expected<PartAtlas, std::string> {
    if (renderer == nullptr) {
        return std::unexpected("PartAtlas: null renderer");
    }

    PartAtlas atlas;
    atlas.renderer_ = renderer;

    for (const auto& cat : manifest.categories) {
        std::vector<Entry> entries;
        entries.reserve(cat.parts.size());
        for (const auto& p : cat.parts) {
            const std::string full_path = std::string{asset_root} + "/" + p.file;
            SDL_Texture* tex = IMG_LoadTexture(renderer, full_path.c_str());
            if (tex == nullptr) {
                // 失败时清理已加载的
                atlas.destroy_textures();
                return std::unexpected(std::format(
                    "IMG_LoadTexture failed for {}: {}", full_path, SDL_GetError()));
            }
            // PNG 已是 alpha，预乘交给 SDL（与我们的白→透明流程一致）。
            // 用 LINEAR：部件图通常会从 256×256 缩到 ~64×64，LINEAR 比 NEAREST 平滑。
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            entries.push_back(Entry{
                .id      = p.id,
                .texture = tex,
                .bbox    = p.bbox,
            });
        }
        atlas.category_meta_.emplace(cat.name, &cat);
        atlas.by_category_.emplace(cat.name, std::move(entries));
    }

    return atlas;
}

auto PartAtlas::category(std::string_view name) const -> const Category* {
    auto it = category_meta_.find(std::string{name});
    if (it == category_meta_.end()) return nullptr;
    return it->second;
}

auto PartAtlas::lookup(std::string_view category, std::string_view id) const
    -> std::optional<std::pair<SDL_Texture*, SDL_Rect>> {
    auto cit = by_category_.find(std::string{category});
    if (cit == by_category_.end()) return std::nullopt;
    for (const auto& e : cit->second) {
        if (e.id == id) return std::pair{e.texture, e.bbox};
    }
    return std::nullopt;
}

auto PartAtlas::pick(std::string_view category, std::uint32_t index) const
    -> std::optional<std::pair<SDL_Texture*, SDL_Rect>> {
    auto cit = by_category_.find(std::string{category});
    if (cit == by_category_.end() || cit->second.empty()) return std::nullopt;
    const auto& entries = cit->second;
    const auto& e = entries[index % entries.size()];
    return std::pair{e.texture, e.bbox};
}

// ---------- 外观索引派生 ----------------------------------------------------

// FNV-1a 哈希 Traits 的 float bits → 4 字节。
// 与 tile_renderer.cpp 里的 gene_color 同一套，保证"同基因 → 同外观 + 同 dot 色"。
namespace {
auto hash_traits(const Traits& t) -> std::uint32_t {
    std::uint32_t h = 0x811C9DC5u;
    for (const float v : t.values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
        h = (h ^ bits) * 0x01000193u;
    }
    return h;
}

// HSV → RGB。h ∈ [0, 360), s, v ∈ [0, 1]。alpha 固定 255。
auto hsv_to_rgb(float h, float s, float v) -> SDL_Color {
    const float c = v * s;
    const float hp = std::fmod(h, 360.0f);
    const float x = c * (1.0f - std::fabs(std::fmod(hp / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if      (hp <  60.0f) { r = c; g = x; b = 0; }
    else if (hp < 120.0f) { r = x; g = c; b = 0; }
    else if (hp < 180.0f) { r = 0; g = c; b = x; }
    else if (hp < 240.0f) { r = 0; g = x; b = c; }
    else if (hp < 300.0f) { r = x; g = 0; b = c; }
    else                  { r = c; g = 0; b = x; }
    return SDL_Color{
        static_cast<std::uint8_t>((r + m) * 255.0f),
        static_cast<std::uint8_t>((g + m) * 255.0f),
        static_cast<std::uint8_t>((b + m) * 255.0f),
        255,
    };
}

// 把 AppearanceGene 的 HSL 参数变成乘性 tint（SDL_SetTextureColorMod 用）。
// 默认 tint = (255,255,255) 表示不染色。
auto compute_tint(const AppearanceGene& g) -> SDL_Color {
    if (g.hue_offset == 0 && g.saturation_mod == 0 && g.value_mod == 0) {
        return SDL_Color{255, 255, 255, 255};
    }
    // 把 saturation/value 的 [-100, +100] 调整映射到 [0, 1] 区间
    // s = 0 表示完全去饱和（灰），s = +100 表示原色饱和度
    const float sat =
        std::clamp(1.0f + static_cast<float>(g.saturation_mod) / 100.0f, 0.0f, 1.0f);
    const float val =
        std::clamp(1.0f + static_cast<float>(g.value_mod) / 100.0f, 0.0f, 1.0f);
    return hsv_to_rgb(static_cast<float>(g.hue_offset), sat, val);
}
}  // namespace

auto derive_appearance(const Traits& gene) -> AppearanceGene {
    const std::uint32_t h = hash_traits(gene);
    return AppearanceGene{
        .head_index       = (h >> 0)  % 12,
        .torso_index      = (h >> 4)  % 12,
        .forelimb_index   = (h >> 8)  % 12,
        .hindlimb_index   = (h >> 12) % 12,
        .tail_index       = (h >> 16) % 12,
        // HSL：8 bit hue + 4 bit sat + 4 bit val 各占一段 hash 位
        .hue_offset       = static_cast<std::uint32_t>(((h >> 20) & 0xFFu) * 360u / 256u),
        .saturation_mod   = static_cast<std::int32_t>(((h >> 28) & 0x0Fu) * 200 / 15 - 100),
        .value_mod        = static_cast<std::int32_t>(((h >> 24) & 0x0Fu) * 200 / 15 - 100),
    };
}

// ---------- CreatureAssembler -----------------------------------------------

CreatureAssembler::CreatureAssembler(const PartAtlas& atlas) noexcept
    : atlas_{atlas} {}

namespace {

// 计算子部件的 dst（世界像素），给定父 anchor 的世界坐标 + 子 attach + 缩放。
// 设计画布是 256×256，实际按 target_canvas_size 缩放。
auto place_child(const SDL_Rect& child_bbox,
                 const AttachPoint& child_attach,
                 int parent_world_anchor_x,
                 int parent_world_anchor_y,
                 float scale) -> SDL_FRect {
    const float child_dst_x = static_cast<float>(parent_world_anchor_x)
                            - static_cast<float>(child_attach.x) * scale;
    const float child_dst_y = static_cast<float>(parent_world_anchor_y)
                            - static_cast<float>(child_attach.y) * scale;
    return SDL_FRect{
        child_dst_x,
        child_dst_y,
        static_cast<float>(child_bbox.w) * scale,
        static_cast<float>(child_bbox.h) * scale,
    };
}

auto scale_point(const AttachPoint& p, float scale) -> SDL_FPoint {
    return SDL_FPoint{
        static_cast<float>(p.x) * scale,
        static_cast<float>(p.y) * scale,
    };
}

// 缩放后的父 anchor 世界坐标 = 父 bbox 左上角（世界像素）+ 父 anchor（缩放后）。
auto world_anchor(const SDL_FRect& parent_dst,
                  const AttachPoint& parent_anchor,
                  float scale) -> SDL_FPoint {
    const auto a = scale_point(parent_anchor, scale);
    return SDL_FPoint{
        parent_dst.x + a.x,
        parent_dst.y + a.y,
    };
}

// 画单个部件，可选染色。染色用完即还原，避免污染后续部件。
auto blit_with_tint(SDL_Renderer* r, SDL_Texture* tex,
                    const SDL_FRect& dst,
                    const std::optional<SDL_Color>& tint) -> void {
    if (tint) {
        SDL_SetTextureColorMod(tex, tint->r, tint->g, tint->b);
    }
    SDL_RenderTexture(r, tex, nullptr, &dst);
    if (tint) {
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }
}

// 画单肢（按 attach 点拼）。caller 控制画在 torso 之前还是之后。
auto draw_limb(SDL_Renderer* r, const PartAtlas& atlas,
               const char* category, std::uint32_t index,
               int anchor_x, int anchor_y,
               float scale,
               const std::optional<SDL_Color>& tint) -> void {
    auto pick = atlas.pick(category, index);
    if (!pick) return;
    const auto* cat = atlas.category(category);
    if (!cat || !cat->attach_point) return;
    const auto dst = place_child(pick->second, *cat->attach_point,
                                 anchor_x, anchor_y, scale);
    blit_with_tint(r, pick->first, dst, tint);
}

}  // namespace

void CreatureAssembler::render(SDL_Renderer* renderer,
                               const AppearanceGene& gene,
                               int center_x, int center_y,
                               int target_canvas_size,
                               std::optional<SDL_Color> tint_override) const {
    if (target_canvas_size <= 0) return;

    const float scale = static_cast<float>(target_canvas_size) / 256.0f;

    // HSL 染色 = 默认；caller 可覆盖（elite / boss 走这条）。
    const SDL_Color gene_tint = compute_tint(gene);
    const SDL_Color tint = tint_override.value_or(gene_tint);
    const std::optional<SDL_Color> tint_opt{tint};

    // 1. Torso — 基准，置于中心。
    auto torso_pick = atlas_.pick("torso", gene.torso_index);
    if (!torso_pick) return;
    SDL_Texture* torso_tex = torso_pick->first;
    SDL_Rect torso_bbox = torso_pick->second;

    const SDL_FRect torso_dst{
        static_cast<float>(center_x) - static_cast<float>(torso_bbox.w) * scale * 0.5f,
        static_cast<float>(center_y) - static_cast<float>(torso_bbox.h) * scale * 0.5f,
        static_cast<float>(torso_bbox.w) * scale,
        static_cast<float>(torso_bbox.h) * scale,
    };

    const auto* torso_cat = atlas_.category("torso");
    if (torso_cat == nullptr || !torso_cat->attach_front || !torso_cat->attach_back) {
        return;
    }
    const auto front = world_anchor(torso_dst, *torso_cat->attach_front, scale);
    const auto back  = world_anchor(torso_dst, *torso_cat->attach_back,  scale);

    // 后肢/前肢 各画 2 次（一后一前，对称偏移）。后肢先画（在 torso 后），
    // 前肢后画（在 torso 前）。偏移 ±kLimbOffsetPx 像素。
    constexpr int kLimbOffsetPx = 4;

    // 2. 后侧肢（在 torso 后）
    draw_limb(renderer, atlas_, "hindlimb", gene.hindlimb_index,
              static_cast<int>(back.x)  - kLimbOffsetPx,
              static_cast<int>(back.y),  scale, tint_opt);
    draw_limb(renderer, atlas_, "forelimb", gene.forelimb_index,
              static_cast<int>(front.x) - kLimbOffsetPx,
              static_cast<int>(front.y), scale, tint_opt);

    // 3. Tail（在 torso 后）
    if (auto pick = atlas_.pick("tail", gene.tail_index)) {
        const auto* cat = atlas_.category("tail");
        if (cat && cat->attach_point) {
            const auto dst = place_child(pick->second, *cat->attach_point,
                                         static_cast<int>(back.x),
                                         static_cast<int>(back.y), scale);
            blit_with_tint(renderer, pick->first, dst, tint_opt);
        }
    }

    // 4. Torso
    blit_with_tint(renderer, torso_tex, torso_dst, tint_opt);

    // 5. 前侧肢（在 torso 前）
    draw_limb(renderer, atlas_, "hindlimb", gene.hindlimb_index,
              static_cast<int>(back.x)  + kLimbOffsetPx,
              static_cast<int>(back.y),  scale, tint_opt);
    draw_limb(renderer, atlas_, "forelimb", gene.forelimb_index,
              static_cast<int>(front.x) + kLimbOffsetPx,
              static_cast<int>(front.y), scale, tint_opt);

    // 6. Head
    if (auto pick = atlas_.pick("head", gene.head_index)) {
        const auto* cat = atlas_.category("head");
        if (cat && cat->attach_point) {
            const auto dst = place_child(pick->second, *cat->attach_point,
                                         static_cast<int>(front.x),
                                         static_cast<int>(front.y), scale);
            blit_with_tint(renderer, pick->first, dst, tint_opt);
        }
    }
}

}  // namespace game

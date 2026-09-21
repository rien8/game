#pragma once

#include "core/gene.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game {

// ---------- Manifest (parsed from assets/parts_processed/manifest.json) ----

// 单部件描述（一个具体 variant，比如 head_001）。
struct PartEntry {
    std::string id;     // "head_001"
    std::string file;   // "head/head_001.png"（相对 asset_root）
    SDL_Rect bbox{};    // post-process 后在 256×256 画布上的 [x, y, w, h]
};

// 单连接点的连接方向（部件 bbox 的哪一侧是连接端）。
enum class AttachSide {
    Top,
    Bottom,
    Left,
    Right,
};

// 单连接点的画布坐标。
struct AttachPoint {
    int x = 0;
    int y = 0;
};

// 一个部件类目（head / torso / ...）。
// 大多数类目是单连接点（attach_side + attach_point）；
// torso 是双连接点（attach_front 接头，attach_back 接腿和尾）。
struct Category {
    std::string name;
    int z_order = 0;                       // 绘制顺序，小=底层
    std::optional<AttachSide>  attach_side;
    std::optional<AttachPoint> attach_point;
    std::optional<AttachPoint> attach_front;
    std::optional<AttachPoint> attach_back;
    std::vector<PartEntry> parts;
};

// 整个 manifest.json 解析结果。
struct PartManifest {
    int canvas_size = 256;
    int white_threshold = 235;
    std::vector<Category> categories;
};

// 从磁盘读 manifest.json。失败原因在 std::unexpected 里。
auto load_manifest(std::string_view path) -> std::expected<PartManifest, std::string>;

// ---------- Atlas（GPU 端的部件纹理表）--------------------------------------

// 拥有所有 PartEntry 对应的 SDL_Texture。
// 加载顺序与 manifest.categories 保持一致（z-order 已经排好）。
class PartAtlas {
public:
    [[nodiscard]] static auto load(SDL_Renderer* renderer,
                                   const PartManifest& manifest,
                                   std::string_view asset_root)
        -> std::expected<PartAtlas, std::string>;

    PartAtlas(PartAtlas&&) noexcept;
    auto operator=(PartAtlas&&) noexcept -> PartAtlas&;
    PartAtlas(const PartAtlas&) = delete;
    auto operator=(const PartAtlas&) -> PartAtlas& = delete;
    ~PartAtlas();

    [[nodiscard]] auto category(std::string_view name) const -> const Category*;

    // 查 (category, id) 对应的纹理和 bbox。找不到返回 std::nullopt。
    [[nodiscard]] auto lookup(std::string_view category,
                              std::string_view id) const
        -> std::optional<std::pair<SDL_Texture*, SDL_Rect>>;

    // 按 index 选该类目的第 N 个部件（gene 索引自动取模）。
    [[nodiscard]] auto pick(std::string_view category, std::uint32_t index) const
        -> std::optional<std::pair<SDL_Texture*, SDL_Rect>>;

private:
    struct Entry {
        std::string id;
        SDL_Texture* texture = nullptr;
        SDL_Rect bbox{};
    };

    PartAtlas() = default;
    void destroy_textures() noexcept;

    std::unordered_map<std::string, std::vector<Entry>> by_category_;
    std::unordered_map<std::string, const Category*>   category_meta_;
    SDL_Renderer* renderer_ = nullptr;  // 仅用于 SDL_DestroyTexture，不拥有
};

// ---------- 基因 → 外观索引 --------------------------------------------------

// 由 Traits（或任何随机源）派生出来的"哪个部件"选择。
// 索引按各类目的部件数自动取模，所以越界也安全。
struct AppearanceGene {
    std::uint32_t head_index     = 0;
    std::uint32_t torso_index    = 0;
    std::uint32_t forelimb_index = 0;
    std::uint32_t hindlimb_index = 0;
    std::uint32_t tail_index     = 0;
    // horn / wing：默认值 = "none"（不在 v1 渲染）

    // HSL 染色：hue 偏移（度）+ 饱和度/明度调整（百分比，[-100, +100]）。
    // 实现为乘性 tint（SDL_SetTextureColorMod），所以 -100 sat 会去饱和到灰。
    std::uint32_t hue_offset      = 0;     // [0, 360)
    std::int32_t  saturation_mod  = 0;     // [-100, +100]
    std::int32_t  value_mod       = 0;     // [-100, +100]
};

// FNV-1a 哈希 Traits → 12 类部件索引 + HSL 参数。确定性：相同 Traits → 相同外观。
auto derive_appearance(const Traits& gene) -> AppearanceGene;

// ---------- 合成器 ----------------------------------------------------------

// 按 gene 选部件，按 anchor 拼起来，渲染到 SDL_Renderer。
// 渲染层级（z-order 升序，由 atlas 内部排序保证）：
//   torso (0) → tail → hindlimb → forelimb → head
// horn / wing 留待 v2。
class CreatureAssembler {
public:
    explicit CreatureAssembler(const PartAtlas& atlas) noexcept;

    // 在 (center_x, center_y) 为中心的窗口像素坐标渲染一个生物。
    // target_canvas_size: 把 256×256 设计画布缩到多大（窗口像素）。
    //   默认 64（≈4 个 16px tile 宽）。
    // tint: 可选的颜色叠加（用 SDL_SetTextureColorMod 实现）。
    //   默认 std::nullopt → 不染色。给 elite / boss 用。
    void render(SDL_Renderer* renderer, const AppearanceGene& gene,
                int center_x, int center_y,
                int target_canvas_size = 64,
                std::optional<SDL_Color> tint = std::nullopt) const;

private:
    const PartAtlas& atlas_;
};

}  // namespace game

# Zoom Float Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change `Camera::px_per_tile` from `int` to `float` to fix zoom deadlock at p=2/3/4 and enable smooth zooming; clamp minimap viewport to its bounds.

**Architecture:** Single mechanical change to the type of one field plus downstream uses. Three files touched: header (type change), renderer (use float math), main (camera init + zoom math). No new abstractions, no new modules.

**Tech Stack:** C++23, SDL3, CMake + Ninja, clang-cl + lld-link, vcpkg (x64-windows).

## Global Constraints

From `CLAUDE.md`:
- C++23 (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`)
- Compiler flags: `-Wall -Wextra -Wpedantic`
- Default ASan + UBSan enabled
- Header guards: `#pragma once`
- Naming: types PascalCase, functions camelCase, variables snake_case, private members `_` suffix
- Smart pointers only (no raw new/delete)
- `std::string_view` for read-only strings, `std::string` for owning
- `std::expected` (C++23) for error handling
- No `using namespace` in headers
- pwsh for terminal; bash via Git Bash
- Temporary files in `temp/` (already in `.gitignore`)
- Build: `cmake --build build` from `E:/gamev2`

From spec (`docs/superpowers/specs/2026-09-21-zoom-float-design.md`):
- `Camera::px_per_tile` becomes `float`
- Zoom bounds `[2.0f, 64.0f]`
- Minimap viewport clamped to minimap bounds
- Existing ">=95% map coverage" early return kept
- Geometric alignment math unchanged

---

## Task 1: Change `Camera::px_per_tile` to float in header

**Files:**
- Modify: `E:/gamev2/include/render/tile_renderer.hpp:27`

**Interfaces:**
- Consumes: nothing
- Produces: `Camera::px_per_tile` is `float`

- [ ] **Step 1: Edit the Camera struct**

In `E:/gamev2/include/render/tile_renderer.hpp`, change line 27:

```cpp
    int   px_per_tile = 16;
```

to:

```cpp
    float px_per_tile = 16.0f;
```

- [ ] **Step 2: Build to surface all int usages**

Run: `cd E:/gamev2 && cmake --build build 2>&1 | head -60`

Expected: build fails with `int → float` conversion errors at every use site. This is intentional — we want to enumerate every use.

If it compiles cleanly: that's a problem — search for direct `px_per_tile` reads and confirm none are missed.

- [ ] **Step 3: Commit header change**

```bash
cd E:/gamev2 && git add include/render/tile_renderer.hpp && git commit -m "camera: px_per_tile int → float"
```

---

## Task 2: Update `src/render/tile_renderer.cpp`

**Files:**
- Modify: `E:/gamev2/src/render/tile_renderer.cpp:153, 175, 182-183, 216-219, 272`

**Interfaces:**
- Consumes: `camera_.px_per_tile: float`
- Produces: same public API (`render`, `render_creatures`, `render_minimap`); outputs unchanged

- [ ] **Step 1: Update `render()` to use float `p`**

In `E:/gamev2/src/render/tile_renderer.cpp`, change lines 152-155:

```cpp
    // 视口 = 屏幕中心 ± 半个可见 tile 数。源 / 目标 rect 按相机缩放。
    // 注意 px_per_tile = 0 是未初始化状态，兜底为 1 防除零。
    const int p = std::max(1, camera_.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / static_cast<float>(p);
    const float vis_h = static_cast<float>(window_h_) / static_cast<float>(p);
```

to:

```cpp
    // 视口 = 屏幕中心 ± 半个可见 tile 数。源 / 目标 rect 按相机缩放。
    // 注意 px_per_tile = 0 是未初始化状态，兜底为 1.0f 防除零。
    const float p = std::max(1.0f, camera_.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / p;
    const float vis_h = static_cast<float>(window_h_) / p;
```

(vis_w / vis_h were already float; just drop the redundant `static_cast<float>(p)`.)

- [ ] **Step 2: Update `render_creatures()` — `p` to float**

In `E:/gamev2/src/render/tile_renderer.cpp`, change line 175:

```cpp
    const int p = std::max(1, camera_.px_per_tile);
```

to:

```cpp
    const float p = std::max(1.0f, camera_.px_per_tile);
```

- [ ] **Step 3: Update sprite canvas size to use float**

In `E:/gamev2/src/render/tile_renderer.cpp`, change lines 182-183:

```cpp
    constexpr int kSpriteCanvasPxBase = 64;
    const int kSpriteCanvasPx = std::max(8, kSpriteCanvasPxBase * p / 16);
```

to:

```cpp
    constexpr float kSpriteCanvasPxBase = 64.0f;
    const int kSpriteCanvasPx = std::max(8,
        static_cast<int>(kSpriteCanvasPxBase * p / 16.0f));
```

(SDL_RenderTexture takes int dst dimensions, so keep the int output.)

- [ ] **Step 4: Update dot size math to use float**

In `E:/gamev2/src/render/tile_renderer.cpp`, change lines 216-219:

```cpp
        std::int32_t size = std::max(2, p / 4);
        std::uint32_t border = 0;
        if (c.is_elite) { size = std::max(2, p / 2); border = kEliteBorder; }
        if (c.is_boss)  { size = std::max(2, p * 3 / 4); border = kBossBorder; }
```

to:

```cpp
        std::int32_t size = std::max(2, static_cast<int>(p / 4.0f));
        std::uint32_t border = 0;
        if (c.is_elite) { size = std::max(2, static_cast<int>(p / 2.0f)); border = kEliteBorder; }
        if (c.is_boss)  { size = std::max(2, static_cast<int>(p * 3.0f / 4.0f)); border = kBossBorder; }
```

- [ ] **Step 5: Update `render_minimap()` — `p` to float + clamp viewport**

In `E:/gamev2/src/render/tile_renderer.cpp`, change line 272:

```cpp
    const int p = std::max(1, cam.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / static_cast<float>(p);
    const float vis_h = static_cast<float>(window_h_) / static_cast<float>(p);
```

to:

```cpp
    const float p = std::max(1.0f, cam.px_per_tile);
    const float vis_w = static_cast<float>(window_w_) / p;
    const float vis_h = static_cast<float>(window_h_) / p;
```

Then change lines 280-290:

```cpp
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
```

to:

```cpp
    const float sx = static_cast<float>(kMmW) / static_cast<float>(width_);
    const float sy = static_cast<float>(kMmH) / static_cast<float>(height_);
    const float vx = (cam.tile_cx - vis_w * 0.5f) * sx;
    const float vy = (cam.tile_cy - vis_h * 0.5f) * sy;
    const float vw = vis_w * sx;
    const float vh = vis_h * sy;
    // clamp 视口到 minimap 边界（小 zoom 时视口比 minimap 还大）
    const float c_vx = std::clamp(vx, 0.0f, std::max(0.0f, static_cast<float>(kMmW) - vw));
    const float c_vy = std::clamp(vy, 0.0f, std::max(0.0f, static_cast<float>(kMmH) - vh));
    const SDL_FRect vp{
        static_cast<float>(mm_x) + c_vx,
        static_cast<float>(mm_y) + c_vy,
        vw, vh
    };
```

(The `>=95%` early return at lines 276-279 stays as-is.)

- [ ] **Step 6: Build to verify renderer compiles**

Run: `cd E:/gamev2 && cmake --build build 2>&1 | head -60`

Expected: only errors from `main.cpp` remain (camera init + zoom math).

- [ ] **Step 7: Commit renderer changes**

```bash
cd E:/gamev2 && git add src/render/tile_renderer.cpp && git commit -m "renderer: use float px_per_tile; clamp minimap viewport"
```

---

## Task 3: Update `src/main.cpp` (camera init + zoom math)

**Files:**
- Modify: `E:/gamev2/src/main.cpp:126, 131, 136, 156-181, 236-238, 256-258`

**Interfaces:**
- Consumes: `Camera::px_per_tile: float`
- Produces: smooth zoom (no deadlock at p=2/3/4)

- [ ] **Step 1: Update default camera init to float literal**

In `E:/gamev2/src/main.cpp`, change line 126:

```cpp
        .px_per_tile  = 8,
```

to:

```cpp
        .px_per_tile  = 8.0f,
```

- [ ] **Step 2: Update `reset_camera`**

In `E:/gamev2/src/main.cpp`, change line 131:

```cpp
        camera.px_per_tile = 8;
```

to:

```cpp
        camera.px_per_tile = 8.0f;
```

- [ ] **Step 3: Update `clamp_camera`**

In `E:/gamev2/src/main.cpp`, change line 136:

```cpp
        const int p = std::max(1, camera.px_per_tile);
        const float vis_w = static_cast<float>(renderer->window_w()) / p;
        const float vis_h = static_cast<float>(renderer->window_h()) / p;
```

to:

```cpp
        const float p = std::max(1.0f, camera.px_per_tile);
        const float vis_w = static_cast<float>(renderer->window_w()) / p;
        const float vis_h = static_cast<float>(renderer->window_h()) / p;
```

- [ ] **Step 4: Update `screen_to_tile`**

In `E:/gamev2/src/main.cpp`, change line 159:

```cpp
        const int p = std::max(1, camera.px_per_tile);
```

to:

```cpp
        const float p = std::max(1.0f, camera.px_per_tile);
```

- [ ] **Step 5: Rewrite `zoom_at_mouse` (kill the int truncation)**

In `E:/gamev2/src/main.cpp`, replace lines 167-181:

```cpp
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
```

with:

```cpp
    auto zoom_at_mouse = [&](int mx, int my, int wheel_y, bool fast) {
        const float step = fast ? 1.5f : 1.2f;
        const float factor = (wheel_y > 0) ? step : (1.0f / step);
        const float new_p = std::clamp(
            camera.px_per_tile * factor, 2.0f, 64.0f);
        // epsilon 兜底：连续 zoom 累积误差 < 1e-3 视为无变化
        if (std::abs(new_p - camera.px_per_tile) < 1e-3f) return;

        const auto [old_tx, old_ty] = screen_to_tile(mx, my);
        camera.px_per_tile = new_p;
        // 反推：缩放后让 old_tx/ty 仍落在 (mx, my)
        const auto [new_tx, new_ty] = screen_to_tile(mx, my);
        camera.tile_cx += old_tx - new_tx;
        camera.tile_cy += old_ty - new_ty;
    };
```

- [ ] **Step 6: Update mouse drag p**

In `E:/gamev2/src/main.cpp`, change line 236:

```cpp
                    const int p = std::max(1, camera.px_per_tile);
                    camera.tile_cx -= static_cast<float>(dx) / p;
                    camera.tile_cy -= static_cast<float>(dy) / p;
```

to:

```cpp
                    const float p = std::max(1.0f, camera.px_per_tile);
                    camera.tile_cx -= static_cast<float>(dx) / p;
                    camera.tile_cy -= static_cast<float>(dy) / p;
```

- [ ] **Step 7: Update WASD pan p**

In `E:/gamev2/src/main.cpp`, change line 256:

```cpp
            const int p = std::max(1, camera.px_per_tile);
            // 速度：每帧 0.5 个 tile，按 zoom 缩放（zoom 越大，1 tile 越大，要走得更快才跟得上视觉）
            float pan = 0.5f * static_cast<float>(p) / 8.0f;
```

to:

```cpp
            const float p = std::max(1.0f, camera.px_per_tile);
            // 速度：每帧 0.5 个 tile，按 zoom 缩放（zoom 越大，1 tile 越大，要走得更快才跟得上视觉）
            float pan = 0.5f * p / 8.0f;
```

(The `static_cast<float>(p)` is redundant since `p` is now `float`.)

- [ ] **Step 8: Add `<cmath>` include if needed**

`std::abs` for floats needs `<cmath>` (also acceptable: `<cstdlib>` provides `std::abs` for ints only; we need float overload). Run the build first to see if `<cmath>` is already transitively included.

If build fails with `std::abs(float)` ambiguous or undefined, add at top of `src/main.cpp` (after other includes, alphabetical):

```cpp
#include <cmath>
```

- [ ] **Step 9: Build everything**

Run: `cd E:/gamev2 && cmake --build build 2>&1 | tail -20`

Expected: clean build, no errors. If warnings about float/int conversion appear, address per `-Wpedantic` rules.

- [ ] **Step 10: Commit main.cpp changes**

```bash
cd E:/gamev2 && git add src/main.cpp && git commit -m "main: use float px_per_tile; smooth zoom"
```

---

## Task 4: Visual verification with screenshots

**Files:**
- Create (ephemeral): `E:/gamev2/temp/scale_p{8,16,32,2}.png`

**Interfaces:**
- Consumes: built `gamev2.exe`
- Produces: PNG screenshots for visual diff against pre-change baseline

- [ ] **Step 1: Screenshot at default zoom (p=8)**

Run: `cd E:/gamev2 && rm -f temp/scale_p8.png && ./build/gamev2.exe --seed 12345 --screenshot temp/scale_p8.png 2>&1 | tail -5`

Expected: `[sprite] loaded 7 categories, 76 total parts` + `saved screenshot: temp/scale_p8.png`.

- [ ] **Step 2: Add a temporary `--zoom` CLI flag for testing**

The default `--screenshot` only shows p=8. To test p=16/32/2, add a one-shot CLI override.

In `E:/gamev2/src/main.cpp`, after `parse_seed` / `parse_screenshot` block (around line 54), add:

```cpp
auto parse_zoom(int argc, char** argv) -> std::optional<float> {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--zoom" && i + 1 < argc) {
            return std::stof(argv[++i]);
        }
    }
    return std::nullopt;
}
```

Then in `main()`, after `parse_screenshot(argc, argv)`:

```cpp
    if (auto z = parse_zoom(argc, argv)) {
        camera.px_per_tile = *z;
    }
```

Build: `cd E:/gamev2 && cmake --build build 2>&1 | tail -10`

Expected: clean build.

Commit: `cd E:/gamev2 && git add src/main.cpp && git commit -m "main: add --zoom CLI flag for testing"` (DO include, since it's a useful debug aid; or skip if you'd rather not pollute main).

- [ ] **Step 3: Screenshot at p=16 (zoomed in)**

Run: `cd E:/gamev2 && rm -f temp/scale_p16.png && ./build/gamev2.exe --seed 12345 --zoom 16 --screenshot temp/scale_p16.png 2>&1 | tail -3`

Expected: `saved screenshot: temp/scale_p16.png`.

- [ ] **Step 4: Screenshot at p=2 (the previously deadlocked value)**

Run: `cd E:/gamev2 && rm -f temp/scale_p2.png && ./build/gamev2.exe --seed 12345 --zoom 2 --screenshot temp/scale_p2.png 2>&1 | tail -3`

Expected: `saved screenshot: temp/scale_p2.png`.

- [ ] **Step 5: Screenshot at p=32**

Run: `cd E:/gamev2 && rm -f temp/scale_p32.png && ./build/gamev2.exe --seed 12345 --zoom 32 --screenshot temp/scale_p32.png 2>&1 | tail -3`

Expected: `saved screenshot: temp/scale_p32.png`.

- [ ] **Step 6: Visual comparison**

Open each PNG:
- `temp/scale_p8.png` — should match the prior baseline (creatures centered on their tiles, sprite ~32px)
- `temp/scale_p16.png` — creatures centered on tiles, sprite ~64px (2× the canvas of p=8)
- `temp/scale_p32.png` — creatures centered on tiles, sprite ~128px
- `temp/scale_p2.png` — creatures as ~2-3px dots, centered on tiles (this is the value that was previously deadlocked)

Verification criterion: in every screenshot, the center of each creature's sprite is at the screen position that corresponds to the world tile center (where the map's tile center is rendered). No visible offset > 1 pixel between creature center and tile center.

If you spot misalignment, document the actual offset (tile X, screen Y) and we need to revisit the math. Otherwise: proceed.

- [ ] **Step 7: Manual interactive test (deadlock fix)**

Run: `cd E:/gamev2 && ./build/gamev2.exe --seed 12345`

Mouse wheel down 10 times → p should drop smoothly below 8 → 4 → 3 → 2.

Then mouse wheel up 5 times → p should climb back up smoothly to 4 → 6 → 8 → 12 → 14 (or so).

Pre-change behavior: getting back from p=2 was impossible. Post-change: every wheel notch produces visible zoom.

ESC to quit.

- [ ] **Step 8: Optional — drop `--zoom` debug flag if undesired**

If you'd rather not keep `--zoom` in main.cpp:

```bash
cd E:/gamev2 && git revert HEAD --no-edit
```

Otherwise leave it; it's useful for future debugging.

- [ ] **Step 9: Final commit if any leftover changes**

```bash
cd E:/gamev2 && git status
```

If anything modified (e.g., screenshots in temp/ should be untracked — temp/ is in `.gitignore`, so they won't show), nothing to commit. Otherwise commit.
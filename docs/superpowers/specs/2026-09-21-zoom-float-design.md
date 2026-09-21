# 缩放功能修复 — px_per_tile 改 float

**日期**: 2026-09-21
**状态**: 已批准（用户 2026-09-21 选定"px_per_tile 改为 float"）
**作者**: 自动（brainstorming → writing-plans）

## 背景

`Camera::px_per_tile` 当前是 `int`。`zoom_at_mouse` 用 `static_cast<int>(px_per_tile * 1.2)`
计算下一档 zoom 后再 clamp 到 [2, 64]。在 px_per_tile = 2/3/4 时：

| 当前 | 1.2× 结果 | int 截断 | 新旧相同？ |
|------|----------|---------|----------|
| 2    | 2.4      | 2       | 是（死锁）|
| 3    | 3.6      | 3       | 是（死锁）|
| 4    | 4.8      | 4       | 是（死锁）|

→ 滚轮往上完全无反应，用户"不能再放大"。

按住 Shift（factor=1.5）时 p=3 也死锁（3×1.5=4.5 → 4，但 zoom_at_mouse
判断 `new_p == px_per_tile` 直接 return — 等等，这里其实不等，会变；但当
new_p=4 → 4×1.5=6 → 变 6。所以 Shift 在 p=3 上没问题；p=2 上 2×1.5=3 也
能变）。

复现：跑 game 默认启动 p=8，滚轮下到死锁，停在 p=2。然后滚轮上 → 无反应。

## 目标

1. **平滑 zoom**：px_per_tile 改为 float，任意 factor 都能产生非零位移。
2. **修 minimap viewport 越界**：在 px_per_tile < ~8 时，视口大小超过地图
3. **保持生物和地图的相对位置**：几何对齐数学保持不变。

## 设计

### `Camera::px_per_tile`: int → float

`include/render/tile_renderer.hpp`:

```cpp
struct Camera {
    float tile_cx = 0.0f;
    float tile_cy = 0.0f;
    float px_per_tile = 16.0f;   // int → float
};
```

默认 16.0f（之前是 16）保持一致；main 里实际默认值 8.0f 保持。

### 渲染层：`tile_renderer.cpp`

`render()`:
- `int p = std::max(1, camera_.px_per_tile);`
  → `float p = std::max(1.0f, camera_.px_per_tile);`
- 后面 vis_w/vis_h/src/dst 全部 float，原本就 OK。

`render_creatures()`:
- `int p = ...` → `float p = ...`
- `win_cx`/`win_cy` 保持 int（pixel 位置）
- 生物 cx/cy 计算已经 float 运算 + int 截断，无需改
- `kSpriteCanvasPx`：
  ```cpp
  constexpr float kSpriteCanvasPxBase = 64.0f;
  const int kSpriteCanvasPx = std::max(8,
      static_cast<int>(kSpriteCanvasPxBase * p / 16.0f));
  ```
  保留 int 输出给 SDL_RenderTexture（整数像素）。
- dot size：
  ```cpp
  std::int32_t size = std::max(2, static_cast<int>(p / 4.0f));
  std::int32_t size = std::max(2, static_cast<int>(p / 2.0f));   // elite
  std::int32_t size = std::max(2, static_cast<int>(p * 3.0f / 4.0f)); // boss
  ```
- boss marker "B" 偏移用 `kSpriteCanvasPx`（已 int），无变化。

`render_minimap()`:
- `int p = ...` → `float p = ...`
- viewport rect 越界处理：clamp 视口矩形到 minimap 边界
  ```cpp
  const float clamped_vx = std::clamp(vx, 0.0f, kMmW - vw);
  const float clamped_vy = std::clamp(vy, 0.0f, kMmH - vh);
  ```
- 保留现有的 "视口覆盖 >=95% 地图" 时早 return 的逻辑。

### 主循环：`main.cpp`

- 相机初始化：`.px_per_tile = 8.0f`
- `clamp_camera`: `int p` → `float p`
- `screen_to_tile`: `int p` → `float p`
- `zoom_at_mouse`：
  ```cpp
  const float new_p = std::clamp(
      camera.px_per_tile * factor, 2.0f, 64.0f);
  if (new_p == camera.px_per_tile) return;
  ```
  （浮点比较在极小 delta 下可能永远不等；但 1.2/1.5 倍数下足够安全。
   如果担心，改为 `if (std::abs(new_p - camera.px_per_tile) < 1e-3f) return;`）
- 鼠标拖动 / WASD 平移的 `int p` → `float p`
- 保留 zoom 上下限 [2.0f, 64.0f]

### 不动

- 生物和地图的位置数学（已经几何对齐）
- "4 tiles per sprite" 的画布比例
- SDL_RenderTexture 的 src/dst rect 写法
- dot fallback 的颜色 / 边框
- minimap 的早 return 规则

## 验证

1. **构建 + 启动**：默认 8.0f 启动，地图铺满窗口，生物位置和地图 tile 中心对齐（眼测 + 截屏）
2. **zoom 流畅**：滚轮从 p=2 一直往上，每步都变化，不卡死
3. **minimap**：p=2/3/4 时 minimap 视口框不超出 minimap 边界；>=95% 覆盖时仍隐藏
4. **极端 zoom**：p=64 时生物可见，sprite 不溢出窗口边缘

测试用 `--screenshot` 模式截屏对比：
- `temp/scale_check_p8.png`（默认）
- `temp/scale_check_p16.png`（zoom in ×2）
- `temp/scale_check_p2.png`（zoom out 死锁测试 → 改完后应能正常截图）

## 风险

- `float` 精度累积：连续 zoom 可能产生微小浮点误差（如 8 → 12.000001 → 14.4）。
  解决：clamp 范围 [2, 64] 内误差远小于 1 像素，不影响视觉。
- 浮点 == 比较：见上面 zoom_at_mouse 注释，加 epsilon 兜底。
---
name: world-will-art-bible
description: |
  Use when working on 《世界意志》(World Will) art assets, UI, environment shaders,
  sprite parts, animation states, era palettes, color tokens, or any visual decision
  that touches the bio / env / UI three-layer system. Always read `doc/美术风格规格.md`
  first. Reject work that breaks §5 "不做什么" (hard constraints) and §3.10 color-blind
  rules. Do not use for non-art code (combat, AI, save), for art work in other projects,
  or for tasks whose scope is unrelated to the visual style.
---

# 《世界意志》美术风格审查技能

> 在做任何与《世界意志》美术相关的工作之前，**必须**先读 `doc/美术风格规格.md`（v1.1+）。
> 本技能只定义**行为契约与最关键的反约束**——具体规范以源文档为唯一权威。

## 一、触发场景

用户提到以下任意一个，应当激活：

- 美术部件 PNG / silhouette test / 256×256 部件规范
- HSL tint、tint palette、生物 tier（Champion / Elite / Mini-Boss / Boss）
- Biome 色板、EraKind（Genesis / Flourishing / Calamity / Chaos）、纪元过渡时序
- 水墨 shader、Simplex 噪声、墨分五色
- UI 烫金 / 烫银 / 撕边面板 / HUD 布局 / 图标 / 状态条 / Minimap
- `kPaper*` / `kInk*` / `kGold*` / `kSilver*` 颜色 token
- 60-30-10 配色比例 / 色弱友好 / oxipng 资源导出
- 美术资源命名规范（§0.6）、新仪式时刻提案
- 美术走查、silhouette test、版本记录更新（§0.7）

## 二、硬性反约束（违反任何一项 → 直接拒绝）

> 这 8 条是从源文档 §5 提炼出来的"风格生存线"，不允许任何"灵活处理"。

1. **不许在生物层用阴影 / 高光**——只允许 1 处 2 px 的暗化结构提示
2. **不许在环境层加粒子 / 流动贴图 / 闪烁**——水墨的"静"是生物"动"的背景
3. **不许在 UI 使用圆角**——剪纸几何 = 直线 + 撕边
4. **不许用 emoji 或写实图标**——所有 UI 图标用剪纸几何
5. **不许用 HDR / Bloom / 后处理色相**——烫金/烫银的程序化绘制是唯一金属高光
6. **不许引入新贴图做动画**——所有动画差分从同一份 PNG 用数学变换产出（§1.2.4）
7. **不许用红绿单一区分功能色**——色弱友好必须走"形状 + 位置 + 文字"多通道（§3.10）
8. **不许烫金 / 烫银 累计视口面积超 15% / 20%**——突破这两个阈值就是全屏金属，破坏水墨（§3.9）

## 三、行为流程

任何美术相关请求，按下面 6 步走：

1. **读源文档**：`doc/美术风格规格.md`（必须）→ 锁定版本号（v1.1）
2. **定位章节**：根据请求类型定位到具体 §
3. **核对反约束**：对照上方"硬性反约束"逐条检查
4. **给出决策**：符合规范 → 直接执行；偏离 → 必须先指出偏差并给出修正方案
5. **输出交付清单**：每次交付时同时提供
   - 文件路径（如修改了文档 / token / 代码）
   - 影响的 §号
   - 是否触发 §0.7 版本记录更新
   - silhouette / 色弱 / 60-30-10 中**相关**的校验结果
6. **保留回溯**：若动了 token / 色板 / 命名规范，必须提醒"代码 + 文档同源"的同步义务

## 四、典型请求的处理模板

| 用户请求 | 应当读取 | 应当产出 |
|---------|---------|---------|
| "画一个新的生物部件" | §1.1–1.6 | silhouette test 假设 + 256×256 部件清单 + HSL tint 范围 + 动画差分表 |
| "加一个新的仪式时刻" | §3.1, §3.6, §3.9 | 时刻名 + 触发条件 + UI 表现 + 烫金/烫银面积预估 + §0.7 版本表更新 |
| "调整某个 biome 颜色" | §2.3, §2.5 | 墨分五色校验（luma 差 ≈ 50）+ 纪元色板派生公式 + 60-30-10 影响 |
| "设计一个新 HUD" | §3.7, §3.8, §3.10 | 网格位置 + 字号 + 色 token + 色弱校验 + viewport 占比校验 |
| "写一个水墨 shader" | §2.7 | 4 步渲染流程 + 墨阶映射 + 可缓存策略 + "不许做"清单 |
| "评审一段动画代码" | §1.2.4 + §5 | 是否引入新贴图、是否走完 5 个状态、是否符合像素位移规范 |
| "选一种字体" | §3.2 | 必须等宽，列出字号矩阵，禁用衬线/手写体 |

## 五、与其他文档的连接

本技能是 `doc/美术风格规格.md` 的 **行为门面**——不要把源文档搬进本 SKILL.md。如果：

- 源文档 §号变了 → 同步更新本 SKILL.md 中所有 §引用
- 反约束条数变了 → 同步更新本 SKILL.md"硬性反约束"列表
- 新增章节 → 同步更新本 SKILL.md"典型请求"表格

## 六、不要做的事

- ❌ **不要凭空给颜色或字号**——必须从源文档或 token 取
- ❌ **不要建议用 emoji / 写实图标 / Bloom / HDR / 流动贴图**——见硬性反约束
- ❌ **不要在评审时"差不多就行"**——silhouette test、60-30-10、色弱校验都要给出具体数字
- ❌ **不要忽略 §0.7 版本记录**——任何对源文档的扩展都要提议版本号 +1

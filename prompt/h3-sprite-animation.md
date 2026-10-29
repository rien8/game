# 模板：H3 游戏精灵动画图（sprite sheet）

把一张静态角色图做成可循环的动画精灵图，供游戏里待机 / 行走等动作切换。

本模板来自 gamev2 项目「宗主 idle + walk」的实际执行记录，参数是实测可用的
基线，不是通用最优解。**每次开工都要先确认规格卡**，不要照抄帧数。

---

## 硬约束

- 消耗类操作（提交 H3 生成）必须在**动手前**让用户确认一次金额/次数。
  确认之后不要再问"要开始了吗"，直接做。
- H3 模型必须显式写 `model: "MiniMax-H3"`，禁止 H2.5、禁止默认回退。
- H3 prompt 必须用官方三字段格式，缺一不可：

  ```text
  How the reference pictures align with the target video — Picture 1 (from Shot 1)
  aligns with the 0.00-second mark of the target video; Picture 2 (from Shot 1)
  aligns with the 6.00-second mark of the target video.

  integrated_multimodal_description: ...

  overall_soundscape: N/A

  non_diegetic_music: N/A
  ```

  FL2VA（首尾帧）模式必须有第一行对齐说明。纯音效也要写 `N/A` 而不是省略。
- 交付前必须过**两道质检门**，任何一项 FAIL 都不许交付：
  1. 源视频体检 `--check-only`
  2. 导出序列的 `loop_qc` / `playback_qc` / `spatial_qc`
- `--allow-qc-fail` 只用于排查问题时产出诊断产物，**绝不用于交付**。
- 不要自动 git commit，提交时机由用户决定。
- Python 解释器用 `py -3`。行宽 ≤ 90 字符。

---

## 阶段 0：规格卡（开工前问用户）

把下面这张表填好，**逐行跟用户确认**再动手。用户没确认的项不要自己发挥。

| 字段 | 说明 | 本次实测值 |
| --- | --- | --- |
| `输入图` | 源角色图的绝对路径 | `assets/sprites/characters/individual/office1_sect_master.png` |
| `动作列表` | 要做哪几个动作 | `idle`（4 帧）、`walk`（6 帧） |
| `单元尺寸` | cell 宽高 | `32×32` |
| `帧率` | 建议 fps | `12` |
| `背景` | `transparent` / `keep` | `transparent` |
| `锚点` | `bottom-center` / `center` | `bottom-center` |
| `朝向` | `front` 单向不镜像 | `front` |
| `调色板` | 量化色数 | `32` |
| `像素化` | 目标帧高 | `32` |
| `纹理上限` | 图集最大边 | `4096` |
| `每动作重试上限` | 失败重生成几次 | `1` |

**帧数是硬参数。** 打包脚本有个默认帧数（8），但那个值对短周期动作会
让抽帧挤在快速相位上，直接把 `playback_qc` 判成 FAIL。必须按规格卡显式
用 `动作=路径:N` 语法指定每段帧数。

---

## 阶段 1：环境与输入体检

需要 `numpy`、`Pillow`、`ffmpeg`。ffmpeg 若不在 PATH，可用 `imageio-ffmpeg`
自带的二进制复制到可执行目录。

判断输入图能不能走 H3 路线：

- 单主体、全身、中立站姿、背景可分离 → **可走**。
- 多人同框、背景复杂、纯侧身 → 先用 `mmx` 生成单主体定妆图再走。

选键色：取主体里最常见的颜色，算它与候选键色（品红/绿/蓝）的距离，选最远
的。本次实测：品红 `#FF00FF` 与主体最近色距离 414（绿 351、蓝 351），选品红。

---

## 阶段 2：合成定妆图

H3 要的是**单帧干净图**，不是拼贴。

1. 把源精灵最近邻放大到 512×512（最近邻，不要用双线性，保留像素感）。
2. 合成到 512×512 的纯键色底。
3. 校验四角都是键色、无半透明。

保存为 `key_<角色名>.png`，后面既做 H3 首帧，也做 `--ref` 体检基准。

---

## 阶段 3：提交 H3

```bash
# 上传定妆图换 HTTPS URL
mcode-tools upload_temp_url "<绝对路径>" --mime-type image/png
# -> {"temp_url": "...", "expires_in_seconds": 3600}
```

提交参数（写成 args.json 再传，避免 shell 转义地狱）：

```json
{
  "model": "MiniMax-H3",
  "reference_type": "first_frame",
  "input_image":      {"url": "<首帧URL>", "mime_type": "image/png"},
  "last_frame_image": {"url": "<末帧URL>", "mime_type": "image/png"},
  "prompt": "<官方三字段文本，见下方模板>",
  "duration": 4,
  "resolution": "768P"
}
```

```bash
mcode-tools connector call connector__matrix__submit_video_generation \
  --args-file "<绝对路径>/submit.json"
# -> {"task_id": "..."}
```

提交是异步的，**多个角色可以并发提交、并发轮询**。同一角色的 idle 与 walk
必须串行（walk 的参考图是 idle 的首帧），不同角色之间没有依赖。串行跑 38 段
视频要 8-10 小时，并发能压到 2-3 小时。

**但 H3 同时在飞的生成任务上限是 5。** 第 6 次提交返回 429
（`video generation limit reached: you already have videos generating or
finished but not yet checked`）。槽位在任务被**查询确认完成**后才释放，所以
调度方式必须是「一波最多 5 个 → 全部轮询到终态 → 再提交下一波」，
而不是「先全提交再轮询」。

### H3 prompt 模板（idle / walk 可直接改）

```text
How the reference pictures align with the target video — Picture 1 (from Shot 1)
aligns with the 0.00-second mark of the target video; Picture 2 (from Shot 1)
aligns with the {duration}-second mark of the target video.

integrated_multimodal_description: [Shot 1] Chibi pixel-art game sprite
animation, two-head-tall proportions, crisp blocky pixels with hard pixel
edges, no anti-aliasing, flat limited-palette colors, clean dark outlines,
a front-facing static shot reproduces the chibi Chinese immortal cultivator of
Picture 1 exactly — <外形要点>. Picture 1 is the only authority on appearance:
the face, hairstyle, headwear, every garment, every colour, every prop and the
whole silhouette are copied unchanged from Picture 1 and must never be
redesigned, recoloured, simplified or restyled; the wording above only restates
what is already visible in Picture 1. Both feet on the ground, his entire body
inside the frame. The flat solid magenta #FF00FF background established by
Picture 1, a single unchanging colour that never shifts, fades or changes hue
at any point. The character's face, hairstyle, headwear, clothing, colours, body
proportions and the background colour never change at any point; the only change
is the body motion described below. <动作描述> The overall framing and scale
remain unchanged throughout. The camera holds a completely static shot. The
motion progressively returns to the same action phase, pose, layer positions,
and exact composition established by Picture 2 at the end of the shot. No
blinking, no hand gestures, no head turning, no camera movement, no additional
characters, no effects and no new props appear, no aura, no glow, no halo and no
shadow on the ground. Any prop already held stays in the same hand and keeps
its shape. The clip is completely silent.

overall_soundscape: N/A

non_diegetic_music: N/A
```

**时长用 4 秒**（H3 的下限）。6 秒的中段更容易漂，4 秒把漂移窗口压到最小。
对齐行里的秒数必须和 `duration` 一致，写成两位小数。

各动作的动作描述段：

- **idle**：一次克制的呼吸加衣摆循环，速度平稳缓慢；胸口和肩部轻微起伏，
  宽袖随呼吸轻移，头发与袍摆轻摆；双脚和下半身保持站定，角色不离开原位。
- **walk**：原地踏步循环，节奏稳定；双脚前后交替但不横向位移，身体随每步
  轻微起伏，宽袖与腿反向摆动，头发随动；角色保持在同一屏幕位置、朝向不变，
  整段只包含完整左右步态周期，末帧落在与首帧相同的着地相位。

**walk 的参考图用 idle 输出视频的第一帧**（同角色串行），这样同一个角色的
idle 和 walk 必然是同一种渲染风格。代价是 walk 的参考是一张渲染图而不是像素
定妆图，**walk 比 idle 更容易出问题**，重试额度要留给 walk。

**参考图要从原始视频取首帧，不要从处理后的视频取。** FL2VA 会把首帧钉在
参考图上，所以原始视频的第 0 帧就是「原始姿势」（实测与处理后抽出来的
`idle_ref.png` 逐像素差 0–1.7）。而处理后的视频首帧已经被切掉了冻结段，
是动作帧，拿它当参考等于让 walk 从一个已经漂移的姿势起步。

---

## 阶段 4：轮询与下载

```bash
mcode-tools connector call connector__matrix__query_video_generation \
  --args '{"task_id":"<id>","model":"MiniMax-H3"}'
# status: pending | processing | succeeded | failed
```

`mcode-tools get_asset_url <node_id>` 拿 `download_url`，再带 User-Agent
下载到目标路径（OSS 直链必须带请求头，否则 403）。

长任务用后台轮询器，**路径必须是字面量绝对路径**（见附录 A 第 5 条）。

---

## 阶段 5：源视频体检

```bash
py -3 "<skill>/scripts/make_atlas.py" --check-only --out "<输出目录>" \
  "idle=<clip>" "walk=<clip>" --ref "<定妆图>" \
  --background-mode transparent --cell-size 32 \
  --asset-kind character --pivot bottom-center --facing front
```

检查项：背景纯度、键色漂移、主体触边、首帧与定妆图的一致性。

- **WARN**（主体触边）可以接受，只要导出的单元 `spatial_qc` 是 PASS。
- **FAIL** 必须处理。

**注意：`--check-only` 的 `bg-drift` FAIL 是常态**，H3 经常把键色底换成别的
颜色。它不影响素材本身能不能用——先跑阶段 5.5 归一化，再体检。

---

## 阶段 5.5：背景归一化 + 稳定窗口（关键，别跳）

H3 两种跑偏，**成本差一个数量级**：

| 现象 | 表现 | 处理 | 成本 |
| --- | --- | --- | --- |
| 背景漂移 | 键色底变成粉/白/青，**角色没变** | 归一化底色 | 零 |
| 角色重画 | 外形、发型、衣着整个变了 | 重新生成 | 一次付费 |

**先按背景漂移处理，别急着重新生成。** 判据是归一化之后逐帧轮廓与首帧的
差异：超过主体面积 25% 才算角色重画。

归一化做四件事：

1. **底色归一**——按每帧自己的边缘主色重新抠一次，再刷回统一键色。
2. **抹掉脚下地面条**——H3 有时会画一条比身体更宽的实心横条当地面。
   特征：紧贴画面最底部、宽度超过下半身中位宽度 1.3 倍。
3. **截取外形稳定的窗口**——按轮廓差异找稳定段，优先选运动量大的。
4. **切掉首尾冻结段**——FL2VA 会把首尾帧钉在参考图上，于是每段视频都是
   「冻结的参考姿势 → 动作 → 冻结的参考姿势」。剑修的 walk 前 10 帧、后 6 帧
   逐帧差分是 0，完全冻结。混进抽帧就会让 `playback_qc` 判成 jump、
   `loop_qc` 的 seam_ratio 飙到 5.47 而 FAIL。判据用累计运动量：动作开始前
   累计量一直是 0，动作结束后不再增长，取 1% 和 99% 分位做切点。

判据阈值：主体与底色的最小颜色距离必须大于 45，否则归一化会啃掉角色上的
浅色部位（实测白底时仍安全，最小距离是 61）。

参考实现见本项目的 `scripts/prepare_clip.py`。

---

## 阶段 6：统一裁剪 + 打包

**必须先把所有动作裁到同一个画布尺寸**，各自裁剪会报
`SOURCE_CANVAS_MISMATCH`：打包脚本的 fixed 对齐要求所有动作共享同一画布。

做法：算所有动作主体的**联合包围盒**（每个动作分别算，再取并集），
四周留 8 像素余量，然后用**同一个 crop 参数**裁所有视频。

```bash
py -3 "<skill>/scripts/make_atlas.py" --out "<输出目录>" \
  "idle=<裁剪后clip>:4" "walk=<裁剪后clip>:6" \
  --fps-suggest 12 --cell-size 32 \
  --asset-kind character --pivot bottom-center --facing front \
  --max-texture-size 4096 --pad 1 --extrude 1 --edge-erode 1 \
  --loop idle,walk --quality-profile game-keyframes \
  --background-mode transparent --pixelate 32 --colors 32
```

参数说明：

| 参数 | 值 | 为什么 |
| --- | --- | --- |
| `pad` | `1` | 脚本要求 `pad >= extrude` |
| `extrude` | `1` | 透明边缘补一点同色 RGB，防引擎滤波漏色 |
| `edge-erode` | `1` | 收掉抠像残留的半透明边 |
| `quality-profile` | `game-keyframes` | 允许低帧数，min_frames=2 |
| `pixelate` / `colors` | `32` / `32` | 像素化 + 32 色硬边量化 |

单元内角色高度 ≈ `cell - 2*(pad+2)`，32 的 cell 下最高 26 像素。

---

## 阶段 7：读质检报告

产物里的 `report.json`：

```text
report.json[<动作>].loop_qc      # 接缝质量、seam_ratio
report.json[<动作>].playback_qc  # 导出序列的节奏：跳变 / 近冻结 / cv
report.json[<动作>].spatial_qc   # 脚底漂移、质心漂移、padding 侵入
report.json[<动作>].key_drift    # 键色漂移，正常是 null
```

判定阈值（脚本内 `playback_quality`）：

- 跳变门：`max(0.75, median_step * 2.0)`
- 近冻结门：`max(0.15, median_step * 0.30)`
- 接缝比门：`seam_ratio > 1.75` 直接 FAIL
- 节奏不均：`cv > 0.60` 记 issue，`cv > 0.80` 直接 FAIL

FAIL 时的处理顺序（从便宜到贵）：

1. **调帧数** — 规格卡里的帧数大概率是对的，脚本默认值才不对。
2. **调裁剪窗口** — 掐掉源视频开头/结尾的静止段。
3. **背景归一化** — 见附录 A 第 4 条。
4. **重新生成** — 消耗积分，最后手段，遵守每动作重试上限。

---

## 阶段 8：交付

必须交付：

```text
atlas.png        图集本体
atlas.json       帧索引 + pivot + qc 结论
manifest.json    引擎导入清单（帧数 / fps / 播放方式 / 锚点）
report.json      质检报告
preview.html     可播放预览页
<动作>.gif       每动作一个循环预览
frames/<动作>/*.png   单独切出的单元帧
```

再更新项目文档（放 `doc/`），写清规格、质检结果、踩过的坑。

---

## 附录 A：踩过的坑

1. **`SOURCE_CANVAS_MISMATCH`** — 多动作打包必须共享画布，用联合包围盒
   裁同一个框。
2. **`PADDING_INVALID: require pad >= extrude >= 0`** — `pad 1` 配
   `extrude 2` 会直接失败。
3. **`playback_qc` FAIL（jump transitions / loop seam spike）** — 多半是
   帧数用了脚本默认值，或抽帧挤在快速相位。按规格卡显式指定帧数。
4. **H3 把键色底漂成另一种颜色** — 这是**最常见**的跑偏，角色本身没变，只是
   底色变了。先归一化底色（零成本），**不要直接重新生成**——重生成要花积分
   而且不保证不漂。阈值要卡在「能清净背景又不伤主体上同色饰物」之间：先量
   主体上最接近底色的像素到背景的最小距离，再定阈值（实测白底时该距离是 61，
   阈值卡 30 安全）。
5. **别把背景漂移误判成角色变形** — 用固定键色抠图时，整片漂掉的背景会被
   判成前景，「与首帧的差异」会飙到接近 100%，看着像角色被换掉了。**必须
   先归一化再比较轮廓**，否则会白白浪费重试额度。
6. **H3 偶尔在脚下画一条地面横条** — 比身体宽的实心矩形，紧贴画面底部。
   会混进角色帧里，要按特征抹掉。
7. **walk 比 idle 更容易失败** — walk 的参考图是 idle 的渲染帧而不是像素
   定妆图，H3 更容易把角色整个重画。重试额度优先留给 walk。
8. **bash 写路径安全策略** — 相对路径写入会被拦截；后台轮询器里的
   `$VAR` 展开也会被拦截，必须写字面量绝对路径（`E:/...` 或 `/e/...`）。
9. **MSYS 路径转换** — 调用 skill 脚本时直接写
   `C:/Users/<user>/.minimax/...` 原生形式，MSYS 会把 `/c/Users/.../.minimax`
   误转成 `C:\Users\...` 并**吃掉 `.minimax` 里的点**，报
   `can't open file`。
10. **临时目录会被清空** — prompt / 参数 json 别放临时子目录。
11. **投影切格** — 把画布边缘留白当零区分隔带会把多个人物并进同一格，
    切点必须限定在有内容的像素范围内。
12. **透明化** — 不能按"亮度接近纯白"漫灌（会啃掉浅色脸和衣服），必须按
    连通性判定轮廓 + 补内洞。
13. **别用「只保留最大连通域」剔地面条** — 剑修的剑、散修的杖都是独立连通
    域，会被一起删掉。
14. **`Image.getdata()` 在 Pillow 13 有 DeprecationWarning** — 不影响功能，
    忽略即可。
15. **prompt 里的文字描述会覆盖参考图，把角色重画** — H3 拿「外形要点」当改造
    指令而不是复述。大乘写的是「dark brown robe trimmed with gold」，而静态精灵
    其实是暗红袖 + 棕袍 + 白靴、根本没有金色，于是 walk 生成时 H3 按文字重画：
    红袖变棕、靴子消失、袍摆外扩成锯齿裙边。**文字只能复述图里已经看得到的
    东西**，并且要在 prompt 里写明「Picture 1 is the only authority on
    appearance」。同理，别把角色身上的光环/圆弧（炼气、筑基、化神）写进描述——
    动画帧按规格卡本来就不带特效，写了反而让 H3 画上去。
16. **描述里的颜色词要跟精灵逐像素对过** — 本项目 22 个角色的初版描述表有 6 处
    硬伤（长老被写成「戴一顶软布帽」，其实秃头白须；元婴/炼虚/合体漏了黑斗笠；
    外门弟子两个的灰绿/灰棕写反了；大乘凭空多出金色）。批量跑之前必须对一遍。
17. **H3 同时在飞的生成任务上限是 5** — 第 6 次提交返回 429
    `video generation limit reached: you already have videos generating or
    finished but not yet checked`。槽位要等任务被查询确认完成后才释放，所以
    调度必须是波次式的。另外批量跑到一半失败时重跑，要复用已提交的 task_id
    （本项目记在 `<动作>.taskid.json`），否则会重复扣积分。

## 附录 B：本次实测基线

| 项 | 值 |
| --- | --- |
| 角色 | 宗主（`office1_sect_master.png`） |
| 动作 | idle 4 帧 / walk 6 帧，12 fps |
| 单元 | 32×32 RGBA，32 色，硬边 alpha |
| 图集 | 192×64（6×2 格），6.1 KB |
| 锚点 | bottom-center（0.5, 0.9375，原点左上） |
| 源视频 | H3 / 4s / 768P / 24fps |
| 质检 | idle 与 walk 的三道 QC 全 PASS，key_drift 为 null |

批量跑 22 个角色时实测：8 段源视频里 6 段靠背景归一化救回，2 段 walk 需要
重新生成——**背景归一化是这条流水线里性价比最高的一步**。

冻结段切除实测：剑修的 walk 107 帧里前 10 帧、后 6 帧逐帧差分恒为 0
（FL2VA 把首尾钉在参考图上）。切掉之后 `playback_qc` 从 FAIL（jump at [0]、
cv 1.035、seam_ratio 5.47）变成 PASS（cv 0.391、seam_ratio 1.277）。

参考脚本（本项目内）：

- `scripts/h3_batch.py` — 多角色并发跑完整链路
- `scripts/make_anim_atlas.py` — 统一裁剪 + 调 `make_atlas.py` 打包
- `scripts/normalize_key_bg.py` — 键色背景归一化
- `scripts/prepare_clip.py` — 归一化 + 地面条剔除 + 冻结段切除
- `scripts/make_sprites.py` — 从合集源图切静态精灵

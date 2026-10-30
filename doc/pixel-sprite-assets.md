# 像素小人素材

给 gamev2 用的 32×32 像素角色精灵。美术设定按仙侠方向对齐项目里的
九境（`life::Realm`）与宗门职位（`life::OfficeKind`）两套枚举。

## 目录

```
assets/sprites/characters/
├── index.png              总索引图（带中文标注，5 倍放大预览）
├── preview-realms_001.jpg  九境修士合集源图
├── preview-offices_001.jpg 宗门职位合集源图
├── preview-paths_001.jpg   四修流派合集源图
├── preview-dacheng_001.jpg 大乘单独生成的源图
├── individual/            切好的 32×32 精灵，文件名见下表
└── anim/                  宗主动画精灵图，见下节

scripts/make_sprites.py    从源图切格并生成索引图，改完它重跑即可
```

重跑切格：

```powershell
py -3 scripts/make_sprites.py
```

## 精灵清单

| 文件名 | 中文名 | 分组 |
| --- | --- | --- |
| `realm1_yinqi` | 引气 | 九境 |
| `realm2_lianqi` | 炼气 | 九境 |
| `realm3_zhuji` | 筑基 | 九境 |
| `realm4_jindan` | 金丹 | 九境 |
| `realm5_yuanying` | 元婴 | 九境 |
| `realm6_huashen` | 化神 | 九境 |
| `realm7_lianxu` | 炼虚 | 九境 |
| `realm8_heti` | 合体 | 九境 |
| `realm9_dacheng` | 大乘 | 九境 |
| `office1_sect_master` | 宗主 | 宗门职位 |
| `office2_elder` | 长老 | 宗门职位 |
| `office3_inner_m` | 内门弟子 | 宗门职位 |
| `office4_inner_f` | 内门弟子 | 宗门职位 |
| `office5_inner_f2` | 内门弟子 | 宗门职位 |
| `office6_outer_m` | 外门弟子 | 宗门职位 |
| `office7_outer_m2` | 外门弟子 | 宗门职位 |
| `office8_outer_f` | 外门弟子 | 宗门职位 |
| `office9_rogue` | 散修 | 宗门职位 |
| `path_sword` | 剑修 | 四修流派 |
| `path_alchemy` | 丹修 | 四修流派 |
| `path_talisman` | 符修 | 四修流派 |
| `path_body` | 体修 | 四修流派 |

九境那组的九个境界按格子顺序排（左到右、上到下），境界名是按提示词
指定的顺序给的；光效强弱是生成模型自己的发挥，与境界高低只大致对应。

**全部 22 个角色都已生成 idle + walk 动画精灵图**，逐个目录在
`assets/sprites/characters/anim/<角色名>/`，见下文「动画精灵图」。

## 规格

- 32×32 像素，PNG，RGBA 带透明通道，量化到 32 色。
- 人物在画布里水平居中、脚底留 1 像素基线，纵向按比例缩到 30 像素高。
- 透明区域是"人物轮廓之外、且与画布边缘连通"的部分；被深色描边围住的
  浅色衣服与脸部亮部保持不透明。

## 在 Qt 里用

```cpp
QPixmap sprite(QStringLiteral(":/assets/sprites/characters/individual/realm5_yuanying.png"));
// 像素画必须关掉平滑插值，否则放大后是糊的
sprite.setDevicePixelRatio(1.0);
painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
```

GUI 那边个体现在是按白点画的（`src/gui/views.cpp`），要换成这批精灵得
先定尺寸与锚点：64×64 格网里放 32×32 精灵是 1:1 还是 1:2 缩放。

## 动画精灵图（idle / walk）

把 `individual/` 里的角色做成带循环的动画精灵图，供游戏里待机与行走时切换。
生成链路是 H3 视频 → 抠像 → 打包。每个角色一个独立目录，目录名与
`individual/` 里的文件名一致。

### 目录

```
assets/sprites/characters/anim/<角色名>/
├── key.png               定妆图：32×32 精灵放大后合成到 512×512 品红底
├── videos/
│   ├── idle_raw.mp4      idle 段源视频（H3 原片）
│   ├── walk_raw.mp4      walk 段源视频（H3 原片）
│   ├── idle.mp4          归一化并截取稳定窗口后的打包输入
│   ├── walk.mp4          同上
│   ├── idle_ref.png      idle 的第一帧，walk 段的首帧参考
│   └── <动作>_裁剪.mp4    统一裁剪框后的打包输入
├── idle.prompt.txt / walk.prompt.txt   本次用的 H3 prompt 原文
├── idle.submit.json / walk.submit.json H3 提交参数
├── idle.task.json / walk.task.json     task_id 与 node_id
└── atlas/
    ├── atlas.png          图集本体，192×64（6 列 × 2 行）
    ├── atlas.json         帧索引、pivot、QC 结论
    ├── manifest.json      引擎侧导入清单
    ├── report.json        逐动作质检报告
    ├── preview.html       可播放的预览页
    ├── idle.gif / walk.gif
    └── frames/idle/*.png、frames/walk/*.png    单独切出的 32×32 帧

scripts/make_key_image.py   合成定妆图
scripts/h3_pipeline.py      H3 提交 / 轮询 / 下载（单个角色串行）
scripts/h3_batch.py         多角色并发跑完整链路（批量时用这个）
scripts/prepare_clip.py     背景归一化 + 稳定窗口截取 + 地面条剔除 + 冻结段切除
scripts/make_anim_atlas.py   统一裁剪 → 调 make_atlas.py 打包
scripts/normalize_key_bg.py  键色背景归一化（被 prepare_clip 调用的底层实现）
```

重跑：

```powershell
py -3 scripts/make_key_image.py <角色名>      # 合成定妆图
py -3 scripts/h3_pipeline.py chain <角色名>   # 生成 idle，再用它首帧生成 walk
py -3 scripts/h3_batch.py <角色名> [...]       # 多角色并发跑完整链路
py -3 scripts/prepare_clip.py <源> <目标>      # 单独重跑归一化与截取
py -3 scripts/make_anim_atlas.py <角色名>     # 重新打包
```

批量时用 `h3_batch.py`：同一角色的 idle 与 walk 必须串行（walk 的参考图是 idle
的首帧），但不同角色之间没有依赖，可以并发提交、并发轮询。串行 38 段视频要
8-10 小时，并发压到 2-3 小时。

**H3 同时在飞的生成任务上限是 5**，第 6 次提交会返回 429
（`video generation limit reached`）。所以 `h3_batch.py` 按波次调度：一波最多
5 个角色，一波走完 idle→walk 再开下一波。槽位在任务被查询确认完成后释放。

`h3_batch.py` 把 task_id 记在 `<动作>.taskid.json`，重跑时复用已有任务，不会
重复烧积分。生成失败、缺素材、打包失败都只影响单个角色，不中断整批。

### 额度耗尽时怎么续跑

H3 视频生成走 M Plan 周限额或账户积分，一段 4 秒 768P 约 **277 credits**。
额度用尽时提交接口返回 `402`：

```
the remaining M Plan weekly limit and account credits cannot cover this
MiniMax-H3 video generation; this task costs 277 credits
```

**费用在取回结果时才结算。** 提交成功的任务先挂在服务端，报错文案里会带出
`earlier generations have N credits in pending charges`。这些 pending 费用会占住
额度，导致后续提交一律 402——哪怕账户余额其实还够。所以一批任务提交后要尽快
把结果下载回来，额度才会释放。2026-10-30 跑第 2 波时撞上过：5 个 walk 里 4 个
已提交成功，第 5 个提交时正好有 1111 credits 挂在 pending 上，直接被 402 挡下；
把前 4 个结果取回、pending 结清后额度才放出来。

**402 是提交前的额度校验，不会扣费**，重试没有意义，脚本遇到 402 直接失败。
真正需要退避重试的是限流（429 `video generation limit reached`）。补充额度后
直接重跑同一条命令即可：

```powershell
py -3 scripts/h3_batch.py <角色名> [...]
```

已提交的 task_id 存在 `<动作>.taskid.json`，会被复用；已经有 `idle.mp4` 的角色
只补 walk。所以中断不丢进度，也不会重复扣积分。

### H3 生成的漂移与修复

H3 常常把纯色键色底换成别的颜色（品红 → 粉 → 白 → 青），**角色本身没变**。
这种是零成本可修的：`normalize_key_bg.py` 按每帧自己的底色重新抠一次再刷回
品红，修好后整段都能用，不需要重新生成。

只有 H3 把角色整个重画（外形、发型都变了）时才需要重新生成。判据是归一化
之后逐帧轮廓与首帧的差异：超过主体面积 25% 就算重画。`prepare_clip.py` 会
自动找外形稳定的窗口并截取，截取后仍不足 24 帧的才判为不可用。

实测 8 段里 6 段靠归一化救回，只有 2 段 walk 需要重新生成——**walk 比 idle
更容易出问题**，因为它的参考图是一张渲染图而不是像素定妆图。

另外 H3 有时会在角色脚下画一条比身体更宽的实心横条当地面，`prepare_clip.py`
会按「紧贴画面底边、宽度超过下半身中位宽度 1.3 倍」的特征把它抹掉。

**FL2VA 会把视频首尾帧钉在参考图上**，于是每段视频的结构都是
「冻结的参考姿势 → 动作 → 冻结的参考姿势」。剑修的 walk 前 10 帧、后 6 帧
逐帧差分恒为 0，混进抽帧会让 `playback_qc` 判成 jump、`loop_qc` 的 seam_ratio
飙到 5.47 而 FAIL。`prepare_clip.py` 用累计运动量的 1% / 99% 分位把首尾冻结段
切掉。

**prompt 里的文字描述会把角色重画。** 大乘的描述里写了
「dark brown robe trimmed with gold」，而静态精灵其实是暗红袖 + 棕袍 + 白靴、
根本没有金色，于是 walk 生成时 H3 按文字重画了角色。`h3_pipeline.py` 的
`DESC` 表已按精灵逐帧核对重写，并且在 prompt 里写明「Picture 1 is the only
authority on appearance」。改 `DESC` 之前必须先对一遍精灵图。


### 规格

| 项 | 值 |
| --- | --- |
| 动作 | idle 4 帧、walk 6 帧，都是 forward loop |
| 帧率 | 12 fps，单帧 83 ms |
| 单元 | 32×32 RGBA，量化 32 色，nearest 采样 |
| 角色尺寸 | 单元内 20×24，底边对齐 |
| 锚点 | bottom-center（0.5, 0.9375，原点左上） |
| 朝向 | front 单向，不镜像 |
| 图集 | 192×64，远低于 4096 纹理上限 |
| 透明 | 硬边 alpha，非透明像素全为 255 |

### 质检

三道门全过：源视频体检、导出序列的 `loop_qc` / `playback_qc` / `spatial_qc`。

| 动作 | loop_qc | playback_qc | spatial_qc | key_drift |
| --- | --- | --- | --- | --- |
| idle | PASS（seam_ratio 1.197） | PASS（cv 0.106） | PASS | 无 |
| walk | PASS（seam_ratio 0.694） | PASS（cv 0.293） | PASS | 无 |

`spatial_qc` 的 baseline_drift 为 0、centroid_drift ≤ 0.2 像素，脚底不飘。

**三道门有盲区：特效被画进角色包围盒内部时全部失效。** `spatial_qc` 只查角色
bbox **之外**的污染，`worst_bg_purity` 也是同一个口径。炼气（`realm2_lianqi`）
有一段 walk 被 H3 画了一个巨大的白色光盘把角色整个包住，光盘整个落在 bbox 内部，
于是被当成角色本体——`loop_qc` / `playback_qc` / `spatial_qc` 三道门全 PASS，
`worst_bg_purity` 还是 0.9903，比 `office1_sect_master` 的 0.9459 还"干净"。
肉眼一看就露馅。

所以 QC 全 PASS **不等于**画面合格，成品必须逐个用对照表看
（`agent-temp/identity_sheet.py <角色名>`，静态精灵与动画帧并排）。这条不补进
自动门禁，是因为光盘和角色连成一体，靠连通域很难在不伤到角色的前提下分开。
根治要落在上游：参考图里不能有特效。

### 踩过的坑

- **H3 会把键色底漂掉。** walk 段生成出来背景从品红变成绿底（150/158 帧
  被判坏帧）。角色本身是干净的，只是底色变了，所以先用
  `normalize_key_bg.py` 把底色归一化回品红，而不是重生成。
- **帧数必须是 4 / 6。** 用脚本默认的 8 帧时，walk 的抽帧落点会挤在
  快速相位上，接缝跳变导致 `playback_qc` FAIL；降到 6 帧后各项全 PASS。
- **两段视频必须用同一个裁剪框。** 打包脚本的 fixed 对齐要求所有动作
  共享同一画布尺寸，各自裁剪会报 `SOURCE_CANVAS_MISMATCH`。
- **`pad` 必须 ≥ `extrude`。** `pad 1 + extrude 2` 会报 `PADDING_INVALID`。
  脚本内部还要再留 2 像素安全边，所以 32 的单元里角色最高只能到 26 像素，
  比静态精灵的 30 像素略矮。
- **FL2VA 会把首尾帧钉在参考图上。** 每段视频都是「冻结的参考姿势 → 动作 →
  冻结的参考姿势」。冻结段混进抽帧会让 `playback_qc` 判成 jump、
  `loop_qc` 的 seam_ratio 飙到 5.47。`prepare_clip.py` 用累计运动量的
  1% / 99% 分位切掉首尾冻结段。
- **prompt 里的文字描述会把角色重画。** 大乘的描述写了「dark brown robe
  trimmed with gold」，而精灵是暗红袖 + 棕袍 + 白靴、根本没有金色，H3 就按
  文字重画了角色。描述只能复述图里看得到的东西，并且 prompt 里要写明
  「Picture 1 是造型的唯一权威」。
- **参考图里的特效会被 H3 放大成主角。** 炼气的静态精灵背后有一圈白色光晕，
  拿没剔过光环的定妆图当参考图，H3 就把它当成角色的组成部分，画成一大片白盘
  把人包住。`make_key_image.py` 现在会在定妆图层剔掉光环（炼气 / 筑基 / 化神
  三个有光环的角色），walk 的参考图则从 idle 的首帧取——前提是那段 idle 也是
  用剔过光环的定妆图生成的。顺序反了就会复现这个问题。

### 在 Qt 里用

```cpp
// 按 manifest.json 的 x/y/w/h 从 atlas.png 里切帧，用 frames/ 下的单帧更简单
QPixmap sheet(QStringLiteral(":/assets/sprites/characters/anim/atlas/atlas.png"));
// 像素画必须关掉平滑插值
sheet.setDevicePixelRatio(1.0);
painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
```

`atlas.json` 里每帧都带 `x/y/w/h`、`source_frame`、`frame_file` 和 `pivot`，
直接照抄进引擎的精灵表即可。

## 已知限制

- 全部由 mmx（`image-01`）生成，不是手绘像素，笔触偏软；放大到 6 倍以上
  能看出它是缩放图而不是原生像素。
- 与人物分离的光效（金环、飘散符纸、火焰）在切格时会被裁掉。要保留得单独
  出一层特效素材。
- 九境组的大乘是单独生成的源图：合集里那一格模型画成了金色光球，不是人。
- 动画是 H3 视频生成的，不是手绘帧。H3 对同一张定妆图每次的风格发挥会
  漂：这次 idle 段被渲染成精致立绘、walk 段保持像素原画风，靠打包阶段的
  32×32 像素化把两者拉回同一套像素语言，但细节量仍有差异。
- idle 段源视频体检是 WARN：宽袖在 36/158 帧里蹭到源画面边缘。导出后的
  单元通过 `spatial_qc`，没有内容被裁进 padding，实际使用不受影响。
- 少数深色角色（炼虚）的轮廓边缘会留下 1–2 个偏品红的像素，是视频压缩把
  角色色和键色底混出来的。原生 32×32 下几乎看不见；把键色距离阈值往下调会
  啃到角色自身的浅色部位（长老的脸、元婴的粉颊），所以没有做全局去边。

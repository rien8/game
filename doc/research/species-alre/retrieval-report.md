# 检索报告：Species: Artificial Life, Real Evolution

- 检索日期：2026-10-14
- 检索执行：多渠道网络检索 + 权威数据源直接抓取
- 研究目标：系统收集该游戏的**基本信息、游戏机制、技术实现、开发历史、玩家评价与当前状态**，为后续分析与二次开发参考提供可信底稿

---

## 一、检索策略

| 项目 | 内容 |
|------|------|
| 关键词组 | `"Species: Artificial Life, Real Evolution" Steam`、`物种 人造生命 真实进化`、`speciesdevblog roadmap 0.15`、`steamdb 774541`、`Monogame XNA Quasar`、`基因 育儿所 生态 物种形成`、`Planetary Life comparison` |
| 检索渠道 | Steam 商店页与 Web API、SteamDB、Steambase、SteamyData、PlayTracker、Indiegogo 官方项目页、官方论坛、MobyGames、玩家评测（多语种）、更新日志/开发日志归档 |
| 时间范围 | 全时段（2012 首个公开版本 → 2026-10 现状） |
| 交叉验证 | 价格/评测数/平台/语言以 Steam 官方 API + 商店页 HTML 为准；并发人数/构建时间以 SteamDB 为准；机制细节以官方更新日志与商店页文案为准；评价以玩家原文为准 |

---

## 二、核心发现

### 1. 基础档案 [已确认]

**来源详情**：
- 标题：Species: Artificial Life, Real Evolution on Steam
- 平台：Steam Store（App ID 774541）
- 位置：页面右栏 "Title / Genre / Developer / Publisher / Release Date" 区块
- 链接：https://store.steampowered.com/app/774541/
- 抓取日期：2026-10-14

**内容摘要**：
- 开发 / 发行：Quasar（独立开发者 James Schumacher，论坛昵称 Quasar）
- 类型：Simulation（28）、Early Access（70）
- Steam 发售日：2018-09-28（UTC 22:51:44）
- 平台：Windows / macOS / Linux
- 语言：**仅英语（界面）**，无中文
- 功能：单人、家庭共享（Family Sharing）
- 输入：键鼠。**无手柄支持**（bFullXboxControllerSupport=false）
- 无障碍功能：**全部标记为不支持**（无字幕、无色彩替代、无难度选项等）
- Steam Deck：Playable（2022-08-31 测试，build 4805084）
- 官网：http://www.speciesgame.com ；支持页：http://www.speciesgame.com/forum/

**可信度**：高 — 来自 Valve 官方商店页与 Web API 双通道。

---

### 2. 定价与销量估算 [已确认 / 部分待验证]

**定价**（来源：Steam Web API `appdetails`，2026-10-14）

| 货币 | 价格 | 货币 | 价格 |
|------|------|------|------|
| 中国人民币 | ¥ 70.00 | 美元 | $19.99 |
| 欧元 | €16.79 | 英镑 | £15.49 |
| 港币 | HK$89.00 | 新台币 | NT$318 |
| 日元 | ¥2050 | 韩元 | ₩20500 |

- 折扣情况：`discount_percent: 0`，当前无折扣
- SteamyData 记录「0 sale periods observed」，即该作**长期未参与任何促销**

**销量估算**（来源：SteamDB / SteamyData / PlayTracker，2026 年读取）

| 估算方 | 估算持有量 |
|--------|-----------|
| VG Insights | ~18.1 k |
| Gamalytic | ~30.7 k |
| PlayTracker | ~51.9 k（区间极宽，自述 90% 置信内 ±154 k） |
| SteamyData | 中位数 ~30 K |

**收入区间估算**（来源：SteamyData）
- 毛收入：$13K – $102K（中等置信度）
- 净收入：$9K – $66K（扣平台分成后）
- 明确说明「底层模型互相不一致，因此区间较宽」

**可信度**：价格 = 高；销量与收入 = **低（第三方模型估算，互相矛盾，仅作数量级参考）**

---

### 3. 技术栈 [已确认]

**来源详情**：
- 标题：Species: Artificial Life, Real Evolution — SteamDB 页面
- 位置："Technologies" 字段
- 链接：https://steamdb.info/app/774541/
- 抓取日期：2026-10-14

**内容摘要**：
- 检测到的技术栈：**MonoGame Engine、XNA Engine、FreeImage SDK、NVIDIA Texture Tools SDK、SDL SDK、SteamworksNET SDK、dotnet SDK**
- 技术演进：项目最初基于 **Microsoft XNA Game Studio**（Indiegogo 页面自述），后迁移至 **MonoGame**
- 0.14.x 技术基线：**基于 .NET Core 与 MonoGame 3.8**，与旧版本有本质差异
  [来源: 0.14.1 发布公告，更新日志 | Quasar | 2022-08-31]

**运行体量**（来源：SteamDB Depots）
- Windows Depot：1.52 GiB（磁盘占用），下载 352.49 MiB
- Linux Depot：1.54 GiB，下载 353.20 MiB
- macOS Depot：2.02 GiB，下载 353.07 MiB
- 三平台合计磁盘 5.08 GiB
- 0.14.0 发布时官方自述体积为 **687 MB**（跨平台打包所致）[来源: 0.14.0 公告，2022-08-14]

**存档**
- **未配置 Steam Auto-Cloud**，存档位于 `[Steam 安装目录]/userdata/{Steam3AccountID}/774541/`
- 用户数据配额：9.54 MiB（10,000,000 字节），最多 100 个文件
  [来源: SteamDB "User File System" 区块]

**可信度**：高

---

### 4. 模拟核心与 AI 架构 [已确认]

**来源详情**：
- 标题：官方更新日志 / 开发日志（经 tuxdb 归档全文）
- 位置：0.13.2.1、0.14.0、0.14.1、0.14.2.x 各版本 Changelog 与公告
- 链接：https://speciesdevblog.wordpress.com/ ；存档 https://www.tuxdb.com/game/6088&gamenews=1

**内容摘要**：

| 能力 | 证据 | 日期 |
|------|------|------|
| 多线程模拟 | 0.12.0 引入多线程原型；后续版本多次修复"multithreading-related crash"（Behaviour Tree 并发修改、目标置空等竞态） | 2019 起 |
| Behaviour Tree AI | 崩溃日志明确提及 creature 修改其 Behaviour Tree 与 combatInstance 的并发访问 | 2020-09-16 |
| AI 性能优化 | 0.14.1.9：Creature AI 运行速度提升约 **17%** | 2022-09-13 |
| 对象内存池化 | 0.14.1.12：对生物、尸体、蛋、树等对象做"substantial refactoring"，改用内存池 | 2022-09-28 |
| 内存泄漏修复 | 0.14.1.14：修复 0.14.1.12 引入的内存泄漏，该泄漏导致连续冻结最终导致无响应 | 2022-10-02 |
| 性能与稳定性耦合 | 官方明示"低性能下仍会发生已知崩溃，建议保持两位数 FPS，最有效的方式是**降低生物数量上限**" | 2022-08-31 |
| Thought Log | 选中生物后按 `T` 显示思考日志；原为调试工具，官方称"给了生物声音"，计划未来正式化 | 2022-08-31 |

**可信度**：高 — 均引自开发者本人发布的更新日志原文。

---

### 5. 游戏机制 [已确认]

**核心三原则**（来源：Steam 商店页 "About This Game"）
- **Variation**：每个生物由其基因唯一定义
- **Mutation**：子代是父代随机微调的版本
- **Natural Selection**：环境作用于每个生物的生存斗争

**明确的设计立场**（来源：Indiegogo 项目页 Creator Story）
> "No fitness function, no predefined goals: just survival. The player can aid and interact with the simulation, but they will never control it."
>
> 无适应度函数、无预设目标，只有生存。玩家可以干预和交互，但**永远不会控制**模拟。

**核心系统**
- 世界生成：随机生成生物群系（biomes）、高度图与肥力图
- 育儿所（Nursery）：隔离区域，供玩家改造基因库；含 **Gene Cycling**（逐基因人工选择，其余自由突变）与 **TURBOCHARGE**（大幅加快蛋/幼体/妊娠生长）
- 漫游车（Rover）：决定哪些生物今天能进食、哪些饿死
- 灾难系统：冰河时代、全球洪水等
- 数据视图：**Clade Diagram（进化枝图）、Satellite Map（卫星地图）、Web of Life（生命之网）、Creature Inspection（生物检视）、Population History（人口历史）**
- 时间加速：`~` / `1` / `2` / `3`

**基因与繁殖**（来源：0.14.0 / 0.14.1 公告，2022-08）
- 新增两个基因：**Egg Length（卵长）与 Pregnancy Length（妊娠期）**，二者较大者决定卵生或胎生
- 卵可被打碎食用（会激怒亲代），是通往食肉行为的"gateway food"
- 妊娠消耗的能量与时间**大幅增加**，成为对觅食能力的强筛选
- 已知基因还包括：Head Type 的 "Eat Rate"（影响氧气吸收）、饮食偏好、尾部属性等

**环境与生态**
- 季节系统：湿季/旱季（Wet/Dry season）循环；官方 2019 年专门发布 "'Tis the Seasons"
- 树木提供遮荫，抵消冷热温度最多 10 °C，向 20 °C 均值偏移
- 水生/陆生两类体型；涨落潮存在但作用有限
- 施肥笔刷、温度笔刷等地图编辑器工具（0.13.0 的重点更新）

**玩家干预工具**（来源：Indiegogo 页 + 评测工具栏）
- 喂食 / 杀死 / 采样基因并拼接进其他个体基因组 / 辐照（导致后代高变异）
- **Play as Creature**（0.13.1.0 experimental，2020-06）：以选中的生物视角游玩，可追踪其直系亲属（父母、兄弟姐妹、后代）

**可信度**：高

---

### 6. 开发历史时间线 [已确认]

| 时间 | 事件 | 来源 |
|------|------|------|
| 2007 起 | 开发者自述项目"以某种形式存在了约 8 年"，2011 年起持续开发 | Indiegogo 项目页 |
| 2012-05-27 | 早期 0.x Alpha 版 Windows 发布（官网免费发放，需 XNA 3.1 运行库） | MobyGames（ID 121512） |
| 2013 | Indiegogo 众筹（灵活筹款 Flexible Funding）。目标 $25,000，**实际仅募得 $1,016 / 43 位支持者**；档位 $10–$200。开发者明确表态"即使赔钱也要把游戏做出来" | Indiegogo 项目页 |
| 2017-12-19 | 首次出现在 SteamDB 记录中 | SteamDB |
| 2018-09-28 | Steam 抢先体验上线 | Steam 官方 |
| 2018-11-21 | 0.11.2.0：macOS 版本发布 + 大量平衡性调整 | 更新日志 |
| 2019-04~09 | 0.12.0.x：修复长期存在的 `ArithmeticException` 崩溃（"最常见的崩溃原因"）、多线程原型、Quickstart 与四段式 New World UI | 更新日志 |
| 2020-06-23 | 0.13.1.0 experimental：Play as Creature | 更新日志 |
| 2020-09-16~17 | 0.13.2.0/1/2：捕食行为大改（潜伏狩猎）、多线程崩溃修复 | 更新日志 |
| **2022-06-19** | **开发者发布回归公告**：说明此前长期中断的原因（自述遭受未经治疗的精神疾病，现已接受治疗并找到合适药物），致歉并承诺回归 | 公告 "I'm back" |
| 2022-08-14 | 0.14.0 experimental：蛋与怀孕机制、MonoGame 3.8 迁移 | 更新日志 |
| 2022-08-31 | 0.14.1 进入 stable 分支 | 更新日志 |
| 2022-09-05~10-02 | 0.14.1.2 ~ 0.14.1.14：性能、育儿所、崩溃与内存泄漏密集修复 | 更新日志 |
| 2022-10-10 | 0.14.2.0 experimental：**开启本地化 mod 支持** + 新增 3 首音乐（社区用户 Poliostasis 制作） | 更新日志 |
| 2022-10-24 | 发布 **"Where to next? — 0.15.0"** 规划：habitat 系统（疲劳/睡眠/温度/暴露整合为生存需求）、重做植被模型、**可进化树木**（第二高需求）、**飞行**（第一高需求） | 更新日志 |
| 2022-11-07 | 0.14.2.3：修复 macOS 启动崩溃等 | 更新日志 |
| 2022-11-25 | 0.14.2.5：修复读档时蛋相关崩溃 | 更新日志 |
| （0.14.2.6） | 最后一版有记录的更新：补充情绪名、AI 例程名、能量变化等本地化字符串 | 更新日志 |
| **2023-05-20** | **最后一次 public / experimental 分支构建（build 11276207）** | SteamDB Branches 区块 |

**可信度**：高

---

### 7. 开发中断与当前状态 [已确认 / 部分待验证]

**已确认事实**
- Steam 商店页 Early Access 区块显示提示：**"The last update made by the developers was over 3 years ago."**（开发者最后一次更新在 3 年多以前），并注明页面上的信息与时间表可能已过时
  [来源: Steam 商店页 Early Access Body 区块，抓取于 2026-10-14]
- SteamDB Branches 显示 public 与 experimental 分支的**最后构建时间均为 2023-05-20 00:43 UTC**，构建号同为 11276207
- 商店页资产（Store Asset Modification Time）最后修改于 **2019-10-06**，即上线一年后几乎未再维护
- Steam Deck 兼容性测试时间为 2022-08-31，此后未再更新

**玩家社区判断**
- Steam 社区与评测中，**大量玩家已将其视为 abandonware**：
  - "This game can be considered abandonware at this point. It's been in early access for over 8 years now and the developer has gone missing with no game or news updates posted for the last 4 years."
  - "It had a lot of potential but will never reach it."
  - "An interesting concept that never really evolved and was abandoned by the devs."
  [来源: Steam 商店页用户评测区 | 各评测发布于 2022-07 之后]
- 2025-11-10 Steam 讨论帖中，玩家建议改买 **Planetary Life**，理由是"能做同样的事，但做得更好，且价格不到一半" [来源: steamcommunity.com/app/774541/discussions]

**可信度**：构建时间与更新状态 = 高；"开发者是否彻底离开" = **待验证**（未直接抓取官网/论坛/开发日志确认其当前在线状态）

---

### 8. 玩家数据 [已确认]

| 指标 | 数值 | 来源 |
|------|------|------|
| 历史最高同时在线 | **257 人（2019-01-15）** | SteamDB |
| 24 小时峰值（近期） | 5–6 人 | SteamDB |
| 当前在线 | 2–6 人 | SteamDB / Steambase |
| 月度峰值趋势 | 2019-01: 257 → 2020-02: 87 → 2022-08: 49 → 2024-09: 15 → 2025-07: 10 → 2026-08: 9 | SteamDB Charts |
| 社区关注者 | 12,149 – 12,151 | SteamDB / Steambase |
| 评测总数 | 1,004 – 1,005 | Steam 官方 / Steambase |
| 好评 / 差评 | 741 / 263（约 **74%**，多半好评 Mostly Positive） | Steambase |
| SteamDB 评级 | 70.68%（740 正 / 265 负） | SteamDB |
| 平均总游戏时长 | 9.7 小时（中位数 2.5 小时） | PlayTracker |
| 鉴赏家评测数 | 14 位 | Steam 商店页 |

**玩家画像**（来源：PlayTracker）
- Veteran Collector 76% ｜ Indie Connoisseur 74% ｜ Deep Diver 68% ｜ Nomad 62%
- 无成就、无创意工坊（Workshop: No），Popularity score 为 0

**用户标签热度**（来源：Steam 商店页，2026-10-14）
Simulation 97 ｜ Sandbox 82 ｜ God Game 60 ｜ Early Access 59 ｜ Science 58 ｜ Experimental 40 ｜ Indie 38 ｜ Real-Time with Pause 35 ｜ Singleplayer 34 ｜ Relaxing 30 ｜ Strategy 22 ｜ Management 22 ｜ Real-Time 21 ｜ Education 18 ｜ Psychological Horror 9 ｜ Open World 7 ｜ Realistic 7 ｜ Nature 6

**可信度**：评测数、并发数、时长 = 高；玩家画像 = 中（第三方模型）

---

### 9. 评价分析 [已确认]

**正面评价**
- 进化模拟的真实性与细节量受到认可
- 世界参数可深度自定义
- "观察或干预"的双重模式设计得好
- 音乐获得好评（含社区投稿曲目）
- 适合长时被动观赏，节奏舒缓
- 中文评测亦承认"自然环境比较丰富，从荒芜的岩地到茂密的丛林，应有尽有，且有演替系统"，题材新颖

**负面评价（高度一致）**
- 未完成 + 大量 bug：崩溃、无响应、内存泄漏
- 高生物数量下性能问题（早期版本有"保留 30GB 内存"的严重内存泄漏报告）
- UI 难以理解，不够直观
- **仅英语，无中文**（中文区玩家反复提及）
- 动物模型有限、形态重复
- 地形与气候对动物的影响有限
- 战斗与捕食-被捕食机制存在问题

**最尖锐的技术批评**（来源：Steam 评测 | 用户 MinMin | 2022-07-31 | 42 人标记有价值）
> 自然界形成新物种是**先突变**、适宜突变保留并积累到一定程度后与原物种相差过大，或直接地理隔离产生。
> 游戏内则是**先产生新物种再突变**——一旦某个个体与原物种产生差异就自动形成新物种，于是产生生殖隔离，使许多进化树上的新物种**只有一个个体样本**，无法繁殖或繁殖困难而导致灭绝。
>
> 生态位设置太过单薄，只有肉食、草食，水生、陆生。草食动物中没有食性区分，不会演化出分别吃种子、草、树叶的新物种……是草通吃，冷热也通吃，导致一片大陆基本上只会有一种草食动物稳定存在，**没有种间竞争**。

另一则中文评测指出：游戏自带的水生初始物种因游泳速度与耐力参数失衡，会在开局约十分钟内集体饿死。

**可信度**：高（均为玩家原文）

---

### 10. 开发团队与社区 [已确认]

- **James Schumacher（Quasar）**：独立游戏程序员，16 岁起写独立游戏，5 年职业经验 + 7 年业余经验；自述还有 7 年在互联网上与创造论者辩论的经历，两个兴趣结合成了 Species 项目
  [来源: Indiegogo 项目页 "Who are you people?"]
- **Jade Taggart（CrashKat）**：自由数字艺术家与平面设计师，负责环境、生物、UI 及计划中的载具与建筑概念
- **Brain Sugar**：音频
- Indiegogo 筹资用途：维持开发团队生活约一年、网站与论坛托管、音频、美术设计、游戏扩展
  [来源: Indiegogo 项目页]
- 社区深度参与设计的传统可追溯到 2015 年：官方论坛用户 "Therminator" 提交了长达数页的自制生物 AI 行为算法方案（基于 0.7.0 版本），包含感知系统、能量优先级、繁殖驱动等模块化设计建议
  [来源: speciesgame.com/forum/viewtopic.php?t=1208 | 2015-04-17]
- 社区翻译者：s.t.andgo、magmacube（0.14.2.6 更新日志中点名致谢）
- 众筹承诺中的"多人共享地图"（把玩家地图物理连接、合并算力形成超大世界）**至今未实现**

**可信度**：高

---

### 11. 同类作品对照 [已确认]

| 作品 | 年份 | 说明 | 来源 |
|------|------|------|------|
| **Planetary Life** | 2025-08-15 抢先体验 | 开发商 Sotenbox。Steam 评分 9.4。融合 SimEarth 与 Spore 机制，可操控板块运动、地核温度、气候、火山、大气，反馈到文明演化。**被玩家普遍推荐为 Species 的替代品，且更便宜** | gamepressure.com；Steam 讨论帖 |
| **Thrive** | 2021 | 生命模拟/进化类，玩家行为实验导向 | GameOn 相似度推荐 |
| **Spore / Spore: Galactic Adventures** | 2008 / 2009 | 最常被类比的参照物（多语种评测与媒体评价中反复出现），但 Species 被评价为"Spore 但更恐怖" | Vinesauce Fandom；多语种评测 |
| Dino Nest / Green Project / PolyPine 等 | 2020–2026 | 生态/进化题材邻近作品 | GameOn 推荐列表 |

**可信度**：中（"更好"属主观评价；作品的发布时间与评分等硬信息为高）

---

## 三、信息缺口

以下内容**未找到可靠资料**，不应在后续分析中臆测：

1. **官方销量与收入数据** —— 开发者从未公开；第三方模型估算互相矛盾（18k / 30k / 52k），仅能作数量级参考
2. **开发者当前状态与官方渠道现状** —— 官网 speciesgame.com、开发日志 speciesdevblog.wordpress.com、论坛均**未直接抓取验证**是否仍在线；最后一次官方表态的确切时间未知
3. **0.15.0 之后的实际进度** —— 仅能确认 2022-10 有 habitat / 可进化树木 / 飞行的规划公告，无任何后续实现证据
4. **当前版本的确切版本号** —— 日志可见 0.14.2.6，但 SteamDB 显示最后构建为 2023-05-20，两者是否对应同一版本未核实
5. **是否有玩家 Mod 生态** —— Steam 创意工坊无内容；0.14.2.0 开放了本地化 mod 机制，但实际社区 mod 数量无数据
6. **成就系统** —— 各数据源均未列出，疑似无成就
7. **游戏内基因/行为的完整参数表** —— 散见于更新日志与玩家评测，无官方完整文档；曾检索到社区自制的 AI 算法设计提案（非官方）
8. **媒体评分** —— Steam 商店页引用了 Games Finder 的 8.0 分，但未找到可核验的原始页面；未找到 Metacritic 等聚合站评分
9. **多人/联机功能** —— Indiegogo 曾作为愿景提出（共享地图），此后无任何实现证据

---

## 四、参考来源（本次检索实际访问/引用）

| 序号 | 类型 | 名称 | 位置 | 链接 |
|------|------|------|------|------|
| [1] | 官方商店页 | Species: ALRE on Steam | About This Game / Early Access / 评测区块 | https://store.steampowered.com/app/774541/ |
| [2] | 官方 API | Steam appdetails (appid=774541) | 全字段 | https://store.steampowered.com/api/appdetails?appids=774541 |
| [3] | 数据站 | SteamDB | Technologies / Branches / Charts / Depots | https://steamdb.info/app/774541/ |
| [4] | 数据站 | Steambase | 评测与并发趋势表 | https://steambase.io/app/774541 |
| [5] | 数据站 | SteamyData | 收入区间估算 | https://steamydata.com/app/774541/ |
| [6] | 数据站 | PlayTracker | 玩家画像与时长 | https://playtracker.net/insight/game/26422 |
| [7] | 官方 | Indiegogo 项目页 | Creator Story / 团队 / 档位 / 操作说明 | https://www.indiegogo.com/projects/jamieschumacher/species-artificial-life-real-evolution |
| [8] | 官方日志 | Species 官方更新日志（归档全文） | 0.11.2.0 – 0.14.2.6 | https://www.tuxdb.com/game/6088&gamenews=1 |
| [9] | 官方日志 | Species 开发博客 | 多篇公告 | https://speciesdevblog.wordpress.com/ |
| [10] | 官方论坛 | Behaviour algorithms（社区 AI 方案） | 2015-04-17 帖 | https://speciesgame.com/forum/viewtopic.php?t=1208 |
| [11] | 玩家评测 | Steam 评测区（中/英/俄多语种） | MinMin 等多篇 | https://store.steampowered.com/app/774541 |
| [12] | 社区讨论 | Steam 讨论区（推荐 Planetary Life） | 2025-11-10 帖 | https://steamcommunity.com/app/774541/discussions |
| [13] | 数据库 | MobyGames | 2012 年版本记录 | https://www.mobygames.com/game/species-artificial-life-real-evolution |
| [14] | 媒体/百科 | Vinesauce Fandom Wiki | 游戏介绍与视频记录 | https://vinesauce.fandom.com/wiki/Species:_Artificial_Life,_Real_Evolution |
| [15] | 对照作品 | Planetary Life（Gamepressure） | 2025-08-15 | https://gamepressure.com/games/planetary-life/z76ec9 |

> 完整来源汇总表见 `sources.md`。
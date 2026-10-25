# Species: Artificial Life, Real Evolution 游戏信息调研报告

**一款基于自然选择原理的人工生命进化模拟器：产品概况、科学机制、开发现状与口碑分析**

**报告日期：2026年10月14日**

## 摘要

《Species: Artificial Life, Real Evolution》是由独立开发者 Jamie Schumacher（网名 Quasar）自 2007 年启动、2018 年 9 月 28 日以抢先体验形式登陆 Steam 的自然选择进化模拟器，标准售价 19.99 美元，历史最低价 9.99 美元。本作不依赖预设脚本或适应度函数，而是以变异、突变、自然选择三条原理构建模拟：每个生物由基因唯一定义，子代是亲代的略微随机修改版本，环境不设定目标、只以生存本身作为筛选压力，因此每次运行都会自行演化出包含种群爆发、物种形成和灭绝的不可复现历史。玩家可在纯观察者与主动干预者之间切换，可用工具包括用于隔离实验基因库的托儿所、可决定喂食或碾死生物的漫游车、可触发冰河时代与全球洪水的环境剧变，以及基因取样拼接、辐射诱发突变，配合进化枝图、卫星地图、生物检查工具形成谱系、空间、个体三个维度的分析能力。

产品现状是本次调研权重最高的结论：项目已长期停滞。最新公开版本 v0.14.2.0 仅在实验分支启用本地化 Mod 支持、无玩法变更，开发者博客末次更新停在 2022 年 11 月 25 日的翻译 Mod 教程，社区确认此后无任何补丁；即便曾发布题为"I'm back"的回归与致歉声明，此后仍无实质更新，距上架超过 8 年仍未脱离抢先体验。众筹期承诺的七项功能中，只有 Mod 支持得到有限兑现，飞行能力、水体环境、可变温湿度系统、迁徙森林、共生与寄生、在线多人连接六项未见实现记录。

口碑呈两极集中：正面评价全部围绕科学准确性，被社区称为"《孢子》的科学解药"、"基于生物学的优秀电子游戏"，适合理解遗传漂变与物种形成，并获 Vinesauce 直播、Reddit 长期追随者、开源项目 Thrive 开发日志对照及 Kotaku、IGN、Rock Paper Shotgun 收录；负面评价集中在频繁崩溃导致进度丢失、冥想式节奏缺乏任务引导、仅英语界面无官方中文三项。Steam 955 篇评测中 74% 为好评，Metacritic 暂无评分。

市场数据呈现单峰衰减：历史最高同时在线 257 人（2019 年 1 月 15 日），当前仅约 2 人、24 小时峰值 4 人；Twitch 历史最高观众 10,161 人，约为最高在线玩家的 39 倍，显示观赏价值远高于游玩转化力。三家机构估计拥有者数量介于 18,100 至 51,900 人，口径差异近 3 倍，应作区间参考。配置方面图形门槛极低（Windows Vista、2 GHz、Shader Model 3.0）但内存需 8 GB，约 79.2% 的玩家设备可满足最低要求，推荐配置始终未公布；Steam Deck 可运行但基因修补类精细操作依赖虚拟键盘。

同类品类并未随本作停滞而萎缩：Evolution of Species 2: Online、Ecosystem、人工生命：动作学习进化论、Substrate: Emergence 四款作品发售时间均晚于 2022 年 11 月且全部支持中文，题材向水生、微观、对抗、机器学习四个方向分化，入侵种（INVADER）则引入 AI 自然语言造物。资料给出的建议是：追求深度科学模拟者可接受其缺陷，期待完整体验或稳定运行者应转向替代品如 Ecosystem。

## 1. 游戏概况与产品定位

《Species: Artificial Life, Real Evolution》（简称 Species: ALRE）是一款以进化科学第一性原理为设计内核的自然选择模拟器，由独立开发者 Jamie Schumacher（网名 Quasar）主导开发[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。游戏于 2018 年 9 月 28 日以"抢先体验"（Early Access）形式在 Steam 平台发售，标准售价为 19.99 美元[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。在 Steam 商店页的类型标签中，它同时被归入模拟、沙盒、抢先体验、上帝模拟、科学、教育六个类别[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，这一标签组合本身就说明了产品的双重身份：既是可供游玩的沙盒模拟游戏，也是可用于理解进化概念的教育工具。

从产品定位看，本作最具辨识度的标签来自社区评价——"《孢子》（Spore）的科学解药"[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。这一定位的成立前提在于两者的机制差异：《孢子》中的生物自定义属于装饰性系统，玩家拼装的外观并不改变生存规则；而本作的生物形态变化是真实模拟的结果，形态差异直接关联生存能力[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。换言之，本作的卖点不是"创造生物的乐趣"，而是"观察真实选择压力作用于随机变异的过程"。

**表1：Species: ALRE 基础属性一览**

| 属性项 | 详情 |
| :--- | :--- |
| 游戏全称 | Species: Artificial Life, Real Evolution[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 中文译名 | 物种：人造生命，真实进化 / 物种：人工生命，真实进化[3DM网游](https://dl.3dmgame.com/pc/137744.html) |
| 开发者与发行商 | Quasar（Jamie Schumacher）[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 发行日期 | 2018年9月28日[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 运行平台 | PC（Windows、macOS、Linux）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 游戏引擎 | MonoGame Engine、XNA Engine[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 游戏类型 | 模拟、沙盒、抢先体验、上帝模拟、科学、教育[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 标准售价 | 19.99 美元[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 历史最低价 | 9.99 美元（5 折）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 支持语言 | 仅英语界面，不支持简体中文[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| Steam 评价 | 多半好评，955 篇评测中 74% 为好评[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| Metacritic 评分 | 暂无专业媒体评分及用户评分[metacritic.com](https://www.metacritic.com/game/species-artificial-life-real-evolution/) |
| 安装体积 | 总计 5.08 GiB，其中 Windows 版约 1.52 GiB[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 创意工坊 | 支持 Steam Workshop Mod[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 家庭共享 | 支持[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| Steam Deck 兼容性 | 可运行，默认图形配置性能良好，但部分功能需触屏或虚拟键盘[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 受限地区 | 德国（DE）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |

对上表信息需要做三点取舍判断。其一，语言层面仅支持英语界面[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，这对中文玩家构成实质门槛，且由于项目已停止活跃开发（详见第 3 章），官方中文在可预见期内不具备落地条件；其二，Metacritic 上暂无专业媒体评分及用户评分[metacritic.com](https://www.metacritic.com/game/species-artificial-life-real-evolution/)，意味着本作的口碑几乎完全由玩家社区构建，缺乏专业媒体评价体系的外部校准；其三，游戏支持 Steam Workshop Mod[steamdb.info](https://steamdb.info/app/774541/patchnotes/) 与家庭共享[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，这两项在长周期停更的项目中反而承担了延长生命周期的作用——社区 Mod 成为官方内容之外的唯一增量来源。综合来看，本作是一款科学机制稀缺性极高、但产品完成度与可访问性存在明显短板的独立作品。

## 2. 科学机制与核心玩法

### 2.1 三大进化原理的模拟实现

本作的设计出发点是从进化科学的第一性原理出发构建模拟，而非使用预设脚本或适应度函数[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。这一区别具有决定性意义：多数带"进化"标签的游戏会预先定义"什么是更优的生物"，玩家的操作实质是在朝既定目标优化；而本作不设置任何预定义目标，只有生存本身作为筛选压力[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。支撑这套机制的是三条严格对应的原理。

第一条是变异（Variation）。游戏中每个生物都由其基因唯一定义，不存在两个完全相同的个体[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。这构成了种群内多样性的事实基础，也是选择能够发生的前提——若个体完全同质，任何环境压力都只会整体淘汰或整体保留种群，不会产生性状频率的偏移。

第二条是突变（Mutation）。子代生物是其亲代的略微随机修改版本，基因在世代传递中不断积累微小变化[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。"略微"与"积累"两个限定词刻画了本作的突变模型特征：单次突变幅度小、方向随机，性状的显著分化依赖长时间跨度的世代叠加，而非一次性的大跳跃。这直接解释了游戏的节奏特征——进化结果无法在短局内观察到，玩家必须投入以世代为单位的时间。

第三条是自然选择（Natural Selection）。环境直接影响每一个生物的生存挣扎[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。由于环境中不存在预设的适应度评分，生物的存活与繁殖结果完全由其在具体环境条件下的表现决定，同一性状在不同环境压力下可能得出相反的筛选结论。

三条原理的组合产生了本作的核心可观察现象：无论是从单一物种起步还是生成随机种群，游戏都会自行演化出包含种群爆发、物种形成和灭绝的独特历史[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。玩家选中某个物种后加速时间，可亲眼目睹其形态和统计数据随世代更替而变化[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。这种"每次运行结果都不可复现"的特性，使本作在玩法上更接近科学实验而非关卡通关——玩家获得的是对某一组初始条件与随机序列的观察记录，而非可重复验证的标准答案。

在生态系统的复杂度层面，游戏包含草食动物与肉食动物、战斗、交配、行为与生理进化、种群动态多个相互耦合的系统[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。作为首个此类宏观尺度的自然选择模拟器，本作提供了包含数百种独特程序化生成生物的 3D 环境[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)——"宏观 3D 世界"这一点将其与多数停留在二维网格或微观尺度的同类人工生命项目区分开来。

### 2.2 玩家干预工具与分析系统

玩家在游戏中的角色并非固定，既可以作为纯粹观察者研究生命之树的构建，也可以通过多种工具主动干预进化进程[3DM网游](https://dl.3dmgame.com/pc/137744.html)。可用的干预与分析手段构成一套完整的人工选择工具箱。

在干预工具方面，托儿所（Nursery）是一个隔离区域，玩家可在其中自由修补和实验基因库[3DM网游](https://dl.3dmgame.com/pc/137744.html)。它的价值在于提供了一块不受野外选择压力干扰的实验场，玩家可以在隔离环境中先验证某一基因组合的表现，再决定是否放归。漫游车（Rover）是可驾驶的机器人载具，玩家用它决定哪些生物被喂食、哪些被碾死，从而实施人工选择压力[3DM网游](https://dl.3dmgame.com/pc/137744.html)。这是最直接的选择干预手段——通过人为设定存活标准，玩家实质上在扮演一个方向明确的选择压力源。全球灾难功能可触发冰河时代、全球洪水一类的环境剧变，用于淘汰脆弱物种[3DM网游](https://dl.3dmgame.com/pc/137744.html)，其作用与漫游车的定向筛选相反：它施加的是无差别的大规模筛选，检验的是种群整体的环境适应广度而非个体性状优劣。基因操作层面，玩家可对生物进行基因取样并拼接到其他个体基因组中，或通过辐射诱发强烈突变[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。辐射诱发突变是对第 2.1 节所述"小幅随机突变"模型的主动加速手段，代价是突变方向的不可控性。

在分析工具方面，游戏提供进化枝图（Cladogram）用于查看进化步骤、卫星地图用于查看物种分布、生物检查工具用于查看详细属性[3DM网游](https://dl.3dmgame.com/pc/137744.html)。三类工具分别对应谱系、空间、个体三个观察维度：进化枝图回答"这个物种从哪里分化而来"，卫星地图回答"它现在占据哪些区域"，生物检查工具回答"这个个体的具体性状数值是多少"。三者的组合使玩家能够把长时间跨度内发生的形态变化还原为可追溯的谱系路径，这也是本作区别于普通观察类模拟器的分析深度所在。

## 3. 开发历程与项目现状

### 3.1 长周期独立开发轨迹

本作的开发周期跨度极长：开发者 Jamie Schumacher 自 2007 年开始断续开发此项目，2011 年起持续投入[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。从 2007 年首次动工到 2018 年 9 月 28 日 Steam 上架[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，中间经历了 11 年；若计至 2026 年，项目从启动至今已超过 19 年。这一时间尺度远超常规独立游戏项目，也决定了它在多个环节上依赖社区与外部支持。

团队分工方面，美术设计由 Jade Taggart（CrashKat）负责，音频由 Brain Sugar 提供[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。这意味着 Quasar 本人承担的是程序与模拟系统部分，视觉与听觉表现由外部合作者补充。

资金层面，项目曾在 Indiegogo 发起众筹，原目标为 25,000 美元但未达标，此后采用灵活筹资模式继续开发[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。灵活筹资（flexible funding）模式的含义是即使未达目标也可保留已筹资金，这一安排使项目在众筹失败后仍能维持推进，但资金规模的不足也为后续的功能裁剪与开发节奏埋下了约束。

在正式上架前，Alpha 0.4.1 版本曾作为免费概念验证演示发布，体积为 43.5 MB[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)。对比正式版总计 5.08 GiB、Windows 版约 1.52 GiB 的安装体积[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，免费演示版以极小的体量承担了核心机制的对外验证功能。

媒体关注方面，Rock Paper Shotgun 早在 2013 年 6 月即对该项目进行了专题报道[rockpapershotgun.com](https://www.rockpapershotgun.com/darwinning-species-artificial-life-real-evolution)——这一时间点比 Steam 上架早了 5 年以上，说明本作的机制设计在开发中期就已获得专业游戏媒体的辨识度。

技术实现上，游戏使用 Microsoft XNA Game Studio 与 MonoGame 引擎开发[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)，并通过 MonoGame 实现了跨平台运行[steamdb.info](https://steamdb.info/app/774541/patchnotes/)。XNA 是微软已于 2013 年停止支持的开发框架，这一技术选型在带来跨平台能力的同时，也使项目长期背负着陈旧技术栈的维护负担。

### 3.2 更新停滞与未兑现的功能承诺

当前项目处于长期停滞状态，这是评估本作是否值得购买时权重最高的一项事实。

从版本证据看，最新公开版本为 v0.14.2.0，在实验分支上线，该版本启用了本地化 Mod 支持，未包含游戏玩法变更[steamcommunity.com](https://steamcommunity.com/app/774541/allnews/)。版本号停留在 0.14 区间、且最新发布只落在实验分支而非主干，两项特征共同表明项目从未走到 1.0 正式版本。开发者博客的最后一次更新停留在 2022 年 11 月 25 日，内容为翻译 Mod 教程[wordpress.com](https://speciesdevblog.wordpress.com/)——末次更新的性质是工具支持文档而非玩法内容，进一步印证开发重心在停更前已从核心模拟转向周边支持。

从社区沟通看，Steam 新闻页曾发布标题为"I'm back: Long Overdue: An update, an explanation and an apology"的帖子，表明开发者曾尝试回归并向社区致歉[steampowered.com](https://store.steampowered.com/news/app/774541/view/5832739282294831223)。但该声明之后仍无实质更新[steampowered.com](https://store.steampowered.com/news/app/774541/view/5832739282294831223)，多名社区成员确认自 2022 年以来未收到任何补丁或更新，开发者疑似已停止活跃开发[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/)。距离首次上架已超过 8 年，游戏仍处于抢先体验阶段[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/)。

功能承诺的兑现情况是判断完成度的另一把标尺。根据 Indiegogo 众筹页面描述，以下七项功能曾被列入未来计划[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)：飞行能力；水体环境（水生与两栖动物）；可变温度与湿度系统；具有遗传特性的可繁殖树木组成的迁徙森林；基础共生与寄生行为；完整的 Mod 支持体系；在线多人连接（合并多玩家地图 CPU 算力）。其中"完整的 Mod 支持体系"通过 v0.14.2.0 启用本地化 Mod 支持[steamcommunity.com](https://steamcommunity.com/app/774541/allnews/) 与 Steam 创意工坊支持[steamdb.info](https://steamdb.info/app/774541/patchnotes/) 得到了有限度的兑现，其余六项在资料中未见实现记录。飞行能力与水体环境的缺失尤其影响生态模拟的完整度——两者分别对应空中生态位与水生生态位，缺失意味着当前的选择压力只作用于陆地表层这一单一环境维度；可变温度与湿度系统的缺失则限制了环境梯度的多样性，而环境梯度正是驱动地理隔离与物种形成的重要机制。

## 4. 社区评价与口碑分歧

### 4.1 正面评价：科学准确性的稀缺价值

本作的正面口碑高度集中在科学准确性这一单一维度上，且证据来源覆盖了玩家、内容创作者、社区长期用户、同类项目开发者与专业媒体五类主体。

在玩家评价层面，游戏被称为"《孢子》的解药"[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。这一评价的实质是机制对比：不同于《孢子》中装饰性的自定义系统，本作的进化是真实物理模拟的结果，生物形态变化直接关联生存能力[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。在 YouTube 评论中，它被称作"基于生物学的优秀电子游戏"，适合理解遗传漂变和物种形成两类概念[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。这一评价把本作的使用场景从娱乐扩展到了教学——遗传漂变与物种形成是进化生物学中两个抽象度高、纯文字讲述难以建立直观理解的概念，而本作的世代级实时观察恰好提供了可视化载体。

在内容传播层面，知名主播 Vinesauce 曾直播游玩，扩大了游戏知名度[fandom.com](https://vinesauce.fandom.com/wiki/Species:_Artificial_Life,_Real_Evolution)。直播形式与本作的观赏性高度契合：长时间跨度的进化过程本身构成一种慢节奏的叙事，观众可以跟随主播见证一个物种从出现到分化或灭绝的完整周期。与之呼应的数据是 Twitch 历史最高观众达到 10,161 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，这一量级远超其历史最高同时在线玩家 257 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，说明本作的观看价值显著高于其游玩价值——大量受众愿意看，但转化为实际玩家的比例有限。

在社区沉淀层面，Reddit 社区中有长期追随者称其为"进化论书呆子的瑰宝"[Reddit](https://www.reddit.com/r/pcgaming/comments/9jruje/our_natural_selection_simulator_species_alre_is/)。在同类项目的专业视角上，开源进化模拟项目 Thrive 的开发团队曾在 2016 年的开发日志中把本作作为同类进化模拟器进行专业比较[revolutionarygamesstudio.com](https://www.revolutionarygamesstudio.com/devblog-5-thrive-vs-species)——被同赛道的开发团队纳入对照分析，是对其技术路线认可度的一种间接背书。在专业媒体层面，Kotaku 和 IGN 均有收录介绍[ign.com](https://www.ign.com/games/species-artificial-life-real-evolution)，Rock Paper Shotgun 在 2013 年 6 月即做过专题报道[rockpapershotgun.com](https://www.rockpapershotgun.com/darwinning-species-artificial-life-real-evolution)。

五类证据的共性在于：全部围绕"科学机制的真实性"展开，无一条以画面表现、操作手感、内容体量或故事性为赞扬理由。这构成了本作口碑结构的显著特征——它在机制稀缺性上无可替代，但在游戏作为娱乐产品的常规评价维度上不具备竞争力。

### 4.2 负面评价：稳定性、节奏与语言门槛

负面反馈同样高度集中，可归纳为五项，其中前三项对购买决策的影响最直接。

第一项是稳定性差。Steam 评测与社区讨论中存在大量关于游戏频繁崩溃的反馈，丢失进度是常见痛点[steamcommunity.com](https://steamcommunity.com/app/774541/discussions/0/3075370124707956701/)。对本作而言，崩溃的代价远高于常规游戏：由于进化结果需要以世代为单位累积（见第 2.1 节），一次崩溃丢失的可能是数小时乃至更长时间跨度的模拟进程，且这段进程不可复现。

第二项是节奏缓慢。游戏的玩法循环偏向冥想式观察，缺乏任务引导和叙事钩子，不适合寻求传统游戏体验的玩家[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。这与第 2.1 节所述的机制特征互为因果：不设置预定义目标[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) 的设计，必然导致游戏无法提供"下一步该做什么"的目标指引，玩家的驱动力只能来自自身的观察兴趣。

第三项是开发停滞。抢先体验超过 8 年未完成，开发者失联多年，承诺的功能大量未实现[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/)。这一项与前两项性质不同：稳定性和节奏属于可通过后续更新修复的产品缺陷，而开发停滞意味着修复通道本身已经关闭。

第四项是语言障碍。游戏仅支持英语界面，无官方中文[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)。虽然 v0.14.2.0 启用了本地化 Mod 支持[steamcommunity.com](https://steamcommunity.com/app/774541/allnews/)，为社区翻译留出了技术接口，但官方中文始终缺位。

第五项是技术摩擦。作为小型个人项目，整体完成度和打磨程度有限[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game)。

把正负两侧评价合并到 Steam 数据上：955 篇评测中 74% 为好评，评价等级为"多半好评"[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，即约 26% 的评测给出了差评。74% 的好评率对一个机制稀缺的作品而言并不算高——通常具备独特卖点的独立作品若产品质量达标，好评率会显著高于此。这一数据侧面反映了负面反馈的强度：科学机制的独特性足以吸引并说服四分之三的评测者，但不足以让另外四分之一忽略崩溃、缓慢与停更带来的实际损耗。基于此，资料给出的建议是：对寻求深度科学模拟的玩家，这是一款独特但存在明显缺陷的作品；对期待完整游戏体验或稳定运行的用户，建议谨慎购买，或关注同类替代品如《Ecosystem》[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)。

## 5. 玩家规模与市场表现

本作的玩家规模呈现出明显的单峰衰减形态，当前活跃度已降至近乎归零的水平。

**表2：Species: ALRE 玩家规模与商业数据**

| 数据项 | 数值 |
| :--- | :--- |
| 当前同时在线 | 约 2 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 24 小时峰值 | 4 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 历史最高同时在线 | 257 人（2019年1月15日）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| Twitch 历史最高观众 | 10,161 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 拥有者数量估计（VG Insights） | 约 18,100 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 拥有者数量估计（Gamalytic） | 约 30,800 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 拥有者数量估计（PlayTracker） | 约 51,900 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 标准售价 | 19.99 美元[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| 历史最低折扣价 | 9.99 美元（-50%）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 各区定价区间 | 从 ₹264 到 CHF 19.50[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |
| 受限地区 | 德国（DE）[steamdb.info](https://steamdb.info/app/774541/patchnotes/) |

历史最高同时在线 257 人出现在 2019 年 1 月 15 日[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，距离 2018 年 9 月 28 日[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) 的上架日期约三个半月，符合抢先体验作品在发售后短期集中曝光的典型峰值位置。而当前同时在线仅约 2 人、24 小时峰值 4 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，与峰值相比衰减了两个数量级。这一衰减轨迹与第 3 章所述的更新停滞在时间上相互印证：开发者博客末次更新停在 2022 年 11 月 25 日[wordpress.com](https://speciesdevblog.wordpress.com/)，社区确认此后无任何补丁[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/)，缺乏新内容供给的模拟类游戏难以维持长期在线人数——这类作品的重复游玩意愿本身就高度依赖新机制与新环境的引入。

一个值得单独指出的结构性落差是观看量与游玩量的背离：Twitch 历史最高观众 10,161 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，是历史最高同时在线玩家 257 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/) 的约 39 倍。这一比例说明本作具备很强的观赏属性而转化力偏弱，其玩法循环偏向冥想式观察、缺乏任务引导[screenwiseapp.com](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game) 的特征正好解释了这个现象——缓慢的世代演进适合被观看，但要求观众转化为亲自动手、承受崩溃风险[steamcommunity.com](https://steamcommunity.com/app/774541/discussions/0/3075370124707956701/) 的玩家则门槛偏高。

拥有者数量方面，三家统计机构给出的估计值分布较宽：VG Insights 约 18,100 人、Gamalytic 约 30,800 人、PlayTracker 约 51,900 人[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，最高值与最低值相差近 3 倍。这类差异源于各家机构采用的估算口径不同（成就完成率反推、评测数倍乘、账号数据抽样推算等方式彼此不等价），因此这三个数字应被视为区间参考而非精确统计。区间中值约 3 万份的量级，结合 19.99 美元的标准售价[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，对单人开发者而言属于可维持项目存续、但不足以支撑长期全职开发的收入规模，这与众筹目标 25,000 美元未达标[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) 的早期资金状况构成一致的约束链条。

商业策略层面，历史最低折扣价为 9.99 美元、折扣力度 50%[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，各区定价从 ₹264 到 CHF 19.50 不等[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，说明本作采用了区域差异化定价加定期半价促销的常规独立游戏策略。游戏在德国（DE）被列为受限地区[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，该地区用户无法正常购买。

## 6. 运行环境与配置要求

本作在技术实现上选择了 MonoGame Engine 与 XNA Engine[steamdb.info](https://steamdb.info/app/774541/patchnotes/)。XNA 是微软的托管游戏开发框架，MonoGame 是其开源实现，正是借助 MonoGame，原本基于 XNA 的项目得以运行在 Windows、macOS、Linux 三个平台[steamdb.info](https://steamdb.info/app/774541/patchnotes/)。这一跨平台能力对一款 2007 年即启动[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)、技术栈定型较早的项目而言是重要收益。

**表3：Species: ALRE 最低配置要求**

| 配置项 | 最低要求 |
| :--- | :--- |
| 操作系统 | Windows Vista 或更高版本[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |
| 处理器 | 2 GHz[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |
| 内存 | 8 GB RAM[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |
| 显卡 | Shader Model 3.0[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |
| Pixel Shaders | 3.0[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |
| Vertex Shaders | 3.0[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) |

配置门槛的整体特征是图形要求极低而内存要求相对偏高。操作系统下限为 Windows Vista[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements)、显卡只需 Shader Model 3.0[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements)、处理器 2 GHz[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements)，这三项在 2026 年的硬件环境下几乎不构成任何限制——Shader Model 3.0 对应的是 2004 年前后的显卡能力级别。真正的门槛在内存：8 GB RAM[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) 的要求与本作需要同时维护数百种程序化生成生物[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) 及完整种群动态系统[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) 的模拟规模相匹配，种群状态、基因数据与谱系记录的持续驻留决定了内存占用高于同画面水准的游戏。根据 Pickgamer 统计，约 79.2% 的玩家设备可满足最低配置要求[pickgamer.com](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements)，即约 20.8% 的潜在玩家设备不达标，未达标的原因大概率集中在 8 GB 内存这一项而非显卡。

两项配置信息缺口需要指出。其一，推荐配置尚未公布[ali213.net](https://down.ali213.net/pcgame/speciesafre.html)——这意味着玩家无法判断在开启更大种群规模或长时间模拟后需要多少硬件余量，只能以最低配置作为唯一下限参考。其二，安装体积数据存在平台差异：总计 5.08 GiB，其中 Windows 版约 1.52 GiB[steamdb.info](https://steamdb.info/app/774541/patchnotes/)，三平台合计与单平台之间相差 3.56 GiB，玩家实际下载量取决于所选平台。

在 Steam Deck 上，游戏可运行且默认图形配置性能良好，但部分功能需触屏或虚拟键盘操作[steamdb.info](https://steamdb.info/app/774541/patchnotes/)。结合第 2.2 节所述的工具集来看，托儿所中的基因修补[3DM网游](https://dl.3dmgame.com/pc/137744.html)、基因取样与拼接[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) 属于精细操作，在缺少实体键鼠的情况下依赖虚拟键盘输入会带来明显的操作摩擦，掌上游玩体验与桌面端存在实质差距。

## 7. 同类进化模拟游戏对照

在本作停更的背景下，资料中出现的六款同类进化与人工生命模拟作品构成了玩家的可迁移选项，其中四款有明确发售时间与中文支持信息的作品列入下表对照。

**表4：同类进化与人工生命模拟游戏对照**

| 游戏名称 | 题材设定 | 中文支持 | 发售时间 | 评价状态 |
| :--- | :--- | :--- | :--- | :--- |
| Species: Artificial Life, Real Evolution | 宏观 3D 世界的自然选择模拟[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) | 仅英语[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) | 2018年9月28日[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) | 多半好评，74% 好评率[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) |
| Evolution of Species 2: Online | 微观生物进化对战[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese) | 支持繁体中文[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese) | 2024年11月1日[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese) |  |
| Ecosystem | 海底生态系统创造模拟[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) | 支持简体中文[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) | 2024年11月8日[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) | 多半好评[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) |
| 人工生命：动作学习进化论 | 基于机器学习与物理引擎的方块生物进化模拟[3DM网游](https://dl.3dmgame.com/pc/149027.html) | 支持简体中文[3DM网游](https://dl.3dmgame.com/pc/149027.html) | 2026年2月12日[3DM网游](https://dl.3dmgame.com/pc/149027.html) |  |
| Substrate: Emergence | 培养皿细胞菌落进化模拟[3DM网游](https://www.3dmgame.com/gl/3989632.html) | 支持简体中文与繁体中文[3DM网游](https://www.3dmgame.com/gl/3989632.html) | 预计2026年9月25日[3DM网游](https://www.3dmgame.com/gl/3989632.html) |  |

从时间维度看，上表除本作外的四款作品发售时间全部晚于本作：Evolution of Species 2: Online 于 2024 年 11 月 1 日发售[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese)，Ecosystem 于 2024 年 11 月 8 日发售[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)，人工生命：动作学习进化论于 2026 年 2 月 12 日发售[3DM网游](https://dl.3dmgame.com/pc/149027.html)，Substrate: Emergence 预计 2026 年 9 月 25 日发售[3DM网游](https://www.3dmgame.com/gl/3989632.html)。也就是说，在本作于 2022 年 11 月停更[wordpress.com](https://speciesdevblog.wordpress.com/) 之后，该细分品类仍在持续有新供给进入，且最后一款作品在本报告撰写时点尚未发售。品类没有随本作停滞而萎缩，活跃供给已转移到新项目上。

从语言维度看，本作仅支持英语界面[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese)，而上表四款有明确中文信息的作品全部支持中文：Ecosystem 与人工生命：动作学习进化论支持简体中文[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)、Evolution of Species 2: Online 支持繁体中文[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese)、Substrate: Emergence 同时支持简体与繁体中文[3DM网游](https://www.3dmgame.com/gl/3989632.html)。对中文玩家而言，语言支持是当前选择同类作品时最容易满足、而本作恰恰缺失的一项条件。

从题材分工看，上表五款作品覆盖了不同的模拟尺度与技术路线：本作处于宏观 3D 世界的自然选择模拟位置[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)；Ecosystem 聚焦海底生态系统创造[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)；Substrate: Emergence 把尺度收缩到培养皿中的细胞菌落[3DM网游](https://www.3dmgame.com/gl/3989632.html)；Evolution of Species 2: Online 走微观生物加在线对战的方向[steampowered.com](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese)；人工生命：动作学习进化论采用机器学习与物理引擎驱动的方块生物[3DM网游](https://dl.3dmgame.com/pc/149027.html)。这一分布显示，同类项目在本作之后并未复制其宏观陆生路线，而是分别向水生、微观、对抗、机器学习四个方向分化。此外，入侵种（INVADER）结合 AI 自然语言造物与生态策略沙盒模拟[TapTap](https://www.taptap.cn/moment/781546522100631820)，代表了把大语言模型能力接入造物环节的新方向。

需要单独说明 Thrive 的对照意义。作为开源进化模拟项目，它的开发团队在 2016 年的开发日志中把本作作为同类进化模拟器进行专业比较[revolutionarygamesstudio.com](https://www.revolutionarygamesstudio.com/devblog-5-thrive-vs-species)。开源属性使 Thrive 不依赖单一开发者的持续投入，这与本作高度依赖 Quasar 个人、在其失联后即陷入停滞[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/) 的模式形成结构性对比——同样的品类，两种开发组织方式在项目存续能力上表现出的差异，是本次调研中最具参照价值的一点。

回到购买建议本身：资料明确指出，对期待完整游戏体验或稳定运行的用户，建议关注同类替代品如《Ecosystem》[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)。结合上表数据，Ecosystem 同时具备简体中文支持[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)、2024 年 11 月 8 日的较新发售时间[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) 与多半好评的评价状态[steampowered.com](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436)，是三项条件都满足的迁移选项；而若玩家追求的是本作不可替代的宏观陆生自然选择模拟体验[indiegogo.com](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution)，则表中没有严格对等的替代品，只能在本作的机制独特性与稳定性缺陷[steamcommunity.com](https://steamcommunity.com/app/774541/discussions/0/3075370124707956701/)、停更风险[steampowered.com](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/) 之间自行权衡。

## 核心参考文献

[Species: Artificial Life, Real…](https://www.indiegogo.com/en/projects/jamieschumacher/species-artificial-life-real-evolution) · [Species: Artificial Life, Real…](https://steamdb.info/app/774541/patchnotes/) · [Steam 上的 Species: Artificial…](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/?curator_clanid=43684419&l=schinese) · [物种人工生命真实进化下载_物种人…](https://down.ali213.net/pcgame/speciesafre.html) · [Species: Artificial Life, Real…](https://screenwiseapp.com/media/species-artificial-life-real-evolution-game) · [Species: Artificial Life, Real…](https://www.pickgamer.com/games/species-artificial-life-real-evolution/requirements) · [物种人造生命真实进化下载-物种人…](https://dl.3dmgame.com/pc/137744.html) · [Darwinning: Species: Artificial…](https://www.rockpapershotgun.com/darwinning-species-artificial-life-real-evolution) · [Species: Artificial Life, Real…](https://store.steampowered.com/app/774541/Species_Artificial_Life_Real_Evolution/) · [Comunidade Steam :: Species: Ar…](https://steamcommunity.com/app/774541?l=portuguese) · [Species: Artificial Life, Real…](https://store.steampowered.com/news/app/774541/view/5832739282294831223) · [My Review of the Game :: Specie…](https://steamcommunity.com/app/774541/discussions/0/3075370124707956701/) · [Species: Artificial Life, Real…](https://www.metacritic.com/game/species-artificial-life-real-evolution/) · [在 Steam 上购买 生态系统 立省 5…](https://store.steampowered.com/app/1133120/Ecosystem/?curator_clanid=27270436) · [Devblog #5: Thrive vs Species](https://www.revolutionarygamesstudio.com/devblog-5-thrive-vs-species) · [Steam 上的 Species: Artificial…](https://store.steampowered.com/app/774541/) · [Artificial Life, Real Evolution…](https://gogunlocked.com/species-artificial-life-real-evolution-free-download/) · [Species: Artificial Life, Real…](https://steamcommunity.com/app/774541/allnews/) · [Species Development Blog – Arti…](https://speciesdevblog.wordpress.com/) · [Species: Artificial Life, Real…](https://vinesauce.fandom.com/wiki/Species:_Artificial_Life,_Real_Evolution) · [Our Natural Selection Simulator…](https://www.reddit.com/r/pcgaming/comments/9jruje/our_natural_selection_simulator_species_alre_is/) · [Species: Artificial Life, Real…](https://www.ign.com/games/species-artificial-life-real-evolution) · [Species: Artificial Life, Real…](https://kotaku.com/games/species-artificial-life-real-evolution) · [Substrate: Emergence 有什么特色…](https://www.3dmgame.com/gl/3989632.html) · [Substrate Emergence中文版下载-S…](https://dl.3dmgame.com/pc/152355.html) · [人工生命官方版-人工生命动作学习…](https://dl.3dmgame.com/pc/149027.html) · [Steam - Evolution of Species 2:…](https://store.steampowered.com/app/3290560/Evolution_of_Species_2_Online/?l=tchinese) · [《入侵种》(INVADER) 简要玩法概…](https://www.taptap.cn/moment/781546522100631820)

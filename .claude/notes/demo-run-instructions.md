# Demo 运行说明（2026-09-24）

## 编译与启动

CLAUDE.md 规定"不编译此项目"，所以请你自己跑：

```bash
pwsh .\build.ps1 -Config Debug    # 配置 + 编译
pwsh .\build.ps1 run             # 编译后运行
```

可选参数（从 main.cpp 看到）：
```
--seed <整数>          固定随机数（默认：随机）
--zoom <浮点>          初始 px_per_tile（默认 8）
--screenshot <路径>    跑 60 帧后截图退出
```

## 操作

| 键位 | 作用 |
|------|------|
| WASD | 平移相机 |
| 鼠标拖拽 | 平移相机 |
| 鼠标滚轮 | 缩放（shift=快速） |
| Space | 暂停/继续 |
| 1/2/3/4 | 速度档（1x/1.5x/2x/4x） |
| R | 重置相机到地图中心 |
| Esc | 退出 |

## 当前能看到的

| 层 | 状态 |
|---|------|
| 地图 | 160×120 tile，tile_renderer 画地形 |
| 生物 | CreatureAssembler 拼接 head+其他 5 类部件 |
| HUD | tick 数 / 人口数 / 世界能量 / 速度 / 状态 |
| 小地图 | 右下角缩略图 |

## 玩法循环（已实现）

- **繁殖**：`EvolutionEngine::reproduce` 无性繁殖 + 变异
- **变异**：默认 0.1 强度，200 tick 内多样化但不崩
- **精英检测**：`detect_elite_boss`，fitness > mean + 2.5σ → EliteBorn 事件
- **Boss 进化**：elite_age 持续 30 tick → BossEvolved 事件
- **环境漂移**：optimum 每 tick 小幅漂移，每 epoch_length (200) tick 剧变 → EnvironmentShift 事件
- **初始种群**：30 个生物随机散布
- **种群上限**：200

## ⚠️ 已知风险

1. **torso 缺失**：manifest 标了 procedural=true，但工程层 PartAtlas::load 现在遇到空 parts 数组会**安全跳过**，但拼接代码（assemble）查 `pick("torso")` 会**返回 nullopt**——需要 fallback 处理。建议先看 demo 跑起来后 torso 区域显示什么。
2. **风格割裂**：head 是山海经彩墨 V3，其余 5 类仍是 V1 卡通——视觉上能看出差异，但不影响玩法验证。
3. **精英怪/Boss 没有风格跳级**：所有生物都用同一套普通风格 + boss 版本视觉上是同一张。

## 验证 checklist

跑起来后，建议按顺序回答：

- [ ] 世界生成是否合理（地形、资源分布）
- [ ] 生物拼接是否正常显示（特别是 torso 区域）
- [ ] 30 初始生物 → 50 tick 后人口变化
- [ ] 100 tick 后能看到 EliteBorn 事件
- [ ] 130 tick 后能看到 BossEvolved 事件
- [ ] 200 tick 后能看到 EnvironmentShift 事件
- [ ] pause + 速度档是否好用
- [ ] 小地图 + HUD 信息是否清晰

跑完一轮（~300 tick）后，把观察反馈给我，我据此决定下一步：
- 玩法循环是否要调整参数？
- 是否要补充精英怪/Boss 风格？
- 是否回头修部件风格统一性？
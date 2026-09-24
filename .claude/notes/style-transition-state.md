# 风格资产状态（2026-09-24）

## 已入库（生产可用）

| 部件 | 风格 | 来源 | 备注 |
|------|------|------|------|
| head (12) | 山海经彩墨 V3 | `assets/parts/head/head_001-012.jpg` | 印记 + 工笔 + 朱绿配色；已更新 manifest (attachPoint=128,254, bbox=0,0,256,256) |
| torso | 程序生成 | manifest 标记 `procedural: true` | 工程层需要实现 fallback 绘制 |
| UI 按钮 (3 张参考) | 鎏金 | `assets/styles_exploration/gilded_ui/` | 待整合到工程层 UI 模块 |

## 仍在 V1 卡通占位（暂用）

| 部件 | 数量 | 风格 | 备注 |
|------|------|------|------|
| forelimb | 12 | 卡通风 V1 | V3 已生成 12 张在 styles_exploration，但成功率 ~50%，未入库 |
| hindlimb | 12 | 卡通风 V1 | V3 12 张已生成，成功率 ~30%，未入库 |
| tail | 12 | 卡通风 V1 | V3 12 张已生成，成功率 ~50%，未入库 |
| wing | 8 | 卡通风 V1 | V3 8 张已生成，成功率 ~75% |
| horn | 8 | 卡通风 V1 | V3 8 张已生成，成功率 ~25% |

## 工程链路当前状态

```
prompt 模板  (docs/prompts/primordial-shanhai.md)
   ↓
mmx image generate
   ↓
assets/styles_exploration/primordial_shanhai_v3/  (52 张备选，未入库)
   ↓
assets/parts/<category>/<category>_NNN.jpg         (已入库)
   ↓
scripts/process_parts.py + update_manifest.py    (去背 + manifest 合并)
   ↓
assets/parts_processed/<category>/<category>_NNN.png
   ↓
creature_assembler.cpp (运行时拼接)
```

## 已知风险

1. **风格不统一**：head 是山海经彩墨，其余 5 类是卡通风，拼接后视觉割裂
2. **torso 占位**：工程层目前 load() 会因为 parts:[] 跳过 torso 加载，需要补 fallback
3. **没有精英怪/Boss 风格跳级**：之前讨论过但未实现，目前所有 head 都是普通怪风格

## 后续可推进的方向

### 短期（推进游戏 demo）
- 实现 torso 程序生成 fallback（简单的椭圆/色块 + tint）
- 把 V1 卡通部件直接喂入游戏，跑一遍基础拼接 demo
- 验证游戏机制是否好玩（演化、祝福、灾厄等）

### 中期（基于 demo 反馈）
- 决定部件风格走向：继续山海经彩墨 vs 回退 V1 卡通 vs 全新风格
- 重做 V3 中失败的部件（用更严格的 prompt 或不同 AI 工具）
- 实现精英怪/Boss 风格跳级

### 长期（视觉品质提升）
- 鎏金 UI 整合（替换 V1 UI 占位）
- 部件去背优化（BG_DIST 调到 20-25，减少边缘残留）
- 尝试文生视频/3D 等新媒介
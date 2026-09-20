# Time-Flow Evolution（时间流向演化）— 设计规格

> 日期：2026-09-20
> 范围：把 `EvolutionEngine` 的回合制演化（每 `step()` 推进一个代际）改为"时间流向"演化（每真实帧累积 dt，按固定 tick_dt 推进模拟），并把生物绑到地图上、引入觅食/繁殖/死亡行为循环。

---

## 1. 目标与非目标

### 1.1 目标

- 让世界以"时间在流"的方式推进，玩家能直观看到生物在地图上移动、觅食、繁殖、消亡
- 把演化规则嵌入模拟循环的子步骤里，**移除"代际"作为顶层时间单位**
- `Simulation` 成为唯一的时钟源；`EvolutionEngine` 降级为规则模块
- 最小可玩循环：地图渲染 + 生物渲染 + 暂停/倍速

### 1.2 非目标（YAGNI）

- 鼠标 hover、选中、详情面板
- 世界祝福 UI（每纪元一次）
- 地形微调、气候干预、生态介入
- SFML 实际接入（保留 CMakeLists 占位，本轮不引用）
- 存档/读档、seed 分享 UI
- 食物链（草食/肉食）— 基因 → 表现型映射留到下一轮
- 水生生态（生物只能在陆地块）

---

## 2. 架构

```
┌──────────────────────────────────────────────────────┐
│ main.cpp  SDL3 主循环：事件 + dt + 帧呈现              │
└─────────────────────┬────────────────────────────────┘
                               │ real_dt（秒）
┌─────────────────────────────▼────────────────────────┐
│ Simulation  ←唯一时钟源                                │
│  · SimulationClock：accumulator + max_ticks_per_frame │
│  · 拥有 World + Creatures + Events                     │
│  · 暴露 advance(dt, speed), events() 等               │
└──┬─────────────────────┬─────────────────────────┬───┘
   │                     │                         │
┌──▼────────┐  ┌──────────▼─────────┐  ┌─────────────▼────┐
│ World     │  │ EvolutionEngine    │  │ TileRenderer     │
│ (扩bio)  │  · evaluate_fitness  │  │  + 生物层        │
│           │  · detect_elite_boss │  │  + HUD          │
│           │  · reproduce         │  │                 │
│           │  · drift_environment │  │                 │
└───────────┘  └────────────────────┘  └─────────────────┘
```

### 2.1 接口边界

- `Simulation` 是"时间相关"的唯一入口；外部只能 push 真实 dt、读 events、查询世界/生物快照
- `EvolutionEngine` 不持有时钟，只持有规则（基因运算 + 适应度 + 精英判定 + optimum 漂移）
- 生物从"种群"变成"地图上有位置的实体集合"
- `World` 拥有 tile + 静态的 biome 表（biomass target/regrowth）

---

## 3. 时间模型

### 3.1 参数

| 名 | 含义 | 默认值 |
|---|---|---|
| `tick_dt` | 1 tick = 1 游戏日 | 0.1 s 真实时间 |
| `speed` | 倍速（玩家可调） | 0.5 / 1.0 / 2.0 / 4.0 |
| `epoch_length` | 每 N tick 一次环境剧变 | 200 tick（=20s @1x） |
| `max_ticks_per_frame` | 单帧消费 tick 上限 | 8（避免慢机器螺旋失控） |

### 3.2 accumulator 模式

```cpp
class SimulationClock {
    float tick_dt = 0.1f;
    float speed = 1.0f;
    std::size_t max_ticks_per_frame = 8;
    float accumulator = 0.0f;

    void advance(float real_dt, auto&& tick_fn) {
        accumulator += real_dt * speed;
        std::size_t consumed = 0;
        while (accumulator >= tick_dt && consumed < max_ticks_per_frame) {
            tick_fn();
            accumulator -= tick_dt;
            ++consumed;
        }
    }
};
```

- 暂停：`speed = 0`，accumulator 不增
- 重现性：同 seed + 同 tick 数 → 同状态（前提：消费者按确定性顺序读 RNG）

---

## 4. 世界扩展（Tile + Biomass）

### 4.1 Tile 扩展

```cpp
struct Tile {
    float elevation = 0.0f;
    float temperature = 0.0f;
    float moisture = 0.0f;
    Biome biome = Biome::DeepOcean;
    float biomass = 0.0f;            // 当前食物量 [0, 1]
    float biomass_target = 0.0f;     // 该 biome 的"满载"基线
    float biomass_regrowth = 0.0f;   // 每 tick 重生率
};
```

### 4.2 生物群系 → 生物量表（生成时写入 tile）

| Biome | target | regrowth |
|---|---|---|
| DeepOcean / Ocean | 0.0 | 0.0 |
| Beach | 0.3 | 0.10 |
| Grassland | 0.7 | 0.15 |
| Forest | 0.9 | 0.10 |
| Rainforest | 1.0 | 0.10 |
| Desert | 0.1 | 0.03 |
| Mountain | 0.1 | 0.03 |
| Snowcap | 0.1 | 0.03 |
| Swamp | 0.6 | 0.08 |
| Tundra | 0.2 | 0.05 |

### 4.3 World 接口扩展

```cpp
class World {
    // ... 现有 ...
    auto regrow_biomass() -> void;                       // 每 tick 重生
    [[nodiscard]] auto total_biomass() const -> float;   // 用于世界能量累积
    [[nodiscard]] auto is_land(std::size_t x, std::size_t y) const -> bool;  // 出生/移动合法判定
};
```

---

## 5. 生物扩展

### 5.1 Creature 扩展

```cpp
struct Creature {
    std::uint64_t id = 0;          // 全局唯一，用于事件关联
    Traits gene;
    std::string name;

    Position pos{0, 0};
    float hunger = 1.0f;            // 0=临界  1=饱
    float energy = 1.0f;            // 0=虚脱  1=精力充沛
    std::uint32_t age = 0;          // tick 数
    bool dead = false;              // 本 tick 内行为机判定死亡

    float fitness = 0.0f;
    bool is_elite = false;
    bool is_boss = false;
    std::uint32_t elite_age = 0;    // 连续保持精英的 tick 数
};
```

### 5.2 行为机（每 tick 对每只活生物执行）

顺序：**先吃 → 再决定走不走 → 否则随机游走**。繁殖为**无性**（单亲克隆 + 变异），避免寻找配偶的开销。

```cpp
void decide_and_act(Creature& self, World& world,
                    std::vector<Creature>& births,
                    EvolutionEngine& evo,
                    std::mt19937_64& rng,
                    std::uint64_t& next_id) {
    constexpr float kHungerRate = 0.02f;
    constexpr float kMaxAge = 200;
    constexpr std::uint32_t kMatingAge = 20;
    constexpr std::uint32_t kMateInterval = 50;

    self.age += 1;
    self.hunger -= kHungerRate;
    recover_energy(self);

    if (self.hunger <= 0.0f || self.age > kMaxAge) {
        self.dead = true;  // 标记，外部 remove_dead 统一收割
        return;
    }

    auto& here = world.at(self.pos.x, self.pos.y);
    // 1) 先看脚下：站着有食物 → 吃（原地不动）
    if (here.biomass > 0.1f && self.hunger < 1.0f) {
        const float eat_amount = std::min(here.biomass * 0.5f, 1.0f - self.hunger);
        here.biomass -= eat_amount * 0.4f;       // 只吃掉其中 40%
        self.hunger += eat_amount * 0.3f;        // 转化为饱食
        self.energy = std::min(1.0f, self.energy + 0.05f);
        return;
    }

    // 2) 仍饿 → 扫 8 邻居找 biomass 最大者，朝它移动 1 步（仅陆地）
    if (self.hunger < 0.5f) {
        Position best = self.pos;
        float best_b = here.biomass;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const std::size_t nx = self.pos.x + dx, ny = self.pos.y + dy;
                if (nx >= world.width() || ny >= world.height()) continue;
                if (!world.is_land(nx, ny)) continue;
                const float b = world.at(nx, ny).biomass;
                if (b > best_b) { best_b = b; best = {nx, ny}; }
            }
        }
        if (best.x != self.pos.x || best.y != self.pos.y) {
            self.pos = best;
        }
        return;
    }

    // 3) 饱 + 能繁殖 → 无性繁殖（每 kMateInterval 50 tick 一次）
    if (self.hunger > 0.7f && self.energy > 0.5f
        && self.age >= kMatingAge
        && (self.age % kMateInterval == 0)) {
        Creature child = evo.reproduce(self);   // 单亲克隆 + 变异
        child.id = next_id++;
        child.pos = self.pos;
        child.age = 0;
        child.hunger = 1.0f;
        child.energy = 0.8f;
        births.push_back(std::move(child));
        self.energy -= 0.3f;                    // 父母消耗
        return;
    }

    // 4) 否则随机游走 1 步（限陆地，重试 4 次，失败原地不动）
    std::uniform_int_distribution<int> dir(0, 7);  // 0..7 八方向
    for (int retry = 0; retry < 4; ++retry) {
        const int d = dir(rng);
        const int dx = (d % 3) - 1;              // -1,0,1
        const int dy = (d / 3) - 1;
        const std::size_t nx = self.pos.x + dx, ny = self.pos.y + dy;
        if (nx >= world.width() || ny >= world.height()) continue;
        if (!world.is_land(nx, ny)) continue;
        self.pos = {nx, ny};
        return;
    }
}
```

**优先级**：`吃饱 > 移动觅食 > 繁殖 > 随机游走`。每个 tick 命中一条即返回。

**性能基线**：
- 地图 80 × 60 = 4800 tiles
- 生物上限 `max_population = 200`
- 每 tick 操作 ≈ 200 × 8 邻居 ≈ 1600 步/查找 → **不需要空间索引**

**参数表**（集中）：
| 常量 | 值 | 含义 |
|---|---|---|
| `kHungerRate` | 0.02 | 每 tick 饥饿下降 |
| `kMaxAge` | 200 | 最大年龄（tick） |
| `kMatingAge` | 20 | 成熟年龄 |
| `kMateInterval` | 50 | 繁殖间隔（tick） |

---

## 6. 演化引擎重构

### 6.1 接口（`EvolutionEngine`）

```cpp
class EvolutionEngine {
public:
    struct Params {
        float mutation_rate = 0.1f;
        float mutation_strength = 0.1f;
        float elite_threshold_sigma = 2.5f;
        std::uint32_t boss_streak_ticks = 30;  // 原 boss_streak 的 tick 化版本
    };

    EvolutionEngine(Params params, std::uint64_t seed);

    // ① 计算所有生物的 fitness：1 / (1 + distance(gene, optimum))
    auto evaluate_fitness(std::span<Creature>, const Traits& optimum) -> void;

    // ② 标记精英与 Boss → 写 events；elite_age 按 tick 累加
    auto detect_elite_boss(std::span<Creature>, std::vector<Event>&) -> void;

    // ③ 无性繁殖（单亲克隆 + 变异 + 新名字 + 重置 vitals）→ 产出子代
    //    vitals 重置由 caller 在 decide_and_act 中处理
    [[nodiscard]] auto reproduce(const Creature& parent) -> Creature;

    // ④ optimum 漂移 + 每 epoch 周期剧变
    auto drift_environment(Traits& optimum, std::uint64_t tick,
                           std::uint64_t epoch_length,
                           std::vector<Event>&) -> void;

    [[nodiscard]] auto mean_fitness(std::span<const Creature>) const -> float;
    [[nodiscard]] auto stddev_fitness(std::span<const Creature>, float mean) const -> float;

private:
    Params params_;
    std::mt19937_64 rng_;
};
```

### 6.2 关键差异（对比旧 `step()`）

| 旧 | 新 |
|---|---|
| 一代一代推进 | 一 tick 一 tick 推进 |
| "下一代"另起 vector | 子代立即加入 population |
| `boss_streak = 3 代` | `boss_streak_ticks = 30` |
| optimum 漂移每代 | optimum 漂移每 tick；每 epoch_length tick 一次剧变 |
| 整体 fitness 统计 | 按 tick 重算 |
| 旧 `select_parent` 选择压力 | 改为行为机内部：饥饿/能量/年龄直接决定生死与繁殖资格（自然选择） |

### 6.3 RNG 流

`EvolutionEngine` 持有自己的 `std::mt19937_64`，由 `pcg::derive_seed(seed, kEvolutionSalt)` 派生。**生物行为决策的随机性**由 `Simulation` 持有的另一个 RNG 流提供（不同 salt）。这样：
- 演化变异算子序列独立于行为随机序列
- 可独立测试二者

---

## 7. Simulation 主循环

### 7.1 接口

```cpp
class Simulation {
public:
    struct Params {
        std::size_t initial_population = 30;
        std::size_t max_population = 200;
        std::uint64_t epoch_length = 200;
        float tick_dt = 0.1f;
        std::size_t max_ticks_per_frame = 8;
        EvolutionEngine::Params evolution{};
    };

    Simulation(World world, std::uint64_t seed, Params params = {});

    // 主入口：由 main loop 调用
    auto advance(float real_dt, float speed) -> void;

    // 快照（给 renderer）
    [[nodiscard]] auto world() const -> const World& { return world_; }
    [[nodiscard]] auto creatures() const -> std::span<const Creature> { return creatures_; }
    [[nodiscard]] auto events() const -> const std::vector<Event>& { return events_; }
    [[nodiscard]] auto world_energy() const -> float { return world_energy_; }
    [[nodiscard]] auto tick_count() const -> std::uint64_t { return tick_; }
    [[nodiscard]] auto is_paused() const -> bool { return speed_ == 0.0f; }

private:
    auto tick() -> void;  // 内部一次 tick

    Params params_;
    World world_;
    Traits optimum_;
    std::vector<Creature> creatures_;
    std::vector<Event> events_;
    std::vector<Creature> pending_births_;   // 本 tick 内繁殖的子代
    EvolutionEngine evolution_;
    SimulationClock clock_;
    std::uint64_t tick_ = 0;
    float world_energy_ = 0.0f;
    float speed_ = 1.0f;
    std::uint64_t next_id_ = 1;
    std::mt19937_64 behavior_rng_;  // 行为决策 RNG 流
};
```

### 7.2 `tick()` 伪代码

```cpp
void Simulation::tick() {
    ++tick_;

    // 1. 世界层：biomass 重生
    world_.regrow_biomass();

    // 2. optimum 漂移 + 周期剧变
    evolution_.drift_environment(optimum_, tick_, params_.epoch_length, events_);

    // 3. 生物行为循环
    pending_births_.clear();
    for (auto& c : creatures_) {
        decide_and_act(c, world_, pending_births_,
                       evolution_, behavior_rng_, next_id_);
    }

    // 4. 收割：移除本 tick 死亡的生物
    std::erase_if(creatures_, [](const Creature& c) {
        return c.dead;
    });
    append_newborns(pending_births_);

    // 5. 演化层：基于本 tick 后的快照
    evolution_.evaluate_fitness(creatures_, optimum_);
    evolution_.detect_elite_boss(creatures_, events_);

    // 6. 资源与能量
    world_energy_ += world_.total_biomass() * 0.01f;

    // 7. 修剪事件队列（保留最近 N 条）
    trim_events();
}
```

### 7.3 `decide_and_act` 位置

放在 `creature.cpp` 内联函数（保持 Simulation.cpp 不依赖具体决策逻辑）。签名：

```cpp
void decide_and_act(Creature& self, World& world,
                    std::vector<Creature>& births,
                    EvolutionEngine& evo,            // non-const: reproduce() 用内部 RNG
                    std::mt19937_64& rng,
                    std::uint64_t& next_id);
```

---

## 8. 渲染层扩展

### 8.1 `TileRenderer` 新增

```cpp
class TileRenderer {
    // 现有
    void render(const World& world);

    // 新增
    void render_creatures(std::span<const Creature> creatures);
    void render_hud(const Simulation& sim);

    [[nodiscard]] auto raw_renderer() noexcept -> SDL_Renderer* { return renderer_.get(); }
};
```

### 8.2 视觉

- 普通个体：2×2 像素点，颜色 = `gene_hash → RGB`（已存在的 `make_name` 哈希复用，或新增 `gene_color`）
- Elite：3×3 像素点 + 黄色边框（1 像素）
- Boss：4×4 像素点 + 红色边框 + 名字浮标（用 `SDL_RenderDebugText`，1 个字符的姓）
- HUD：`SDL_RenderDebugTextFormat` 输出 tick / pop / energy / FPS / 暂停标记

### 8.3 帧顺序

```
clear → render world tiles → render creatures → render HUD → present
```

---

## 9. 主程序（`src/main.cpp` 重写）

```cpp
int main() {
    SetConsoleOutputCP(CP_UTF8);  // 保留控制台输出
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;

    const std::uint64_t seed = parse_seed(argc, argv);  // 默认随机
    World world = WorldGenerator(80, 60).generate(seed);
    Simulation sim(std::move(world), seed);
    auto renderer = TileRenderer::create(80, 60, "World Will");
    if (!renderer) { /* log + return 1 */ }

    bool running = true;
    bool paused = false;
    float speed = 1.0f;
    Uint64 last = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: running = false; break;
                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_SPACE: paused = !paused; break;
                        case SDLK_1: speed = 0.5f; break;
                        case SDLK_2: speed = 1.0f; break;
                        case SDLK_3: speed = 2.0f; break;
                        case SDLK_4: speed = 4.0f; break;
                    }
                    break;
            }
        }

        Uint64 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.25f) dt = 0.25f;  // cap large frames

        sim.advance(paused ? 0.0f : dt, speed);

        renderer->render(sim.world());
        renderer->render_creatures(sim.creatures());
        renderer->render_hud(sim);
        SDL_RenderPresent(renderer->raw_renderer());

        SDL_Delay(16);  // 60 FPS 上限
    }

    SDL_Quit();
    return 0;
}
```

---

## 10. 文件结构

```
include/core/
  gene.hpp              (existing, 不变)
  creature.hpp          (扩展: pos, hunger, energy, age, id, Position)
  evolution.hpp         (重构: step() → evaluate/detect/reproduce/drift)
  simulation.hpp        (新增)
  position.hpp          (新增: 二维坐标 POD)

src/core/
  creature.cpp          (扩展: decide_and_act、recover_energy)
  evolution.cpp         (重构: 实现新接口)
  simulation.cpp        (新增)

include/world/
  world.hpp             (扩展: biomass 三件套、is_land、total_biomass)
  world_generator.hpp   (扩展: 生物群系→生物量表 + 初始放生物)
src/world/
  world.cpp             (扩展)
  world_generator.cpp   (扩展)

include/render/
  tile_renderer.hpp     (扩展: render_creatures, render_hud, raw_renderer)
src/render/
  tile_renderer.cpp     (扩展)

src/main.cpp            (重写: SDL3 主循环)
```

---

## 11. 测试策略

本轮**不引入单元测试框架**（CLAUDE.md 未要求；项目目前无测试目录）。但通过以下方式保证可观察性：

- **重放性**：同 seed + 同 tick 数 → 同状态（前提：RNG 种子派生规则一致；accumulator 在初始帧被清零）
- **HUD 显示**：tick / pop / FPS / energy / 暂停 / 倍速
- **控制台事件日志**：每 tick 打印精英诞生、Boss 蜕变、环境剧变（保留旧 main.cpp 风格的可选 stdout 调试）
- **手动验证场景**：
  1. 启动后 10s 内能看到生物移动（屏幕 1→2 像素位置变化）
  2. 暂停后生物冻结
  3. 4x 倍速下生物移动明显加快
  4. 持续运行 60s 不崩溃、不泄漏（通过 Task Manager 验证）

---

## 12. 风险与权衡

### 12.1 风险

| 风险 | 缓解 |
|---|---|
| 生物"扎堆饿死"或"指数爆炸" | `max_population = 200` 上限 + 自动停止繁殖；饥饿死亡是正常自然选择 |
| accumulator 在慢机器上失控 | `max_ticks_per_frame = 8` 上限 |
| RNG 流冲突（演化变异 vs 行为决策） | 两个独立 salt |
| `WindowWillEnterBackground` 等 SDL3 事件本轮不处理 | 桌面应用，影响小 |

### 12.2 不在本轮的范围

- 基因 → 表现型（move 速度/吃效率/max_age）—— 当前用全局常量
- 食物链与生态位
- 水生生物
- 视觉表现（procedural sprite、动画）—— 当前 2×2 像素点即可
- 多世界并行、纪元间传承系统
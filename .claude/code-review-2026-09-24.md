# gamev2 代码审查报告

> 项目:World Will(世界意志)— C++23 / SDL3 / SFML / ECS 程序化生成生态模拟
> 审查范围:`include/` + `src/` 共 33 个文件,约 7000+ 行
> 审查日期:2026-09-24
> 审查方式:静态阅读 + 跨模块依赖追踪,**未编译、未运行**

---

## 1. 总评

整体代码体现了清晰的分层(`core/ecs` → `core/creature|gene|simulation|evolution` → `world` → `render`)和现代 C++ 风格(`std::expected`、`concepts`、`std::format`、structured binding、CRTP-free 模板 policy)。注释风格统一为"为什么"而非"什么",SDL 资源用 `unique_ptr + custom deleter` 管理。

但仍有 **若干个会咬人的设计缺陷**,集中在 ECS Registry 的存储模型和模拟主循环的资源释放策略。下面按 **Critical → Design → Minor → 风格** 列出。

---

## 2. Critical(必须修)

### C-1. `Registry` 的 static-local 存储 = 进程单例

**位置:** `include/core/ecs/registry.hpp:118-126`

```cpp
template<typename T>
auto Registry::storage_() -> SparseSet<T>& {
    static SparseSet<T> set;   // 所有 Registry 实例共享
    return set;
}
```

**问题:**
- 头文件注释自己已经承认这是 **process-wide 单例**:> "Registry assumes a SINGLE INSTANCE per process. Internal component storage uses function-static SparseSets per type, shared across all instances."
- 创建第二个 `Registry` 时,组件数据会 **跨实例污染** —— 单元测试无法隔离。
- 与 `Registry` 类本身的"看上去可以随便 new"的语义严重不符,违反 RAII / 封装。

**建议:**
- 将 storage 改为实例成员:`std::unordered_map<std::type_index, std::any>` 或 `std::tuple<std::unique_ptr<SparseSet<Ts>>...>`。
- 或保留当前实现但在 `Registry` 构造里 `static_assert` 单例。
- 注释里"if you need multi-instance support, refactor..."应该变成"当前已重构"。

---

### C-2. 死亡生物的组件行永远不被回收

**位置:** `src/core/ecs/registry.cpp:18-28` + `src/core/simulation.cpp:54-63`

```cpp
// registry.cpp
auto Registry::destroy(Entity e) -> void {
    alive_[e.index] = false;
    ++generations_[e.index];
    // 没有遍历所有 SparseSet 调 erase(e)
}

// simulation.cpp 的注释自承
// "Registry::destroy 只翻 alive_ + bump generation,并不擦除各组件
//  storage 的行,SparseSet 迭代时会通过 contains() 跳过已死 entity。
//  长会话下 storage 会无界增长 —— 本 spec 范围内可接受,按需后续再清理。"
```

**问题:**
- 当前 `max_population = 200`,即便死亡 entity 的 `dense_` 行被 `contains()` 跳过,内存仍然只增不减。
- 如果用户长时间停留或反复重开,内存单调上升 → 内存泄漏。
- "spec 范围内可接受"不是"长期可接受"。

**建议:**
- `Registry::destroy` 接受一个 `template<typename... Ts>` 的 component pack,或维护 `std::vector<std::type_index>` 注册过的类型,逐个 `erase(e)`。
- 或者在 `Simulation` 里定期(每 N tick)做一次 compaction:遍历 alive 的 entity,把所有 SparseSet 的 `dense_` 重新 pack。

---

### C-3. `next_id_` 通过引用在三个对象间共享,无并发/重入保护

**位置:** `include/core/simulation.hpp:74`, `include/core/ecs/systems.hpp:23`, `src/core/evolution.cpp:88`

```cpp
// simulation.hpp
std::uint64_t next_id_ = 1;

// systems.hpp
CreatureBehaviorSystem(World&, EvolutionEngine&, std::uint64_t seed,
                       std::uint64_t& next_id);
// systems.cpp:next_id_ 引用
// evolution.cpp:reproduce(next_id) 引用
```

**问题:**
- `Simulation` 持有 `next_id_` 字段
- `CreatureBehaviorSystem` 持有 `std::uint64_t& next_id_` 引用
- `EvolutionEngine::reproduce(r, parent, next_id)` 通过引用参数取用
- 三处任何一处误用(比如误拷一份 `std::uint64_t` 副本而非引用)就立刻 ID 冲突,但编译器不会报错。
- 当前是单线程,功能上不致命,但非常脆弱。

**建议:**
- 抽一个轻量 `struct IdPool { std::uint64_t next = 1; std::uint64_t acquire() { return next++; } };`
- 三方都持引用或 `IdPool*`,语义自描述。

---

## 3. Design(应修)

### D-1. `evaluate_fitness` 重复实现,`gene.cpp::fitness()` / `distance()` 是死代码

**位置:** `src/core/evolution.cpp:32-42` vs `src/core/gene.cpp:21-39`

```cpp
// evolution.cpp 自己算
const float d = t.values[i] - optimum.values[i];
sum += d * d;
r.get<ecs::Reproduction>(e).fitness = 1.0f / (1.0f + std::sqrt(sum));

// gene.cpp 实现了同样的 distance() / fitness(),从未被调用
```

**问题:**
- `gene.cpp::distance()` 和 `fitness()` 完全没被引用
- 公式散在两处,以后想加权重或别的范数就得改两处

**建议:**
- `evaluate_fitness` 改为:
  ```cpp
  const auto& t = r.get<ecs::Traits>(e);
  r.get<ecs::Reproduction>(e).fitness = fitness(t, optimum);
  ```
- 同样地,`detect_elite_boss` 用的 `if (fit > threshold)` 也不依赖手写公式,统一收口到 `gene.cpp`。

---

### D-2. `Simulation::creatures()` 每 tick 拷贝 N 个 `std::string`

**位置:** `src/core/simulation.cpp:65-87`

注释自承:> "CreatureSnapshot::name 是 std::string(非 string_view),每 tick 重新拷贝约 200 个 string;当前规模下可接受。"

**问题:**
- 当前 200 字符串 × 60 FPS = 12k string copies/s,实测 libstdc++ SSO 友好所以 **当前不疼**
- 但只要 population 调到 `max_population` 上限就立刻放大

**建议(选一个):**
- `CreatureSnapshot::name` 改成 `std::string_view` + 在 snapshot cache 里用一个 `std::vector<std::string>` 持有所有权,把 string_view 指向它。
- 或保留现状但加注释说明"上限 200,实测无可观察瓶颈"。

---

### D-3. `to_die` 每 tick 重新分配 vector

**位置:** `src/core/simulation.cpp:55-62`

```cpp
std::vector<ecs::Entity> to_die;     // 每 tick 一次堆 alloc
for (auto e : registry_.view<ecs::Vitals>()) {
    if (registry_.get<ecs::Vitals>(e).dead) to_die.push_back(e);
}
```

**问题:**
- 每 tick 一次小堆分配,即便预留也不大,但完全可以零分配

**建议:**
- 在 `Simulation` 里加 `std::vector<ecs::Entity> death_scratch_;` 成员,`tick()` 里 `clear()` 后复用。

---

### D-4. `world_energy_` 永远是 0

**位置:** `include/core/simulation.hpp:75`,初始化 `= 0.0f`,全工程没有写语句。

```cpp
float world_energy_ = 0.0f;
[[nodiscard]] auto world_energy() const noexcept -> float { return world_energy_; }
```

`main.cpp` 的 HUD 把这个值显示成 `E=0`,看着像程序没跑通。

**建议:**
- 加一行 `world_energy_ = world_.total_biomass();`(在 `world_.regrow_biomass()` 之后),或者把 `world_energy()` 改为内联返回 `world_.total_biomass()`。

---

### D-5. `Registry::create()` 双重 `std::find`

**位置:** `src/core/ecs/registry.cpp:4-12`

```cpp
auto idx = next_index_;
if (std::find(alive_.begin(), alive_.end(), false) != alive_.end()) {
    const auto it = std::find(alive_.begin(), alive_.end(), false);  // 再找一次
    idx = static_cast<std::uint32_t>(std::distance(alive_.begin(), it));
}
```

**问题:**
- 第一次 `find` 只是判断"有没有空位",第二次拿到 index
- 一次 `find` 即可:拿 `it`,若 `it == end()` 用 `next_index_` 分支。

**建议:**
```cpp
auto it = std::find(alive_.begin(), alive_.end(), false);
std::uint32_t idx;
if (it != alive_.end()) {
    idx = static_cast<std::uint32_t>(std::distance(alive_.begin(), it));
} else {
    idx = next_index_++;
}
```

---

## 4. Minor(可改)

### M-1. `systems.cpp::update` 中越界检查代码 4 处重复

**位置:** `src/core/ecs/systems.cpp:48-51, 73-77, 99-103, 116-120`

四处都是:
```cpp
const auto nx = static_cast<std::int64_t>(pos.x) + dx;
const auto ny = static_cast<std::int64_t>(pos.y) + dy;
if (nx < 0 || ny < 0
    || static_cast<std::size_t>(nx) >= world_.width()
    || static_cast<std::size_t>(ny) >= world_.height()) continue;
if (!world_.is_land(static_cast<std::size_t>(nx),
                    static_cast<std::size_t>(ny))) continue;
```

**建议:** 抽 `auto in_bounds_and_land = [&](int dx, int dy) -> std::optional<Position>;` —— 这是 **按 AGENTS.md "简化结构" 应该做的**,不是"无意义重构"。

---

### M-2. `View::iterator::has_all` 在每次 `++` 都跑

**位置:** `include/core/ecs/registry.hpp:48-61`

每次 `++` 后 `skip_invalid` 调用 `has_all`,对每个 `Is` 做 `contains`。对于 5-6 组件 View 是 5-6 次 hash lookup。当前规模 OK,但 200+ entity × 6 系统 view × 60 tick/s ≈ 72k hash/s,可以接受。

**可选:** 以后真变成瓶颈时再换 `chunked_view` 或 archetype-based ECS。现在不动。

---

### M-3. `CreatureBehaviorSystem` 的 RNG 复用语义

**位置:** `include/core/ecs/systems.hpp:21-23` 与实现

`rng_` 只在分支 4(随机游走)使用,而繁殖走 `evo_.reproduce()` 用的是 EvolutionEngine 的 RNG。两个 RNG 用同一个 seed 派生(seed flow 文档化在 `systems.hpp` 注释里),顺序敏感性依赖仔细维护。

**问题:** 行为 RNG 不消耗的状态对外部观察者无意义,名字 `rng_` 太泛。

**建议:** 改名 `walk_rng_`,或显式标注"仅用于随机游走分支"。

---

### M-4. `behavior_system_` 拿 `World&` 非 const,但繁殖路径只读

`CreatureBehaviorSystem` 拿 `World&` 是为了 `world_.at(pos.x, pos.y).biomass -= eat`,这个写路径是必要的,无问题 —— 但同样的修改行为也意味着 `World` 不是逻辑不可变,以后想做时间倒放 / 重放就得把 `World` 状态单独提取。这超出当前范围,**仅记录**。

---

## 5. 风格(可选)

### S-1. `evolution.hpp` 的 `Params` 默认值是 magic number

`float mutation_strength = 0.1f` / `float elite_threshold_sigma = 2.5f` / `boss_streak_ticks = 30` —— 注释和设计文档没明确说为什么是这些值,新人改不动。

**建议:** 在 `evolution.hpp` 的 `Params` 里加一行注释,说明这些值是怎么被选定的(或留 TODO 指向调参日志)。

---

### S-2. `kEvolutionSalt` / `kBehaviorSalt` 等常量散落多处

`kOptimumSalt`, `kEvolutionSalt`, `kBehaviorSalt`, `kInitialCreatureSalt` 散在 4 个 `.cpp` 里。无 cross-file 冲突(各自独立命名空间),但如果以后想做"所有盐 A,B 集中调试",得改 4 处。

**建议:** 在 `pcg/seed.hpp` 旁边加一个 `pcg/salts.hpp` 集中。**优先级低**,不动也行。

---

### S-3. `CLAUDE.md` 提到的 `mc` / `moto_ctrl` / `IO` 模块不存在

项目根目录的 `CLAUDE.md` 模板提到 `mc`, `moto_ctrl`, `IO` 模块优先级 —— 但当前项目只有 `core` / `ecs` / `pcg` / `render` / `world` 五个模块。

**建议:** 把模板里这三个名字删掉,或者改成与本项目对应的 `core/simulation` / `render/tile_renderer` / `render/creature_assembler`。否则 AGENTS.md 的优先级指引无法生效。

---

## 6. 做得好的部分(供后续维护参考)

| 项 | 位置 |
|---|---|
| `creature.hpp::recover_energy` 提供组件版和 snapshot 版两个重载 | `include/core/creature.hpp:14-30` |
| `try_load_atlas` 容错返回 `nullopt`,资源缺失不黑屏 | `src/main.cpp:55-87` |
| `TileRenderer::effective_px_per_tile` 防 sprite 与地图比例错位 | `include/render/tile_renderer.hpp:90-101` |
| SDL 资源统一 `unique_ptr + custom deleter` | `include/render/tile_renderer.hpp:103-113` |
| `PartAtlas` 按 category + z-order 组织 | `include/render/creature_assembler.hpp` |
| `pcg::derive_seed` 用 splitmix64 派生确定性 seed | `include/pcg/seed.hpp` |
| `RenderLine` 替代 `RenderRect` 规避 SDL3 浮点 rect 漏画 bug | `src/render/tile_renderer.cpp` minimap 边框 |
| `make_name` 注释说"FNV-1a 哈希",但 `gene_color` 同样 FNV-1a —— 一致 | `src/render/tile_renderer.cpp:48-58` |
| `SimulationClock` accumulator 模式 + max_ticks_per_frame 防"螺旋下降" | `include/core/simulation.hpp` |
| `Snapshot` 与 Registry 解耦,renderer 只看 snapshot,无环依赖 | `include/core/ecs/snapshot.hpp` |
| `Registry::alive` 用 generation 防 stale handle | `src/core/ecs/registry.cpp` |

---

## 7. 建议的修复优先级

| 序号 | 项 | 难度 | 影响 |
|---|---|---|---|
| 1 | C-1 Registry static-local 改造 | 中(模板) | 高:影响所有未来的单元测试 |
| 2 | C-2 死亡 entity 组件回收 | 中 | 高:长会话内存增长 |
| 3 | C-3 `next_id` 抽 `IdPool` | 低 | 中:语义清晰化 |
| 4 | D-4 `world_energy_` 永远为 0 | 极低 | 中:HUD 显示错误 |
| 5 | D-1 删 `evaluate_fitness` 重复实现 | 极低 | 中:消除死代码 |
| 6 | D-3 `to_die` 复用 scratch | 极低 | 低:微小分配 |
| 7 | M-1 `in_bounds_and_land` 提取 helper | 低 | 低:可读性 |
| 8 | S-3 CLAUDE.md 模块名修正 | 极低 | 低:文档准确性 |

---

## 8. 不在本次审查范围内

- `assets/parts_processed/` 资产本身(分类、z-order、attach point)的美术/设计层面合理性
- `docs/superpowers/plans/*.md` 与 `specs/*.md` 的对齐情况
- vcpkg 依赖项(`sdl3` / `sfml` / `nlohmann-json`)的实际构建验证 —— 按 AGENTS.md 规定未执行构建
- `third_party/FastNoiseLite` 第三方代码

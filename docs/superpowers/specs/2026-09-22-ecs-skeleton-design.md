# ECS 骨架（Spec A）

**日期**: 2026-09-22
**状态**: 待批准
**作者**: 自动（brainstorming → writing-plans）
**前置**: 无
**后续**: Spec B（8 维基因 + 物种聚类）、Spec C（行为系统 + 捕食）

## 背景

当前 `Creature` 是一个结构体，所有数据（位置/基因/饥饿/能量/年龄/身份/适应度）
堆在一个 struct 里。`Simulation::tick()` 直接遍历 `creatures_` vector，调用
`decide_and_act(c, ...)` 处理每只生物。

这种结构的问题（对照未来 Spec B/C）：

| 问题 | 影响 |
|------|------|
| `Creature` 字段膨胀 | Spec B 加 Species / Spec C 加 BehaviorState 后字段会爆 |
| 行为和数据耦合 | `decide_and_act` 是 free function，要靠外部传 vector，未来难拆 system |
| 死亡 swap-erase 与 `next_id_` 错位 | index 重用后旧 creature 还会被旧 ID 引用 |
| 没有 entity 身份层 | renderer 拿到的 span 与 simulation 内部状态耦合 |

设计文档（《世界意志》）的第九节明确：生态模拟走 **ECS 架构 + 实体池**。
先把骨架搭起来再填血肉，避免每次加新维度都大改。

## 目标

1. 把 `Creature` 的字段平移到 **组件**（Position / Traits / Vitals / Identity / Reproduction）。
2. 引入 **Registry**，统一管理 entity 生命周期、组件存储、视图查询。
3. 引入 **System concept**，规范系统接口，为 Spec B/C 留接口位。
4. **行为完全不变**：同 seed 出同 trajectory，main.cpp / renderer / WorldGenerator 一行不动。
5. 通过新 CLAUDE.md 规则：模板 + concepts + std::expected + std::span + std::ranges 激进使用。

## 不在范围

- 不动 Traits 维度（保持 4 维）。
- 不引入 Species 组件 / 聚类（Spec B 范围）。
- 不引入 BehaviorState 组件 / 行为系统（Spec C 范围）。
- 不重构 Renderer / WorldGenerator / World。
- 不引入外部 ECS 库（手写，~300 行）。

## 设计

### 1. 模块布局

新增 `include/core/ecs/` 与 `src/core/ecs/`：

```
include/core/ecs/
    entity.hpp         // Entity ID 类型 + generation 策略
    concepts.hpp       // Component / System concept 定义
    sparse_set.hpp     // 单类型组件存储（template）
    registry.hpp       // 顶层注册表 + view 查询
    components.hpp     // Position / Traits / Vitals / Identity / Reproduction
    systems.hpp        // System 调度器（按 phase 顺序）

src/core/ecs/
    registry.cpp       // Entity 池 / generation 回收
```

### 2. Entity

```cpp
// include/core/ecs/entity.hpp
namespace game::ecs {

struct Entity {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] friend auto operator==(Entity, Entity) -> bool = default;
};

}  // namespace game::ecs
```

- `index`：实体在 `entities_` vector 中的位置。
- `generation`：每次 `destroy()` 自增；持有旧 generation 的 EntityHandle 自动失效。
- 永不重用 index（spec 期内累计 < 100 万，资源无忧）。`next_id_` 这个独立字段保留（兼容现有 `Creature::id` 语义）。

### 3. Component concept

```cpp
// include/core/ecs/concepts.hpp
namespace game::ecs {

template<typename T>
concept Component = std::is_trivially_copyable_v<T>
                  && std::is_default_constructible_v<T>
                  && requires { typename T::is_component; };

template<typename T>
inline constexpr bool is_component_v = Component<T>;

}  // namespace game::ecs
```

每个组件类型自声明 `using is_component = void;` 作为 tag。traits 类型用模板
确保它们继续走 `std::is_trivially_copyable_v` 通道（结构体里只放 trivially
copyable 字段）。

### 4. SparseSet

```cpp
// include/core/ecs/sparse_set.hpp
namespace game::ecs {

template<Component T>
class SparseSet {
public:
    auto insert(Entity e, T value) -> void;
    auto erase(Entity e) -> bool;            // O(1) swap-erase
    [[nodiscard]] auto contains(Entity e) const -> bool;
    [[nodiscard]] auto get(Entity e) -> T&;
    [[nodiscard]] auto get(Entity e) const -> const T&;

    [[nodiscard]] auto dense()       const -> std::span<const T>       { return dense_; }
    [[nodiscard]] auto dense_entities() const -> std::span<const Entity> { return dense_entities_; }

    auto clear() -> void;

private:
    std::vector<T>      dense_;             // 紧凑数组，迭代友好
    std::vector<Entity> dense_entities_;    // 与 dense_ 一一对应
    std::vector<std::pair<std::uint32_t, bool>> sparse_;  // [index] -> (dense_pos, alive)
};

}  // namespace game::ecs
```

- `dense_entities_[i]` 是 dense 数组第 i 项对应的 Entity。
- `sparse_[e.index]` = `{dense_pos, alive}`，alive=false 表示 slot 已被回收。
- 删除时：与末尾 swap，O(1)。`dense_` 顺序不稳定，行为不变性靠 RNG 保证（**见风险**）。

### 5. Registry

```cpp
// include/core/ecs/registry.hpp
namespace game::ecs {

class Registry {
public:
    Registry() = default;

    // Entity 生命周期
    [[nodiscard]] auto create() -> Entity;
    auto destroy(Entity e) -> void;
    [[nodiscard]] auto alive(Entity e) const -> bool;

    // 单组件
    template<Component T>
    auto emplace(Entity e, T value = {}) -> T&;

    template<Component T>
    auto get(Entity e) -> T&;

    template<Component T>
    auto try_get(Entity e) -> std::optional<std::reference_wrapper<T>>;

    template<Component T>
    auto erase(Entity e) -> bool;

    template<Component T>
    [[nodiscard]] auto storage() -> SparseSet<T>&;

    // 视图（多组件交集）
    template<Component... Ts>
    [[nodiscard]] auto view() -> View<Ts...>;

    // 整局清理
    auto reset() -> void;

private:
    std::vector<std::uint32_t> generations_;
    std::vector<bool>          alive_;
    std::uint32_t              next_index_ = 0;

    // 类型擦除：每个组件类型独立 SparseSet
    // 用模板成员函数 + std::tuple<unique_ptr<SparseSet<T>>...>
    template<Component T>
    auto storage_() -> SparseSet<T>&;
};

}  // namespace game::ecs
```

`view<Ts...>()` 返回一个迭代器对，迭代时对 `dense_entities_` 求交：选最小
稀疏集的 dense，逐项检查其他组件是否包含。这是 entt 的 view 模式，O(N+k)
其中 N 是最小集合大小、k 是其他集合查询代价。

### 6. 组件定义

```cpp
// include/core/ecs/components.hpp
namespace game::ecs {

struct Position {
    using is_component = void;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct Traits {
    using is_component = void;
    std::array<float, 4> values{};   // 保持 4 维；Spec B 改 8 维
};

struct Vitals {
    using is_component = void;
    float       hunger = 1.0f;
    float       energy = 1.0f;
    std::uint32_t age  = 0;
    bool          dead = false;
};

struct Identity {
    using is_component = void;
    std::uint64_t      id         = 0;     // 全局单调（替代 next_id_ 概念）
    std::string        name;
    bool               is_elite   = false;
    bool               is_boss    = false;
    std::uint32_t      elite_age  = 0;
};

struct Reproduction {
    using is_component = void;
    float fitness = 0.0f;
};

}  // namespace game::ecs
```

注：`std::string` 不是 trivially_copyable。`Identity` 必须满足 Component
concept（is_trivially_copyable_v）。两种选择：

- **(选)** 把 `name` 抽到独立 `Name` 组件；`Identity` 退化为 POD。Spec B
  物种名也能复用 Name。
- 改 concept 放行 `std::string`（不推荐，破坏 trivially_copy 假设）。

→ **采用抽 Name**。最终 6 个组件：Position / Traits / Vitals / Identity
（仅 ID + elite/boss 标志）/ Name / Reproduction。

### 7. System concept

```cpp
// include/core/ecs/concepts.hpp（追加）
namespace game::ecs {

template<typename S>
concept System = std::is_default_constructible_v<S>
              && requires(S& s, Registry& r, std::uint64_t tick) {
                   { s.update(r, tick) } -> std::same_as<void>;
               };

template<typename... Ts>
concept AllComponents = (Component<Ts> && ...);

}  // namespace game::ecs
```

### 8. Simulation 编排

`Simulation::creatures()` 保留 API：返回 `std::span<const CreatureSnapshot>`，
renderer / main.cpp 一行不动。`CreatureSnapshot` 是平铺结构（id + name +
gene + pos + hunger + ...），由 `creatures()` 内部从 registry 拼装（O(N)，
每帧一次，可接受）。

具体实现：
```cpp
struct CreatureSnapshot {
    std::uint64_t id;
    std::string_view name;
    game::Traits   gene;
    Position       pos;
    float          hunger;
    float          energy;
    std::uint32_t  age;
    float          fitness;
    bool           is_elite;
    bool           is_boss;
};
```

快照拼装时按 `Identity::id` 排序（保持稳定顺序），与现有行为一致。

### 9. System 落地

把现有 free function 改造为 system：

| 现有 | 新 |
|------|-----|
| `decide_and_act(c, world, births, evo, rng, next_id)` | `CreatureBehaviorSystem::update(r, tick)`（**直接删除**旧函数，不留兼容） |
| `EvolutionEngine::evaluate_fitness` | 内部改用 registry 视图 |
| `EvolutionEngine::detect_elite_boss` | 同上 |

`CreatureBehaviorSystem` 持有 `World&` / `EvolutionEngine&` / `std::mt19937_64&`
引用；每次 `update` 走 `view<Position, Traits, Vitals, Identity, Name, Reproduction>`
迭代。**视觉上行为完全不变**。

**RNG 调用顺序保持**：view 按 `Identity::id` 排序后迭代，等价于原
`creatures_` vector 的天然顺序（`erase_if` 保相对顺序 + births
`push_back` 续号 → vector 顺序 ≡ id 升序），RNG 调用次数与位置与原代码一致。

**生命周期**：`World` / `EvolutionEngine` / RNG 由 `Simulation` 持有，所有
system 通过引用借用；Simulation 全程存活，system 内无悬空。

### 10. 新 CLAUDE.md 规则的体现

- **模板优先**：`SparseSet<T>` / `View<Ts...>` / `System` concept 全模板。
- **`std::expected`**：`Registry::create()` 不可能失败，但保留 `try_*` 入口
  返回 `std::optional` / `std::expected`；`view()` 不可能失败，返回 `View<Ts...>`。
- **`std::span`**：所有"指针+长度"参数换 `std::span`（`dense()` / `creatures()`）。
- **`std::ranges`**：内部聚合用 `std::ranges::sort` / `std::ranges::any_of` 等。
- **错误处理**：现有 API 不抛异常；新 API 返回 `std::expected`。
- **编译期**：`if constexpr` 在 view 模板中按组件数分支。

### 11. 兼容性

- `core/creature.hpp` 保留 `Creature` 类型作为 `CreatureSnapshot` 的 alias：
  ```cpp
  using Creature = CreatureSnapshot;
  ```
  旧代码通过 `sim.creatures()` 仍能拿到扁平结构。
- `decide_and_act(c, ...)` 函数**删除**（spec 9 已声明）。`make_name` /
  `recover_energy` 保留为头文件 inline 函数供 system 复用。

## 验证

1. **构建**：`-Wall -Wextra -Wpedantic -Werror` 无 warning。
2. **运行同 seed 比对**：
   - 用 git stash 暂存重构
   - 跑 baseline 60 帧，存 `temp/baseline.bin`（序列化 sim 状态）
   - 应用本 spec 实现
   - 同 seed 重跑，对比 hash 一致
3. **随机行为不变**：`CreatureBehaviorSystem` 内部的 RNG 顺序、调用顺序与
   原 `decide_and_act` 完全一致；`reproduce` / `eat` / `move` 的概率分支不变。
4. **渲染不变**：截屏对比。
5. **死亡 / 重生一致**：触发种群灭绝 → 自动重启 5 只，generation 自增，
   旧 Entity handle 在 renderer 端因 `CreatureSnapshot` 已经拷贝数据而不受影响。

## 风险

| 风险 | 缓解 |
|------|------|
| `dense_` swap-erase 后生物遍历顺序变化 → RNG 调用顺序变化 → 行为漂移 | `view` 迭代前对 entity 按 `Identity::id` 排序，RNG 行为不变 |
| `std::string` 不是 trivially_copyable → Identity 不能直接是 Component | 抽 `Name` 组件隔离字符串 |
| view 模板展开膨胀（多组件组合编译慢） | 当前只有 6 个组件，组合数 64 可控；按需编译 |
| `CreatureSnapshot` 每帧拼装性能（拷贝 / 字符串视图失效） | `name` 用 `std::string_view`，指向 registry 内 `std::string`；registry 在 Simulation 全程存活，无悬空 |
| 测试覆盖不足：k-means、捕食未来对组件顺序敏感 | 此 spec 行为完全不变 → Spec B/C 才有新风险；本 spec 不引入新算法 |

## 拆分建议（writing-plans 阶段细化）

1. entity.hpp + concepts.hpp + sparse_set.hpp（含单元自测）
2. registry.hpp / .cpp（含 entity 池）
3. components.hpp（含 Name 组件）
4. CreatureSnapshot + Simulation::creatures() 适配
5. Simulation 内部走 registry（行为循环）
6. EvolutionEngine 内部走 registry
7. 端到端 build + 运行 + 截屏对比


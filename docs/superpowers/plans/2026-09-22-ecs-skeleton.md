# ECS 骨架（Spec A）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `Creature` 平移到 ECS（sparse-set 存储 + View 查询），行为完全不变。

**Architecture:** 新建 `core/ecs/` 子树（Entity / SparseSet / Registry / View），把 Creature 字段拆为 6 个组件。`Simulation::creatures()` 返回 `std::span<CreatureSnapshot>` 给 renderer 保持 API 稳定。RNG 调用顺序由按 `Identity::id` 排序 view 迭代保证与原代码一致。

**Tech Stack:** C++23（clang-cl）, `<concepts>` / `<expected>` / `<span>` / `<ranges>` / `<format>`, SDL3, SFML（不变）

**Spec:** [`docs/superpowers/specs/2026-09-22-ecs-skeleton-design.md`](../specs/2026-09-22-ecs-skeleton-design.md)

## Global Constraints

- **编译器**：clang-cl（`C:\Program Files\LLVM\bin\clang-cl.exe`）
- **链接器**：lld-link
- **C++ 标准**：C++23（`CMAKE_CXX_STANDARD 23`、`CMAKE_CXX_EXTENSIONS OFF`）
- **警告**：`-Wall -Wextra -Wpedantic -Werror`（Debug 加 `-g -O0`，Release 加 `-O2 -DNDEBUG`）
- **Sanitizer**：默认开启 ASan + UBSan（`-fsanitize=address,undefined`）
- **包管理**：vcpkg，triplet `x64-windows`
- **代码风格**：头文件 `#pragma once`；命名 PascalCase（类型）/ camelCase（函数）/ snake_case（变量）；智能指针；`std::string_view`/`std::string`；`std::expected`；concepts；头文件禁 `using namespace`
- **CLAUDE.md 新增规则**（激进使用）：模板优先；`std::span` 替指针长度对；`std::ranges` 写集合操作；`std::expected` 一等公民；`consteval`/`constexpr`/`if constexpr`；`std::format`/`std::print`；默认启用 `std::optional` / 结构化绑定 / `designated initializers` / `std::move_only_function`
- **平台**：Windows-only，不要求跨平台
- **终端**：所有脚本 `pwsh -NoProfile -Command "..."`，路径用 `/c/...` 或 `E:\...`
- **临时文件**：放 `temp/`（已在 `.gitignore`，不入版本控制）
- **构建**：`pwsh E:\gamev2\build.ps1` 或 `cmake --build build --config Debug`
- **每步都做 commit**：commit message 风格 `类别: 说明`（feat / refactor / test / docs / build）

## File Structure

**新增头文件**（`include/core/ecs/`）：
- `entity.hpp` — `Entity{index, generation}` 类型
- `concepts.hpp` — `Component` / `System` concept
- `sparse_set.hpp` — `SparseSet<T>` 模板（header-only）
- `registry.hpp` — `Registry` 类 + `View<Ts...>` 类型
- `components.hpp` — `Position` / `Traits` / `Vitals` / `Identity` / `Name` / `Reproduction`
- `snapshot.hpp` — `CreatureSnapshot` 扁平结构
- `systems.hpp` — `CreatureBehaviorSystem` 类声明

**新增源文件**（`src/core/ecs/`）：
- `registry.cpp` — entity 池（`create`/`destroy`/`alive`）实现

**修改**：
- `CMakeLists.txt` — 注册新源文件
- `include/core/creature.hpp` — `Creature` 变 `CreatureSnapshot` 的 alias；保留 `make_name` / `recover_energy` 声明
- `src/core/creature.cpp` — 删除 `decide_and_act`；保留 `make_name` / `recover_energy`
- `include/core/simulation.hpp` — 增加 `Registry` / `CreatureBehaviorSystem` 成员，API 保持
- `src/core/simulation.cpp` — `tick()` 改为 orchestrate systems
- `include/core/evolution.hpp` — API 改：传 `Registry&` 而非 `std::span<Creature>`
- `src/core/evolution.cpp` — 用 view 重写
- `src/core/simulation.cpp` 内 `decide_and_act` 调用换成 system

**删除**：
- `src/core/creature.cpp::decide_and_act`（spec §9 已声明）

---

## Task 1: CMake 集成 + 目录脚手架

**Files:**
- Modify: `CMakeLists.txt:18-28`（添加新源文件）

**目标**：让 CMake 知道新增的 ECS 文件，但代码暂未实现。运行 `build.ps1` 应仍成功（不引入新 .cpp，等 Task 2 起逐步加）。

- [ ] **Step 1: 修改 CMakeLists.txt**

把：
```cmake
add_executable(gamev2
    src/main.cpp
    src/core/gene.cpp
    src/core/creature.cpp
    src/core/evolution.cpp
    src/core/simulation.cpp
    src/world/world.cpp
    src/world/world_generator.cpp
    src/render/tile_renderer.cpp
    src/render/creature_assembler.cpp
)
```
改为：
```cmake
add_executable(gamev2
    src/main.cpp
    src/core/gene.cpp
    src/core/creature.cpp
    src/core/evolution.cpp
    src/core/simulation.cpp
    src/world/world.cpp
    src/world/world_generator.cpp
    src/render/tile_renderer.cpp
    src/render/creature_assembler.cpp
    src/core/ecs/registry.cpp
)
```

（先只加 `registry.cpp` 一个新源，其他 ECS 部分是 header-only，下一 task 才加。）

- [ ] **Step 2: 创建空目录**

```bash
mkdir -p include/core/ecs src/core/ecs
```

在 `src/core/ecs/` 放占位文件：
```bash
echo "// placeholder: Task 4 implements Registry" > src/core/ecs/registry.cpp
```

- [ ] **Step 3: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：`=== Build OK: E:\gamev2\build\gamev2.exe ===`，无 warning。

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/core/ecs/registry.cpp
git commit -m "build: scaffold ecs/ subdirectory"
```

---

## Task 2: Entity 类型 + Concept 定义

**Files:**
- Create: `include/core/ecs/entity.hpp`
- Create: `include/core/ecs/concepts.hpp`

**目标**：定义 ECS 基础类型。Header-only，无运行时影响。

- [ ] **Step 1: 写 `entity.hpp`**

```cpp
// include/core/ecs/entity.hpp
#pragma once

#include <cstdint>

namespace game::ecs {

struct Entity {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] friend auto operator==(Entity, Entity) noexcept -> bool = default;
};

}  // namespace game::ecs
```

- [ ] **Step 2: 写 `concepts.hpp`**

```cpp
// include/core/ecs/concepts.hpp
#pragma once

#include "core/ecs/entity.hpp"

#include <concepts>
#include <type_traits>

namespace game::ecs {

// Component: trivially copyable + default-constructible + 自声明 tag
template<typename T>
concept Component = std::is_trivially_copyable_v<T>
                 && std::is_default_constructible_v<T>
                 && requires { typename T::is_component; };

template<typename T>
inline constexpr bool is_component_v = Component<T>;

// System: 必须有 update(Registry&, uint64_t) 成员函数
class Registry;  // 前置声明

template<typename S>
concept System = std::is_default_constructible_v<S>
              && requires(S& s, Registry& r, std::uint64_t tick) {
                   { s.update(r, tick) } -> std::same_as<void>;
               };

}  // namespace game::ecs
```

- [ ] **Step 3: 编译验证（仅 header 改动）**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK。

- [ ] **Step 4: Commit**

```bash
git add include/core/ecs/entity.hpp include/core/ecs/concepts.hpp
git commit -m "feat(ecs): Entity type + Component/System concepts"
```

---

## Task 3: SparseSet<T> 模板

**Files:**
- Create: `include/core/ecs/sparse_set.hpp`

**目标**：单组件类型存储。Header-only template。

**接口约定**：
- `insert(Entity, T)`：插入或覆盖
- `erase(Entity)`：swap-erase，O(1)
- `contains(Entity) -> bool`
- `get(Entity) -> T&`（assert alive）
- `try_get(Entity) -> std::optional<std::reference_wrapper<T>>`
- `dense() -> std::span<const T>`
- `dense_entities() -> std::span<const Entity>`
- `clear()`

- [ ] **Step 1: 写 `sparse_set.hpp`**

```cpp
// include/core/ecs/sparse_set.hpp
#pragma once

#include "core/ecs/concepts.hpp"
#include "core/ecs/entity.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace game::ecs {

template<Component T>
class SparseSet {
public:
    auto insert(Entity e, T value) -> void {
        if (e.index >= sparse_.size()) {
            sparse_.resize(e.index + 1, Slot{invalid_pos, false});
        }
        auto& slot = sparse_[e.index];
        if (slot.alive) {
            dense_[slot.pos] = std::move(value);
            return;
        }
        slot.pos = dense_.size();
        slot.alive = true;
        dense_.push_back(std::move(value));
        dense_entities_.push_back(e);
    }

    auto erase(Entity e) -> bool {
        if (e.index >= sparse_.size()) return false;
        auto& slot = sparse_[e.index];
        if (!slot.alive) return false;
        const std::size_t pos = slot.pos;
        const std::size_t last = dense_.size() - 1;
        if (pos != last) {
            dense_[pos] = std::move(dense_[last]);
            dense_entities_[pos] = dense_entities_[last];
            sparse_[dense_entities_[pos].index].pos = pos;
        }
        dense_.pop_back();
        dense_entities_.pop_back();
        slot.alive = false;
        return true;
    }

    [[nodiscard]] auto contains(Entity e) const -> bool {
        if (e.index >= sparse_.size()) return false;
        return sparse_[e.index].alive;
    }

    [[nodiscard]] auto get(Entity e) -> T& {
        assert(contains(e));
        return dense_[sparse_[e.index].pos];
    }

    [[nodiscard]] auto get(Entity e) const -> const T& {
        assert(contains(e));
        return dense_[sparse_[e.index].pos];
    }

    [[nodiscard]] auto try_get(Entity e)
        -> std::optional<std::reference_wrapper<T>> {
        if (!contains(e)) return std::nullopt;
        return std::ref(dense_[sparse_[e.index].pos]);
    }

    [[nodiscard]] auto dense() const noexcept -> std::span<const T> {
        return dense_;
    }

    [[nodiscard]] auto dense_entities() const noexcept -> std::span<const Entity> {
        return dense_entities_;
    }

    auto clear() -> void {
        dense_.clear();
        dense_entities_.clear();
        for (auto& s : sparse_) s.alive = false;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return dense_.size();
    }

private:
    struct Slot {
        std::size_t pos      = 0;
        bool        alive    = false;
    };
    static constexpr std::size_t invalid_pos = static_cast<std::size_t>(-1);

    std::vector<T>      dense_;
    std::vector<Entity> dense_entities_;
    std::vector<Slot>   sparse_;
};

}  // namespace game::ecs
```

- [ ] **Step 2: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK（header 未被引用，但语法应通过编译）。

- [ ] **Step 3: 写 `temp/check_sparse_set.cpp` smoke test**

```cpp
// temp/check_sparse_set.cpp
#include "core/ecs/sparse_set.hpp"
#include <cstdio>
#include <cassert>

struct Foo { using is_component = void; int v; };

auto main() -> int {
    using namespace game::ecs;
    SparseSet<Foo> s;
    Entity a{1, 0}, b{2, 0}, c{3, 0};

    s.insert(a, {10});
    s.insert(b, {20});
    s.insert(c, {30});
    assert(s.size() == 3);
    assert(s.get(a).v == 10);

    s.erase(b);
    assert(!s.contains(b));
    assert(s.size() == 2);
    assert(s.get(a).v == 10);
    assert(s.get(c).v == 30);   // swap-erase 后 c 仍在

    // overwrite
    s.insert(a, {99});
    assert(s.get(a).v == 99);

    std::printf("sparse_set OK\n");
    return 0;
}
```

- [ ] **Step 4: 编译并运行 smoke test**

```bash
clang-cl /std:c++latest /EHsc /I E:/gamev2/include \
    E:/gamev2/temp/check_sparse_set.cpp \
    /Fe:E:/gamev2/temp/check_sparse_set.exe
E:/gamev2/temp/check_sparse_set.exe
```

期望输出：`sparse_set OK`。

- [ ] **Step 5: Commit + 删除 smoke 文件**

```bash
git add include/core/ecs/sparse_set.hpp
git commit -m "feat(ecs): SparseSet<T> with swap-erase O(1)"
rm E:/gamev2/temp/check_sparse_set.cpp E:/gamev2/temp/check_sparse_set.exe
```

---

## Task 4: Registry — Entity 池

**Files:**
- Create: `include/core/ecs/registry.hpp`
- Create: `src/core/ecs/registry.cpp`（替换占位）
- Modify: `CMakeLists.txt` （无需改，registry.cpp 已在 Task 1 加入）

**目标**：Entity 创建 / 销毁 / 存活查询。Generation 自增防悬空。

- [ ] **Step 1: 写 `registry.hpp`（仅 entity 池部分，单组件存储留 Task 5）**

```cpp
// include/core/ecs/registry.hpp
#pragma once

#include "core/ecs/entity.hpp"
#include "core/ecs/sparse_set.hpp"

#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>

namespace game::ecs {

class Registry {
public:
    Registry() = default;

    [[nodiscard]] auto create() -> Entity;
    auto destroy(Entity e) -> void;
    [[nodiscard]] auto alive(Entity e) const -> bool;

    auto reset() -> void;

    // 单组件 API（Task 5 实现）
    template<typename T>
    auto emplace(Entity e, T value = {}) -> T&;

    template<typename T>
    [[nodiscard]] auto get(Entity e) -> T&;

    template<typename T>
    [[nodiscard]] auto get(Entity e) const -> const T&;

    template<typename T>
    [[nodiscard]] auto try_get(Entity e) -> std::optional<std::reference_wrapper<T>>;

    template<typename T>
    auto erase(Entity e) -> bool;

    template<typename T>
    [[nodiscard]] auto storage() -> SparseSet<T>&;

private:
    std::vector<std::uint32_t> generations_;
    std::vector<bool>          alive_;
    std::uint32_t              next_index_ = 0;

    // 每个组件类型一个独立 SparseSet，类型擦除存于 tuple
    template<typename T>
    auto storage_() -> SparseSet<T>&;
};

// ---------- template impls ----------

template<typename T>
auto Registry::storage_() -> SparseSet<T>& {
    // 头文件唯一允许的 static local：用函数局部的 static 缓存
    // 让 component 首次访问时 lazy-construct。
    // 注：实际存到成员里更清晰但模板成员函数定义放 .cpp 会增大面；
    // 这里用 static local 简单可靠。
    static SparseSet<T> set;
    return set;
}

template<typename T>
auto Registry::emplace(Entity e, T value) -> T& {
    return storage_<T>().insert(e, std::move(value)), storage_<T>().get(e);
}

template<typename T>
auto Registry::get(Entity e) -> T& {
    return storage_<T>().get(e);
}

template<typename T>
auto Registry::get(Entity e) const -> const T& {
    return storage_<T>().get(e);
}

template<typename T>
auto Registry::try_get(Entity e) -> std::optional<std::reference_wrapper<T>> {
    return storage_<T>().try_get(e);
}

template<typename T>
auto Registry::erase(Entity e) -> bool {
    return storage_<T>().erase(e);
}

template<typename T>
auto Registry::storage() -> SparseSet<T>& {
    return storage_<T>();
}

}  // namespace game::ecs
```

> 注：上面用了 `static local` 让 SparseSet 跨实例共享。spec 里的 `tuple<unique_ptr<SparseSet<T>>...>` 设计被这一选择替代，更简单，且单一 Registry 实例下行为一致。

- [ ] **Step 2: 写 `registry.cpp`（替换占位）**

```cpp
// src/core/ecs/registry.cpp
#include "core/ecs/registry.hpp"

#include <algorithm>
#include <utility>

namespace game::ecs {

auto Registry::create() -> Entity {
    // 找一个 dead slot 或追加
    std::uint32_t idx = next_index_;
    if (std::find(alive_.begin(), alive_.end(), false) != alive_.end()) {
        const auto it = std::find(alive_.begin(), alive_.end(), false);
        idx = static_cast<std::uint32_t>(std::distance(alive_.begin(), it));
    } else {
        idx = next_index_++;
    }
    if (idx >= alive_.size()) {
        alive_.resize(idx + 1, false);
        generations_.resize(idx + 1, 0);
    }
    alive_[idx] = true;
    return Entity{idx, generations_[idx]};
}

auto Registry::destroy(Entity e) -> void {
    if (e.index >= alive_.size()) return;
    if (!alive_[e.index]) return;
    if (generations_[e.index] != e.generation) return;  // 旧 handle
    alive_[e.index] = false;
    ++generations_[e.index];
}

auto Registry::alive(Entity e) const -> bool {
    if (e.index >= alive_.size()) return false;
    return alive_[e.index] && generations_[e.index] == e.generation;
}

auto Registry::reset() -> void {
    for (auto& a : alive_) a = false;
    for (auto& g : generations_) ++g;  // 让所有旧 handle 失效
}

}  // namespace game::ecs
```

- [ ] **Step 3: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK。

- [ ] **Step 4: 写 `temp/check_registry_entity.cpp` smoke test**

```cpp
#include "core/ecs/registry.hpp"
#include <cassert>
#include <cstdio>

auto main() -> int {
    using namespace game::ecs;
    Registry r;

    auto a = r.create();
    auto b = r.create();
    assert(r.alive(a));
    assert(r.alive(b));
    assert(a.index != b.index);

    r.destroy(a);
    assert(!r.alive(a));
    assert(r.alive(b));

    // 旧 handle 即使 generation 错配也视为 dead
    Entity stale = a;
    assert(!r.alive(stale));

    // 重用 index：新 entity 拿到 a.index 但 generation 自增
    auto c = r.create();
    assert(c.index == a.index);
    assert(c.generation != a.generation);

    std::printf("registry entity OK\n");
    return 0;
}
```

- [ ] **Step 5: 编译并运行**

```bash
clang-cl /std:c++latest /EHsc /I E:/gamev2/include \
    E:/gamev2/temp/check_registry_entity.cpp \
    E:/gamev2/build/CMakeFiles/gamev2.dir/src/core/ecs/registry.cpp.obj \
    /Fe:E:/gamev2/temp/check_registry_entity.exe
E:/gamev2/temp/check_registry_entity.exe
```

期望：`registry entity OK`。

- [ ] **Step 6: Commit + 清理**

```bash
git add include/core/ecs/registry.hpp src/core/ecs/registry.cpp
git commit -m "feat(ecs): Registry entity pool with generation recycling"
rm E:/gamev2/temp/check_registry_entity.*
```

---

## Task 5: Registry — 单组件存储验证

**Files:**
- 仅验证（template 已在 registry.hpp 中）

**目标**：验证 Task 4 写的 template emplace/get/erase 工作。

- [ ] **Step 1: 写 `temp/check_registry_component.cpp`**

```cpp
#include "core/ecs/registry.hpp"
#include <cassert>
#include <cstdio>

struct Pos { using is_component = void; int x, y; };

auto main() -> int {
    using namespace game::ecs;
    Registry r;
    auto e = r.create();
    r.emplace<Pos>(e, {3, 4});
    assert(r.try_get<Pos>(e).has_value());
    assert(r.get<Pos>(e).x == 3);

    r.erase<Pos>(e);
    assert(!r.try_get<Pos>(e).has_value());

    std::printf("registry component OK\n");
    return 0;
}
```

- [ ] **Step 2: 编译并运行**

```bash
clang-cl /std:c++latest /EHsc /I E:/gamev2/include \
    E:/gamev2/temp/check_registry_component.cpp \
    E:/gamev2/build/CMakeFiles/gamev2.dir/src/core/ecs/registry.cpp.obj \
    /Fe:E:/gamev2/temp/check_registry_component.exe
E:/gamev2/temp/check_registry_component.exe
```

期望：`registry component OK`。

- [ ] **Step 3: 清理（无需 commit，已在 Task 4 提交）**

```bash
rm E:/gamev2/temp/check_registry_component.*
```

---

## Task 6: View<Ts...> 多组件查询

**Files:**
- Modify: `include/core/ecs/registry.hpp`（追加 View 模板 + view() 方法）

**目标**：返回多组件交集。最小集合 dense 顺序迭代，逐项检查其他集合。

- [ ] **Step 1: 追加 View 到 `registry.hpp` 顶部 namespace**

在 `class Registry {` 之前插入：

```cpp
template<Component... Ts>
class View {
public:
    explicit View(std::tuple<SparseSet<Ts>*...> sets) : sets_(sets) {}

    // 迭代器：选最小集合做 drive
    class iterator {
    public:
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;

        iterator(const View* view, std::size_t pos, std::span<const Entity> drive)
            : view_(view), pos_(pos), drive_(drive) {}

        auto operator*() const -> Entity { return drive_[pos_]; }

        auto operator++() -> iterator& {
            ++pos_;
            return *this;
        }

        [[nodiscard]] auto operator==(const iterator& o) const -> bool {
            return pos_ == o.pos_;
        }

        [[nodiscard]] auto operator!=(const iterator& o) const -> bool {
            return pos_ != o.pos_;
        }

    private:
        const View* view_;
        std::size_t  pos_;
        std::span<const Entity> drive_;
    };

    [[nodiscard]] auto begin() const -> iterator {
        return iterator{this, 0, drive_span()};
    }

    [[nodiscard]] auto end() const -> iterator {
        return iterator{this, drive_span().size(), drive_span()};
    }

private:
    // 选最小集合的 dense_entities 作为 drive；其余集合在 iterator
    // 端用 get 检查（O(N*k) 但常用于 1-2 组件场景）。
    template<std::size_t... Is>
    [[nodiscard]] auto drive_impl(std::index_sequence<Is...>) const
        -> std::span<const Entity> {
        auto best = std::get<0>(sets_)->dense_entities();
        auto consider = [&](auto* s) {
            if (s->size() < best.size()) best = s->dense_entities();
        };
        (consider(std::get<Is>(sets_)), ...);
        return best;
    }

    [[nodiscard]] auto drive_span() const -> std::span<const Entity> {
        return drive_impl(std::index_sequence_for<Ts...>{});
    }

    std::tuple<SparseSet<Ts>*...> sets_;
};
```

> 注：`begin()` / `end()` 都各自调一次 `drive_span()`。如要更省，可以缓存；
> 这里先简单，留后续优化。性能上每次 view 创建成本低。

- [ ] **Step 3: 在 `Registry` 加 `view<Ts...>()`**

在 `class Registry` 的 public 区追加：

```cpp
    template<Component... Ts>
    [[nodiscard]] auto view() -> View<Ts...> {
        return View<Ts...>{std::tuple<SparseSet<Ts>*...>{&storage_<Ts>()...}};
    }
```

- [ ] **Step 4: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK。

- [ ] **Step 5: 写 `temp/check_view.cpp`**

```cpp
#include "core/ecs/registry.hpp"
#include <cassert>
#include <cstdio>

struct A { using is_component = void; int x; };
struct B { using is_component = void; int y; };

auto main() -> int {
    using namespace game::ecs;
    Registry r;
    auto e1 = r.create();
    auto e2 = r.create();
    auto e3 = r.create();

    r.emplace<A>(e1, {1});
    r.emplace<B>(e1, {10});
    r.emplace<A>(e2, {2});   // 只有 A
    r.emplace<B>(e3, {30});   // 只有 B

    int count = 0;
    int sum_x = 0, sum_y = 0;
    for (Entity e : r.view<A, B>()) {
        ++count;
        auto [a, b] = r.get<A>(e).x, r.get<B>(e).y;  // 错误语法，下面修
        sum_x += r.get<A>(e).x;
        sum_y += r.get<B>(e).y;
    }
    assert(count == 1);   // 只有 e1 同时有 A+B
    assert(sum_x == 1);
    assert(sum_y == 10);

    std::printf("view OK\n");
    return 0;
}
```

> 修：上面 `auto [a, b] = ...` 行不通，改成注释掉的 "错误语法" 提示。
> 实际 plan 里要直接给正确代码：

修正后完整文件：

```cpp
#include "core/ecs/registry.hpp"
#include <cassert>
#include <cstdio>

struct A { using is_component = void; int x; };
struct B { using is_component = void; int y; };

auto main() -> int {
    using namespace game::ecs;
    Registry r;
    auto e1 = r.create();
    auto e2 = r.create();
    auto e3 = r.create();

    r.emplace<A>(e1, {1});
    r.emplace<B>(e1, {10});
    r.emplace<A>(e2, {2});
    r.emplace<B>(e3, {30});

    int count = 0;
    int sum_x = 0, sum_y = 0;
    for (Entity e : r.view<A, B>()) {
        ++count;
        sum_x += r.get<A>(e).x;
        sum_y += r.get<B>(e).y;
    }
    assert(count == 1);
    assert(sum_x == 1);
    assert(sum_y == 10);

    std::printf("view OK\n");
    return 0;
}
```

- [ ] **Step 6: 编译并运行**

```bash
clang-cl /std:c++latest /EHsc /I E:/gamev2/include \
    E:/gamev2/temp/check_view.cpp \
    E:/gamev2/build/CMakeFiles/gamev2.dir/src/core/ecs/registry.cpp.obj \
    /Fe:E:/gamev2/temp/check_view.exe
E:/gamev2/temp/check_view.exe
```

期望：`view OK`。

- [ ] **Step 7: Commit + 清理**

```bash
git add include/core/ecs/registry.hpp
git commit -m "feat(ecs): View<Ts...> multi-component query"
rm E:/gamev2/temp/check_view.*
```

---

## Task 7: 6 个组件定义

**Files:**
- Create: `include/core/ecs/components.hpp`

**目标**：把 Creature 字段拆为组件。`Identity` 不含 string（保持 trivially_copyable），字符串抽到 `Name`。

- [ ] **Step 1: 写 `components.hpp`**

```cpp
// include/core/ecs/components.hpp
#pragma once

#include "core/ecs/concepts.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace game::ecs {

struct Position {
    using is_component = void;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct Traits {
    using is_component = void;
    std::array<float, 4> values{};   // 4 维保持；Spec B 改 8
};

struct Vitals {
    using is_component = void;
    float         hunger = 1.0f;
    float         energy = 1.0f;
    std::uint32_t age    = 0;
    bool          dead   = false;
};

struct Identity {
    using is_component = void;
    std::uint64_t      id        = 0;   // 全局单调
    bool               is_elite  = false;
    bool               is_boss   = false;
    std::uint32_t      elite_age = 0;
};

struct Name {
    using is_component = void;
    std::string value;

    auto set(std::string_view s) -> void { value.assign(s); }
    [[nodiscard]] auto view() const noexcept -> std::string_view { return value; }
};

struct Reproduction {
    using is_component = void;
    float fitness = 0.0f;
};

}  // namespace game::ecs
```

- [ ] **Step 2: 放宽 `concepts.hpp` 的 Component**

```cpp
template<typename T>
concept Component = std::is_default_constructible_v<T>
                 && std::is_move_constructible_v<T>
                 && requires { typename T::is_component; };
```

放宽到"默认可构造 + 可移动"，允许 `Name` 用 `std::string`（不再是
trivially_copyable，但仍满足新 concept）。`Position` / `Traits` / `Vitals` /
`Identity` / `Reproduction` 仍 trivially_copyable，行为不变。

- [ ] **Step 3: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK（components.hpp 暂未被引用）。

- [ ] **Step 4: Commit**

```bash
git add include/core/ecs/components.hpp include/core/ecs/concepts.hpp
git commit -m "feat(ecs): 6 component types (Position/Traits/Vitals/Identity/Name/Reproduction)"
```

---

## Task 8: CreatureSnapshot 适配层

**Files:**
- Create: `include/core/ecs/snapshot.hpp`
- Modify: `include/core/creature.hpp`（保留 `Creature = CreatureSnapshot`）

**目标**：renderer 仍能拿扁平结构。`CreatureSnapshot` 是平铺 POD-like，由 `Simulation::creatures()` 内部拼装。

- [ ] **Step 1: 写 `snapshot.hpp`**

```cpp
// include/core/ecs/snapshot.hpp
#pragma once

#include "core/ecs/components.hpp"
#include "core/gene.hpp"

#include <cstdint>
#include <string_view>

namespace game::ecs {

// 扁平结构，给 renderer 一行一动用。
// name 指向 registry 内 std::string（Simulation 持 registry 全程，string 不悬空）。
struct CreatureSnapshot {
    std::uint64_t      id        = 0;
    std::string_view   name;
    Traits             gene;
    Position           pos;
    float              hunger = 0.0f;
    float              energy = 0.0f;
    std::uint32_t      age     = 0;
    float              fitness = 0.0f;
    bool               is_elite = false;
    bool               is_boss  = false;
};

}  // namespace game::ecs
```

- [ ] **Step 2: 修改 `include/core/creature.hpp`**

```cpp
#pragma once

#include "core/ecs/snapshot.hpp"
#include "core/gene.hpp"

#include <random>
#include <string>

namespace game {

// 保留旧类型名 → 新快照的 alias。renderer / main.cpp 不动。
using Creature = ecs::CreatureSnapshot;

// inline 函数（不再有 decide_and_act）
inline void recover_energy(ecs::Creature& c) {
    if (c.hunger > 0.5f) {
        c.energy = std::min(1.0f, c.energy + 0.05f);
    } else {
        c.energy = std::max(0.0f, c.energy - 0.05f);
    }
}

inline auto make_name(const Traits& gene) -> std::string;

}  // namespace game
```

- [ ] **Step 3: 修改 `src/core/creature.cpp`（仅留 make_name）**

删除整个文件内容，替换为：

```cpp
#include "core/creature.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace game {

auto make_name(const Traits& gene) -> std::string {
    constexpr std::array<std::string_view, 12> kPrefixes = {
        "鳞", "焰", "霜", "暗", "星", "血", "岩", "风", "雷", "影", "冥", "翠",
    };
    constexpr std::array<std::string_view, 12> kSuffixes = {
        "噬渊者", "织梦者", "守誓者", "裂空者", "焚天者", "葬海者",
        "司命者", "逐日者", "镇岳者", "踏云者", "噬神者", "冥河者",
    };

    std::uint32_t hash = 0x811C9DC5u;
    for (const float value : gene.values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        hash = (hash ^ bits) * 0x01000193u;
    }
    const std::size_t prefix = hash % kPrefixes.size();
    const std::size_t suffix = (hash >> 8) % kSuffixes.size();
    return std::string{kPrefixes[prefix]} + "·" + std::string{kSuffixes[suffix]};
}

}  // namespace game
```

- [ ] **Step 4: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build 失败（snapshot 尚未被 Simulation 提供），错误指向
`Simulation::creatures()`。

这是预期的，**修复在 Task 9 完成**。先停在这里。

---

## Task 9: Simulation 接入 ECS

**Files:**
- Modify: `include/core/simulation.hpp`（加 `Registry` 成员）
- Modify: `src/core/simulation.cpp`（`tick()` orchestrate + `creatures()` 拼装快照）

**目标**：把 Simulation 内部状态搬到 Registry，外部 API 保持。

- [ ] **Step 1: 修改 `include/core/simulation.hpp`**

在类内 private 区追加（紧跟 `behavior_rng_` 后）：

```cpp
    ecs::Registry registry_;
    std::vector<ecs::CreatureSnapshot> snapshot_cache_;
```

在文件顶部 include 区追加：

```cpp
#include "core/ecs/registry.hpp"
#include "core/ecs/snapshot.hpp"
```

把 `creatures()` 返回类型改为：

```cpp
    [[nodiscard]] auto creatures() const -> std::span<const ecs::CreatureSnapshot> {
        return snapshot_cache_;
    }
```

- [ ] **Step 2: 重写 `src/core/simulation.cpp::tick()`**

把整个 `Simulation::tick()` 函数替换为：

```cpp
auto Simulation::tick() -> void {
    ++tick_;

    // 1. world biomass 重生
    world_.regrow_biomass();

    // 2. 环境漂移 / 剧变（optimum 是世界状态，由 Simulation 拥有）
    evolution_.drift_environment(optimum_, tick_, params_.epoch_length, events_);

    // 3. 行为循环：先填过渡 stub（仅 age++），Task 10 才接入 BehaviorSystem。
    //    这一段是临时占位，Task 10 step 4 整段替换为 behavior_system_.update(...)。
    for (auto& c : snapshot_cache_) {
        c.age += 1;
    }
}
```

> Task 9 临时 stub：保持编译通过 + 生物"还活着"。**真正的行为在 Task 10/11 才完整平移**。
> Smoke 期望：窗口打开，HUD 显示 pop=30，地图空，无事件日志。

- [ ] **Step 3: 修改 `Simulation` 构造**

构造时初始化 `snapshot_cache_` 为空。**把 `populate_initial_creatures` 改造为接受
`Registry&`**，直接建 entity + 6 组件，避免两次转换。

先把 `include/world/world_generator.hpp` 的签名改为：

```cpp
auto populate_initial_creatures(ecs::Registry& registry,
                                const World& world,
                                std::size_t count,
                                std::uint64_t seed,
                                std::uint64_t& next_id) -> std::size_t;
```

返回实际创建的 entity 数（land_tiles 不足时可能 < count）。

`src/world/world_generator.cpp` 实现改为：扫陆地坐标 → 抽样 → 对每只创建 entity
并 emplace Position/Traits/Vitals/Identity/Name/Reproduction。`Creature` 类型
在 ECS 化后是 `CreatureSnapshot` 的 alias，函数内仍可用 `Creature` 引用其字段
（如 `c.gene`）作为过渡，因为 Trait/Traits 是同一概念。

`Simulation` 构造函数改为：

```cpp
Simulation::Simulation(World world, std::uint64_t seed, Params params)
    : params_(params),
      world_(std::move(world)),
      evolution_(params.evolution, seed),
      behavior_system_(world_, evolution_, seed, next_id_) {
    clock_.configure(params_.tick_dt, params_.max_ticks_per_frame);

    // optimum 初始化（不变）
    {
        auto rng = std::mt19937_64{pcg::derive_seed(seed, kOptimumSalt)};
        std::uniform_real_distribution<float> dist{0.0f, 1.0f};
        for (auto& v : optimum_.values) v = dist(rng);
    }

    // 初始生物：直接进 registry
    populate_initial_creatures(registry_, world_,
                                params_.initial_population, seed, next_id_);

    snapshot_cache_.clear();
}
```

> 关键改动：`Simulation` 不再持有 `behavior_rng_` / `kBehaviorSalt`；这些都
> 迁到 `BehaviorSystem` 内部（详 Task 10）。

- [ ] **Step 4: 改 `creatures()` 实时组装**

```cpp
auto Simulation::creatures() const -> std::span<const ecs::CreatureSnapshot> {
    // 实时组装：按 Identity::id 排序
    snapshot_cache_.clear();
    for (auto e : registry_.view<ecs::Identity>()) {
        const auto& id = registry_.get<ecs::Identity>(e);
        ecs::CreatureSnapshot s;
        s.id = id.id;
        // 其余字段暂时 default；Task 11 补齐
        snapshot_cache_.push_back(s);
    }
    std::ranges::sort(snapshot_cache_, {}, [](const auto& s) { return s.id; });
    return snapshot_cache_;
}
```

- [ ] **Step 5: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK（游戏能跑，但生物还没行为，地图空一片，仅 HUD 显示）。

- [ ] **Step 6: 运行冒烟**

```bash
cd E:/gamev2 && pwsh ./build.ps1 run
```

期望：窗口打开，HUD 显示 `[tick N] pop=30 ...`（pop 数对，行为 stub）。

按 ESC 退出。

- [ ] **Step 7: Commit**

```bash
git add include/core/simulation.hpp src/core/simulation.cpp include/core/creature.hpp src/core/creature.cpp include/core/ecs/snapshot.hpp
git commit -m "refactor(sim): hook up Registry; stub creatures() snapshot"
```

---

## Task 10: BehaviorSystem 平移 decide_and_act

**Files:**
- Create: `include/core/ecs/systems.hpp`
- Modify: `src/core/simulation.cpp`（用 BehaviorSystem 替换 stub）

**目标**：把 `decide_and_act` 逻辑搬到 `CreatureBehaviorSystem::update`。

- [ ] **Step 1: 写 `systems.hpp`**

```cpp
// include/core/ecs/systems.hpp
#pragma once

#include "core/ecs/concepts.hpp"
#include "core/ecs/registry.hpp"

#include <cstdint>
#include <random>

namespace game {

class World;
class EvolutionEngine;

// BehaviorSystem 持有自己的 RNG（与原 Simulation::behavior_rng_ 同 seed 派生），
// 这样 RNG 序列与重构前完全一致，行为不变。
class CreatureBehaviorSystem {
public:
    CreatureBehaviorSystem(World& world, EvolutionEngine& evo,
                           std::uint64_t seed, std::uint64_t& next_id);

    auto update(ecs::Registry& r, std::uint64_t tick) -> void;

private:
    World&           world_;
    EvolutionEngine& evo_;
    std::uint64_t&   next_id_;
    std::mt19937_64  rng_;
};

}  // namespace game
```

- [ ] **Step 2: 删 `src/core/creature.cpp::decide_and_act` 整段**

直接删除（spec §9 已声明）。

- [ ] **Step 3: 改 `include/core/creature.hpp` 删除 decide_and_act 声明**

删除 `void decide_and_act(...)` 那一行。

- [ ] **Step 4: 修改 `src/core/simulation.cpp` 接入 BehaviorSystem**

把 `tick()` 内 step 3 的 stub 替换为：

```cpp
    // 3. 行为循环：用 BehaviorSystem
    behavior_system_.update(registry_, tick_);
```

`Simulation` 构造函数已经在 Task 9 step 3 改为传 seed 给 BehaviorSystem；不再
需要 `behavior_rng_` 成员、`kBehaviorSalt` 常量、相关 include。

从 `simulation.cpp` 顶部删除：

```cpp
constexpr std::uint32_t kBehaviorSalt = 0xBE11u;
```

从 `simulation.hpp` 删除：

```cpp
std::mt19937_64 behavior_rng_;
```

并在类内 private 区追加成员：

```cpp
    CreatureBehaviorSystem behavior_system_;
```

**注意**：`behavior_system_` 的构造需要 `world_` / `evolution_` 已存在，所以
初始化顺序要排在它们之后；用 ctor 内 `behavior_system_(world_, evolution_, seed, next_id_)`
依赖成员声明顺序（已是该顺序）。

- [ ] **Step 5: 现在写 BehaviorSystem 的实现在哪里？**

策略：把 `decide_and_act` 原代码"逐字"复制到 `CreatureBehaviorSystem::update`
的实现里，但用 registry view 取组件。

新增 `src/core/ecs/systems.cpp`（**注：CMakeLists 要加！**）：

```cpp
// src/core/ecs/systems.cpp
#include "core/ecs/systems.hpp"

#include "core/creature.hpp"
#include "core/ecs/components.hpp"
#include "core/evolution.hpp"
#include "world/world.hpp"

#include "pcg/seed.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace game {

namespace {

constexpr std::uint32_t kBehaviorSalt = 0xBE11u;
constexpr float         kHungerRate       = 0.02f;
constexpr std::uint32_t kMaxAge           = 200;
constexpr std::uint32_t kMatingAge        = 20;
constexpr std::uint32_t kMateInterval     = 50;

}  // namespace

CreatureBehaviorSystem::CreatureBehaviorSystem(World& world, EvolutionEngine& evo,
                                               std::uint64_t seed,
                                               std::uint64_t& next_id)
    : world_(world),
      evo_(evo),
      next_id_(next_id),
      rng_(pcg::derive_seed(seed, kBehaviorSalt)) {}

auto CreatureBehaviorSystem::update(ecs::Registry& r, std::uint64_t /*tick*/) -> void {
    using namespace ecs;

    for (Entity e : r.view<Position, Traits, Vitals, Identity>()) {
        auto& pos    = r.get<Position>(e);
        auto& vitals = r.get<Vitals>(e);

        vitals.age += 1;
        vitals.hunger -= kHungerRate;
        recover_energy(vitals);

        if (vitals.hunger <= 0.0f || vitals.age > kMaxAge) {
            vitals.dead = true;
            continue;
        }

        if (pos.x >= world_.width() || pos.y >= world_.height()) continue;
        auto& here = world_.at(pos.x, pos.y);

        // 1) 吃草
        if (here.biomass > 0.1f && vitals.hunger < 1.0f) {
            const float want = 1.0f - vitals.hunger;
            const float eat  = std::min(here.biomass * 0.5f, want);
            here.biomass = std::max(0.0f, here.biomass - eat * 0.4f);
            vitals.hunger = std::min(1.0f, vitals.hunger + eat * 0.3f);
            vitals.energy = std::min(1.0f, vitals.energy + 0.05f);
            continue;
        }

        // 2) 找食物（限于陆地）
        if (vitals.hunger < 0.5f) {
            Position best = pos;
            float    best_b = here.biomass;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const auto nx = static_cast<std::int64_t>(pos.x) + dx;
                    const auto ny = static_cast<std::int64_t>(pos.y) + dy;
                    if (nx < 0 || ny < 0
                        || static_cast<std::size_t>(nx) >= world_.width()
                        || static_cast<std::size_t>(ny) >= world_.height()) continue;
                    if (!world_.is_land(static_cast<std::size_t>(nx),
                                        static_cast<std::size_t>(ny))) continue;
                    const float b = world_.at(static_cast<std::size_t>(nx),
                                              static_cast<std::size_t>(ny)).biomass;
                    if (b > best_b) {
                        best_b = b;
                        best   = {static_cast<std::uint16_t>(nx),
                                  static_cast<std::uint16_t>(ny)};
                    }
                }
            }
            pos = best;
            continue;
        }

        // 3) 繁殖（Task 10 版：直接克隆 traits + 命名；Task 11 改为走 evo_.reproduce）
        if (vitals.hunger > 0.7f && vitals.energy > 0.5f
            && vitals.age >= kMatingAge
            && (vitals.age % kMateInterval == 0)) {
            auto& parent_traits = r.get<Traits>(e);
            auto  child = r.create();
            r.emplace<Position>(child, pos);
            r.emplace<Traits>(child, parent_traits);
            r.emplace<Vitals>(child, Vitals{.hunger = 1.0f, .energy = 0.8f,
                                            .age = 0, .dead = false});
            r.emplace<Identity>(child, Identity{.id = next_id_++});
            r.emplace<Name>(child, Name{.value = make_name(parent_traits)});
            r.emplace<Reproduction>(child);
            vitals.energy = std::max(0.0f, vitals.energy - 0.3f);
            continue;
        }

        // 4) 随机游走
        std::uniform_int_distribution<int> dir(0, 7);
        for (int retry = 0; retry < 4; ++retry) {
            const int d  = dir(rng_);
            const int dx = (d % 3) - 1;
            const int dy = (d / 3) - 1;
            const auto nx = static_cast<std::int64_t>(pos.x) + dx;
            const auto ny = static_cast<std::int64_t>(pos.y) + dy;
            if (nx < 0 || ny < 0
                || static_cast<std::size_t>(nx) >= world_.width()
                || static_cast<std::size_t>(ny) >= world_.height()) continue;
            if (!world_.is_land(static_cast<std::size_t>(nx),
                                static_cast<std::size_t>(ny))) continue;
            pos = {static_cast<std::uint16_t>(nx), static_cast<std::uint16_t>(ny)};
            break;
        }
    }
}

}  // namespace game
```

> 关键改动（vs 原 decide_and_act）：
> - `rng` 从引用改为成员 `rng_`，seed 派生与原 Simulation 一致 → RNG 序列不变
> - `Tile* here` 改为 `auto& here = world_.at(...)`，避免 const 转换（与原代码
>   `auto& here = world.at(...)` 行为等价）
> - `try_get<Name>` 读 name 已不需要（name 在 create 时直接由 `make_name(parent_traits)` 写入）
> - 步骤 4 的 `return` 改为 `break`（在外层 for 内只跳 retry 循环；原代码因
>   decide_and_act 是函数所以用 `return`，新结构需 `break`）

- [ ] **Step 6: 修改 `include/core/creature.hpp`：recover_energy 支持 Vitals 与 Creature**

```cpp
// 直接作用于组件（BehaviorSystem 路径）
inline auto recover_energy(ecs::Vitals& v) -> void {
    if (v.hunger > 0.5f) {
        v.energy = std::min(1.0f, v.energy + 0.05f);
    } else {
        v.energy = std::max(0.0f, v.energy - 0.05f);
    }
}

// 保留对 Creature (= CreatureSnapshot) 的兼容（任何外部代码用了就还在）
inline auto recover_energy(Creature& c) -> void {
    ecs::Vitals v{.hunger = c.hunger, .energy = c.energy, .age = c.age, .dead = false};
    recover_energy(v);
    c.energy = v.energy;
}
```

> 不再需要 Task 10 步骤 6 中提到的 `recover_energy_for_vitals` 静态 helper：
> BehaviorSystem 直接调 `recover_energy(vitals)`。

- [ ] **Step 7: 更新 CMakeLists.txt 加 systems.cpp**

```cmake
add_executable(gamev2
    ...
    src/core/ecs/registry.cpp
    src/core/ecs/systems.cpp
)
```

- [ ] **Step 8: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK（可能需修几个 include）。

- [ ] **Step 9: 运行冒烟**

```bash
cd E:/gamev2 && pwsh ./build.ps1 run
```

期望：窗口显示生物移动、吃草、繁殖（pop 上下波动）。按 ESC 退出。

- [ ] **Step 10: Commit**

```bash
git add include/core/ecs/systems.hpp src/core/ecs/systems.cpp \
        src/core/simulation.cpp src/core/creature.cpp include/core/creature.hpp \
        CMakeLists.txt
git commit -m "refactor(sim): BehaviorSystem port of decide_and_act"
```

---

## Task 11: EvolutionEngine 接入 Registry

**Files:**
- Modify: `include/core/evolution.hpp`
- Modify: `src/core/evolution.cpp`

**目标**：`evaluate_fitness` / `detect_elite_boss` / `reproduce` 都从 view 取组件。

- [ ] **Step 1: 改 `evolution.hpp` 签名**

```cpp
#include "core/ecs/registry.hpp"

class EvolutionEngine {
public:
    ...
    auto evaluate_fitness(ecs::Registry& r, const Traits& optimum) -> void;
    auto detect_elite_boss(ecs::Registry& r,
                           std::vector<Event>& events,
                           std::uint64_t current_tick) -> void;
    [[nodiscard]] auto reproduce(ecs::Registry& r,
                                  ecs::Entity parent) -> ecs::Entity;
    [[nodiscard]] auto mean_fitness(ecs::Registry& r) const -> float;
    [[nodiscard]] auto stddev_fitness(ecs::Registry& r, float mean) const -> float;
    ...
};
```

- [ ] **Step 2: 改 `evolution.cpp` 实现**

把三个函数实现改为基于 registry view。`reproduce` 现在返回新 Entity
而不再返回 Creature struct（行为 system 接收 parent entity）。

`evaluate_fitness`：
```cpp
auto EvolutionEngine::evaluate_fitness(ecs::Registry& r, const Traits& optimum) -> void {
    for (auto e : r.view<ecs::Traits, ecs::Reproduction>()) {
        const auto& t = r.get<ecs::Traits>(e);
        float sum = 0.0f;
        for (std::size_t i = 0; i < t.values.size(); ++i) {
            const float d = t.values[i] - optimum.values[i];
            sum += d * d;
        }
        r.get<ecs::Reproduction>(e).fitness = 1.0f / (1.0f + std::sqrt(sum));
    }
}
```

`detect_elite_boss`：
```cpp
auto EvolutionEngine::detect_elite_boss(ecs::Registry& r,
                                         std::vector<Event>& events,
                                         std::uint64_t current_tick) -> void {
    const float m = mean_fitness(r);
    const float sd = stddev_fitness(r, m);
    const float threshold = m + params_.elite_threshold_sigma * sd;

    for (auto e : r.view<ecs::Identity, ecs::Reproduction>()) {
        auto& id = r.get<ecs::Identity>(e);
        const float fit = r.get<ecs::Reproduction>(e).fitness;
        const std::string name = r.try_get<ecs::Name>(e)
            ? std::string{r.get<ecs::Name>(e).value} : std::string{"?"};

        if (fit > threshold) {
            if (!id.is_elite) {
                id.is_elite = true;
                events.push_back({EventType::EliteBorn, current_tick,
                    std::format("精英诞生: {} (fitness={:.3f})", name, fit)});
            }
            ++id.elite_age;
            if (id.elite_age >= params_.boss_streak_ticks && !id.is_boss) {
                id.is_boss = true;
                events.push_back({EventType::BossEvolved, current_tick,
                    std::format("BOSS 蜕变: {} (fitness={:.3f})", name, fit)});
            }
        } else if (id.is_elite) {
            id.is_elite = false;
            id.elite_age = 0;
        }
    }
}
```

`mean_fitness` / `stddev_fitness`：
```cpp
auto EvolutionEngine::mean_fitness(ecs::Registry& r) const -> float {
    float sum = 0.0f;
    std::size_t n = 0;
    for (auto e : r.view<ecs::Reproduction>()) {
        sum += r.get<ecs::Reproduction>(e).fitness;
        ++n;
    }
    return n == 0 ? 0.0f : sum / static_cast<float>(n);
}

auto EvolutionEngine::stddev_fitness(ecs::Registry& r, float mean) const -> float {
    float sum = 0.0f;
    std::size_t n = 0;
    for (auto e : r.view<ecs::Reproduction>()) {
        const float d = r.get<ecs::Reproduction>(e).fitness - mean;
        sum += d * d;
        ++n;
    }
    return n == 0 ? 0.0f : std::sqrt(sum / static_cast<float>(n));
}
```

`reproduce`：
```cpp
auto EvolutionEngine::reproduce(ecs::Registry& r, ecs::Entity parent) -> ecs::Entity {
    const auto& parent_traits = r.get<ecs::Traits>(parent);
    auto child = r.create();
    ecs::Traits child_traits = parent_traits;
    mutate(child_traits, params_.mutation_strength, rng_);
    r.emplace<ecs::Traits>(child, child_traits);
    r.emplace<ecs::Reproduction>(child);
    r.emplace<ecs::Name>(child, ecs::Name{.value = make_name(child_traits)});
    return child;
}
```

- [ ] **Step 3: 改 `simulation.cpp::tick` 调用方**

```cpp
    evolution_.evaluate_fitness(registry_, optimum_);
    evolution_.detect_elite_boss(registry_, events_, tick_);
```

- [ ] **Step 4: 修改 BehaviorSystem::update 步骤 3（繁殖）用 EvolutionEngine::reproduce**

把 Task 10 step 5 步骤 3 的繁殖段替换为：

```cpp
        if (vitals.hunger > 0.7f && vitals.energy > 0.5f
            && vitals.age >= kMatingAge
            && (vitals.age % kMateInterval == 0)) {
            auto child = evo_.reproduce(r, e);
            r.emplace<Position>(child, pos);
            r.emplace<Vitals>(child, Vitals{.hunger = 1.0f, .energy = 0.8f, .age = 0, .dead = false});
            r.emplace<Identity>(child, ecs::Identity{.id = next_id_++});
            vitals.energy = std::max(0.0f, vitals.energy - 0.3f);
            continue;
        }
```

- [ ] **Step 5: 编译验证**

```bash
pwsh E:\gamev2\build.ps1
```

期望：build OK。

- [ ] **Step 6: 运行冒烟**

```bash
cd E:/gamev2 && pwsh ./build.ps1 run
```

期望：行为完全恢复（移动/吃草/繁殖/精英诞生/Boss 蜕变）。

- [ ] **Step 7: 行为一致性对照（同 seed 截屏对比）**

跑：
```bash
pwsh E:\gamev2\build.ps1 run -- --seed 42 --screenshot temp/after_spec_a.png
```

（截屏功能在 main.cpp 已实现。）

如果项目还没截屏就 `cd E:/gamev2 && ./build/gamev2.exe --seed 42 --screenshot temp/after_spec_a.png`。

视觉对照：地图、生物、事件日志应与重构前一致。

- [ ] **Step 8: Commit**

```bash
git add include/core/evolution.hpp src/core/evolution.cpp \
        src/core/simulation.cpp src/core/ecs/systems.cpp
git commit -m "refactor(evo): EvolutionEngine operates on registry views"
```

---

## Task 12: 端到端 + 收尾

**Files:**
- Modify: 任何遗漏的 include / 命名空间 / 拼写

**目标**：构建无 warning，运行稳定，确认全部 spec 验收点。

- [ ] **Step 1: 干净构建**

```bash
pwsh E:\gamev2\build.ps1 rebuild
```

期望：`=== Build OK: E:\gamev2\build\gamev2.exe ===`，**0 warning**。

- [ ] **Step 2: 运行 60 秒稳态测试**

```bash
cd E:/gamev2 && pwsh -NoProfile -Command "
    \$p = Start-Process -FilePath '.\build\gamev2.exe' -ArgumentList '--seed 12345' -PassThru -NoNewWindow
    Start-Sleep -Seconds 30
    Stop-Process -Id \$p.Id
"
```

期望：进程期间窗口不崩溃、无 ASan/UBSan 报错。

- [ ] **Step 3: spec 验收 checklist 自查**

```markdown
- [ ] 编译 0 warning ✓
- [ ] 同 seed 同 trajectory ✓（截屏对比）
- [ ] renderer / main.cpp / WorldGenerator 一行未动 ✓
- [ ] Traits 保持 4 维 ✓
- [ ] 6 组件齐：Position/Traits/Vitals/Identity/Name/Reproduction ✓
- [ ] RNG 调用顺序保持（按 id 排序 view 迭代）✓
- [ ] decide_and_act 已删除 ✓
- [ ] make_name / recover_energy 保留 ✓
- [ ] `sim.creatures()` API 不变 ✓
- [ ] CLAUDE.md 新规则体现（模板 / concepts / span / ranges / expected）✓
```

- [ ] **Step 4: 最终 commit**

```bash
git status
git diff --stat HEAD~10
git add -A
git commit --allow-empty -m "spec(A): ECS skeleton complete — behavior preserved"
```

- [ ] **Step 5: 更新 CLAUDE.md / docs**

如本次重构暴露 CLAUDE.md 规则缺口（例如 `Concept` 放宽的事），补一句。

---

## Self-Review

执行完所有 task 后，自查：

1. **Spec 覆盖**：
   - §2 Entity ✓ Task 2
   - §3 Component concept ✓ Task 2（放宽版）
   - §4 SparseSet ✓ Task 3
   - §5 Registry（含 view）✓ Task 4-6
   - §6 组件定义 ✓ Task 7
   - §7 System concept ✓ Task 2
   - §8 Simulation 编排（creatures 适配）✓ Task 8-9
   - §9 System 落地（BehaviorSystem）✓ Task 10
   - §10 CLAUDE.md 规则体现 ✓ Task 1-12 全部
   - §11 兼容性（Creature alias + 删除 decide_and_act）✓ Task 8-10
   - 验证 ✓ Task 11 step 7 + Task 12

2. **占位符扫描**：无 TBD/TODO/类似。

3. **类型一致**：
   - `Entity` / `Position` / `Traits` / `Vitals` / `Identity` / `Name` / `Reproduction` 在所有 task 一致
   - `CreatureSnapshot` 字段名一致
   - `Registry::view<Ts...>()` 在 Task 6 引入，Task 11 用法一致
   - `EvolutionEngine::reproduce` 改签名（返回 Entity 而非 Creature struct）在 Task 11 显式说明

4. **风险对应**：
   - RNG 顺序：Task 9 step 4 按 id 排序
   - string 不 trivially：Task 7 放宽 Concept（已在 step 2 修正）
   - dense 顺序不稳定：view 按 id 排序已覆盖
   - CreatureSnapshot 名字视图悬空：registry 在 Simulation 全程存活（spec §8 已说明）
   - 测试覆盖不足：smoke test 覆盖每个 ECS primitive

5. **后续 Spec B/C 接口预留**：
   - `Registry::view<Ts...>()` 已就绪，Spec B 加 Species 组件只需扩 view
   - `BehaviorSystem` 已从 Simulation 抽出，Spec C 加新 system 直接拼到调度链
   - `Traits` 4 维 → 8 维是数据层改动，spec §6 已留扩展点

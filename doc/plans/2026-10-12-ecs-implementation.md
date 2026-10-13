# ECS 模块实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个面向游戏客户端逻辑层的 C++23 ECS 模块，提供 Archetype
分块存储、编译期类型化遍历、读写集驱动的多线程调度器，以及帧末 flush 的
Command Buffer。

**Architecture:** 实体按组件组合归入 archetype，每个 archetype 内组件按 4KB
chunk 连续存储。结构性变更只在帧末单线程发生，通过 swap 尾部实体填补空洞，
保证除尾块外所有 chunk 永远满，使并行遍历可直接切连续区间。调度器从 System
声明的访问集派生读写集，贪心分层拆分并行批次，批内无读写冲突可并发。

**Tech Stack:** C++23、clang++ 23.1.1、MSVC STL 14.51、CMake 4.4、
Unix Makefiles、零第三方依赖。

**设计文档：** `doc/2026-10-12-ecs-design.md`

## Global Constraints

以下约束适用于本计划的每一个任务：

- 目标标准 C++23，直接使用其设施，不为旧标准添加兼容层
- 编译器固定为 clang++。MinGW g++ 8.1.0 不支持 C++23，不得使用
- 禁止引入任何第三方库，包含测试框架
- 禁止使用 `shared_ptr` 与 `unique_ptr`
- 禁止在头文件中使用 `using namespace`
- 注释使用 doxygen `///` 风格，且只解释「为什么」而非「是什么」
- 任何一行不得超过 90 个字符，超长行必须手动折行
- Windows 环境下通过 `pwsh` 调用终端
- 不得执行 `git commit`，提交时机由用户决定

构建命令（在仓库根目录执行）：

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

运行测试：

```powershell
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

已知工具链限制：`std::views::sort` 在该 STL 上不可用，需改用
`std::ranges::sort`。

## 文件结构

| 路径 | 职责 |
|---|---|
| `CMakeLists.txt` | 构建定义，ecs 库与 ecs_tests 可执行文件 |
| `include/gamev2/ecs/entity.hpp` | EntityId 句柄类型 |
| `include/gamev2/ecs/diagnostic.hpp` | Diagnostic 与 DiagnosticHandler |
| `src/ecs/diagnostic.cpp` | 默认诊断处理器实现 |
| `include/gamev2/ecs/component.hpp` | ComponentId、ComponentTypeInfo、注册表 |
| `src/ecs/component.cpp` | 注册表实现与组件 ID 分配 |
| `include/gamev2/ecs/thread_pool.hpp` | 线程池 |
| `src/ecs/thread_pool.cpp` | 线程池实现 |
| `include/gamev2/ecs/storage.hpp` | Column、Archetype、Storage |
| `src/ecs/storage.cpp` | 存储与迁移原语实现 |
| `include/gamev2/ecs/world.hpp` | World 公开接口 |
| `src/ecs/world.cpp` | World 实现 |
| `include/gamev2/ecs/view.hpp` | View 编译期遍历（全模板） |
| `include/gamev2/ecs/command_buffer.hpp` | CommandBuffer |
| `src/ecs/command_buffer.cpp` | CommandBuffer 的 flush 实现 |
| `include/gamev2/ecs/system.hpp` | System 基类与访问集派生 |
| `include/gamev2/ecs/scheduler.hpp` | Scheduler |
| `src/ecs/scheduler.cpp` | 冲突分批与并行执行 |
| `tests/test_framework.hpp` | 极简断言与静态注册 |
| `tests/main.cpp` | 测试入口 |
| `tests/test_*.cpp` | 各任务对应的测试 |

**包含依赖方向**（无环）：

```
entity.hpp          ← 无
diagnostic.hpp      ← 无
component.hpp       ← 无
thread_pool.hpp     ← 无
storage.hpp         ← component.hpp, entity.hpp
world.hpp           ← component.hpp, diagnostic.hpp, entity.hpp,
                       storage.hpp, thread_pool.hpp
                      末尾追加 include view.hpp（为 view() 的类外定义）
view.hpp            ← world.hpp
command_buffer.hpp  ← world.hpp
system.hpp          ← command_buffer.hpp, world.hpp
scheduler.hpp       ← system.hpp, thread_pool.hpp, world.hpp
```

---

### Task 1: 构建骨架与测试框架

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/test_framework.hpp`
- Create: `tests/main.cpp`
- Create: `tests/test_smoke.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `test::TEST(name)` 宏，声明并注册一个测试函数
  - `CHECK(cond)` / `CHECK_EQ(a, b)` 断言宏
  - `test::failures()` 当前用例的失败计数
  - CMake 目标 `ecs`（静态库）与 `ecs_tests`（可执行文件）

- [ ] **Step 1: 创建 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.28)
project(gamev2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ecs STATIC)
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ecs_tests
    tests/main.cpp
    tests/test_smoke.cpp
)
target_link_libraries(ecs_tests PRIVATE ecs)
```

后续任务逐个向两个源文件列表追加条目。

- [ ] **Step 2: 创建 tests/test_framework.hpp**

```cpp
#pragma once

#include <cstdio>
#include <string_view>
#include <vector>

namespace test {

struct TestCase
{
    std::string_view name;
    void (*fn)();
};

inline int& failures()
{
    static int n = 0;
    return n;
}

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar
{
    Registrar(std::string_view name, void (*fn)())
    {
        registry().push_back({name, fn});
    }
};

int run_all();

}  // namespace test

#define TEST(name)                                            \
    static void name();                                       \
    static const ::test::Registrar reg_##name{#name, &name};  \
    static void name()

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ::test::failures()++;                                        \
        }                                                                \
    } while (false)

#define CHECK_EQ(a, b)                                   \
    do {                                                 \
        if (!((a) == (b))) {                             \
            std::printf("  FAIL %s:%d  %s == %s\n",      \
                        __FILE__, __LINE__, #a, #b);     \
            ::test::failures()++;                        \
        }                                                \
    } while (false)
```

- [ ] **Step 3: 创建 tests/main.cpp**

```cpp
#include "test_framework.hpp"

#include <cstdio>

namespace test {

int run_all()
{
    int failed = 0;
    for (const auto& tc : registry())
    {
        failures() = 0;
        std::printf("[ RUN  ] %.*s\n",
                    static_cast<int>(tc.name.size()), tc.name.data());
        tc.fn();
        if (failures() == 0)
        {
            std::printf("[  OK  ] %.*s\n",
                        static_cast<int>(tc.name.size()), tc.name.data());
        }
        else
        {
            std::printf("[ FAIL ] %.*s  (%d)\n",
                        static_cast<int>(tc.name.size()), tc.name.data(),
                        failures());
            ++failed;
        }
    }
    const int total = static_cast<int>(registry().size());
    std::printf("%d test(s), %d failed\n", total, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace test

int main()
{
    return test::run_all();
}
```

- [ ] **Step 4: 创建 tests/test_smoke.cpp**

先写一个必定失败的用例，验证失败计数真的生效：

```cpp
#include "test_framework.hpp"

TEST(suite_runs)
{
    CHECK(true);
}

TEST(deliberate_failure)
{
    CHECK(1 == 2);
}
```

- [ ] **Step 5: 构建并验证失败计数生效**

Run:

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `suite_runs` 为 OK，`deliberate_failure` 为 FAIL，
`2 test(s), 1 failed`，`exit=1`。

- [ ] **Step 6: 删除反向验证用例**

把 `tests/test_smoke.cpp` 替换为：

```cpp
#include "test_framework.hpp"

TEST(suite_runs)
{
    CHECK(true);
}

TEST(failure_counter_starts_at_zero)
{
    CHECK_EQ(::test::failures(), 0);
}
```

- [ ] **Step 7: 重新构建，验证全绿**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `2 test(s), 0 failed`，`exit=0`。

---

### Task 2: 实体句柄与诊断通道

**Files:**
- Create: `include/gamev2/ecs/entity.hpp`
- Create: `include/gamev2/ecs/diagnostic.hpp`
- Create: `src/ecs/diagnostic.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_entity.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `gamev2::ecs::EntityId`，含 `index()`、`version()`、`make()`、
    `operator bool`、`operator==`
  - `gamev2::ecs::DiagnosticLevel::warning` / `::error`
  - `gamev2::ecs::Diagnostic{level, code, message}`
  - `gamev2::ecs::DiagnosticHandler`
  - `gamev2::ecs::default_diagnostic_handler()`
  - `gamev2::ecs::emit_diagnostic(const Diagnostic&, const DiagnosticHandler&)`
  - `std::hash<gamev2::ecs::EntityId>`

- [ ] **Step 1: 创建 include/gamev2/ecs/entity.hpp**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gamev2::ecs {

/// 实体句柄。下 32 位为存储索引，上 32 位为版本号。
///
/// 销毁实体时递增版本号，使旧句柄自然失效。value 为 0 是保留的非法值，
/// 因此版本号必须从 1 起始，否则索引 0 的实体会与非法哨兵撞值。
struct EntityId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr std::uint32_t index() const noexcept
    {
        return static_cast<std::uint32_t>(value & 0xFFFFFFFFull);
    }

    [[nodiscard]] constexpr std::uint32_t version() const noexcept
    {
        return static_cast<std::uint32_t>(value >> 32);
    }

    [[nodiscard]] static constexpr EntityId make(std::uint32_t idx,
                                                 std::uint32_t ver) noexcept
    {
        return EntityId{(static_cast<std::uint64_t>(ver) << 32) | idx};
    }

    constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }

    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
};

}  // namespace gamev2::ecs

template<>
struct std::hash<gamev2::ecs::EntityId>
{
    [[nodiscard]] std::size_t
    operator()(gamev2::ecs::EntityId e) const noexcept
    {
        return std::hash<std::uint64_t>{}(e.value);
    }
};
```

- [ ] **Step 2: 创建 include/gamev2/ecs/diagnostic.hpp**

```cpp
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace gamev2::ecs {

enum class DiagnosticLevel
{
    warning,
    error,
};

struct Diagnostic
{
    DiagnosticLevel level;
    /// 稳定标识，供工具按 code 匹配，不随文案变化。
    std::string_view code;
    std::string message;
};

using DiagnosticHandler = std::function<void(const Diagnostic&)>;

/// 默认处理器把诊断写到 stderr。World 构造时可替换。
[[nodiscard]] DiagnosticHandler default_diagnostic_handler();

void emit_diagnostic(const Diagnostic& diag,
                     const DiagnosticHandler& handler);

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 创建 src/ecs/diagnostic.cpp**

```cpp
#include "gamev2/ecs/diagnostic.hpp"

#include <cstdio>

namespace gamev2::ecs {

DiagnosticHandler default_diagnostic_handler()
{
    return [](const Diagnostic& diag)
    {
        const char* level =
            diag.level == DiagnosticLevel::error ? "error" : "warn";
        std::fprintf(stderr, "[%s] %.*s: %s\n", level,
                     static_cast<int>(diag.code.size()), diag.code.data(),
                     diag.message.c_str());
    };
}

void emit_diagnostic(const Diagnostic& diag,
                     const DiagnosticHandler& handler)
{
    if (handler)
    {
        handler(diag);
    }
}

}  // namespace gamev2::ecs
```

- [ ] **Step 4: 更新 CMakeLists.txt**

在**现有列表上追加**，不要用下面的文本整体替换 —— `src/ecs.cpp` 是锚点
翻译单元，删掉它会让 CMake 因目标无源文件而配置失败：

```cmake
add_library(ecs STATIC
    src/ecs.cpp
    src/ecs/diagnostic.cpp
)

add_executable(ecs_tests
    tests/main.cpp
    tests/test_smoke.cpp
    tests/test_entity.cpp
)
```

- [ ] **Step 5: 创建 tests/test_entity.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/entity.hpp"

#include <unordered_set>

using gamev2::ecs::EntityId;

TEST(entity_id_roundtrip)
{
    const auto e = EntityId::make(1234, 7);
    CHECK_EQ(e.index(), 1234u);
    CHECK_EQ(e.version(), 7u);
    CHECK(e == EntityId::make(1234, 7));
}

TEST(entity_id_default_is_invalid)
{
    CHECK(!EntityId{});
    CHECK_EQ(EntityId{}.value, 0ull);
}

TEST(entity_id_first_slot_is_not_invalid)
{
    // 索引 0 + 版本 1 是首个合法实体，不应撞上 value==0 的哨兵。
    const auto e = EntityId::make(0, 1);
    CHECK(static_cast<bool>(e));
    CHECK_EQ(e.index(), 0u);
}

TEST(entity_id_equality_is_by_value)
{
    CHECK(EntityId::make(5, 1) != EntityId::make(5, 2));
    CHECK(EntityId::make(5, 1) != EntityId::make(6, 1));
}

TEST(entity_id_is_usable_as_hash_key)
{
    std::unordered_set<EntityId> seen;
    seen.insert(EntityId::make(1, 1));
    seen.insert(EntityId::make(1, 1));
    seen.insert(EntityId::make(2, 1));
    CHECK_EQ(seen.size(), std::size_t{2});
}
```

- [ ] **Step 6: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `7 test(s), 0 failed`，`exit=0`。

---

### Task 3: 组件类型注册

**Files:**
- Create: `include/gamev2/ecs/component.hpp`
- Create: `src/ecs/component.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_component.cpp`

**Interfaces:**
- Consumes: 无（本任务不依赖 Diagnostic）
- Produces:
  - `gamev2::ecs::ComponentId{ std::uint32_t value; }`
  - `gamev2::ecs::ComponentTypeInfo{ id, size, alignment, name, destroy, move }`
  - `gamev2::ecs::ComponentRegistry`
    - `std::span<const ComponentTypeInfo> types() const noexcept`
    - `ComponentId register_type_info(std::size_t size,
      std::size_t alignment, const char* name, void (*destroy)(void*),
      void (*move)(void* dst, void* src))`
  - `gamev2::ecs::global_component_registry()`
  - `gamev2::ecs::component_id_of<T>()`
  - `gamev2::ecs::kColumnAllocationAlignment`

- [ ] **Step 1: 创建 include/gamev2/ecs/component.hpp**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// std::vector<std::byte> 的分配对齐上限，组件对齐不得超过它。
inline constexpr std::size_t kColumnAllocationAlignment = 16;

/// 组件类型的进程内唯一标识。
struct ComponentId
{
    std::uint32_t value = 0;

    friend constexpr bool operator==(ComponentId, ComponentId) noexcept
        = default;
};

struct ComponentTypeInfo
{
    ComponentId id;
    std::size_t size;
    std::size_t alignment;
    const char* name;
    /// 类型擦除的析构入口，使存储层无需知道 T。
    void (*destroy)(void*);
    /// 类型擦除的移动赋值入口，供 archetype 批量搬运使用。
    void (*move)(void* dst, void* src);
};

namespace detail {

template<typename T>
void destroy_component(void* p)
{
    static_cast<T*>(p)->~T();
}

template<typename T>
void move_component(void* dst, void* src)
{
    *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
}

}  // namespace detail

class ComponentRegistry
{
public:
    [[nodiscard]] std::span<const ComponentTypeInfo> types() const noexcept
    {
        return infos_;
    }

    /// 按调用顺序分配下一个组件 ID 并记录类型信息。
    [[nodiscard]] ComponentId register_type_info(
        std::size_t size, std::size_t alignment, const char* name,
        void (*destroy)(void*), void (*move)(void* dst, void* src));

private:
    std::vector<ComponentTypeInfo> infos_;
};

[[nodiscard]] ComponentRegistry& global_component_registry();

template<typename T>
[[nodiscard]] ComponentId register_component()
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>,
                  "组件必须是对象类型且不能是数组");
    static_assert(std::is_default_constructible_v<T>,
                  "组件必须可默认构造");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "组件必须支持 noexcept 移动赋值");
    static_assert(alignof(T) <= kColumnAllocationAlignment,
                  "组件对齐要求超出列存储的分配对齐能力");

    static const ComponentId id =
        global_component_registry().register_type_info(
            sizeof(T), alignof(T), typeid(T).name(),
            &detail::destroy_component<T>, &detail::move_component<T>);
    return id;
}

/// 取组件类型 ID。首次调用时完成注册，组件作者无需写注册代码。
template<typename T>
[[nodiscard]] ComponentId component_id_of() noexcept
{
    return register_component<T>();
}

}  // namespace gamev2::ecs
```

- [ ] **Step 2: 创建 src/ecs/component.cpp**

```cpp
#include "gamev2/ecs/component.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace gamev2::ecs {

namespace {

std::atomic<std::uint32_t> g_next_component_id{1};

}  // namespace

ComponentRegistry& global_component_registry()
{
    static ComponentRegistry registry;
    return registry;
}

ComponentId ComponentRegistry::register_type_info(
    std::size_t size, std::size_t alignment, const char* name,
    void (*destroy)(void*), void (*move)(void*))
{
    const auto raw = g_next_component_id.fetch_add(1);
    if (raw == std::numeric_limits<std::uint32_t>::max())
    {
        std::fprintf(stderr, "component id space exhausted\n");
        std::abort();
    }

    const ComponentId id{raw};
    infos_.push_back(
        ComponentTypeInfo{id, size, alignment, name, destroy, move});
    return id;
}

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 更新 CMakeLists.txt**

`add_library` 源文件列表追加 `src/ecs/component.cpp`，
`add_executable` 源文件列表追加 `tests/test_component.cpp`。

- [ ] **Step 4: 创建 tests/test_component.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/component.hpp"

#include <string>

using gamev2::ecs::ComponentId;
using gamev2::ecs::ComponentTypeInfo;
using gamev2::ecs::component_id_of;
using gamev2::ecs::global_component_registry;

namespace {

struct Position
{
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity
{
    float dx = 0.0F;
    float dy = 0.0F;
};

const ComponentTypeInfo* find_info(ComponentId id)
{
    for (const auto& info : global_component_registry().types())
    {
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}

}  // namespace

TEST(component_id_is_stable_for_same_type)
{
    CHECK(component_id_of<Position>() == component_id_of<Position>());
}

TEST(component_id_differs_between_types)
{
    CHECK(component_id_of<Position>() != component_id_of<Velocity>());
}

TEST(component_id_is_never_zero)
{
    CHECK(component_id_of<Position>().value != 0);
}

TEST(component_type_info_is_registered)
{
    const auto id = component_id_of<Position>();
    const auto* info = find_info(id);
    CHECK(info != nullptr);
    if (info != nullptr)
    {
        CHECK_EQ(info->size, sizeof(Position));
        CHECK_EQ(info->alignment, alignof(Position));
        CHECK(std::string{info->name}.find("Position") != std::string::npos);
    }
}

TEST(component_destroy_entry_runs_destructor)
{
    struct Counted
    {
        static inline int destructions = 0;
        int value = 0;

        Counted() = default;
        Counted(const Counted&) = default;
        Counted& operator=(const Counted&) = default;
        ~Counted() { ++destructions; }
    };

    const auto* info = find_info(component_id_of<Counted>());
    CHECK(info != nullptr);
    if (info != nullptr)
    {
        Counted obj{7};
        info->destroy(&obj);
        CHECK_EQ(Counted::destructions, 1);
    }
}

TEST(component_move_entry_transfers_value)
{
    struct Movable
    {
        static inline int moves = 0;
        int value = 0;

        Movable() = default;
        Movable(const Movable&) = default;
        Movable& operator=(const Movable&) = default;
        Movable& operator=(Movable&&) noexcept
        {
            ++moves;
            value = 42;
            return *this;
        }
        ~Movable() = default;
    };

    const auto* info = find_info(component_id_of<Movable>());
    CHECK(info != nullptr);
    if (info != nullptr)
    {
        Movable src{1};
        Movable dst{};
        info->move(&dst, &src);
        CHECK_EQ(Movable::moves, 1);
        CHECK_EQ(dst.value, 42);
    }
}
```

- [ ] **Step 5: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `13 test(s), 0 failed`，`exit=0`。

---

### Task 4: 类型擦除的组件列

**Files:**
- Create: `include/gamev2/ecs/storage.hpp`（本任务只写 Column）
- Create: `src/ecs/storage.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_storage.cpp`

**Interfaces:**
- Consumes: `ComponentId`、`ComponentTypeInfo`、`component_id_of<T>`
  （Task 3）
- Produces:
  - `gamev2::ecs::kChunkTargetBytes`
  - `gamev2::ecs::compute_chunk_capacity(std::span<const ComponentId>)`
  - `gamev2::ecs::Column`
    - `Column(const ComponentTypeInfo&, std::size_t chunk_capacity)`
    - `void ensure_chunk(std::uint32_t)`
    - `void pop_chunk()`
    - `std::byte* chunk_data(std::uint32_t)` 及 const 重载
    - `T* at<T>(chunk, row)` 及 const 重载
    - `T* construct<T, Args...>(chunk, row, Args&&...)`
    - `void move_assign_raw(dst_chunk, dst_row, src_chunk, src_row)`
    - `template<typename T> void move_assign(...)`
    - `void destroy_row(chunk, row)`

- [ ] **Step 1: 创建 include/gamev2/ecs/storage.hpp**

```cpp
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/entity.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 单 chunk 目标字节数，取页大小，让并行的每个 chunk 尽量落在
/// 独立缓存行组上，降低 false sharing。
inline constexpr std::size_t kChunkTargetBytes = 4096;

/// 按该 archetype 中最大的组件尺寸计算每 chunk 可容纳的实体数。
[[nodiscard]] std::size_t
compute_chunk_capacity(std::span<const ComponentId> ids);

/// 一个 archetype 内某种组件的存储。每个 chunk 一段定长数据。
///
/// 内层用 std::vector<std::byte> 而非裸分配：外层 vector 扩容时移动的是
/// 内层 vector，而移动不搬运数据，因此 chunk 数据地址在扩容后依然有效。
class Column
{
public:
    Column(const ComponentTypeInfo& info, std::size_t chunk_capacity);

    Column(Column&&) noexcept = default;
    Column& operator=(Column&&) noexcept = default;
    Column(const Column&) = delete;
    Column& operator=(const Column&) = delete;

    [[nodiscard]] std::size_t chunk_count() const noexcept
    {
        return chunks_.size();
    }

    void ensure_chunk(std::uint32_t chunk);
    void pop_chunk();

    [[nodiscard]] std::byte* chunk_data(std::uint32_t chunk) noexcept
    {
        return chunks_[chunk].data();
    }

    [[nodiscard]] const std::byte* chunk_data(std::uint32_t chunk) const noexcept
    {
        return chunks_[chunk].data();
    }

    template<typename T>
    [[nodiscard]] T* at(std::uint32_t chunk, std::uint32_t row) noexcept
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        return std::launder(
            reinterpret_cast<T*>(chunk_data(chunk) + row * sizeof(T)));
    }

    template<typename T>
    [[nodiscard]] const T* at(std::uint32_t chunk,
                              std::uint32_t row) const noexcept
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        return std::launder(reinterpret_cast<const T*>(
            chunk_data(chunk) + row * sizeof(T)));
    }

    /// 在 (chunk, row) 处构造一个 T，返回其地址。
    template<typename T, typename... Args>
    T* construct(std::uint32_t chunk, std::uint32_t row, Args&&... args)
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        auto* p = chunk_data(chunk) + row * sizeof(T);
        return std::construct_at(reinterpret_cast<T*>(p),
                                 std::forward<Args>(args)...);
    }

    /// 按列自身记录的组件信息做移动赋值，无需调用方知道 T。
    void move_assign_raw(std::uint32_t dst_chunk, std::uint32_t dst_row,
                         std::uint32_t src_chunk, std::uint32_t src_row);

    template<typename T>
    void move_assign(std::uint32_t dst_chunk, std::uint32_t dst_row,
                     std::uint32_t src_chunk, std::uint32_t src_row)
    {
        assert(info_.id == component_id_of<T>());
        *at<T>(dst_chunk, dst_row) = std::move(*at<T>(src_chunk, src_row));
    }

    void destroy_row(std::uint32_t chunk, std::uint32_t row);

private:
    ComponentTypeInfo info_{};
    std::size_t capacity_ = 1;
    std::vector<std::vector<std::byte>> chunks_;
};

}  // namespace gamev2::ecs
```

- [ ] **Step 2: 创建 src/ecs/storage.cpp**

```cpp
#include "gamev2/ecs/storage.hpp"

#include <algorithm>
#include <cassert>

namespace gamev2::ecs {

std::size_t compute_chunk_capacity(std::span<const ComponentId> ids)
{
    std::size_t largest = 1;
    const auto types = global_component_registry().types();
    for (const auto id : ids)
    {
        const auto it = std::ranges::find_if(types, [id](const auto& info)
                                             { return info.id == id; });
        if (it != types.end())
        {
            largest = std::max(largest, it->size);
        }
    }
    return std::max(std::size_t{1}, kChunkTargetBytes / largest);
}

Column::Column(const ComponentTypeInfo& info, std::size_t chunk_capacity)
    : info_{info}, capacity_{chunk_capacity}
{
    assert(chunk_capacity >= 1);
}

void Column::ensure_chunk(std::uint32_t chunk)
{
    if (chunk == chunks_.size())
    {
        chunks_.emplace_back(capacity_ * info_.size);
    }
    assert(chunk < chunks_.size());
}

void Column::pop_chunk()
{
    chunks_.pop_back();
}

void Column::move_assign_raw(std::uint32_t dst_chunk, std::uint32_t dst_row,
                             std::uint32_t src_chunk, std::uint32_t src_row)
{
    if (dst_chunk == src_chunk && dst_row == src_row)
    {
        return;
    }
    info_.move(chunk_data(dst_chunk) + dst_row * info_.size,
               chunk_data(src_chunk) + src_row * info_.size);
}

void Column::destroy_row(std::uint32_t chunk, std::uint32_t row)
{
    info_.destroy(chunk_data(chunk) + row * info_.size);
}

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 更新 CMakeLists.txt**

`add_library` 源文件列表追加 `src/ecs/storage.cpp`，
`add_executable` 源文件列表追加 `tests/test_storage.cpp`。

- [ ] **Step 4: 创建 tests/test_storage.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/storage.hpp"

#include <cstdlib>
#include <vector>

using gamev2::ecs::Column;
using gamev2::ecs::ComponentTypeInfo;
using gamev2::ecs::compute_chunk_capacity;
using gamev2::ecs::component_id_of;
using gamev2::ecs::global_component_registry;

namespace {

struct Slot
{
    int value = 0;
};

const ComponentTypeInfo& type_info_of_slot()
{
    for (const auto& info : global_component_registry().types())
    {
        if (info.id == component_id_of<Slot>())
        {
            return info;
        }
    }
    std::abort();
}

}  // namespace

TEST(chunk_capacity_uses_largest_component)
{
    const std::vector<gamev2::ecs::ComponentId> ids{
        component_id_of<Slot>()};
    // 4096 / sizeof(Slot) = 4096 / 4
    CHECK_EQ(compute_chunk_capacity(ids), std::size_t{1024});
}

TEST(chunk_capacity_never_below_one)
{
    struct Huge
    {
        char padding[8192];
    };
    const std::vector<gamev2::ecs::ComponentId> ids{component_id_of<Huge>()};
    CHECK_EQ(compute_chunk_capacity(ids), std::size_t{1});
}

TEST(column_construct_and_read_back)
{
    Column col{type_info_of_slot(), 4};
    col.ensure_chunk(0);
    col.construct<Slot>(0, 0, Slot{42});
    CHECK_EQ(col.at<Slot>(0, 0)->value, 42);
}

TEST(column_rows_are_independent)
{
    Column col{type_info_of_slot(), 4};
    col.ensure_chunk(0);
    for (std::uint32_t r = 0; r < 4; ++r)
    {
        col.construct<Slot>(0, r, Slot{static_cast<int>(r)});
    }
    for (std::uint32_t r = 0; r < 4; ++r)
    {
        CHECK_EQ(col.at<Slot>(0, r)->value, static_cast<int>(r));
    }
}

TEST(column_chunks_are_independent)
{
    Column col{type_info_of_slot(), 2};
    col.ensure_chunk(0);
    col.ensure_chunk(1);
    col.construct<Slot>(0, 0, Slot{1});
    col.construct<Slot>(1, 1, Slot{2});
    CHECK_EQ(col.at<Slot>(0, 0)->value, 1);
    CHECK_EQ(col.at<Slot>(1, 1)->value, 2);
    CHECK_EQ(col.chunk_count(), std::size_t{2});
}

TEST(column_growth_preserves_existing_data)
{
    Column col{type_info_of_slot(), 2};
    col.ensure_chunk(0);
    col.construct<Slot>(0, 0, Slot{99});

    // 触发外层 vector 扩容，验证已有 chunk 的地址仍然有效。
    for (std::uint32_t c = 1; c < 32; ++c)
    {
        col.ensure_chunk(c);
    }

    CHECK_EQ(col.at<Slot>(0, 0)->value, 99);
}

TEST(column_move_assign_transfers_value)
{
    Column col{type_info_of_slot(), 4};
    col.ensure_chunk(0);
    col.construct<Slot>(0, 1, Slot{55});
    col.construct<Slot>(0, 3, Slot{});
    col.move_assign<Slot>(0, 3, 0, 1);
    CHECK_EQ(col.at<Slot>(0, 3)->value, 55);
}

TEST(column_pop_chunk_releases_tail)
{
    Column col{type_info_of_slot(), 2};
    col.ensure_chunk(0);
    col.ensure_chunk(1);
    CHECK_EQ(col.chunk_count(), std::size_t{2});
    col.pop_chunk();
    CHECK_EQ(col.chunk_count(), std::size_t{1});
}
```

- [ ] **Step 5: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `21 test(s), 0 failed`，`exit=0`。

---

### Task 5: Archetype 与迁移原语

**Files:**
- Modify: `include/gamev2/ecs/storage.hpp`（在 `Column` 之后追加）
- Modify: `src/ecs/storage.cpp`
- Modify: `CMakeLists.txt`（仅追加测试文件）
- Test: `tests/test_storage.cpp`（追加用例）

**Interfaces:**
- Consumes: `Column`、`compute_chunk_capacity`（Task 4）
- Produces:
  - `gamev2::ecs::ArchetypeId`、`kInvalidArchetype`、`kNoIndex`
  - `gamev2::ecs::ComponentKey`、`ComponentKeyHash`
  - `gamev2::ecs::Archetype`
    - `Archetype(std::vector<ComponentId>, std::size_t chunk_capacity)`
    - `std::span<const ComponentId> key() const noexcept`
    - `bool has(ComponentId) const noexcept`
    - `std::size_t find(ComponentId) const noexcept`
    - `Column& column_at(std::size_t) noexcept` 及 const 重载
    - `std::size_t entity_count() const noexcept`
    - `std::size_t chunk_capacity() const noexcept`
    - `std::uint32_t chunk_count() const noexcept`
    - `std::size_t count_in(std::uint32_t) const noexcept`
    - `void push_back(EntityId)`
    - `void pop_back()`
    - `std::uint32_t back_chunk() const noexcept`
    - `std::uint32_t back_row() const noexcept`
    - `EntityId entity_at(std::uint32_t, std::uint32_t) const noexcept`
    - `void set_entity(std::uint32_t, std::uint32_t, EntityId)`
    - `void swap_into(dst_chunk, dst_row, src_chunk, src_row)`
  - `gamev2::ecs::Storage`
    - `ArchetypeId find_or_create(std::span<const ComponentId>)`
    - `ArchetypeId with(ArchetypeId, ComponentId, bool add)`
    - `Archetype& at(ArchetypeId) noexcept` 及 const 重载
    - `std::span<const ArchetypeId> archetypes() const noexcept`
    - `static void copy_shared(...)`
    - `EntityId fill_hole(Archetype&, std::uint32_t, std::uint32_t)`

- [ ] **Step 1: 在 storage.hpp 的 Column 之后追加**

先在文件顶部的 include 区追加：

```cpp
#include <limits>
#include <unordered_map>
```

然后在 `Column` 类定义之后、`}  // namespace gamev2::ecs` 之前追加：

```cpp
using ArchetypeId = std::uint32_t;

inline constexpr ArchetypeId kInvalidArchetype =
    std::numeric_limits<ArchetypeId>::max();

/// 组件查找失败时的返回值，避免与合法下标混淆。
inline constexpr std::size_t kNoIndex =
    std::numeric_limits<std::size_t>::max();

/// archetype 索引。上界为同时存在的不同组件组合数。
///
/// 不变式：除最后一个 chunk 外，所有 chunk 永远满载。这样并行遍历时
/// 每个 worker 拿到的都是连续区间，无需处理空洞。
class Archetype
{
public:
    Archetype(std::vector<ComponentId> ids, std::size_t chunk_capacity);

    [[nodiscard]] std::span<const ComponentId> key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] bool has(ComponentId id) const noexcept
    {
        return find(id) != kNoIndex;
    }

    [[nodiscard]] std::size_t find(ComponentId id) const noexcept
    {
        const auto it = std::ranges::find(key_, id);
        if (it == key_.end())
        {
            return kNoIndex;
        }
        return static_cast<std::size_t>(std::distance(key_.begin(), it));
    }

    [[nodiscard]] Column& column_at(std::size_t index) noexcept
    {
        return columns_[index];
    }

    [[nodiscard]] const Column& column_at(std::size_t index) const noexcept
    {
        return columns_[index];
    }

    [[nodiscard]] std::size_t entity_count() const noexcept
    {
        return count_;
    }

    [[nodiscard]] std::size_t chunk_capacity() const noexcept
    {
        return capacity_;
    }

    [[nodiscard]] std::uint32_t chunk_count() const noexcept;

    [[nodiscard]] std::size_t count_in(std::uint32_t chunk) const noexcept;

    void push_back(EntityId e);
    void pop_back();

    /// 尾部空行的位置。仅在 push_back 之后有意义。
    [[nodiscard]] std::uint32_t back_chunk() const noexcept;
    [[nodiscard]] std::uint32_t back_row() const noexcept;

    [[nodiscard]] EntityId entity_at(std::uint32_t chunk,
                                     std::uint32_t row) const noexcept
    {
        return entities_[global_row(chunk, row)];
    }

    void set_entity(std::uint32_t chunk, std::uint32_t row, EntityId e)
    {
        entities_[global_row(chunk, row)] = e;
    }

    /// 把 (src_chunk, src_row) 整行移动到 (dst_chunk, dst_row)。
    /// 不修改 src 的结构。
    void swap_into(std::uint32_t dst_chunk, std::uint32_t dst_row,
                   std::uint32_t src_chunk, std::uint32_t src_row);

private:
    [[nodiscard]] std::size_t global_row(std::uint32_t chunk,
                                         std::uint32_t row) const noexcept
    {
        return static_cast<std::size_t>(chunk) * capacity_ + row;
    }

    std::vector<ComponentId> key_;
    std::size_t capacity_ = 1;
    std::vector<Column> columns_;
    std::vector<EntityId> entities_;
    std::size_t count_ = 0;
};

using ComponentKey = std::vector<ComponentId>;

/// 组件集合键的哈希，FNV-1a。
struct ComponentKeyHash
{
    [[nodiscard]] std::size_t operator()(
        std::span<const ComponentId> ids) const noexcept
    {
        std::size_t h = 1469598103934665603ull;
        for (const auto id : ids)
        {
            h ^= static_cast<std::size_t>(id.value);
            h *= 1099511628211ull;
        }
        return h;
    }
};

/// 让 unordered_map 直接以 span 形式的排序组件列表为键，
/// 避免在 map 内重复存一份键数组。
struct ComponentKeyEqual
{
    [[nodiscard]] bool operator()(std::span<const ComponentId> a,
                                  std::span<const ComponentId> b) const noexcept
    {
        return std::ranges::equal(a, b);
    }
};

class Storage
{
public:
    [[nodiscard]] ArchetypeId find_or_create(
        std::span<const ComponentId> ids);

    /// 在 current 的组件集上增删一个组件后的 archetype，按需创建。
    [[nodiscard]] ArchetypeId with(ArchetypeId current, ComponentId id,
                                   bool add);

    [[nodiscard]] Archetype& at(ArchetypeId id) noexcept
    {
        return archetypes_[id];
    }

    [[nodiscard]] const Archetype& at(ArchetypeId id) const noexcept
    {
        return archetypes_[id];
    }

    [[nodiscard]] std::span<const ArchetypeId> archetypes() const noexcept
    {
        return archetypes_;
    }

    /// 把源行中与目标共有的组件移动到目标行，实体句柄一并移动。
    static void copy_shared(const Archetype& src, std::uint32_t src_chunk,
                            std::uint32_t src_row, Archetype& dst,
                            std::uint32_t dst_chunk, std::uint32_t dst_row);

    /// 从 src 移除已被移空的 (chunk, row)，用尾部实体填补。
    /// 返回被填补实体的句柄；未发生填补时返回 EntityId{}。
    [[nodiscard]] EntityId fill_hole(Archetype& src, std::uint32_t chunk,
                                     std::uint32_t row);

private:
    std::vector<Archetype> archetypes_;
    std::unordered_map<ComponentKey, ArchetypeId, ComponentKeyHash,
                       ComponentKeyEqual>
        index_;
};
```

- [ ] **Step 2: 在 storage.cpp 追加 Archetype 与 Storage 实现**

```cpp
Archetype::Archetype(std::vector<ComponentId> ids,
                     std::size_t chunk_capacity)
    : key_{std::move(ids)}, capacity_{chunk_capacity}
{
    std::ranges::sort(key_);
    key_.erase(std::ranges::unique(key_).begin(), key_.end());

    columns_.reserve(key_.size());
    const auto types = global_component_registry().types();
    for (const auto id : key_)
    {
        const auto it = std::ranges::find_if(types, [id](const auto& info)
                                             { return info.id == id; });
        assert(it != types.end());
        columns_.emplace_back(*it, capacity_);
    }
}

std::uint32_t Archetype::chunk_count() const noexcept
{
    if (count_ == 0)
    {
        return 0;
    }
    return static_cast<std::uint32_t>((count_ + capacity_ - 1) / capacity_);
}

std::size_t Archetype::count_in(std::uint32_t chunk) const noexcept
{
    const auto total = chunk_count();
    if (chunk >= total)
    {
        return 0;
    }
    if (chunk + 1 == total)
    {
        const auto rem = count_ % capacity_;
        return rem == 0 ? capacity_ : rem;
    }
    return capacity_;
}

void Archetype::push_back(EntityId e)
{
    const auto c = static_cast<std::uint32_t>(count_ / capacity_);
    for (auto& col : columns_)
    {
        col.ensure_chunk(c);
    }
    entities_.push_back(e);
    ++count_;
}

void Archetype::pop_back()
{
    assert(count_ > 0);
    if (count_ % capacity_ == 0)
    {
        for (auto& col : columns_)
        {
            col.pop_chunk();
        }
    }
    entities_.pop_back();
    --count_;
}

std::uint32_t Archetype::back_chunk() const noexcept
{
    return static_cast<std::uint32_t>(count_ / capacity_);
}

std::uint32_t Archetype::back_row() const noexcept
{
    const auto rem = count_ % capacity_;
    return static_cast<std::uint32_t>(rem == 0 ? capacity_ - 1 : rem - 1);
}

void Archetype::swap_into(std::uint32_t dst_chunk, std::uint32_t dst_row,
                          std::uint32_t src_chunk, std::uint32_t src_row)
{
    const auto dst = global_row(dst_chunk, dst_row);
    const auto src = global_row(src_chunk, src_row);
    for (auto& col : columns_)
    {
        col.move_assign_raw(dst_chunk, dst_row, src_chunk, src_row);
    }
    std::swap(entities_[dst], entities_[src]);
}

ArchetypeId Storage::find_or_create(std::span<const ComponentId> ids)
{
    ComponentKey key{ids.begin(), ids.end()};
    std::ranges::sort(key);
    key.erase(std::ranges::unique(key).begin(), key.end());

    if (const auto it = index_.find(key); it != index_.end())
    {
        return it->second;
    }

    const auto id = static_cast<ArchetypeId>(archetypes_.size());
    archetypes_.emplace_back(key, compute_chunk_capacity(key));
    index_.emplace(std::move(key), id);
    return id;
}

ArchetypeId Storage::with(ArchetypeId current, ComponentId id, bool add)
{
    ComponentKey key{at(current).key().begin(), at(current).key().end()};
    if (add)
    {
        key.push_back(id);
        std::ranges::sort(key);
    }
    else
    {
        std::erase(key, id);
    }
    return find_or_create(key);
}

void Storage::copy_shared(const Archetype& src, std::uint32_t src_chunk,
                          std::uint32_t src_row, Archetype& dst,
                          std::uint32_t dst_chunk, std::uint32_t dst_row)
{
    for (const auto id : dst.key())
    {
        const auto si = src.find(id);
        if (si == kNoIndex)
        {
            continue;
        }
        dst.column_at(dst.find(id)).move_assign_raw(dst_chunk, dst_row,
                                                    src_chunk, src_row);
    }
    dst.set_entity(dst_chunk, dst_row, src.entity_at(src_chunk, src_row));
}

EntityId Storage::fill_hole(Archetype& src, std::uint32_t chunk,
                            std::uint32_t row)
{
    if (src.entity_count() == 1)
    {
        src.pop_back();
        return {};
    }

    const auto total = src.entity_count();
    const auto cap = src.chunk_capacity();
    const auto last_chunk = static_cast<std::uint32_t>((total - 1) / cap);
    const auto last_row = static_cast<std::uint32_t>((total - 1) % cap);

    if (last_chunk == chunk && last_row == row)
    {
        src.pop_back();
        return {};
    }

    const EntityId moved = src.entity_at(last_chunk, last_row);
    src.swap_into(chunk, row, last_chunk, last_row);
    return moved;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt**

本任务复用 Task 4 已加入的 `src/ecs/storage.cpp`，无需新增源文件。

- [ ] **Step 4: 在 tests/test_storage.cpp 追加用例**

```cpp
#include <algorithm>

using gamev2::ecs::Archetype;
using gamev2::ecs::ComponentId;
using gamev2::ecs::EntityId;
using gamev2::ecs::Storage;
using gamev2::ecs::kNoIndex;

namespace {

struct TagA
{
    int a = 0;
};

struct TagB
{
    int b = 0;
};

}  // namespace

TEST(archetype_sorts_and_dedupes_key)
{
    const auto a = component_id_of<TagA>();
    const auto b = component_id_of<TagB>();
    Archetype arche{{b, a, b}, 4};
    const auto key = arche.key();
    CHECK_EQ(key.size(), std::size_t{2});
    CHECK(key[0] <= key[1]);
}

TEST(archetype_reports_chunk_count)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    CHECK_EQ(arche.chunk_count(), 0u);

    for (int i = 0; i < 3; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 1u);
    CHECK_EQ(arche.count_in(0), std::size_t{3});
}

TEST(archetype_all_chunks_full_except_last)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 10; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 3u);
    CHECK_EQ(arche.count_in(0), std::size_t{4});
    CHECK_EQ(arche.count_in(1), std::size_t{4});
    CHECK_EQ(arche.count_in(2), std::size_t{2});
}

TEST(archetype_pop_back_drops_empty_tail_chunk)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 8; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 2u);
    for (int i = 0; i < 4; ++i)
    {
        arche.pop_back();
    }
    CHECK_EQ(arche.chunk_count(), 1u);
    CHECK_EQ(arche.entity_count(), std::size_t{4});
}

TEST(archetype_back_points_to_last_row)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 5; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
        CHECK_EQ(arche.back_chunk(), static_cast<std::uint32_t>(i / 4));
        CHECK_EQ(arche.back_row(), static_cast<std::uint32_t>(i % 4));
    }
}

TEST(archetype_entity_at_roundtrip)
{
    Archetype arche{{component_id_of<TagA>()}, 2};
    const auto e0 = EntityId::make(10, 1);
    const auto e1 = EntityId::make(11, 1);
    const auto e2 = EntityId::make(12, 1);
    arche.push_back(e0);
    arche.push_back(e1);
    arche.push_back(e2);
    CHECK(arche.entity_at(0, 0) == e0);
    CHECK(arche.entity_at(0, 1) == e1);
    CHECK(arche.entity_at(1, 0) == e2);
}

TEST(archetype_find_returns_no_index_when_absent)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    CHECK(arche.find(component_id_of<TagB>()) == kNoIndex);
    CHECK(!arche.has(component_id_of<TagB>()));
}

TEST(storage_find_or_create_is_stable)
{
    Storage storage;
    const std::vector<ComponentId> ids{component_id_of<TagA>()};
    const auto first = storage.find_or_create(ids);
    const auto second = storage.find_or_create(ids);
    CHECK(first == second);
    CHECK(first != gamev2::ecs::kInvalidArchetype);
}

TEST(storage_with_adds_component)
{
    Storage storage;
    const auto base = storage.find_or_create(
        std::span<const ComponentId>{});
    const auto with_a = storage.with(base, component_id_of<TagA>(), true);
    CHECK(storage.at(with_a).has(component_id_of<TagA>()));
    CHECK(!storage.at(base).has(component_id_of<TagA>()));
}

TEST(storage_with_removes_component)
{
    Storage storage;
    const auto both = storage.find_or_create(
        std::vector<ComponentId>{component_id_of<TagA>(),
                                 component_id_of<TagB>()});
    const auto only_a = storage.with(both, component_id_of<TagB>(), false);
    CHECK(storage.at(only_a).has(component_id_of<TagA>()));
    CHECK(!storage.at(only_a).has(component_id_of<TagB>()));
}

TEST(storage_fill_hole_swaps_tail_entity)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);

    const auto e0 = EntityId::make(0, 1);
    const auto e1 = EntityId::make(1, 1);
    const auto e2 = EntityId::make(2, 1);
    arche.push_back(e0);
    arche.push_back(e1);
    arche.push_back(e2);

    const auto moved = storage.fill_hole(arche, 0, 0);
    CHECK(moved == e2);
    CHECK(arche.entity_at(0, 0) == e2);
    CHECK_EQ(arche.entity_count(), std::size_t{2});
}

TEST(storage_fill_hole_on_tail_row_returns_empty)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);
    arche.push_back(EntityId::make(0, 1));
    arche.push_back(EntityId::make(1, 1));

    const auto moved = storage.fill_hole(arche, 1, 0);
    CHECK(!moved);
    CHECK_EQ(arche.entity_count(), std::size_t{1});
}

TEST(storage_fill_hole_on_last_entity_returns_empty)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);
    arche.push_back(EntityId::make(0, 1));

    const auto moved = storage.fill_hole(arche, 0, 0);
    CHECK(!moved);
    CHECK_EQ(arche.entity_count(), std::size_t{0});
}
```

- [ ] **Step 5: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `33 test(s), 0 failed`，`exit=0`。

---

### Task 6: World 实体生命周期与组件增删

**Files:**
- Create: `include/gamev2/ecs/world.hpp`
- Create: `src/ecs/world.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_world.cpp`

**Interfaces:**
- Consumes: `EntityId`（Task 2）、`component_id_of<T>`（Task 3）、
  `Storage` / `Archetype` / `kInvalidArchetype`（Task 5）、
  `Diagnostic`（Task 2）
- Produces:
  - `gamev2::ecs::EcsError::entity_invalid` / `component_missing` /
    `component_exists`
  - `gamev2::ecs::EntitySlot`
  - `gamev2::ecs::World`
    - `World()`、`~World()`
    - `void set_diagnostic_handler(DiagnosticHandler)`
    - `[[nodiscard]] EntityId create()`
    - `void destroy(EntityId)`
    - `template<typename T, typename... Args>
      std::expected<void, EcsError> add(EntityId, Args&&...)`
    - `template<typename T> std::expected<void, EcsError> remove(EntityId)`
    - `template<typename T> T* get_if(EntityId) noexcept`
    - `template<typename T> bool has(EntityId) const noexcept`
    - `[[nodiscard]] std::size_t entity_count() const noexcept`
    - `[[nodiscard]] bool is_alive(EntityId) const noexcept`
    - `void report(Diagnostic) const`
    - `Storage& storage() noexcept`
    - `const EntitySlot* slot_of(EntityId) const noexcept`
    - `void enter_system_execution() const noexcept`
    - `void exit_system_execution() const noexcept`
    - `bool parallelize_allowed() const noexcept`
    - `ThreadPool& parallel_pool() noexcept`

- [ ] **Step 1: 创建 include/gamev2/ecs/thread_pool.hpp**

```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gamev2::ecs {

/// 固定大小的 worker 池。用原子计数器分发任务，无需每任务加锁。
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return workers_.size();
    }

    /// 提交一批互相独立的任务并等待全部完成。
    /// 调用线程也参与执行，因此 worker 数为 0 时行为退化为串行。
    void run(const std::vector<std::function<void()>>& jobs);

private:
    void worker_loop(std::uint64_t seen);
    void drain();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;

    const std::vector<std::function<void()>>* jobs_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::atomic<std::size_t> done_{0};
    std::uint64_t generation_ = 0;
    bool stop_ = false;
};

}  // namespace gamev2::ecs
```

- [ ] **Step 2: 创建 src/ecs/thread_pool.cpp**

```cpp
#include "gamev2/ecs/thread_pool.hpp"

namespace gamev2::ecs {

ThreadPool::ThreadPool(std::size_t thread_count)
{
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
    {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

void ThreadPool::drain()
{
    const auto* jobs = jobs_;
    if (jobs == nullptr)
    {
        return;
    }
    for (;;)
    {
        const auto i = next_.fetch_add(1, std::memory_order_relaxed);
        if (i >= jobs->size())
        {
            return;
        }
        (*jobs)[i]();
        done_.fetch_add(1, std::memory_order_release);
    }
}

void ThreadPool::run(const std::vector<std::function<void()>>& jobs)
{
    if (jobs.empty())
    {
        return;
    }
    if (workers_.empty())
    {
        for (const auto& job : jobs)
        {
            job();
        }
        return;
    }

    {
        std::lock_guard lock{mutex_};
        jobs_ = &jobs;
        next_.store(0, std::memory_order_relaxed);
        done_.store(0, std::memory_order_relaxed);
        ++generation_;
    }
    cv_.notify_all();

    drain();

    std::unique_lock lock{mutex_};
    done_cv_.wait(lock, [this, &jobs]
                  { return done_.load(std::memory_order_acquire) >=
                           jobs.size(); });
    jobs_ = nullptr;
}

void ThreadPool::worker_loop(std::uint64_t seen)
{
    for (;;)
    {
        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock,
                     [this, seen] { return generation_ != seen || stop_; });
            if (stop_)
            {
                return;
            }
            seen = generation_;
        }
        drain();
    }
}

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 创建 include/gamev2/ecs/world.hpp**

```cpp
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/diagnostic.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/storage.hpp"
#include "gamev2/ecs/thread_pool.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

namespace gamev2::ecs {

enum class EcsError
{
    entity_invalid,
    component_missing,
    component_exists,
};

/// EntityId 索引到这张表即可定位存储位置。
struct EntitySlot
{
    ArchetypeId archetype = kInvalidArchetype;
    std::uint32_t version = 0;
    std::uint32_t chunk = 0;
    std::uint32_t row = 0;
};

class World
{
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void set_diagnostic_handler(DiagnosticHandler handler);

    [[nodiscard]] EntityId create();

    /// 重复销毁静默忽略：游戏逻辑中 destroy 被多次调用是常态。
    void destroy(EntityId e);

    template<typename T, typename... Args>
    std::expected<void, EcsError> add(EntityId e, Args&&... args);

    template<typename T>
    std::expected<void, EcsError> remove(EntityId e);

    /// 实体失效或组件缺失时返回 nullptr。热路径，不做错误检查。
    template<typename T>
    [[nodiscard]] T* get_if(EntityId e) noexcept
    {
        const auto* s = slot_of(e);
        if (s == nullptr)
        {
            return nullptr;
        }
        auto& arche = storage_.at(s->archetype);
        const auto ci = arche.find(component_id_of<T>());
        if (ci == kNoIndex)
        {
            return nullptr;
        }
        return arche.column_at(ci).template at<T>(s->chunk, s->row);
    }

    template<typename T>
    [[nodiscard]] bool has(EntityId e) const noexcept
    {
        const auto* s = slot_of(e);
        if (s == nullptr)
        {
            return false;
        }
        return storage_.at(s->archetype).has(component_id_of<T>());
    }

    [[nodiscard]] std::size_t entity_count() const noexcept
    {
        return alive_;
    }

    [[nodiscard]] bool is_alive(EntityId e) const noexcept
    {
        return slot_of(e) != nullptr;
    }

    void report(Diagnostic diag) const
    {
        emit_diagnostic(diag, diag_);
    }

    [[nodiscard]] Storage& storage() noexcept { return storage_; }
    [[nodiscard]] const Storage& storage() const noexcept
    {
        return storage_;
    }

    [[nodiscard]] const EntitySlot* slot_of(EntityId e) const noexcept;

    [[nodiscard]] ThreadPool& parallel_pool() noexcept { return pool_; }

    /// System 执行上下文的进入与退出，供 View 判断能否并行。
    void enter_system_execution() const noexcept
    {
        in_system_.store(true, std::memory_order_relaxed);
    }

    void exit_system_execution() const noexcept
    {
        in_system_.store(false, std::memory_order_relaxed);
    }

    [[nodiscard]] bool parallelize_allowed() const noexcept
    {
        return !in_system_.load(std::memory_order_relaxed);
    }

private:
    /// 填补空洞后把被移动实体的位置更新回它的 slot。
    void relocate(EntityId moved, std::uint32_t chunk, std::uint32_t row)
    {
        auto& ms = slots_[moved.index()];
        ms.chunk = chunk;
        ms.row = row;
    }

    Storage storage_;
    ArchetypeId empty_archetype_ = kInvalidArchetype;
    std::vector<EntitySlot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t alive_ = 0;
    DiagnosticHandler diag_;
    mutable std::atomic<bool> in_system_{false};
    ThreadPool pool_;
};

}  // namespace gamev2::ecs

#include "gamev2/ecs/view.hpp"

namespace gamev2::ecs {

/// View 在上面的 include 之后才是完整类型，定义放在此处。
template<typename... Ts>
[[nodiscard]] auto World::view()
{
    return View<Ts...>{*this};
}

/// add 与 remove 的实现也在此处，跟随头文件隐式实例化。
/// 若放在 world.cpp 中，非模板翻译单元无法为新组件类型生成实例。

template<typename T, typename... Args>
std::expected<void, EcsError> World::add(EntityId e, Args&&... args)
{
    if (slot_of(e) == nullptr)
    {
        return EcsError::entity_invalid;
    }

    auto& slot = slots_[e.index()];
    const auto cid = component_id_of<T>();
    const auto src_id = slot.archetype;

    if (storage_.at(src_id).has(cid))
    {
        return EcsError::component_exists;
    }

    const auto target = storage_.with(src_id, cid, true);
    // with() 可能新建 archetype 并触发 archetypes_ 重新分配，
    // 因此对存储的引用必须在它之后再取，否则悬垂。
    auto& src = storage_.at(src_id);
    auto& dst = storage_.at(target);

    dst.push_back(e);
    const auto dc = dst.back_chunk();
    const auto dr = dst.back_row();

    Storage::copy_shared(src, slot.chunk, slot.row, dst, dc, dr);
    dst.column_at(dst.find(cid))
        .template construct<T>(dc, dr, std::forward<Args>(args)...);

    const EntityId moved = storage_.fill_hole(src, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = target;
    slot.chunk = dc;
    slot.row = dr;
    return {};
}

template<typename T>
std::expected<void, EcsError> World::remove(EntityId e)
{
    if (slot_of(e) == nullptr)
    {
        return EcsError::entity_invalid;
    }

    auto& slot = slots_[e.index()];
    const auto cid = component_id_of<T>();
    const auto src_id = slot.archetype;

    const auto si = storage_.at(src_id).find(cid);
    if (si == kNoIndex)
    {
        return EcsError::component_missing;
    }

    const auto target = storage_.with(src_id, cid, false);
    // 与 add 相同：引用必须在 with() 之后再取。
    auto& src = storage_.at(src_id);
    auto& dst = storage_.at(target);

    dst.push_back(e);
    const auto dc = dst.back_chunk();
    const auto dr = dst.back_row();

    Storage::copy_shared(src, slot.chunk, slot.row, dst, dc, dr);
    // 必须先析构再填补：填补的移动赋值会覆盖尚未析构的组件。
    src.column_at(si).destroy_row(slot.chunk, slot.row);

    const EntityId moved = storage_.fill_hole(src, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = target;
    slot.chunk = dc;
    slot.row = dr;
    return {};
}

}  // namespace gamev2::ecs
```

- [ ] **Step 4: 创建 include/gamev2/ecs/view.hpp**

```cpp
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/storage.hpp"
#include "gamev2/ecs/world.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 编译期类型化的实体视图。Ts 中的 const 表示只读访问。
///
/// 构造时预先算出匹配的 archetype 与 chunk 列表，遍历时不再做集合运算。
template<typename... Ts>
class View
{
public:
    static_assert(sizeof...(Ts) > 0, "View 至少需要一个组件类型");

    struct ChunkTask
    {
        ArchetypeId archetype;
        std::uint32_t chunk;
    };

    explicit View(World& world);

    template<typename F>
    void each(F&& f)
    {
        for (const auto& task : tasks_)
        {
            run_chunk(task, f);
        }
    }

    /// 按 chunk 提交线程池并行遍历。
    ///
    /// 当前线程已在执行 System 时退化为串行：批内并行与内部并行嵌套会让
    /// worker 全部阻塞等待池中排队中的自身任务。
    template<typename F>
    void each_parallel(F&& f)
    {
        if (!world_->parallelize_allowed())
        {
            each(std::forward<F>(f));
            return;
        }

        std::vector<std::function<void()>> jobs;
        jobs.reserve(tasks_.size());
        for (const auto& task : tasks_)
        {
            jobs.emplace_back([this, &f, task] { run_chunk(task, f); });
        }
        world_->parallel_pool().run(jobs);
    }

private:
    template<std::size_t I>
    [[nodiscard]] auto column_base(ArchetypeId aid, std::uint32_t chunk)
        noexcept
    {
        using T =
            std::remove_const_t<std::tuple_element_t<I, std::tuple<Ts...>>>;
        auto& arche = world_->storage().at(aid);
        return arche.column_at(arche.find(component_id_of<T>()))
            .template at<T>(chunk, 0);
    }

    template<typename F>
    void run_chunk(const ChunkTask& task, F& f)
    {
        auto& arche = world_->storage().at(task.archetype);
        const auto n = arche.count_in(task.chunk);
        if (n == 0)
        {
            return;
        }

        auto bases = std::tuple{column_base<0>(task.archetype, task.chunk)...};
        for (std::size_t r = 0; r < n; ++r)
        {
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                f(*std::get<I>(bases) + r);
            }(std::index_sequence_for<Ts...>{});
        }
    }

    World* world_ = nullptr;
    std::vector<ChunkTask> tasks_;
};

template<typename... Ts>
View<Ts...>::View(World& world)
    : world_{&world}
{
    const std::vector<ComponentId> required{component_id_of<
        std::remove_const_t<Ts>>()...};

    for (const auto aid : world.storage().archetypes())
    {
        const auto& arche = world.storage().at(aid);
        if (!std::ranges::all_of(required, [&arche](ComponentId id)
                                 { return arche.has(id); }))
        {
            continue;
        }
        for (std::uint32_t c = 0; c < arche.chunk_count(); ++c)
        {
            if (arche.count_in(c) > 0)
            {
                tasks_.push_back(ChunkTask{aid, c});
            }
        }
    }
}

}  // namespace gamev2::ecs
```

> **包含顺序**：`world.hpp` 在命名空间闭合之后才 include `view.hpp`，
> 这样 `World::view()` 的类外声明可见完整的 `View` 类型，而
> `view.hpp` 又能正常使用已定义的 `World`。

- [ ] **Step 5: 创建 src/ecs/world.cpp**

```cpp
#include "gamev2/ecs/world.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace gamev2::ecs {

namespace {

/// 留一个主线程给调用方，其余核分给 worker。
[[nodiscard]] std::size_t default_worker_count()
{
    const auto cores = std::thread::hardware_concurrency();
    if (cores <= 2)
    {
        return 0;
    }
    return static_cast<std::size_t>(cores) - 1;
}

}  // namespace

World::World()
    : diag_{default_diagnostic_handler()},
      pool_{default_worker_count()}
{
    empty_archetype_ = storage_.find_or_create(std::span<const ComponentId>{});
}

World::~World() = default;

const EntitySlot* World::slot_of(EntityId e) const noexcept
{
    const auto idx = e.index();
    if (idx >= slots_.size())
    {
        return nullptr;
    }
    const auto& slot = slots_[idx];
    if (slot.version != e.version() || slot.archetype == kInvalidArchetype)
    {
        return nullptr;
    }
    return &slot;
}

EntityId World::create()
{
    std::uint32_t idx = 0;
    if (!free_indices_.empty())
    {
        idx = free_indices_.back();
        free_indices_.pop_back();
    }
    else
    {
        idx = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(EntitySlot{});
        slots_.back().archetype = empty_archetype_;
        // 版本号从 1 起始，否则索引 0 会与 EntityId{} 哨兵撞值。
        slots_.back().version = 1;
    }

    auto& slot = slots_[idx];
    auto& arche = storage_.at(slot.archetype);
    const auto e = EntityId::make(idx, slot.version);
    arche.push_back(e);
    slot.chunk = arche.back_chunk();
    slot.row = arche.back_row();

    ++alive_;
    return e;
}

void World::destroy(EntityId e)
{
    if (slot_of(e) == nullptr)
    {
        return;
    }

    auto& slot = slots_[e.index()];
    auto& arche = storage_.at(slot.archetype);

    for (std::size_t i = 0; i < arche.key().size(); ++i)
    {
        arche.column_at(i).destroy_row(slot.chunk, slot.row);
    }

    const EntityId moved = storage_.fill_hole(arche, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = kInvalidArchetype;
    ++slot.version;
    if (slot.version == 0)
    {
        // 跳过与 EntityId{} 非法哨兵撞值的版本号。
        ++slot.version;
    }
    free_indices_.push_back(e.index());
    --alive_;
}

}  // namespace gamev2::ecs
```

> `add` 与 `remove` 不在本文件中。模板定义必须对每个使用它的翻译
> 单元可见，声明在 world.cpp 而定义在 world.hpp 会导致链接错误。
> 它们的定义已随 Step 3 的 world.hpp 一并给出。

- [ ] **Step 6: 更新 CMakeLists.txt**

`add_library` 源文件列表追加 `src/ecs/thread_pool.cpp`、
`src/ecs/world.cpp`，
`add_executable` 源文件列表追加 `tests/test_world.cpp`。

- [ ] **Step 7: 创建 tests/test_world.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/world.hpp"

#include <vector>

using gamev2::ecs::EcsError;
using gamev2::ecs::EntityId;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
    int y = 0;
};

struct Velocity
{
    int dx = 0;
    int dy = 0;
};

struct Health
{
    int hp = 0;
};

}  // namespace

TEST(world_create_yields_alive_entity)
{
    World world;
    const auto e = world.create();
    CHECK(static_cast<bool>(e));
    CHECK(world.is_alive(e));
    CHECK_EQ(world.entity_count(), std::size_t{1});
}

TEST(world_first_entity_does_not_collide_with_sentinel)
{
    World world;
    const auto e = world.create();
    CHECK_EQ(e.index(), 0u);
    CHECK_EQ(e.version(), 1u);
    CHECK(static_cast<bool>(e));
    CHECK(world.is_alive(e));
}

TEST(world_add_then_get)
{
    World world;
    const auto e = world.create();
    const auto r = world.add<Position>(e, Position{3, 4});
    CHECK(r.has_value());

    const auto* p = world.get_if<Position>(e);
    CHECK(p != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 3);
        CHECK_EQ(p->y, 4);
    }
}

TEST(world_add_duplicate_returns_component_exists)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{});
    const auto r = world.add<Position>(e, Position{});
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::component_exists);
}

TEST(world_add_on_dead_entity_returns_entity_invalid)
{
    World world;
    const auto r = world.add<Position>(EntityId::make(99, 1), Position{});
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::entity_invalid);
}

TEST(world_get_missing_component_returns_null)
{
    World world;
    const auto e = world.create();
    CHECK(world.get_if<Position>(e) == nullptr);
    CHECK(!world.has<Position>(e));
}

TEST(world_remove_missing_returns_component_missing)
{
    World world;
    const auto e = world.create();
    const auto r = world.remove<Position>(e);
    CHECK(!r.has_value());
    CHECK(r.error() == EcsError::component_missing);
}

TEST(world_migration_preserves_other_components)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{10, 20});
    (void)world.add<Velocity>(e, Velocity{1, 2});
    (void)world.add<Health>(e, Health{77});

    (void)world.remove<Velocity>(e);

    CHECK(!world.has<Velocity>(e));
    const auto* p = world.get_if<Position>(e);
    const auto* h = world.get_if<Health>(e);
    CHECK(p != nullptr && h != nullptr);
    if (p != nullptr && h != nullptr)
    {
        CHECK_EQ(p->x, 10);
        CHECK_EQ(p->y, 20);
        CHECK_EQ(h->hp, 77);
    }
}

TEST(world_destroy_invalidates_stale_handle)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{5, 6});
    world.destroy(e);

    CHECK(!world.is_alive(e));
    CHECK(world.get_if<Position>(e) == nullptr);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(world_repeated_destroy_is_silent)
{
    World world;
    const auto e = world.create();
    world.destroy(e);
    world.destroy(e);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(world_recycled_slot_bumps_version)
{
    World world;
    const auto first = world.create();
    world.destroy(first);
    const auto second = world.create();

    CHECK(second.index() == first.index());
    CHECK(second.version() != first.version());
    CHECK(!world.is_alive(first));
    CHECK(world.is_alive(second));
}

TEST(world_destroy_neighbour_keeps_its_components)
{
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < 8; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    world.destroy(entities[0]);

    for (std::size_t i = 1; i < entities.size(); ++i)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        CHECK(h != nullptr);
        if (h != nullptr)
        {
            CHECK_EQ(h->hp, static_cast<int>(i));
        }
    }
}

TEST(world_many_entities_survive_churn)
{
    World world;
    std::vector<EntityId> entities;
    for (int i = 0; i < 2000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
        entities.push_back(e);
    }

    for (std::size_t i = 0; i < entities.size(); i += 2)
    {
        world.destroy(entities[i]);
    }

    std::size_t alive = 0;
    for (std::size_t i = 1; i < entities.size(); i += 2)
    {
        const auto* h = world.get_if<Health>(entities[i]);
        if (h != nullptr && h->hp == static_cast<int>(i))
        {
            ++alive;
        }
    }
    CHECK_EQ(alive, std::size_t{1000});
    CHECK_EQ(world.entity_count(), std::size_t{1000});
}
```

- [ ] **Step 8: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `46 test(s), 0 failed`，`exit=0`。

---

### Task 7: View 遍历与并行执行

**Files:**
- Test: `tests/test_view.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `World::view<Ts...>()`、`View::each` / `each_parallel`、
  `ThreadPool`（Task 6）
- Produces: 无新增公开接口，仅验证遍历与并行正确性

- [ ] **Step 1: 创建 tests/test_view.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/view.hpp"

#include <vector>

using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 0;
};

struct Health
{
    int hp = 0;
};

}  // namespace

TEST(view_matches_only_full_component_sets)
{
    World world;
    const auto a = world.create();
    const auto b = world.create();
    (void)world.add<Position>(a, Position{1});
    (void)world.add<Position>(b, Position{2});
    (void)world.add<Velocity>(b, Velocity{3});

    std::vector<int> seen;
    world.view<Position, Velocity>().each(
        [&seen](Position& p, Velocity& v) { seen.push_back(p.x + v.dx); });

    CHECK_EQ(seen.size(), std::size_t{1});
    if (!seen.empty())
    {
        CHECK_EQ(seen[0], 5);
    }
}

TEST(view_readonly_param_yields_values)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{42});

    int total = 0;
    world.view<const Position>().each(
        [&total](const Position& p) { total += p.x; });
    CHECK_EQ(total, 42);
}

TEST(view_visits_every_entity_across_chunks)
{
    World world;
    for (int i = 0; i < 5000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{i});
    }

    std::vector<int> seen;
    seen.reserve(5000);
    world.view<Health>().each([&seen](Health& h) { seen.push_back(h.hp); });
    CHECK_EQ(seen.size(), std::size_t{5000});

    long long sum = 0;
    for (const auto v : seen)
    {
        sum += v;
    }
    CHECK_EQ(sum, 4999LL * 5000 / 2);
}

TEST(view_parallel_matches_sequential)
{
    World world;
    for (int i = 0; i < 20000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{i});
        (void)world.add<Velocity>(e, Velocity{2 * i});
    }

    std::vector<int> seq;
    std::vector<int> par;
    seq.reserve(20000);
    par.reserve(20000);

    world.view<Position, Velocity>().each(
        [&seq](Position& p, Velocity& v) { seq.push_back(p.x + v.dx); });
    world.view<Position, Velocity>().each_parallel(
        [&par](Position& p, Velocity& v) { par.push_back(p.x + v.dx); });

    CHECK_EQ(seq.size(), par.size());
    if (seq.size() == par.size())
    {
        bool same = true;
        for (std::size_t i = 0; i < seq.size(); ++i)
        {
            if (seq[i] != par[i])
            {
                same = false;
                break;
            }
        }
        CHECK(same);
    }
}

TEST(view_parallel_writes_accumulate)
{
    World world;
    for (int i = 0; i < 10000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{1});
    }

    world.view<Health>().each_parallel([](Health& h) { h.hp += 100; });

    std::size_t updated = 0;
    world.view<Health>().each([&updated](const Health& h)
                               { updated += (h.hp == 101) ? 1u : 0u; });
    CHECK_EQ(updated, std::size_t{10000});
}

TEST(view_parallel_degrades_inside_system)
{
    World world;
    for (int i = 0; i < 500; ++i)
    {
        const auto e = world.create();
        (void)world.add<Health>(e, Health{2});
    }

    std::size_t count = 0;
    world.enter_system_execution();
    world.view<Health>().each_parallel(
        [&count](Health& h) { count += static_cast<std::size_t>(h.hp); });
    world.exit_system_execution();

    CHECK_EQ(count, std::size_t{1000});
}

TEST(view_empty_when_no_match)
{
    World world;
    const auto e = world.create();
    (void)world.add<Health>(e, Health{1});
    std::size_t count = 0;
    world.view<Position>().each([&count](Position&) { ++count; });
    CHECK_EQ(count, std::size_t{0});
}

TEST(view_spans_multiple_archetypes)
{
    World world;
    for (int i = 0; i < 100; ++i)
    {
        const auto a = world.create();
        (void)world.add<Position>(a, Position{i});
        (void)world.add<Velocity>(a, Velocity{i});

        const auto b = world.create();
        (void)world.add<Position>(b, Position{i});
        (void)world.add<Velocity>(b, Velocity{i});
        (void)world.add<Health>(b, Health{i});
    }

    std::size_t both = 0;
    std::size_t with_health = 0;
    world.view<Position, Velocity>().each([&both](Position&, Velocity&)
                                          { ++both; });
    world.view<Position, Velocity, Health>().each(
        [&with_health](Position&, Velocity&, Health&) { ++with_health; });

    CHECK_EQ(both, std::size_t{200});
    CHECK_EQ(with_health, std::size_t{100});
}
```

- [ ] **Step 2: 更新 CMakeLists.txt**

`add_executable` 源文件列表追加 `tests/test_view.cpp`。

- [ ] **Step 3: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `54 test(s), 0 failed`，`exit=0`。

---

### Task 8: Command Buffer

**Files:**
- Create: `include/gamev2/ecs/command_buffer.hpp`
- Create: `src/ecs/command_buffer.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_command_buffer.cpp`

**Interfaces:**
- Consumes: `World`、`EntityId`、`ComponentId`（Task 2、3、6）
- Produces:
  - `void CommandBuffer::destroy(EntityId)`
  - `template<typename T, typename... Args> void add(EntityId, Args&&...)`
  - `template<typename T> void remove(EntityId)`
  - `void append(CommandBuffer&&)`
  - `std::size_t pending() const noexcept`
  - `void flush(World&)`

- [ ] **Step 1: 创建 include/gamev2/ecs/command_buffer.hpp**

```cpp
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstddef>
#include <functional>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 延迟结构性变更的队列。
///
/// 命令按入队顺序执行。flush 前目标实体若已失效，该命令被拒绝并记诊断，
/// 因此 destroy 之后 add 的语义是可预测的。
class CommandBuffer
{
public:
    void destroy(EntityId e);

    template<typename T, typename... Args>
    void add(EntityId e, Args&&... args);

    template<typename T>
    void remove(EntityId e);

    /// 把另一个缓冲区的命令按顺序接到自己后面。
    void append(CommandBuffer&& other);

    [[nodiscard]] std::size_t pending() const noexcept
    {
        return commands_.size();
    }

    /// 帧末主线程单线程执行。
    void flush(World& world);

private:
    /// 命令体直接持有可调用对象，因此无需为组件负载引入所有权类型。
    struct Command
    {
        EntityId target;
        std::function<void(World&)> apply;
    };

    std::vector<Command> commands_;
};

inline void CommandBuffer::destroy(EntityId e)
{
    commands_.push_back(Command{e, [e](World& w) { w.destroy(e); }});
}

template<typename T, typename... Args>
void CommandBuffer::add(EntityId e, Args&&... args)
{
    static_assert(std::is_constructible_v<T, Args...>,
                  "组件类型无法用给定参数构造");
    auto tuple = std::make_tuple(std::forward<Args>(args)...);
    commands_.push_back(Command{
        e, [e, t = std::move(tuple)](World& w) mutable
        {
            std::apply(
                [&w, e](auto&&... a)
                {
                    (void)w.template add<T>(
                        e, std::forward<decltype(a)>(a)...);
                },
                std::move(t));
        }});
}

template<typename T>
void CommandBuffer::remove(EntityId e)
{
    commands_.push_back(Command{
        e, [e](World& w) { (void)w.template remove<T>(e); }});
}

inline void CommandBuffer::append(CommandBuffer&& other)
{
    commands_.insert(commands_.end(),
                     std::make_move_iterator(other.commands_.begin()),
                     std::make_move_iterator(other.commands_.end()));
}

}  // namespace gamev2::ecs
```

- [ ] **Step 2: 创建 src/ecs/command_buffer.cpp**

```cpp
#include "gamev2/ecs/command_buffer.hpp"

#include <string>

namespace gamev2::ecs {

void CommandBuffer::flush(World& world)
{
    for (const auto& cmd : commands_)
    {
        if (!world.is_alive(cmd.target))
        {
            world.report(Diagnostic{
                DiagnosticLevel::error,
                "ecs.entity_invalid",
                std::string{"命令目标实体已失效，命令被拒绝"}});
            continue;
        }
        cmd.apply(world);
    }
    commands_.clear();
}

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 更新 CMakeLists.txt**

`add_library` 源文件列表追加 `src/ecs/command_buffer.cpp`，
`add_executable` 源文件列表追加 `tests/test_command_buffer.cpp`。

- [ ] **Step 4: 创建 tests/test_command_buffer.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/command_buffer.hpp"

#include <string>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::Diagnostic;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 0;
};

}  // namespace

TEST(command_buffer_defers_changes_until_flush)
{
    World world;
    const auto e = world.create();

    CommandBuffer cmd;
    cmd.add<Position>(e, Position{5});
    CHECK_EQ(cmd.pending(), std::size_t{1});
    CHECK(world.get_if<Position>(e) == nullptr);

    cmd.flush(world);
    CHECK_EQ(cmd.pending(), std::size_t{0});
    CHECK(world.get_if<Position>(e) != nullptr);
}

TEST(command_buffer_preserves_enqueue_order)
{
    World world;
    const auto e = world.create();

    CommandBuffer cmd;
    cmd.add<Position>(e, Position{1});
    cmd.add<Position>(e, Position{2});
    cmd.add<Position>(e, Position{3});
    cmd.flush(world);

    const auto* p = world.get_if<Position>(e);
    CHECK(p != nullptr);
    if (p != nullptr)
    {
        CHECK_EQ(p->x, 3);
    }
}

TEST(command_buffer_rejects_add_after_destroy)
{
    World world;
    const auto e = world.create();

    std::vector<std::string> codes;
    world.set_diagnostic_handler(
        [&codes](const Diagnostic& d) { codes.emplace_back(d.code); });

    CommandBuffer cmd;
    cmd.destroy(e);
    cmd.add<Position>(e, Position{1});
    cmd.flush(world);

    CHECK(!world.is_alive(e));
    CHECK_EQ(codes.size(), std::size_t{1});
    if (!codes.empty())
    {
        CHECK(codes[0] == "ecs.entity_invalid");
    }
}

TEST(command_buffer_remove_applies_at_flush)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{1});
    (void)world.add<Velocity>(e, Velocity{2});

    CommandBuffer cmd;
    cmd.remove<Velocity>(e);
    CHECK(world.has<Velocity>(e));

    cmd.flush(world);
    CHECK(!world.has<Velocity>(e));
    CHECK(world.has<Position>(e));
}

TEST(command_buffer_append_keeps_order)
{
    World world;
    const auto a = world.create();
    const auto b = world.create();

    CommandBuffer first;
    first.add<Position>(a, Position{10});

    CommandBuffer second;
    second.add<Position>(b, Position{20});

    first.append(std::move(second));
    first.flush(world);

    CHECK_EQ(world.get_if<Position>(a)->x, 10);
    CHECK_EQ(world.get_if<Position>(b)->x, 20);
}

TEST(command_buffer_flush_of_empty_buffer_is_noop)
{
    World world;
    CommandBuffer cmd;
    cmd.flush(world);
    CHECK_EQ(world.entity_count(), std::size_t{0});
}

TEST(command_buffer_many_commands)
{
    World world;
    std::vector<gamev2::ecs::EntityId> entities;
    for (int i = 0; i < 500; ++i)
    {
        entities.push_back(world.create());
    }

    CommandBuffer cmd;
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        cmd.add<Position>(entities[i], Position{static_cast<int>(i)});
    }
    cmd.flush(world);

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const auto* p = world.get_if<Position>(entities[i]);
        CHECK(p != nullptr);
        if (p != nullptr)
        {
            CHECK_EQ(p->x, static_cast<int>(i));
        }
    }
}
```

- [ ] **Step 5: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `61 test(s), 0 failed`，`exit=0`。

---

### Task 9: System 访问集与 Scheduler 冲突分批

**Files:**
- Create: `include/gamev2/ecs/system.hpp`
- Create: `include/gamev2/ecs/scheduler.hpp`
- Create: `src/ecs/scheduler.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_scheduler.cpp`

**Interfaces:**
- Consumes: `World`、`CommandBuffer`、`ThreadPool`（Task 6、8）
- Produces:
  - `gamev2::ecs::System`，纯虚 `execute(World&, CommandBuffer&)`
  - `gamev2::ecs::detail::all_distinct<Tuple>`
  - `gamev2::ecs::access_tuple<T>`、`read_set_valid<S>`、`write_set_valid<S>`
  - `gamev2::ecs::write_set_of<S>()`、`read_set_of<S>()`
  - `gamev2::ecs::Scheduler`
    - `explicit Scheduler(std::size_t worker_count)`
    - `template<typename S> void add(S&)`
    - `void plan()`
    - `void run(World&, CommandBuffer&)`
    - `std::size_t batch_count() const noexcept`

- [ ] **Step 1: 创建 include/gamev2/ecs/system.hpp**

```cpp
#pragma once

#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/world.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <vector>

namespace gamev2::ecs {

class System
{
public:
    virtual ~System() = default;

    /// 结构性变更必须走 cmd，不得直接改动 World。
    virtual void execute(World& world, CommandBuffer& cmd) = 0;
};

namespace detail {

/// 判断 T 是否出现在 Ts 中。
template<typename T, typename... Ts>
inline constexpr bool tuple_contains = (std::is_same_v<T, Ts> || ...);

template<typename... Ts>
struct all_distinct_impl;

template<>
struct all_distinct_impl<>
{
    static constexpr bool value = true;
};

template<typename T, typename... Rest>
struct all_distinct_impl<T, Rest...>
{
    static constexpr bool value =
        !tuple_contains<T, Rest...> && all_distinct_impl<Rest...>::value;
};

template<typename Tuple>
struct all_distinct;

template<typename... Ts>
struct all_distinct<std::tuple<Ts...>> : all_distinct_impl<Ts...>
{};

template<typename Tuple, std::size_t... I>
[[nodiscard]] std::vector<ComponentId> to_component_ids_impl(
    std::index_sequence<I...>)
{
    return std::vector<ComponentId>{component_id_of<
        std::remove_const_t<std::tuple_element_t<I, Tuple>>>()...};
}

template<typename Tuple>
[[nodiscard]] std::vector<ComponentId> to_component_ids()
{
    return to_component_ids_impl<Tuple>(
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

}  // namespace detail

/// 访问集必须是 std::tuple，才能取出 tuple_size 与 tuple_element。
template<typename T>
concept access_tuple = requires
{
    { std::tuple_size_v<std::remove_const_t<T>> }
        -> std::convertible_to<std::size_t>;
};

template<typename S>
concept read_set_valid = requires
{
    typename S::ReadSet;
} && access_tuple<typename S::ReadSet>;

template<typename S>
concept write_set_valid = requires
{
    typename S::WriteSet;
} && access_tuple<typename S::WriteSet> &&
      detail::all_distinct<typename S::WriteSet>::value;

/// 写集 = WriteSet 全部元素 ∪ ReadSet 中的非 const 元素。
///
/// ReadSet 里带 const 的元素只读，不计入写集。
template<typename S>
[[nodiscard]] std::vector<ComponentId> write_set_of()
{
    static_assert(read_set_valid<S>,
                  "ReadSet 必须是 std::tuple 组成的组件访问集");
    static_assert(write_set_valid<S>,
                  "WriteSet 必须是元素互不相同的 std::tuple 访问集");

    auto ids = detail::to_component_ids<typename S::WriteSet>();
    for (std::size_t i = 0; i < std::tuple_size_v<typename S::ReadSet>; ++i)
    {
        using E = std::tuple_element_t<i, typename S::ReadSet>;
        if constexpr (!std::is_const_v<E>)
        {
            ids.push_back(component_id_of<std::remove_const_t<E>>());
        }
    }
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
    return ids;
}

/// 读集 = 所有被访问的组件。写集天然也在读，修改前必先读取。
template<typename S>
[[nodiscard]] std::vector<ComponentId> read_set_of()
{
    auto ids = detail::to_component_ids<typename S::ReadSet>();
    const auto w = write_set_of<S>();
    ids.insert(ids.end(), w.begin(), w.end());
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
    return ids;
}

}  // namespace gamev2::ecs
```

- [ ] **Step 2: 创建 include/gamev2/ecs/scheduler.hpp**

```cpp
#pragma once

#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/thread_pool.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 依据读写集贪心分层，把无冲突的 System 拆进同一批并行执行。
class Scheduler
{
public:
    explicit Scheduler(std::size_t worker_count);

    template<typename S>
    void add(S& system);

    void plan();

    void run(World& world, CommandBuffer& cmd);

    [[nodiscard]] std::size_t batch_count() const noexcept
    {
        return batches_.size();
    }

private:
    struct Entry
    {
        System* system;
        std::vector<ComponentId> reads;
        std::vector<ComponentId> writes;
    };

    std::vector<Entry> entries_;
    std::vector<std::vector<std::size_t>> batches_;
    bool planned_ = false;
    ThreadPool pool_;
};

template<typename S>
void Scheduler::add(S& system)
{
    Entry entry;
    entry.system = &system;
    entry.writes = write_set_of<S>();
    entry.reads = read_set_of<S>();
    entries_.push_back(std::move(entry));
    planned_ = false;
}

}  // namespace gamev2::ecs
```

- [ ] **Step 3: 创建 src/ecs/scheduler.cpp**

```cpp
#include "gamev2/ecs/scheduler.hpp"

#include <algorithm>
#include <functional>

namespace gamev2::ecs {

namespace {

[[nodiscard]] bool intersects(const std::vector<ComponentId>& a,
                              const std::vector<ComponentId>& b)
{
    return std::ranges::any_of(a, [&b](ComponentId id)
                               { return std::ranges::find(b, id) != b.end(); });
}

}  // namespace

Scheduler::Scheduler(std::size_t worker_count)
    : pool_{worker_count}
{
}

void Scheduler::plan()
{
    batches_.clear();

    std::vector<bool> taken(entries_.size(), false);
    std::size_t remaining = entries_.size();

    while (remaining > 0)
    {
        std::vector<std::size_t> batch;
        std::vector<ComponentId> written;
        std::vector<ComponentId> read;

        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            if (taken[i])
            {
                continue;
            }
            const auto& e = entries_[i];
            const bool clashes = intersects(e.writes, written) ||
                                 intersects(e.writes, read) ||
                                 intersects(e.reads, written);
            if (clashes)
            {
                continue;
            }
            batch.push_back(i);
            taken[i] = true;
            --remaining;
            written.insert(written.end(), e.writes.begin(), e.writes.end());
            read.insert(read.end(), e.reads.begin(), e.reads.end());
        }

        if (batch.empty())
        {
            // 正常声明下不可能出现：至少有一个 System 与已选集合不冲突。
            break;
        }
        batches_.push_back(std::move(batch));
    }

    planned_ = true;
}

void Scheduler::run(World& world, CommandBuffer& cmd)
{
    if (!planned_)
    {
        plan();
    }

    // 每个 System 一个 CommandBuffer，按注册顺序合并，保证命令顺序确定。
    std::vector<CommandBuffer> buffers(entries_.size());

    for (const auto& batch : batches_)
    {
        if (batch.size() == 1)
        {
            world.enter_system_execution();
            entries_[batch[0]].system->execute(world, buffers[batch[0]]);
            world.exit_system_execution();
            continue;
        }

        std::vector<std::function<void()>> jobs;
        jobs.reserve(batch.size());
        for (const auto idx : batch)
        {
            jobs.emplace_back(
                [&world, this, idx, &buffers]
                {
                    world.enter_system_execution();
                    entries_[idx].system->execute(world, buffers[idx]);
                    world.exit_system_execution();
                });
        }
        pool_.run(jobs);
    }

    for (auto& buffer : buffers)
    {
        cmd.append(std::move(buffer));
    }
    cmd.flush(world);
}

}  // namespace gamev2::ecs
```

- [ ] **Step 4: 更新 CMakeLists.txt**

`add_library` 源文件列表追加 `src/ecs/scheduler.cpp`，
`add_executable` 源文件列表追加 `tests/test_scheduler.cpp`。

- [ ] **Step 5: 创建 tests/test_scheduler.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/scheduler.hpp"

#include <tuple>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::Scheduler;
using gamev2::ecs::World;
using gamev2::ecs::read_set_of;
using gamev2::ecs::write_set_of;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 0;
};

struct Health
{
    int hp = 0;
};

struct WritesPosition
    : gamev2::ecs::System
{
    int delta = 1;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each([this](Position& p) { p.x += delta; });
    }
};

struct WritesPositionOther
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Position>().each([](Position& p) { p.x += 100; });
    }
};

struct ReadsPosition
    : gamev2::ecs::System
{
    int observed = 0;

    using ReadSet = std::tuple<const Position>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Position>().each(
            [this](const Position& p) { observed += p.x; });
    }
};

struct ReadsVelocity
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Velocity>().each(
            [](const Velocity&) {});
    }
};

struct WritesHealth
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Health>().each([](Health& h) { h.hp -= 1; });
    }
};

struct Mixed
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Velocity, Position>().each(
            [](const Velocity& v, Position& p) { p.x += v.dx; });
    }
};

}  // namespace

TEST(access_sets_derive_from_declaration)
{
    CHECK_EQ(write_set_of<WritesPosition>().size(), std::size_t{1});
    CHECK_EQ(read_set_of<WritesPosition>().size(), std::size_t{1});
    CHECK_EQ(write_set_of<ReadsPosition>().size(), std::size_t{0});
    CHECK_EQ(read_set_of<ReadsPosition>().size(), std::size_t{1});
}

TEST(access_sets_count_const_reads_as_read_only)
{
    // Mixed 的 ReadSet 带 const，Velocity 计入读集但不计入写集。
    const auto w = write_set_of<Mixed>();
    CHECK_EQ(w.size(), std::size_t{1});
    const auto r = read_set_of<Mixed>();
    CHECK_EQ(r.size(), std::size_t{2});
}

TEST(scheduler_merges_disjoint_systems_into_one_batch)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsVelocity b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
}

TEST(scheduler_separates_write_write_conflicts)
{
    Scheduler scheduler{0};
    WritesPosition a;
    WritesPositionOther b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_separates_write_read_conflicts)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsPosition b;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_replans_after_add)
{
    Scheduler scheduler{0};
    WritesPosition a;
    scheduler.add(a);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    WritesPositionOther b;
    scheduler.add(b);
    scheduler.plan();
    CHECK_EQ(scheduler.batch_count(), std::size_t{2});
}

TEST(scheduler_reader_runs_before_conflicting_writer)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{10});

    Scheduler scheduler{0};
    ReadsPosition reader;
    WritesPosition writer;
    scheduler.add(reader);
    scheduler.add(writer);
    scheduler.plan();

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // reader 注册在前且无冲突可入第一批，必须先读到初始值 10。
    CHECK_EQ(reader.observed, 10);

    scheduler.run(world, cmd);

    // 第二帧 writer 已完成迁移并更新 slot，reader 仍读到 11。
    CHECK_EQ(reader.observed, 21);
}

TEST(scheduler_runs_all_systems_in_one_frame)
{
    World world;
    for (int i = 0; i < 1000; ++i)
    {
        const auto e = world.create();
        (void)world.add<Position>(e, Position{0});
    }

    Scheduler scheduler{0};
    WritesPosition w1;
    WritesPositionOther w2;
    scheduler.add(w1);
    scheduler.add(w2);

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    int total = 0;
    world.view<const Position>().each(
        [&total](const Position& p) { total += p.x; });
    CHECK_EQ(total, 1000 * 101);
}

TEST(scheduler_three_way_split)
{
    Scheduler scheduler{0};
    WritesPosition a;
    WritesPositionOther b;
    ReadsPosition c;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.add(c);
    scheduler.plan();

    // a 与 b 写 Position 冲突，c 读 Position，三者必须分三批。
    CHECK_EQ(scheduler.batch_count(), std::size_t{3});
}

TEST(scheduler_independent_chains_stay_separate)
{
    Scheduler scheduler{0};
    WritesPosition a;
    ReadsVelocity b;
    WritesHealth c;
    scheduler.add(a);
    scheduler.add(b);
    scheduler.add(c);
    scheduler.plan();

    // 三者访问集两两不交，应同批并行。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});
}
```

- [ ] **Step 6: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `70 test(s), 0 failed`，`exit=0`。

---

### Task 10: 端到端帧循环

**Files:**
- Test: `tests/test_integration.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `World`、`Scheduler`、`CommandBuffer`（Task 6、8、9）
- Produces: 无新增公开接口，仅验证模块整体可用

- [ ] **Step 1: 创建 tests/test_integration.cpp**

```cpp
#include "test_framework.hpp"

#include "gamev2/ecs/scheduler.hpp"

#include <tuple>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::Scheduler;
using gamev2::ecs::World;

namespace {

struct Position
{
    int x = 0;
};

struct Velocity
{
    int dx = 1;
};

struct Health
{
    int hp = 100;
};

struct Spawner
    : gamev2::ecs::System
{
    int spawned = 0;

    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<>;

    void execute(World& world, CommandBuffer& cmd) override
    {
        const auto e = world.create();
        cmd.add<Position>(e, Position{0});
        cmd.add<Velocity>(e, Velocity{1});
        cmd.add<Health>(e, Health{100});
        ++spawned;
    }
};

struct Movement
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<const Velocity>;
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<const Velocity, Position>().each(
            [](const Velocity& v, Position& p) { p.x += v.dx; });
    }
};

struct Damage
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Health>;

    void execute(World& world, CommandBuffer&) override
    {
        world.view<Health>().each([](Health& h) { h.hp -= 1; });
    }
};

}  // namespace

TEST(integration_frame_loop_is_stable)
{
    World world;
    Scheduler scheduler{0};
    Spawner spawner;
    Movement movement;
    Damage damage;

    scheduler.add(spawner);
    scheduler.add(movement);
    scheduler.add(damage);

    CommandBuffer cmd;
    for (int frame = 0; frame < 200; ++frame)
    {
        scheduler.run(world, cmd);
    }

    CHECK_EQ(spawner.spawned, 200);
    CHECK_EQ(world.entity_count(), std::size_t{200});

    // Spawner 无读写集，与另两个 System 同批。worker 数为 0 时退化为
    // 按注册顺序串行：第 k 帧创建的实体在 flush 之后才拿到组件，
    // 从第 k+1 帧起每帧移动 1 点，累计 199 + 198 + ... + 0 = 19900。
    int moved = 0;
    world.view<const Position>().each(
        [&moved](const Position& p) { moved += p.x; });
    CHECK_EQ(moved, 19900);
}

TEST(integration_independent_systems_share_a_batch)
{
    World world;
    const auto e = world.create();
    (void)world.add<Position>(e, Position{0});
    (void)world.add<Health>(e, Health{100});

    Scheduler scheduler{0};
    Movement movement;
    Damage damage;
    scheduler.add(movement);
    scheduler.add(damage);
    scheduler.plan();

    // Movement 与 Damage 的访问集不相交，应同批。
    CHECK_EQ(scheduler.batch_count(), std::size_t{1});

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    const auto* p = world.get_if<Position>(e);
    const auto* h = world.get_if<Health>(e);
    CHECK(p != nullptr && h != nullptr);
    if (p != nullptr && h != nullptr)
    {
        CHECK_EQ(p->x, 1);
        CHECK_EQ(h->hp, 99);
    }
}

TEST(integration_survives_mass_destruction)
{
    World world;
    std::vector<EntityId> entities;

    CommandBuffer cmd;
    for (int i = 0; i < 1000; ++i)
    {
        const auto e = world.create();
        cmd.add<Position>(e, Position{i});
        cmd.add<Health>(e, Health{i});
        entities.push_back(e);
    }
    cmd.flush(world);
    CHECK_EQ(world.entity_count(), std::size_t{1000});

    for (std::size_t i = 0; i < entities.size(); i += 2)
    {
        world.destroy(entities[i]);
    }
    CHECK_EQ(world.entity_count(), std::size_t{500});

    std::size_t intact = 0;
    for (std::size_t i = 1; i < entities.size(); i += 2)
    {
        const auto* p = world.get_if<Position>(entities[i]);
        if (p != nullptr && p->x == static_cast<int>(i))
        {
            ++intact;
        }
    }
    CHECK_EQ(intact, std::size_t{500});
}

TEST(integration_command_buffer_defers_to_frame_end)
{
    World world;
    Scheduler scheduler{0};
    Spawner spawner;
    scheduler.add(spawner);

    CommandBuffer cmd;
    scheduler.run(world, cmd);

    // Spawner 只调 world.create()，组件经 CommandBuffer 在帧末落地。
    CHECK_EQ(world.entity_count(), std::size_t{1});
    scheduler.run(world, cmd);
    CHECK_EQ(world.entity_count(), std::size_t{2});

    std::size_t with_position = 0;
    std::size_t with_health = 0;
    world.view<const Position>().each(
        [&with_position](const Position&) { ++with_position; });
    world.view<const Health>().each(
        [&with_health](const Health&) { ++with_health; });
    CHECK_EQ(with_position, std::size_t{1});
    CHECK_EQ(with_health, std::size_t{1});
}
```

- [ ] **Step 2: 更新 CMakeLists.txt**

`add_executable` 源文件列表追加 `tests/test_integration.cpp`。

- [ ] **Step 3: 构建并运行**

Run:

```powershell
cmake --build build
& .\build\ecs_tests.exe
Write-Output ('exit=' + $LASTEXITCODE)
```

Expected: `74 test(s), 0 failed`，`exit=0`。

---

## 实施说明

### 负向编译验证

设计文档 7.2 的编译期校验需要在实现后验证它真的会拦下错误声明。
在 `tests/test_scheduler.cpp` 末尾临时追加以下内容，**确认编译失败后删除**：

```cpp
struct DuplicateWrite
    : gamev2::ecs::System
{
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<Position, Position>;

    void execute(World&, CommandBuffer&) override {}
};

TEST(temporary_negative_case)
{
    static_assert(not gamev2::ecs::write_set_valid<DuplicateWrite>);
    static_assert(gamev2::ecs::write_set_of<DuplicateWrite>().empty());
}
```

Expected: 编译失败，`static_assert(not ...)` 不成立 —— 因为
`Position` 重复出现，`all_distinct` 为 false。验证后删除该段。

### 测试数量

每个任务的「构建并运行」步骤给出的期望数量是累计值。若实际数量不同，
以「0 failed 且退出码为 0」为准。

### 提交

按项目约定不自动执行 `git commit`。每个任务完成后由用户决定是否提交。

# ECS 模块设计文档

- 日期：2026-10-12
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23

## 1. 背景与目标

为 gamev2 提供实体-组件-系统（ECS）底层运行逻辑，承载游戏客户端的逻辑层对象。

规模定位：单场景几万级实体。

本模块需要自带 System 调度器，支持多线程并行执行。

### 1.1 设计目标

- 组件数据在遍历时保持连续，支撑 cache 友好访问
- 读写集声明式声明，调度器自动拆分并行批次
- 结构性变更时机可控，多线程正确性有保证
- 零第三方依赖，仅用 C++23 标准库

### 1.2 非目标

本轮明确不做以下内容，避免范围蔓延：

- 序列化与存档
- 热重载与运行时组件类型注册
- 实体原型（prefab）与模板继承
- 实体间直接引用（一律通过 EntityId 间接访问）
- 网络同步与回滚
- 单 System 内并行时的向量化或 SIMD 优化

后续若需要存档，可在 `ComponentRegistry` 上叠加 `name → ComponentId`
的稳定映射，不改动存储层结构。详见 [11.2 组件 ID 不稳定](#112-组件-id-不稳定)。

## 2. 总体架构

```
                    ┌──────────────┐
                    │   Scheduler  │  冲突分批 + 并行执行
                    └──────┬───────┘
                           │ 调用
                    ┌──────▼───────┐
                    │    System    │  声明 ReadSet / WriteSet
                    └──────┬───────┘
                           │ 遍历 / 随机访问
                    ┌──────▼───────┐
                    │     View     │  编译期类型化视图
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │    World     │  实体生命周期 / 组件增删
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────▼─────┐ ┌────▼─────┐ ┌────▼──────┐
        │ Archetype │ │EntitySlot│ │ComponentId│
        │   /Chunk  │ │   表     │ │  + Registry│
        └───────────┘ └──────────┘ └───────────┘
```

### 2.1 文件划分

```
include/gamev2/ecs/
  entity.hpp           EntityId 句柄
  component.hpp        ComponentId + 类型注册
  diagnostic.hpp       诊断通道
  storage.hpp/.cpp     Archetype / Chunk / 迁移
  world.hpp/.cpp       实体生命周期 + 组件增删 + view 入口
  view.hpp             编译期类型化 view（全模板，无实现）
  system.hpp           System 基类 + 读写集派生
  scheduler.hpp/.cpp   冲突分批 + 并行执行
  command_buffer.hpp   延迟结构变更
src/ecs/*.cpp
tests/
  test_framework.hpp   极简断言 + 静态注册
doc/
CMakeLists.txt
```

`entity.hpp`、`component.hpp`、`diagnostic.hpp`、`view.hpp`、`system.hpp`、
`command_buffer.hpp` 为纯头文件；`storage`、`world`、`scheduler` 有对应
`.cpp`。

## 3. 核心类型

### 3.1 EntityId

```cpp
/// 实体句柄。下 32 位为存储索引，上 32 位为版本号。
/// 销毁实体时递增版本号，使旧句柄自然失效，避免悬垂引用。
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

    /// 允许在条件表达式中直接用作布尔值。
    constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }
};

template<>
struct std::hash<EntityId>
{
    [[nodiscard]] std::size_t operator()(EntityId e) const noexcept
    {
        return std::hash<std::uint64_t>{}(e.value);
    }
};
```

**为何 64 位而非 32 位**：32 位需在索引与版本号之间取舍。版本号回绕后，
旧句柄会误命中被复用的槽位，表现为极难定位的数据串扰。多出的 4 字节
换取整类缺陷的消除，在几万实体规模下内存开销可忽略。

**`value == 0` 为非法值**：默认构造的 `EntityId{}` 天然无效，无需额外的
「是否已初始化」状态。

由此推出**版本号从 1 起始**：索引 0 的实体若版本号为 0，则
`EntityId::make(0, 0).value == 0`，会与非法哨兵撞值。`EntitySlot::version`
的初值为 1，每次销毁该槽位时递增。

### 3.2 ComponentId

```cpp
/// 组件类型的进程内唯一标识。
struct ComponentId
{
    std::uint32_t value = 0;
};
```

32 位上限约 42 亿种组件，实际用量在数十至数百之间。宽度选择为后续扩展
元数据预留空间，同时便于将多个 ID 打包进 64 位 hash key。

### 3.3 类型注册

```cpp
/// 组件类型的进程内注册表，由 component_id_of 隐式填充。
class ComponentRegistry
{
public:
    template<typename T>
    ComponentId register_type();

    [[nodiscard]] std::span<const ComponentTypeInfo> types() const noexcept;
};

/// 全局注册表。进程内唯一。
[[nodiscard]] ComponentRegistry& global_component_registry();

/// 获取组件类型的唯一 ID，并在首次调用时完成运行时类型信息注册。
/// 组件作者无需编写任何注册代码。
template<typename T>
[[nodiscard]] ComponentId component_id_of() noexcept
{
    static const ComponentId id =
        global_component_registry().register_type<T>();
    return id;
}
```

函数内 `static` 保证只初始化一次，且初始化本身线程安全（C++ 起 magic
static 语义）。

### 3.4 组件契约

存储层需要在不知道具体类型 `T` 的情况下析构组件，因此每个组件必须
满足：

```cpp
static_assert(std::is_default_constructible_v<T>,
              "组件必须可默认构造");
static_assert(std::is_nothrow_move_constructible_v<T>,
              "组件必须支持 noexcept 移动构造");
```

注意契约是**移动构造**而非移动赋值：存储层只提供构造原语（见 4.6），
迁移时把源对象移动构造进目标的 RAW 存储，从不对已存在的对象做赋值。

该约束换来：加组件时 `std::construct_at` 原地构造，迁移时移动构造，
删组件时通过函数指针调用析构。无需为每种组件生成构造或移动代码。

## 4. 存储层

### 4.1 运行时类型信息

```cpp
struct ComponentTypeInfo
{
    ComponentId id;
    std::size_t size;
    std::size_t alignment;
    const char* name;
    void (*destroy)(void*);
    /// 移动构造入口。dst 必须是 RAW 存储（见 4.6）。
    void (*move_construct)(void* dst, void* src);
};
```

`component_id_of<T>()` 的首次调用连带注册该表。

### 4.2 实体定位

```cpp
/// archetype 索引，上界为同时存在的不同组件组合数，实际不超过数十。
using ArchetypeId = std::uint32_t;

inline constexpr ArchetypeId kInvalidArchetype =
    std::numeric_limits<ArchetypeId>::max();

struct EntitySlot
{
    ArchetypeId archetype = kInvalidArchetype;
    std::uint32_t version = 0;
    std::uint32_t chunk = 0;
    std::uint32_t row = 0;
};

std::vector<EntitySlot> slots_;
```

以 `EntityId` 的索引部分访问该表即可定位存储位置。

**取舍**：每次访问组件需两次间接寻址。换来 O(1) 随机访问，System 中可按
实体 id 直接取组件，无需顺序遍历。

### 4.3 Archetype 键

组件 ID 排序后作为键：

```cpp
using ArchetypeKey = std::vector<ComponentId>;  ///< 已排序去重

std::unordered_map<ArchetypeKey, ArchetypeId, KeyHash> archetype_index_;
```

`KeyHash` 使用 FNV-1a 遍历各 ID。查询匹配即对目标组件集合排序后在表中查找。

### 4.4 Chunk 容量

```cpp
/// 单 chunk 目标字节数，取页大小以让并行的每个 chunk
/// 尽量落在独立缓存行组上，降低 false sharing。
inline constexpr std::size_t kChunkTargetBytes = 4096;

/// 按该 archetype 中最大的组件尺寸计算每 chunk 可容纳的实体数。
[[nodiscard]] std::size_t
compute_chunk_capacity(std::span<const ComponentId> ids);
```

实现为 `kChunkTargetBytes / max(sizeof(T)...)`，下限为 1。

### 4.5 类型擦除的组件列

```cpp
/// 一个 archetype 内某种组件的存储。每个 chunk 一段定长数据，行偏移固定。
class Column
{
public:
    Column(const ComponentTypeInfo& info, std::size_t chunk_capacity);

    [[nodiscard]] std::byte* chunk_data(std::uint32_t chunk) noexcept;
    [[nodiscard]] std::byte* at(std::uint32_t chunk,
                                std::uint32_t row) noexcept;

    /// 取类型化指针，内部校验列类型，类型不符说明调用方取错了 T。
    template<typename T>
    [[nodiscard]] T* at(std::uint32_t chunk, std::uint32_t row) noexcept;
};
```

每个 archetype 为每种组件持有一个 `Column`，内部按 chunk 分段存储。

### 4.6 行的生命周期

一个 archetype 中的行有且仅有两种状态：

- **RAW** —— 字节已分配，组件尚未构造
- **LIVE** —— 该行的每个组件都已构造完成

合法转换只有两条，且**不得跨越**：

```
RAW ──构造──> LIVE ──析构──> RAW
```

因此存储层提供的是**构造**原语而非赋值原语。把 `T` 移动赋值进一块从未构造的
存储是未定义行为，哪怕字节层面「看起来对」也不行。

关键约束：`Archetype::push_back` 只分配 RAW 行，**不构造任何组件**。由调用
方负责在暴露该行之前把全部组件构造完毕。

### 4.7 迁移算法

以「加组件」为例：

```
1. 目标 archetype B = 当前组件集 ∪ {新组件}，排序后查表，无则创建
2. B.push_back() → 取尾部 RAW 行 (b_chunk, b_row)
3. 对每个共有组件 c：把 A 中的 LIVE 对象移动构造到 B 的 RAW 行
4. 新组件 T：construct_at(B[T][b_chunk][b_row], args...)
   此时 B 的该行已转为 LIVE
5. 析构 A 中该实体的全部组件 —— (a_chunk, a_row) 转为 RAW
6. fill_hole(A, a_chunk, a_row)：把 A 最后一个实体移动构造进这个 RAW 空洞，
   随即析构它在原位的组件并 pop 掉原行；返回被填补实体的句柄
7. 更新本实体的 slot 指向 (B, b_chunk, b_row)
```

第 6 步之所以能安全地把尾部实体移进空洞，正是因为第 5 步已经把空洞变回
RAW 状态 —— 对 RAW 存储做移动构造是合法的。

删除组件算法对称：跳过第 4 步。

**为什么不用「交换」**：交换需要对两个都已构造的对象做赋值或交换，而空洞
在一开始根本不是对象。拆成「先析构变 RAW，再移动构造填入」两步，两个操作
各自的对象状态都是明确的。

代价是行索引不稳定：填洞会改变其他实体的所在位置。因此结构性变更只能发生
在帧末单线程 flush 阶段，这正是 Command Buffer 方案的前提。

除最后一个 chunk 外所有 chunk 永远满，这样 System 做实体级并行切片时，
每个 worker 拿到的都是连续区间，无需处理空洞与边界。

## 5. 查询层

```cpp
template<typename... Ts>
class View
{
public:
    /// 单线程遍历所有匹配实体。
    template<typename F>
    void each(F&& f);

    /// 按 chunk 切分提交线程池并行遍历。
    /// 当前线程已在执行 System 时退化为串行，见 7.3。
    template<typename F>
    void each_parallel(F&& f);
};
```

匹配条件：实体所在 archetype 的组件集合包含 `Ts` 的全部组件。

`const` 修饰的模板参数表示只读访问。该信息是 6 节读写集派生的依据。

## 6. System

```cpp
class System
{
public:
    virtual ~System() = default;

    /// 结构性变更必须走 cmd，不得直接改动 World。
    virtual void execute(World& world, CommandBuffer& cmd) = 0;
};
```

System 通过两个类型别名声明其访问签名，**读写集由此派生，不再单独声明**，
因此不存在声明与实际访问漂移的可能：

```cpp
class MoveSystem final : public System
{
public:
    /// 只读访问的组件。带 const 的表示本 System 不修改它。
    using ReadSet  = std::tuple<Transform, const Velocity>;
    /// 写访问的组件。均视为可修改。
    using WriteSet = std::tuple<Position>;

    void execute(World& world, CommandBuffer& cmd) override
    {
        world.view<Transform, const Velocity, Position>()
            .each([](Transform& t, const Velocity& v, Position& p)
                  {
                      p.value = t.value + v.value;
                  });
    }
};
```

派生规则：

- **写集** = 两个别名中所有非 `const` 组件的并集，此处为 `{Transform,
  Position}`
- **读集** = 所有 `const` 组件，加上写集本身（修改前必先读取），此处为
  `{Velocity, Transform, Position}`

## 7. 调度器

### 7.1 冲突分批

下文的 `s.reads` 与 `s.writes` 为 6 节派生的读写集，以 `std::vector<ComponentId>`
表示。

贪心分层算法：

```
剩余 ← 所有 System
while 剩余非空:
    本批 ← [];  已写 ← ∅;  已读 ← ∅
    for s in 剩余:
        若 s.writes ∩ (已写 ∪ 已读) == ∅ 且 s.reads ∩ 已写 == ∅:
            本批.push(s)
            已写 ∪= s.writes
            已读 ∪= s.reads
    若 本批为空 → 内部错误（正常声明下不可能发生）
    批次.push(本批)
    剩余 -= 本批
```

批内 System 两两无读写冲突，可并发执行；批间设 barrier。

**语义保证**：后批的 System 一定读到前批写入的新值。

分批结果仅在 System 集合变化时重算并缓存，每帧零开销。

### 7.2 读写集编译期校验

校验对象是 6 节声明的 `ReadSet` 与 `WriteSet` 类型，而非 `execute()` 函数体
中的 lambda。

用两个 concept 完成：

- `read_set_valid<S>`：确保 `ReadSet` 中每个组件类型都不是 `const`
  限定的退化形式以外的非平凡类型，且元素均为非引用、非数组类型
- `write_set_valid<S>`：确保 `WriteSet` 中元素类型唯一，无重复

校验在 `Scheduler::add` 实例化时通过 `static_assert` 触发，错误信息直接
指出冲突的 System 类型名。

**理由**：读写集错误的后果是静默数据竞争。在 System 声明处做编译期
约束，挡住的是「把会修改的组件写进了 ReadSet」这类常见失误。

**已知盲区**：校验只覆盖声明本身。若 `execute()` 中通过 `const_cast`、
裸指针或 `get_if` 访问未声明的组件，本校验无法拦截，运行时也不会告警。
详见 [11.1](#111-读写集声明错误不被运行时检测)。

### 7.3 并行执行与降级规则

批内 System 并行执行时，若各 System 内部再提交 chunk 任务到线程池，会
导致死锁：worker 全部阻塞等待池中排队中的自身任务。

因此设定降级规则：

> 当一批 System 正在**线程池的 worker 上并发执行**时，标志置位，
> 各 System 内部的 `each_parallel` 退化为串行遍历；否则正常提交线程池。

标志是「本批是否在进行中」的批级判据，不是「当前线程是否在 System 内」的
线程级判据。这两者在本设计中恰好等价：只有多 System 批次才会把 System 派到
worker 上，而单 System 批次就在调用线程上执行，不存在 worker 互相等待的窗口。

实际效果：

| 批次构成 | 批内 System 并行 | 单 System 内 chunk 并行 |
|---|---|---|
| 仅 1 个 System | 否 | **是** |
| 多个 System | 是 | 否（降级） |

`each_parallel` 按 chunk 切分任务，粒度天然为一页 4KB，无需额外调参。

**单 System 批次的内并行是本模块的核心收益之一**：它正是当初选择 Archetype
分块存储而非每组件独立稀疏存储的原因（见 [11.3](#113-批内多-system-时内部并行降级)）。
任何让单 System 批次也降级的改动都属于回退，必须由设计所有者显式确认。

## 8. Command Buffer

```cpp
class CommandBuffer
{
public:
    void destroy(EntityId e);

    template<typename T, typename... Args>
    void add(EntityId e, Args&&... args);

    template<typename T>
    void remove(EntityId e);

    /// 帧末主线程单线程执行。
    void flush(World& world);
};
```

内部为 `std::variant` 命令队列。

**顺序语义**：命令按入队顺序执行。`flush` 期间命中已失效实体的命令
被拒绝并记录诊断。例如同一帧内先 `destroy(e)` 再 `add(e, T)`，`add`
会被拒绝并记录 `ecs.entity_invalid`。

## 9. 错误处理与诊断

### 9.1 错误分类

```cpp
enum class EcsError
{
    entity_invalid,      ///< 句柄无效或版本号不匹配
    component_missing,   ///< 实体没有该组件
    component_exists,    ///< 组件已存在，重复添加
};
```

### 9.2 分层原则

热路径不做错误检查，低频结构变更路径返回 `std::expected`。

```cpp
class World
{
public:
    [[nodiscard]] EntityId create();

    /// 重复销毁静默忽略：游戏逻辑中 destroy 被多次调用是常态。
    void destroy(EntityId e);

    template<typename T, typename... Args>
    std::expected<void, EcsError> add(EntityId e, Args&&... args);

    template<typename T>
    std::expected<void, EcsError> remove(EntityId e);

    /// 实体失效或组件缺失时返回 nullptr。热路径，无错误检查。
    template<typename T>
    [[nodiscard]] T* get_if(EntityId e) noexcept;

    template<typename T>
    [[nodiscard]] bool has(EntityId e) const noexcept;

    template<typename... Ts>
    [[nodiscard]] auto view();

    [[nodiscard]] std::size_t entity_count() const noexcept;
};
```

结构变更每帧仅数次，适合返回 `std::expected`；`get_if` 每帧调用数万次，
返回裸指针，不抛异常、不检查错误。

CommandBuffer 命令不返回错误，在 `flush` 阶段统一转为诊断。

### 9.3 诊断通道

```cpp
enum class DiagnosticLevel
{
    warning,
    error,
};

struct Diagnostic
{
    DiagnosticLevel level;
    std::string_view code;   ///< 稳定标识，如 "ecs.entity_invalid"
    std::string message;
};

using DiagnosticHandler = std::function<void(const Diagnostic&)>;
```

默认处理器输出到 stderr，运行时可替换。

## 10. 测试策略

零依赖，`tests/test_framework.hpp` 实现静态注册加断言宏的极简框架。

每条测试对应一个可验证目标：

| # | 目标 | 验证方式 |
|---|---|---|
| 1 | 迁移不丢组件值 | 随机增删组件后逐字段比对 |
| 2 | 槽位复用后旧句柄失效 | 用旧 `EntityId` 访问返回 nullptr |
| 3 | view 只命中含全部组件的实体 | 混合 archetype，计数核对 |
| 4 | 冲突分批正确 | 构造读写冲突 System，断言批次划分 |
| 5 | flush 顺序语义 | destroy 后 add 被拒绝并记诊断 |
| 6 | 并行结果等于串行结果 | 万级实体 `each_parallel` 逐项比对 |
| 7 | 读写集编译期校验 | 负向用例，断言编译失败 |

第 6 条为关键项：并行正确性不能依赖人工审查，必须与串行结果逐项对拍。

## 11. 已知限制与风险

### 11.1 读写集声明错误不被运行时检测

7.2 的编译期校验只作用于 System 声明的 `ReadSet` / `WriteSet` 本身。若
`execute()` 中通过 `const_cast`、裸指针或 `get_if` 访问未声明的组件，
校验无法拦截，运行时也不会有任何告警。

**缓解**：将读写集声明纳入 code review 约定；禁止在 `execute()` 中使用
`const_cast`。

### 11.2 组件 ID 不稳定

`ComponentId` 按首次调用顺序分配。测试之间、进程重启后具体数值不同，
只能依赖 `component_id_of<T>()` 的返回值。

**影响**：本轮不做序列化，无实际影响。若后续接入存档，需在
`ComponentRegistry` 增加 `name → ComponentId` 稳定映射，存储层不受影响。

### 11.3 批内多 System 时内部并行降级

见 7.3。若某重 System 经常与其他 System 同批，它的内并行不会生效。
当前的应对是让该 System 在读写集上与其他 System 产生冲突，从而强制单独成批 ——
单独成批时它独占调用线程，内并行照常开启。代价是牺牲一定的跨 System 并行度，
在当前规模下可接受。

### 11.4 组件类型的生命周期要求

存储层提供构造原语而非赋值原语（见 4.6），因此组件可以是持有资源的类型，
析构由存储层经 `ComponentTypeInfo::destroy` 驱动。

代价是每次实体迁移都要真正构造与析构对象，比字节搬运贵。对于客户端逻辑层
的几万实体规模，这个开销可接受。

**为什么值得**：早期设计让 `Archetype::push_back` 只分配字节、由迁移路径做
移动**赋值**。对平凡可复制类型恰好正确，但一旦组件类型写了自己的移动赋值
（哪怕只是先释放旧资源），就是未定义行为。而 API 表面（`destroy_row`、
`ComponentTypeInfo::destroy`）又在主动邀请这类组件 —— 接口承诺了它做不到的
事情，比明确不支持更糟。

2026-10-13 决策：补齐语义，保留对持有资源组件的支持。

## 12. 构建

已验证工具链：CMake 4.4.2 + Unix Makefiles + clang++ 23.1.1 +
MSVC STL 14.51.36231 + Windows SDK 10.0.28000.0。

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

注意事项：

- MinGW g++ 8.1.0 不支持 C++23，不可作为编译器。
- 已确认 `std::expected`、`std::print` / `std::println`、
  `std::ranges::sort`、`std::views::transform` 均可用。
- `std::views::sort` 在该 STL 上不可用，需改用 `std::ranges::sort`。

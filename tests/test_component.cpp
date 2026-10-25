#include "test_framework.hpp"

#include "gamev2/ecs/component.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <cstddef>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

/// 局部类不允许静态数据成员，故计数器挂在命名空间作用域。
struct Counted
{
    static inline int destructions = 0;
    int value = 0;

    Counted& operator=(const Counted&) = default;
    ~Counted() { ++destructions; }
};

/// 移动构造函数自增 moves：存储层搬运行必须真的构造出对象，字节搬运
/// 不会调用任何构造函数。移动赋值一并留着，好让「实现退化成赋值」
/// 这种错误仍能编译，从而被断言抓住而不是变成编译错误。
struct Movable
{
    static inline int moves = 0;
    int value = 0;

    Movable() = default;
    explicit Movable(int v) : value{v} {}
    Movable(const Movable&) = default;
    Movable(Movable&&) noexcept : value{42} { ++moves; }
    ~Movable() = default;

    Movable& operator=(const Movable&) = default;
    Movable& operator=(Movable&&) = default;
};

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
    const auto* info = find_info(component_id_of<Counted>());
    CHECK(info != nullptr);
    if (info != nullptr)
    {
        Counted obj{7};
        info->destroy(&obj);
        CHECK_EQ(Counted::destructions, 1);
    }
}

TEST(component_move_construct_entry_transfers_value)
{
    const auto* info = find_info(component_id_of<Movable>());
    CHECK(info != nullptr);
    if (info != nullptr)
    {
        Movable src{1};
        // 目标必须是从未构造的存储：入口走的是移动构造，对已构造的
        // 对象赋值是另一回事，也不符合行生命周期的约定。
        alignas(Movable) std::byte raw[sizeof(Movable)];
        info->move_construct(raw, &src);
        const auto* dst = std::launder(
            reinterpret_cast<const Movable*>(raw));
        CHECK_EQ(Movable::moves, 1);
        CHECK_EQ(dst->value, 42);
    }
}

namespace {

/// 每个线程注册自己独有的一族类型。若几个线程共用同一批类型，函数内 static
/// 会把它们串行化（谁先到谁注册，其余等 guard），真正的并发窗口反而被测试
/// 自己堵死——必须让所有首注册两两不同，才是真的并发追加。
template<std::size_t Thread, std::size_t N>
struct Concurrent
{
    float v = static_cast<float>(N);
};

constexpr std::size_t kPerThread = 64;
constexpr int kThreads = 8;

/// 花括号初始化保证从左到右求值，out 因此按 N 的升序拿到 id。
template<std::size_t Thread, std::size_t... N>
void register_all(std::vector<ComponentId>& out, std::index_sequence<N...>)
{
    (void)std::initializer_list<int>{
        (out.push_back(component_id_of<Concurrent<Thread, N>>()), 0)...};
}

/// 起跑门：没有它，线程按创建顺序错峰进入注册，临界区几乎撞不上。
/// 线程下标必须是编译期量才能选出一族互不相同的类型，因此在这里展开。
template<std::size_t... T>
void spawn_threads(std::array<std::thread, kThreads>& threads,
                   std::array<std::vector<ComponentId>, kThreads>& per_thread,
                   std::barrier<>& gate, std::index_sequence<T...>)
{
    ((threads[T] = std::thread([&per_thread, &gate] {
         gate.arrive_and_wait();
         register_all<T>(per_thread[T], std::make_index_sequence<kPerThread>{});
     })),
     ...);
}

}  // namespace

TEST(registry_survives_concurrent_first_registration)
{
    // 同批并行的两个 system 各自首次用到一批新组件类型，就是两个 worker
    // 同时向 infos_ 追加。没有互斥时这些 push_back 会写坏同一个 vector，
    // 而症状是随机的：ID 重复、条目丢失，或干脆堆损坏。
    //
    // 断言的是可观测的契约。**这不是竞争的可执行证明**：去掉互斥后本测试在
    // x64 Debug 下反复通过（4096 次并发追加也抓不到），因为撞坏 vector 需要
    // 恰好那次交错。确定性地证明需要 TSAN，而本仓库只有 ASAN 选项
    // （见 CMakeLists.txt）。加锁的理由是构造性的：两个线程 append 同一个
    // std::vector 本就是未定义行为，与能否在此复现无关。
    std::barrier gate(static_cast<std::ptrdiff_t>(kThreads));
    std::array<std::vector<ComponentId>, kThreads> per_thread;
    std::array<std::thread, kThreads> threads{};

    spawn_threads(threads, per_thread, gate,
                  std::make_index_sequence<kThreads>{});
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::vector<ComponentId> ids;
    for (const auto& bucket : per_thread)
    {
        CHECK_EQ(bucket.size(), kPerThread);
        ids.insert(ids.end(), bucket.begin(), bucket.end());
    }
    CHECK_EQ(ids.size(), kPerThread * kThreads);

    for (const auto id : ids)
    {
        CHECK(id.value != 0);
        CHECK(find_info(id) != nullptr);
    }

    // 全部 id 必须两两不同：计数器与存储顺序错开时这里立刻炸。
    auto sorted = ids;
    std::ranges::sort(sorted, {}, &ComponentId::value);
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());

    // 每种类型在注册表里恰好一条记录：少一条是并发追加丢了条目，多一条是
    // 同一类型被写进两行。按 id 精确匹配，不能只比 size——别的测试注册的
    // 4 字节组件会和 Concurrent<T, N> 撞上。
    std::size_t records = 0;
    for (const auto& info : global_component_registry().types())
    {
        if (std::ranges::find(ids, info.id) != ids.end())
        {
            ++records;
        }
    }
    CHECK_EQ(records, kPerThread * kThreads);
}

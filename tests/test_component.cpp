#include "test_framework.hpp"

#include "gamev2/ecs/component.hpp"

#include <cstddef>
#include <new>
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

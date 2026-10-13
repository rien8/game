#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// operator new 对基本对齐类型实际保证的对齐。chunk 缓冲只经由它分配，
/// 因此组件对齐不得超过它；放宽需要对齐分配器，本模块刻意不引入。
inline constexpr std::size_t kColumnAllocationAlignment =
    alignof(std::max_align_t);

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
    /// 类型擦除的移动构造入口，供 archetype 批量搬运使用。dst 必须是
    /// RAW 存储：行只有 RAW 与 LIVE 两种状态，合法转换只有构造与析构
    /// 两条，见设计文档 4.6。
    void (*move_construct)(void* dst, void* src);
};

namespace detail {

template<typename T>
void destroy_component(void* p)
{
    static_cast<T*>(p)->~T();
}

template<typename T>
void move_construct_component(void* dst, void* src)
{
    std::construct_at(static_cast<T*>(dst), std::move(*static_cast<T*>(src)));
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
        void (*destroy)(void*), void (*move_construct)(void* dst, void* src));

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
    // 存储层只用移动构造：行迁移是「移动构造进 RAW 行」，没有赋值原语，
    // 因此要求落在移动构造上而不是移动赋值。
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "组件必须支持 noexcept 移动构造");
    static_assert(alignof(T) <= kColumnAllocationAlignment,
                  "组件对齐要求超出列存储的分配对齐能力");

    static const ComponentId id =
        global_component_registry().register_type_info(
            sizeof(T), alignof(T), typeid(T).name(),
            &detail::destroy_component<T>,
            &detail::move_construct_component<T>);
    return id;
}

/// 取组件类型 ID。首次调用时完成注册，组件作者无需写注册代码。
template<typename T>
[[nodiscard]] ComponentId component_id_of() noexcept
{
    return register_component<T>();
}

}  // namespace gamev2::ecs

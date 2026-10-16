/// @file system.hpp
/// @brief System 基类与访问集声明：System 用 ReadSet / WriteSet 两个 std::tuple
///        自描述它读哪些组件、可写哪些组件，Scheduler 据此做冲突检测与并行
///        分层，本模块不另设手写的依赖描述。
///
/// 类型上的 const 即访问权限声明：ReadSet 里的 const 元素只读、不计入写集，
/// 「读」与「可写」因此只需在一处声明。WriteSet 内部元素必须互不相同，否则
/// 写集定义失去意义，由 static_assert 在编译期拒绝。
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

/// 系统的抽象基类。execute 每帧被调度器调用一次，具体系统通过派生类覆写
/// 读写集并实现 execute。
///
/// 结构性变更（增删实体、增删组件）不得在此期间直接改动 World，必须经 cmd
/// 排队到帧末统一 flush，理由见 execute 的说明。
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

template<typename Tuple, std::size_t I>
void push_mutable_id(std::vector<ComponentId>& ids)
{
    using E = std::tuple_element_t<I, Tuple>;
    // const 出现在访问集里就是只读声明，不进写集。
    if constexpr (!std::is_const_v<E>)
    {
        ids.push_back(component_id_of<std::remove_const_t<E>>());
    }
}

template<typename Tuple, std::size_t... I>
void push_mutable_ids(std::vector<ComponentId>& ids, std::index_sequence<I...>)
{
    (push_mutable_id<Tuple, I>(ids), ...);
}

/// 去重前必须先排序，而 ComponentId 只定义了 operator==：
/// 排序只能投影到 value 字段，不能假定存在 operator<。
inline void normalize(std::vector<ComponentId>& ids)
{
    std::ranges::sort(ids, {}, &ComponentId::value);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
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
    // 索引必须走编译期序列：std::tuple_element_t 的模板实参要求常量表达式，
    // 运行期循环变量传不进去。
    detail::push_mutable_ids<typename S::ReadSet>(
        ids, std::make_index_sequence<
                  std::tuple_size_v<typename S::ReadSet>>{});
    detail::normalize(ids);
    return ids;
}

/// 读集 = 所有被访问的组件。写集天然也在读，修改前必先读取。
template<typename S>
[[nodiscard]] std::vector<ComponentId> read_set_of()
{
    auto ids = detail::to_component_ids<typename S::ReadSet>();
    const auto w = write_set_of<S>();
    ids.insert(ids.end(), w.begin(), w.end());
    detail::normalize(ids);
    return ids;
}

}  // namespace gamev2::ecs

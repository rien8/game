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

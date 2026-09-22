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

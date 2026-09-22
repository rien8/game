// include/core/ecs/registry.hpp
#pragma once

#include "core/ecs/entity.hpp"
#include "core/ecs/sparse_set.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>

namespace game::ecs {

template<Component... Ts>
class View {
public:
    explicit View(std::tuple<SparseSet<Ts>*...> sets) : sets_(sets) {}

    // 迭代器：选最小集合做 drive，逐项检查其他集合
    class iterator {
    public:
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;

        iterator(const View* view, std::size_t pos, std::span<const Entity> drive)
            : view_(view), pos_(pos), drive_(drive) {
            skip_invalid();
        }

        auto operator*() const -> Entity { return drive_[pos_]; }

        auto operator++() -> iterator& {
            ++pos_;
            skip_invalid();
            return *this;
        }

        [[nodiscard]] auto operator==(const iterator& o) const -> bool {
            return pos_ == o.pos_;
        }

        [[nodiscard]] auto operator!=(const iterator& o) const -> bool {
            return pos_ != o.pos_;
        }

    private:
        template<std::size_t... Is>
        auto has_all_impl(std::index_sequence<Is...>) const -> bool {
            Entity e = drive_[pos_];
            return (std::get<Is>(view_->sets_)->contains(e) && ...);
        }

        auto has_all() const -> bool {
            if (pos_ >= drive_.size()) return true;
            return has_all_impl(std::index_sequence_for<Ts...>{});
        }

        auto skip_invalid() -> void {
            while (pos_ < drive_.size() && !has_all()) {
                ++pos_;
            }
        }

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

    template<Component... Ts>
    [[nodiscard]] auto view() -> View<Ts...> {
        return View<Ts...>{std::tuple<SparseSet<Ts>*...>{&storage_<Ts>()...}};
    }

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

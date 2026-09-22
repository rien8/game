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

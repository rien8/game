// src/core/ecs/registry.cpp
#include "core/ecs/registry.hpp"

#include <algorithm>
#include <utility>

namespace game::ecs {

auto Registry::create() -> Entity {
    // 找一个 dead slot 或追加
    std::uint32_t idx = next_index_;
    if (std::find(alive_.begin(), alive_.end(), false) != alive_.end()) {
        const auto it = std::find(alive_.begin(), alive_.end(), false);
        idx = static_cast<std::uint32_t>(std::distance(alive_.begin(), it));
    } else {
        idx = next_index_++;
    }
    if (idx >= alive_.size()) {
        alive_.resize(idx + 1, false);
        generations_.resize(idx + 1, 0);
    }
    alive_[idx] = true;
    return Entity{idx, generations_[idx]};
}

auto Registry::destroy(Entity e) -> void {
    if (e.index >= alive_.size()) return;
    if (!alive_[e.index]) return;
    if (generations_[e.index] != e.generation) return;  // 旧 handle
    alive_[e.index] = false;
    ++generations_[e.index];
}

auto Registry::alive(Entity e) const -> bool {
    if (e.index >= alive_.size()) return false;
    return alive_[e.index] && generations_[e.index] == e.generation;
}

auto Registry::reset() -> void {
    for (std::uint32_t i = 0; i < alive_.size(); ++i) {
        alive_[i] = false;
    }
    for (auto& g : generations_) ++g;  // 让所有旧 handle 失效
}

}  // namespace game::ecs

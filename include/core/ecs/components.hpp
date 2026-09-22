// include/core/ecs/components.hpp
#pragma once

#include "core/ecs/concepts.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace game::ecs {

struct Position {
    using is_component = void;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct Traits {
    using is_component = void;
    std::array<float, 4> values{};   // 4 维保持；Spec B 改 8
};

struct Vitals {
    using is_component = void;
    float         hunger = 1.0f;
    float         energy = 1.0f;
    std::uint32_t age    = 0;
    bool          dead   = false;
};

struct Identity {
    using is_component = void;
    std::uint64_t      id        = 0;   // 全局单调
    bool               is_elite  = false;
    bool               is_boss   = false;
    std::uint32_t      elite_age = 0;
};

struct Name {
    using is_component = void;
    std::string value;

    auto set(std::string_view s) -> void { value.assign(s); }
    [[nodiscard]] auto view() const noexcept -> std::string_view { return value; }
};

struct Reproduction {
    using is_component = void;
    float fitness = 0.0f;
};

}  // namespace game::ecs

#pragma once

#include <cstdint>
#include <utility>

namespace game {

// 二维坐标（无符号）。用于 Creature 在地图上的位置。
struct Position {
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    constexpr Position() noexcept = default;
    constexpr Position(std::uint32_t x_, std::uint32_t y_) noexcept : x{x_}, y{y_} {}

    [[nodiscard]] constexpr auto operator==(const Position&) const noexcept -> bool = default;
};

}  // namespace game

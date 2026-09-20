#pragma once

#include <cstddef>

namespace game {

// 二维坐标（无符号）。用于 Creature 在地图上的位置。
struct Position {
    std::size_t x = 0;
    std::size_t y = 0;

    constexpr Position() noexcept = default;
    constexpr Position(std::size_t x_, std::size_t y_) noexcept : x{x_}, y{y_} {}

    [[nodiscard]] constexpr auto operator==(const Position&) const noexcept -> bool = default;
};

}  // namespace game

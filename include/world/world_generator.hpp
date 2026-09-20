#pragma once

#include "world/world.hpp"

#include <cstddef>
#include <cstdint>

namespace game {

// 用 seed + 噪声生成随机世界（地形 / 气候 / 生物群系）。
// 满足 pcg::Generator<World> 概念。
class WorldGenerator {
public:
    WorldGenerator(std::size_t width, std::size_t height);

    [[nodiscard]] auto generate(std::uint64_t seed) const -> World;

private:
    std::size_t width_;
    std::size_t height_;
};

}  // namespace game

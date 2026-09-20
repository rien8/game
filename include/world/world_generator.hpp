#pragma once

#include "core/creature.hpp"
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

// 在陆地上生成初始生物。
auto populate_initial_creatures(const World& world,
                               std::size_t count,
                               std::uint64_t seed) -> std::vector<Creature>;

}  // namespace game

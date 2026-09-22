#pragma once

#include "core/creature.hpp"
#include "core/ecs/registry.hpp"
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

// 在陆地上生成初始生物，直接进 Registry。
// 返回实际创建的 entity 数（land_tiles 不足时可能 < count）。
auto populate_initial_creatures(ecs::Registry& registry,
                                const World& world,
                                std::size_t count,
                                std::uint64_t seed,
                                std::uint64_t& next_id) -> std::size_t;

}  // namespace game

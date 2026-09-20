#include "world/world.hpp"

#include <cstddef>
#include <vector>

namespace game {

World::World(std::size_t width, std::size_t height)
    : width_(width), height_(height), tiles_(width * height) {}

auto World::at(std::size_t x, std::size_t y) -> Tile& {
    return tiles_[y * width_ + x];
}

auto World::at(std::size_t x, std::size_t y) const -> const Tile& {
    return tiles_[y * width_ + x];
}

}  // namespace game

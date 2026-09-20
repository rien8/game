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

auto World::regrow_biomass() -> void {
    for (auto& tile : tiles_) {
        if (tile.biomass_regrowth <= 0.0f) continue;
        const float diff = tile.biomass_target - tile.biomass;
        tile.biomass += diff * tile.biomass_regrowth;
        if (tile.biomass < 0.0f) tile.biomass = 0.0f;
        if (tile.biomass > 1.0f) tile.biomass = 1.0f;
    }
}

auto World::is_land(std::size_t x, std::size_t y) const -> bool {
    const Biome b = tiles_[y * width_ + x].biome;
    return b != Biome::DeepOcean && b != Biome::Ocean;
}

auto World::total_biomass() const -> float {
    float total = 0.0f;
    for (const auto& tile : tiles_) {
        total += tile.biomass;
    }
    return total;
}

}  // namespace game

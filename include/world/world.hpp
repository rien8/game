#pragma once

#include "world/biome.hpp"

#include <cstddef>
#include <vector>

namespace game {

// 单个地图格的数据。
struct Tile {
    float elevation = 0.0f;    // 海拔 [0, 1]
    float temperature = 0.0f;  // 温度 [0, 1]
    float moisture = 0.0f;     // 湿度 [0, 1]
    Biome biome = Biome::DeepOcean;
    float biomass = 0.0f;            // 当前食物量 [0, 1]
    float biomass_target = 0.0f;     // 该 biome 的"满载"基线（生成时由 WorldGenerator 写入）
    float biomass_regrowth = 0.0f;   // 每 tick 重生率（生成时写入）
};

// 二维 tile 网格，内部一维扁平存储。
class World {
public:
    World(std::size_t width, std::size_t height);

    [[nodiscard]] auto at(std::size_t x, std::size_t y) -> Tile&;
    [[nodiscard]] auto at(std::size_t x, std::size_t y) const -> const Tile&;

    [[nodiscard]] auto width() const noexcept -> std::size_t { return width_; }
    [[nodiscard]] auto height() const noexcept -> std::size_t { return height_; }

private:
    std::size_t width_;
    std::size_t height_;
    std::vector<Tile> tiles_;
};

}  // namespace game

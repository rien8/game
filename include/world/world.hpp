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

#pragma once

namespace game {

// 生物群系，由海拔 + 温度 + 湿度推导。
enum class Biome {
    DeepOcean,
    Ocean,
    Beach,
    Grassland,
    Forest,
    Rainforest,
    Desert,
    Swamp,
    Tundra,
    Mountain,
    Snowcap,
};

}  // namespace game

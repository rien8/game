/// @file world.hpp
/// @brief 世界容器。纯数据，不含行为：生成阶段以 World& 为唯一操作对象
///        就地改写它，读取方按坐标查询，World 从不反向引用实体。
#pragma once

#include "gamev2/world/enums.hpp"
#include "gamev2/world/grid.hpp"
#include "gamev2/world/params.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 全部生成阶段之间共享的唯一状态。
class World
{
public:
    explicit World(WorldParams params);

    [[nodiscard]] const WorldParams& params() const noexcept
    {
        return params_;
    }

    /// 总格深 = z_levels * z_cell_depth。
    [[nodiscard]] std::uint32_t depth() const noexcept;

    /// z 层号加层内格深，换成总格深坐标。
    [[nodiscard]] std::uint32_t cell_depth(std::uint32_t z,
                                           std::uint32_t d) const noexcept;

    /// 阶段 1 反解出的实际海平面。
    [[nodiscard]] std::uint8_t sea_level() const noexcept { return sea_level_; }

    [[nodiscard]] bool is_ocean(std::uint32_t x,
                                std::uint32_t y) const noexcept;

    /// 地表量，每 (x, y) 一个值。
    Grid<std::uint8_t>  elevation;
    Grid<std::uint8_t>  rainfall;
    Grid<std::uint8_t>  temperature;
    Grid<Biome>         biome;
    Grid<std::uint32_t> downstream;
    Grid<std::uint32_t> flow;
    Grid<std::uint8_t>  river_width;
    Grid<std::uint8_t>  lake_depth;
    Grid<std::uint8_t>  drainage;
    Grid<std::uint8_t>  soil_depth;
    Grid<std::uint8_t>  aquifer_top;

    /// 地下量，每 z 层一份同构网格。
    std::vector<Grid<Stone>>   stone;
    std::vector<Grid<Deposit>> deposit;
    std::vector<Grid<bool>>    cavern;
    std::vector<Grid<bool>>    aquifer;

private:
    friend void make_elevation(World&);

    WorldParams params_;
    std::uint8_t sea_level_ = 0;
};

}  // namespace gamev2::world

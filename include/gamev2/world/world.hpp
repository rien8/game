/// @file world.hpp
/// @brief 世界容器。纯数据，不含行为：生成阶段以 World& 为唯一操作对象
///        就地改写它，读取方按坐标查询，World 从不反向引用实体。
#pragma once

#include "gamev2/world/enums.hpp"
#include "gamev2/world/grid.hpp"
#include "gamev2/world/params.hpp"
#include "gamev2/world/spirit.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 全部生成阶段之间共享的唯一状态。
class World
{
private:
    friend void make_elevation(World&);

    /// 生成参数。声明放在所有网格之前：构造函数的初始化列表按声明序写，
    /// 参数因而先于网格就位，网格初始化若要读它不会读到未初始化的值。
    WorldParams params_;

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

    // ── 灵气层，地表量 ──
    Grid<std::uint8_t>  ley_strength;
    Grid<std::uint8_t>  ley_depth;
    Grid<Element>       ley_element;
    Grid<VeinRank>      vein_rank;
    Grid<std::uint32_t> vein_id;
    Grid<std::uint32_t> eye_flow;
    Grid<std::uint8_t>  domain_concentration;
    /// 灵域分等：地脉网络给出的**浓度**分档。
    ///
    /// 与 `land_type` 有意不一致——一个福地塌成死地之后，这里仍是 blessed。
    /// 「这里现在能不能住」看 `land_type`，「这里的灵气网络有多强」看这里。
    Grid<DomainGrade>   domain_grade;
    Grid<Scar>          scar_kind;
    Grid<std::uint8_t>  spirit_suppression;
    /// 最终地貌：浓度分等叠加历史与地脉属性之后的**结论**。
    /// 读取方要判断某格能不能住，看的是这张网格，不是 `domain_grade`。
    Grid<LandType>      land_type;
    Grid<std::uint8_t>  eye_purity;
    Grid<bool>          cave_heaven;
    /// 是否为渡劫场的劫心。阶段 18 的格级镜像。
    Grid<bool>          tribulation_site;

    /// 地下量，每 z 层一份同构网格。
    std::vector<Grid<Stone>>   stone;
    std::vector<Grid<Deposit>> deposit;
    std::vector<Grid<bool>>    cavern;
    std::vector<Grid<bool>>    aquifer;

    // ── 灵气层，非网格量 ──
    SpiritTide spirit_tide;
    std::vector<LeyChannel>  ley_channels;
    std::vector<LeyNode>     ley_nodes;
    std::vector<SpiritVein>  veins;
    std::vector<SpiritEye>   eyes;
    std::vector<ScarSite>    scars;
    std::vector<CaveHeaven>  cave_heavens;
    std::vector<TribulationSite> tribulation_sites;

private:
    std::uint8_t sea_level_ = 0;
};

}  // namespace gamev2::world

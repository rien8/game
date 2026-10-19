/// @file generate.hpp
/// @brief 世界生成的十个阶段与编排入口。
///
/// 阶段之间是严格的单向依赖，而编排顺序只写在 generate_world() 一处：
/// 调换顺序不会报错，只会静默产出错误世界。因此每个阶段的 doxygen 都写明
/// 它读哪些字段，新增阶段时必须同步更新 generate_world()。
#pragma once

#include "gamev2/world/world.hpp"

#include <cstdint>
#include <expected>

namespace gamev2::world {

/// 阶段 1。只读坐标与种子，写 elevation 与反解出的海平面。
void make_elevation(World& world);

/// 阶段 2。读 elevation，写 rainfall。
void make_rainfall(World& world);

/// 阶段 3。读 elevation 与 rainfall，写 temperature。
void make_temperature(World& world);

/// 阶段 4。读 elevation，写 downstream、flow、river_width、lake_depth、
/// drainage。水文排在群系之前，是因为群系判定要用它产出的排水量。
void make_hydrology(World& world);

/// 阶段 5。读 elevation、rainfall、temperature、drainage、lake_depth，
/// 写 biome。
void make_biomes(World& world);

/// 阶段 6。读 biome 与 rainfall，写 soil_depth。
void make_soils(World& world);

/// 阶段 7。读 biome 与 rainfall，写 stone 与 aquifer_top。
/// 含水层的填充推迟到阶段 10，那时洞室已定型。
void make_strata(World& world);

/// 阶段 8。读尺寸与种子，写 cavern。不挖穿 z=0。
void make_caverns(World& world);

/// 阶段 9。读 stone 与 cavern，写 deposit。矿脉不落进洞室。
void make_deposits(World& world);

/// 阶段 10。读 aquifer_top、cavern，写 aquifer。洞室不蓄水，因此这一步
/// 必须排在 make_caverns 之后。
void finalize_aquifers(World& world);

/// 汇流量超过该阈值即为河道。调参项，见设计文档 11.2。
inline constexpr std::uint32_t kRiverThreshold = 200;

/// 生成一个世界。参数非法时返回 WorldError::invalid_params。
[[nodiscard]] std::expected<World, WorldError>
generate_world(const WorldParams& params);

}  // namespace gamev2::world

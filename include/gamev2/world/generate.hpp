/// @file generate.hpp
/// @brief 世界生成的十八个阶段与编排入口。
///
/// 阶段之间是严格的单向依赖，而编排顺序只写在 generate_world() 一处：
/// 调换顺序不会报错，只会静默产出错误世界。因此每个阶段的 doxygen 都写明
/// 它读哪些字段，新增阶段时必须同步更新 generate_world()。
#pragma once

#include "gamev2/world/spirit_tables.hpp"
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

/// 阶段 11。只读 params.spirit_year，写 spirit_tide。
void make_spirit_tide(World& world);

/// 阶段 12。读 elevation、sea_level、downstream、flow、stone，
/// 写 ley_channels、ley_nodes 与三张地脉镜像网格。
void make_ley_network(World& world);

/// 阶段 13。读 ley_channels、ley_nodes、elevation，写 veins、eyes、
/// vein_rank、vein_id、eye_flow。灵脉品阶由排名决定，数量是硬约束。
void make_spirit_veins(World& world);

/// 某一品阶的实得与应得。
struct VeinCount
{
    std::uint32_t got = 0;       ///< 实得
    std::uint32_t expected = 0;  ///< 应得
    /// 缺口：应得减实得，实得超过应得时取零。
    [[nodiscard]] std::uint32_t shortfall() const noexcept
    {
        return expected > got ? expected - got : 0u;
    }
};

/// 灵脉层的实得与应得对照。
///
/// 数量是设定给的硬约束，但**凑不齐不是错误**：设计文档 §5 说灵气层不引入
/// 新的失败模式，世界照常生成，缺口由调用方自己读。因此这里给出逐品阶的
/// 实得与应得，而不是抛错——「实得 1/9/33、缺 7 条支脉」这类问题由它回答。
struct VeinCensus
{
    VeinCount dragon;    ///< 龙脉：天下恰好一条
    VeinCount main;      ///< 主脉：跨州级
    VeinCount branch;    ///< 支脉：郡县级
    std::uint32_t barren = 0;  ///< 荒脉：数量不可考，只报实得
    std::uint32_t eyes = 0;    ///< 灵眼数：有品阶的脉各一眼
    VeinCount depleted;  ///< 枯眼：应得数随灵眼数定
    /// 图幅面积（格）。灵脉定额按面积相对它缩放，而浓度场的统计量
    /// 只在默认图幅下可比——调用方要靠这个数知道自己拿的是哪一口径。
    std::uint32_t map_area = 0;

    /// 灵脉定额的缺口合计。零表示龙、主、支三条定额都凑齐了。
    [[nodiscard]] std::uint32_t total_shortfall() const noexcept
    {
        return dragon.shortfall() + main.shortfall() + branch.shortfall();
    }
};

/// 统计灵脉层的实得与应得。应得数来自 vein_targets(area) 与
/// round(灵眼数 × kDepletedRatio)，实得数来自 world.veins 与 world.eyes。
[[nodiscard]] VeinCensus vein_census(const World& world);

/// 灵脉数量是否达标。纯查询，不改变生成结果：设计文档 §5 说灵气层不引入
/// 新的失败模式，数量凑不齐的世界照常生成，缺口由 vein_census() 报出。
[[nodiscard]] bool veins_complete(const World& world);

/// 阶段 14。读 eyes 与 spirit_tide，写 domain_concentration、domain_grade。
/// 浓度场是灵眼的衰减叠加，不得凭空生成。
void make_spirit_domains(World& world);

/// 阶段 15。读 veins、eyes、elevation、domain_grade，
/// 写 scars、scar_kind、spirit_suppression。
void make_historical_scars(World& world);

/// 阶段 16。读 domain_grade、domain_concentration、ley_element、scars、
/// eyes，写 land_type、eye_purity。纯度由阶段 16 写，因为它依赖周边地貌。
void make_land_types(World& world);

/// 阶段 17。读 land_type、domain_grade、veins、domain_concentration，
/// 写 cave_heaven、cave_heavens。洞天只落在福地上。
void make_cave_heavens(World& world);

/// 阶段 18。读 land_type、domain_concentration、scar_kind，
/// 写 tribulation_site、tribulation_sites。渡劫场只落在无旧疤的福地上。
void make_tribulation_sites(World& world);

/// 汇流量超过该阈值即为河道。调参项，见设计文档 11.2。
inline constexpr std::uint32_t kRiverThreshold = 200;

/// 生成一个世界。参数非法时返回 WorldError::invalid_params。
[[nodiscard]] std::expected<World, WorldError>
generate_world(const WorldParams& params);

}  // namespace gamev2::world

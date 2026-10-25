#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 该格与已放置的渡劫场中心距是否小于给定间距。
///
/// 距离口径与阶段 17 的洞天、阶段 15 的疤痕一致：欧氏距离。
[[nodiscard]] bool too_close_to(const std::vector<TribulationSite>& placed,
                                std::uint32_t cell, std::uint32_t gap,
                                const WorldParams& p) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const TribulationSite& site : placed)
    {
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(site.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(site.cell / p.width);
        if (dx * dx + dy * dy
            < static_cast<std::int64_t>(gap) * static_cast<std::int64_t>(gap))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

void make_tribulation_sites(World& world)
{
    const WorldParams& p = world.params();
    world.tribulation_sites.clear();
    world.tribulation_site = Grid<bool>(p.width, p.height, false);

    // 候选：福地，且没有旧伤疤。
    //
    // 福地是设定 5.2 的「渡劫福地」的地理前提。排除旧伤疤是因为设定说
    // 渡劫失败会让该地「百年内沦为死地」——一个已经死过的地方不该再排劫。
    //
    // 浓度门槛没有单独设：福地（domain_grade == blessed）本身要求浓度
    // ≥ kBlessedThreshold，再加一道阈值是死代码。浓度的差别由选点规则
    // 承担——按浓度降序取。
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.land_type.at(i) != LandType::blessed)
            {
                continue;
            }
            if (world.scar_kind.at(i) != Scar::none)
            {
                continue;
            }
            candidates.push_back(i);
        }
    }
    if (candidates.empty())
    {
        return;
    }

    // 按浓度降序排。同浓度时保留下标次序，排序结果因此稳定。
    std::ranges::stable_sort(candidates, [&world](std::uint32_t a,
                                                  std::uint32_t b) {
        return world.domain_concentration.at(a)
               > world.domain_concentration.at(b);
    });

    // 间距不够就跳过这个候选看下一个，而不是只在一小块池子里选址——
    // 池子化会把劫云挤成一片，劫云重叠处灵气会互相搅浑。
    for (const std::uint32_t cell : candidates)
    {
        if (too_close_to(world.tribulation_sites, cell, kTribulationSpacing, p))
        {
            continue;
        }
        // vein 记该格所属的灵脉。福地的范围未必落在脉上，那时是 kNoVein：
        // 劫云抽的是方圆百里的环境灵气，具体抽走哪条脉的存量由时序层判定。
        world.tribulation_sites.push_back(TribulationSite{
            cell, kTribulationRadius, world.vein_id.at(cell)});
        world.tribulation_site.at(cell) = true;
    }
}

}  // namespace gamev2::world

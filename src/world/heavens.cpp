#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 一座洞天摊到的福地面积，格。洞天数 = 福地面积 / 它。
constexpr std::uint32_t kCaveHeavenArea = 400;
/// 浓度换算成可锁住的灵气量。
constexpr std::uint32_t kCaveHeavenCapacity = 8;
/// 容量换算成年耗灵石。红线 9：开一座洞天要多花灵石，那灵石就少了。
constexpr std::uint32_t kCaveHeavenUpkeep = 2;
/// 两座洞天的最小中心距，格。洞天不是贴着开的。
constexpr std::uint32_t kCaveHeavenSpacing = 8;

/// 该格与已放置的洞天中心距是否小于给定间距。
[[nodiscard]] bool too_close_to(const std::vector<CaveHeaven>& placed,
                                std::uint32_t cell, std::uint32_t gap,
                                const WorldParams& p) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const CaveHeaven& cave : placed)
    {
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(cave.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(cave.cell / p.width);
        if (dx * dx + dy * dy
            < static_cast<std::int64_t>(gap) * static_cast<std::int64_t>(gap))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

void make_cave_heavens(World& world)
{
    const WorldParams& p = world.params();
    world.cave_heavens.clear();
    world.cave_heaven = Grid<bool>(p.width, p.height, false);

    // 候选：只有福地。设定 2.5：洞天是福地之上人工开辟的小空间。
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.land_type.at(i) == LandType::blessed)
            {
                candidates.push_back(i);
            }
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

    const auto count = static_cast<std::uint32_t>(candidates.size())
                       / kCaveHeavenArea;
    if (count == 0)
    {
        return;
    }

    // 按浓度降序逐个检视（设计文档 §3.7）：洞天要开在灵气最浓的地方。
    // 间距不够就跳过这个候选看下一个，而不是只在一小块池子里选址——
    // 池子化会让「洞天数 = 福地格数 / kCaveHeavenArea」这条公式开不满：
    // 同浓度时池子落在行主序的头几行，一行放不下几座洞天，洞天反而更
    // 集中（实测灵潮期末 6 个有福地的世界里，池化版 4 个开不满）。
    for (const std::uint32_t cell : candidates)
    {
        if (world.cave_heavens.size()
            >= static_cast<std::size_t>(count))
        {
            break;
        }
        if (too_close_to(world.cave_heavens, cell, kCaveHeavenSpacing, p))
        {
            continue;
        }
        // 用有效浓度而不是原始浓度：洞天是把外界灵气收进来锁住，
        // 被疤痕压过的地方当然锁不住那么多。福地格不会被死地类疤盖住
        // （地貌规则里死地压过福地），因此福地上的有效浓度就是原始浓度。
        const std::uint32_t concentration = effective_concentration(
            world.domain_concentration.at(cell),
            world.spirit_suppression.at(cell));
        const std::uint32_t capacity = concentration * kCaveHeavenCapacity;
        const std::uint32_t upkeep = capacity * kCaveHeavenUpkeep;
        world.cave_heavens.push_back(CaveHeaven{cell, capacity, upkeep});
        world.cave_heaven.at(cell) = true;
    }
}

}  // namespace gamev2::world

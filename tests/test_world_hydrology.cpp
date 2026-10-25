#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::kRiverThreshold;
using gamev2::world::kNoDownstream;
using gamev2::world::make_elevation;
using gamev2::world::make_hydrology;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.seed = seed;
    return p;
}

TEST(hydrology_downstream_never_cycles)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 无环由一把钥匙保证：**弹出次序沿指针严格递减**。主规则的目标填平高度
    // 严格更低，最小堆必让它先弹出；parent 是把本格推入堆的那一格，本格
    // 只在它弹出之后才可能被推入。本测试是对这条推理的兜底核对，逐格扫描。
    bool terminates = true;
    for (std::uint32_t start = 0; start < w.downstream.size(); ++start)
    {
        std::uint32_t cur = start;
        std::uint32_t steps = 0;
        while (cur != kNoDownstream && steps < w.downstream.size())
        {
            cur = w.downstream.at(cur);
            ++steps;
        }
        terminates = terminates && steps < w.downstream.size();
    }
    CHECK(terminates);
}

TEST(hydrology_stops_at_the_ocean)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool oceans_are_terminal = true;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            if (!w.is_ocean(x, y))
            {
                continue;
            }
            const std::uint32_t i = w.elevation.index_of(x, y);
            oceans_are_terminal =
                oceans_are_terminal && w.downstream.at(i) == kNoDownstream;
        }
    }
    CHECK(oceans_are_terminal);
}

TEST(hydrology_flow_grows_downstream)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool monotonic = true;
    std::uint32_t river_cells = 0;
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        const std::uint32_t d = w.downstream.at(i);
        if (d != kNoDownstream)
        {
            monotonic = monotonic && w.flow.at(d) >= w.flow.at(i);
        }
        river_cells += w.river_width.at(i) > 0 ? 1u : 0u;
    }
    CHECK(monotonic);
    CHECK(river_cells > 0);
}

TEST(hydrology_river_width_follows_flow)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool consistent = true;
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        const std::uint32_t f = w.flow.at(i);
        const std::uint8_t width = w.river_width.at(i);
        consistent = consistent && ((f >= kRiverThreshold) == (width > 0));
        if (width > 0)
        {
            consistent = consistent && width <= 5;
        }
    }
    CHECK(consistent);
}

TEST(hydrology_flow_matches_chain_counting)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 本任务的核心断言：flow[i] 就是「沿链能走到 i 的格子数」。
    // 累加用的是弹出次序的逆序而非按高程排序，故只能这样独立核对 ——
    // 若拓扑序错了，流量会静默丢失而单调性检查仍可能通过。
    std::vector<std::uint32_t> counted(w.flow.size(), 0);
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        std::uint32_t cur = i;
        std::uint32_t steps = 0;
        while (cur != kNoDownstream && steps < w.downstream.size())
        {
            ++counted[cur];
            cur = w.downstream.at(cur);
            ++steps;
        }
    }

    bool same = true;
    for (std::uint32_t i = 0; i < w.flow.size(); ++i)
    {
        same = same && counted[i] == w.flow.at(i);
    }
    CHECK(same);
}

TEST(hydrology_terminates_in_ocean_or_lake)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool ends_in_water = true;
    std::uint32_t lakes = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (w.lake_depth.at(i) > 0)
            {
                ++lakes;
            }
            if (w.downstream.at(i) != kNoDownstream)
            {
                continue;
            }
            // 终点有三种：海里、湖里，以及图边界。边界是水离开世界的出口 ——
            // 洪泛正是拿边界格当种子，洼地里的水若没有更低的下游可走，沿链
            // 最后就会流到某一格边界上出图。这不是内流，是泄洪。
            const bool on_border = x == 0 || y == 0
                                   || x == w.params().width - 1
                                   || y == w.params().height - 1;
            ends_in_water = ends_in_water
                           && (w.is_ocean(x, y) || w.lake_depth.at(i) > 0
                               || on_border);
        }
    }
    CHECK(ends_in_water);
    CHECK(lakes > 0);
}

TEST(lakes_have_the_worst_drainage_on_the_map)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    bool lakes_are_wettest = true;
    bool some_cell_is_dry = false;
    for (std::uint32_t i = 0; i < w.drainage.size(); ++i)
    {
        if (w.lake_depth.at(i) > 0)
        {
            lakes_are_wettest = lakes_are_wettest && w.drainage.at(i) == 0;
        }
        some_cell_is_dry = some_cell_is_dry || w.drainage.at(i) == 100;
    }
    CHECK(lakes_are_wettest);
    CHECK(some_cell_is_dry);
}

TEST(river_cells_are_never_fully_drained)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 排水量按定义是「离水多远」，所以有河的格子按构造不可能排干。
    // World 构造时把 drainage 填成 100，故这条断言实际证明的是
    // derive_drainage 确实覆盖写了这些格子。
    bool rivers_are_wet = true;
    std::uint32_t rivers = 0;
    for (std::uint32_t i = 0; i < w.drainage.size(); ++i)
    {
        if (w.river_width.at(i) == 0)
        {
            continue;
        }
        ++rivers;
        rivers_are_wet = rivers_are_wet && w.drainage.at(i) < 100;
    }
    CHECK(rivers > 0);
    CHECK(rivers_are_wet);
}

TEST(hydrology_is_deterministic)
{
    World a(params(3));
    World b(params(3));
    make_elevation(a);
    make_elevation(b);
    make_hydrology(a);
    make_hydrology(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.flow.size(); ++i)
    {
        same = same && a.flow.at(i) == b.flow.at(i);
        same = same && a.downstream.at(i) == b.downstream.at(i);
        same = same && a.river_width.at(i) == b.river_width.at(i);
    }
    CHECK(same);
}

}  // namespace

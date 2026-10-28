#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <chrono>

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

/// 四邻域里是否有湖。越界方向跳过。
bool touches_lake(const World& w, std::uint32_t x, std::uint32_t y)
{
    const int cx = static_cast<int>(x);
    const int cy = static_cast<int>(y);
    const int nx[4] = {cx - 1, cx + 1, cx, cx};
    const int ny[4] = {cy, cy, cy - 1, cy + 1};
    for (int d = 0; d < 4; ++d)
    {
        if (nx[d] < 0 || ny[d] < 0
            || nx[d] >= static_cast<int>(w.params().width)
            || ny[d] >= static_cast<int>(w.params().height))
        {
            continue;
        }
        const auto j = w.elevation.index_of(
            static_cast<std::uint32_t>(nx[d]),
            static_cast<std::uint32_t>(ny[d]));
        if (w.lake_depth.at(j) > 0)
        {
            return true;
        }
    }
    return false;
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

TEST(lakes_lift_adjacent_river_cells_out_of_drought)
{
    World w(params(3));
    make_elevation(w);
    make_hydrology(w);

    // 湖是湿量 100 的强源，河最湿也只有 50。若泛洪前把每个源的目标湿度一次
    // 写死，河格自身那点湿度就再也不会被紧邻的湖抬高——湖口那一圈河格会
    // 报出排水量 90，尽管离湖只有一步。只扫「既是河又贴着湖」的格子，它们
    // 是旧实现唯一漏掉的形状。
    std::uint32_t lake_mouths = 0;
    std::uint32_t dried = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (w.river_width.at(i) == 0 || w.lake_depth.at(i) > 0
                || w.is_ocean(x, y) || !touches_lake(w, x, y))
            {
                continue;
            }
            ++lake_mouths;
            dried += w.drainage.at(i) != 1 ? 1u : 0u;
        }
    }
    // 夹具有效性：这张地图上确实有河直接入湖，否则本用例是空转。
    CHECK(lake_mouths > 0);
    CHECK_EQ(dried, 0u);
}

TEST(flat_bottom_lakes_are_not_carved_quadratically)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.seed = 3;
    World w(p);

    // 手工造一个平底盆地：盆沿与边界同高，盆底整片同高。填平后整片盆底共享
    // 一个水位，于是每个盆底格都成了「成湖种子」。旧实现逐个种子重灌整片湖，
    // 代价随湖面积平方增长（64² 实测 7.2 秒）；按水位分桶的多源泛洪每格只
    // 写一次，与面积线性。跳过 make_elevation 是为了让盆底精确平坦——fbm
    // 噪声造不出 64² 全平的洼。
    constexpr std::uint32_t kFloor = 10;
    constexpr std::uint32_t kRim = 40;
    constexpr std::uint32_t kInset = 16;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const bool basin = x >= kInset && y >= kInset
                               && x < p.width - kInset
                               && y < p.height - kInset;
            w.elevation(x, y) =
                static_cast<std::uint8_t>(basin ? kFloor : kRim);
        }
    }

    // 与 genetics 的性能护栏同样的取舍：Debug 构建下也留足余量，只抓复杂度
    // 爆炸而不是机器快慢。旧实现要 7 秒以上，新实现是毫秒级。
    constexpr auto kBudget = std::chrono::milliseconds(2000);
    const auto start = std::chrono::steady_clock::now();
    make_hydrology(w);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    bool floor_flooded = true;
    bool rim_dry = true;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const bool basin = x >= kInset && y >= kInset
                               && x < p.width - kInset
                               && y < p.height - kInset;
            if (basin)
            {
                floor_flooded =
                    floor_flooded
                    && w.lake_depth(x, y)
                           == static_cast<std::uint8_t>(kRim - kFloor);
            }
            else
            {
                rim_dry = rim_dry && w.lake_depth(x, y) == 0;
            }
        }
    }

    std::println("flat-bottom lake ({} cells): {}",
                 (p.width - 2 * kInset) * (p.height - 2 * kInset), elapsed);
    // 平底湖的水面处处等高：整片盆底都该没到盆沿。
    CHECK(floor_flooded);
    CHECK(rim_dry);
    CHECK(elapsed < kBudget);
}

}  // namespace

#include "gamev2/world/generate.hpp"
#include "gamev2/world/hydrology.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <ranges>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

constexpr int kDx4[4] = {1, -1, 0, 0};
constexpr int kDy4[4] = {0, 0, 1, -1};
constexpr int kDx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

/// 成湖所需的最小填平深度。浅于此的洼是 fbm 的细碎噪声，理平它，好让最陡
/// 下降有连续的路；深于此的是真盆地，保留积水成湖 —— 设计文档 §11.3 说
/// 河流终止于湖而非海是正常地理，端内流世界不是缺陷。
/// 端内流靠**截断指针**实现，而不是靠压低洪泛：洪泛一旦加深度上限，推入值
/// 会低于当前高度，堆的弹出序列不再按高度非降，按弹出次序建的拓扑序失效，
/// 指针会成环。截断只删边不添边，因此无环性不受影响。
constexpr int kLakeMinDepth = 4;

/// 湖面水量的上限，也就是排水量的下限。取 100 是为了让 drainage 与 wetness
/// 互为反数，读代码时不必在脑子里做一次换算。
constexpr std::uint16_t kMaxWetness = 100;

bool in_bounds(const WorldParams& p, int x, int y) noexcept
{
    return x >= 0 && y >= 0 && x < static_cast<int>(p.width)
           && y < static_cast<int>(p.height);
}

/// priority-flood：反复取出当前最低的格子，抬高邻格填平洼地，直到全图处理完。
///
/// 保证的是「每格都能走到某个种子」，不是「没有洼地」：洪泛只走 4 邻域，
/// 唯一出口在对角线上的洼永远填不平。因此下游指针仍需要 parent 兜底。
///
/// 顺手记下两样事后无法重建的东西：把本格推入堆的那一格（parent，即通向
/// 出口的方向），以及弹出次序（order）。parent 只认**第一次**推入：后续推入
/// 携带的高度不低于首次，若被覆盖就会指向一个更高的邻格，方向反而错了。
void fill_depressions(const World& world, FilledDem& out)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    out.level.assign(n, 0);
    out.parent.assign(n, kNoDownstream);
    out.order.clear();
    out.order.reserve(n);
    std::vector<bool> done(n, false);

    using Entry = std::pair<std::uint8_t, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

    // 边界格与海洋格是「水流出图外」的天然出口，作为填平的种子。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const bool edge = x == 0 || y == 0 || x == p.width - 1
                              || y == p.height - 1;
            if (edge || world.is_ocean(x, y))
            {
                heap.emplace(world.elevation.at(i), i);
            }
        }
    }

    while (!heap.empty())
    {
        const auto [level, i] = heap.top();
        heap.pop();
        if (done[i])
        {
            continue;
        }
        done[i] = true;
        out.level[i] = level;
        out.order.push_back(i);

        const auto x = static_cast<int>(i % p.width);
        const auto y = static_cast<int>(i / p.width);
        for (int d = 0; d < 4; ++d)
        {
            const int nx = x + kDx4[d];
            const int ny = y + kDy4[d];
            if (!in_bounds(p, nx, ny))
            {
                continue;
            }
            const std::uint32_t j = world.elevation.index_of(
                static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
            if (done[j])
            {
                continue;
            }
            if (out.parent[j] == kNoDownstream)
            {
                out.parent[j] = i;
            }
            // 标准 priority-flood：抬到本格原始高程与当前溢出高度之大者。
            // 刻意不加「最多抬 k 层」的上限 —— 那样会让本函数推入低于当前
            // 高度的值，堆的弹出序列不再按高度非降，于是「填平高度更低 ⇒
            // 先弹出」失效，按弹出次序做的拓扑序随之失效，指针会成环
            // （实测 cap=3/4 各有 3/2 处逆序）。端内流盆地改由
            // build_downstream 截断实现，见 kLakeMinDepth。
            heap.emplace(std::max(world.elevation.at(j), level), j);
        }
    }
}

/// 最陡下降。主规则取填平高度严格更低的邻居；该规则在填平后的等高平台上
/// 无路可走（平台是 priority-flood 造出来的，洼地内部被抬到溢流高度后与
/// 溢流格同级），此时退回 parent —— 把本格推入堆的那一格。
///
/// 退回 parent 而非「原始高程更低者」是关键：洼地内部原始高程最低处在
/// 洼底，按原始高程下降等于把水引进坑里；水该沿溢流方向走出去，而 parent
/// 正是那个方向。
///
/// 无环性用一把钥匙论证：**弹出次序沿指针严格递减**。两类边都如此 ——
/// 主规则的目标填平高度严格更低，最小堆必让它先弹出；parent 是把本格推入
/// 堆的那一格，本格只在它弹出之后才可能被推入。于是指针走一步，弹出次序
/// 必退一格，而严格递减的次序不可能成环。∎
void build_downstream(World& world, const FilledDem& dem)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.is_ocean(x, y))
            {
                // 水在海里汇集，不该再往海底流。
                world.downstream.at(i) = kNoDownstream;
                continue;
            }

            std::uint8_t best = dem.level[i];
            std::uint32_t target = kNoDownstream;
            for (int d = 0; d < 8; ++d)
            {
                const int nx = static_cast<int>(x) + kDx8[d];
                const int ny = static_cast<int>(y) + kDy8[d];
                if (!in_bounds(p, nx, ny))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (dem.level[j] < best)
                {
                    best = dem.level[j];
                    target = j;
                }
            }
            if (target != kNoDownstream)
            {
                world.downstream.at(i) = target;
                continue;
            }

            // 主规则落空时才作选择：深洼成湖不外泄，浅洼退回 parent 找出路。
            // 截断只删边，绝不添边，故无环性不受影响。
            const int depth = static_cast<int>(dem.level[i])
                              - static_cast<int>(world.elevation.at(i));
            world.downstream.at(i) = depth >= kLakeMinDepth
                                         ? kNoDownstream
                                         : dem.parent[i];
        }
    }
}

/// 汇流量累加：把 dem.order 逆序遍历即可，无需再排序。
///
/// 弹出次序沿下游指针严格递减（见 build_downstream 的论证），故逆序正是
/// 一个拓扑序：本格一定排在自己的所有下游之前被处理，交给下游的流量必然
/// 已经算完。填平高度在这里**不能**当排序键 —— 键必须是那个对两类边都
/// 严格下降的量，而高度只在主规则边上下降，parent 边上可以持平。
void accumulate_flow(World& world, const FilledDem& dem)
{
    const auto n = static_cast<std::size_t>(world.downstream.size());

    std::vector<std::uint32_t> flow(n, 1);
    for (std::size_t k = dem.order.size(); k-- > 0;)
    {
        const std::uint32_t i = dem.order[k];
        const std::uint32_t d = world.downstream.at(i);
        if (d != kNoDownstream)
        {
            flow[d] += flow[i];
        }
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        world.flow.at(static_cast<std::uint32_t>(i)) = flow[i];
    }
}

std::uint8_t river_width_of(std::uint32_t flow) noexcept
{
    if (flow < kRiverThreshold)
    {
        return 0;
    }
    if (flow < 2 * kRiverThreshold)
    {
        return 1;
    }
    if (flow < 6 * kRiverThreshold)
    {
        return 2;
    }
    if (flow < 20 * kRiverThreshold)
    {
        return 3;
    }
    if (flow < 60 * kRiverThreshold)
    {
        return 4;
    }
    return 5;
}

void mark_rivers(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            // 海洋格同样按流量定宽：河必然入海，入海口那一格是终点，会攒下
            // 整条河的流量，若在此处清零，「流量超阈值 ⇔ 有河宽」这条不变式
            // 就会在入海口破掉。指针那头海洋仍是终点，水不再往海底流。
            world.river_width.at(i) = river_width_of(world.flow.at(i));
        }
    }
}

/// 洼地成湖，水位铺满整个洼底。只标最低那一格会让湖缩成一个点，
/// 也撑不起排水量扩散所需的源。
void carve_lakes(World& world, const FilledDem& dem)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<std::uint32_t> seeds;

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.is_ocean(x, y)
                || world.downstream.at(i) != kNoDownstream)
            {
                continue;
            }
            // 成不成湖由 kLakeMinDepth 独家决定：build_downstream 在填平深度
            // 够深时截断了指针，于是「终点」正是这些洼底。汇流量不再作筛选 ——
            // 内流盆地攒的那点水按成河标准衡量微不足道，拿流量去卡只会让地图
            // 上一个湖都不剩，而截断指针已经替我们完成了取舍。
            const int depth =
                static_cast<int>(dem.level[i]) - world.elevation.at(i);
            if (depth <= 0)
            {
                continue;
            }
            seeds.push_back(i);
            world.lake_depth.at(i) =
                static_cast<std::uint8_t>(std::min(depth, 255));
        }
    }

    std::vector<bool> seen(n, false);
    std::vector<std::uint32_t> queue;
    for (const std::uint32_t seed : seeds)
    {
        // 逐湖重置：两个湖的洼底可以部分重叠，共用一个 seen 会让后一个
        // 的水面缺一块。
        std::ranges::fill(seen, false);
        const std::uint8_t level = dem.level[seed];
        queue.clear();
        queue.push_back(seed);
        seen[seed] = true;

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            const std::uint32_t i = queue[head];
            const auto x = static_cast<int>(i % p.width);
            const auto y = static_cast<int>(i / p.width);
            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + kDx4[d];
                const int ny = y + kDy4[d];
                if (!in_bounds(p, nx, ny))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                // 海格也挡：洼地若从边界泄出去，水位会漫过低于它的海面，把海
                // 标成湖。群系判定先看 lake_depth 再看高程，那样的格会被贴上
                // Biome::lake，尽管它在海平面以下。
                if (seen[j] || world.is_ocean(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny))
                    || world.elevation.at(j) >= level)
                {
                    continue;
                }
                seen[j] = true;
                world.lake_depth.at(j) = static_cast<std::uint8_t>(
                    level - world.elevation.at(j));
                queue.push_back(j);
            }
        }
    }
}

/// 排水量是 DF 的群系判定输入之一，但它由泛洪平原计算得出、算法未公开。
/// 这里以水体为源做一次多源 BFS 扩散作为等价近似：离水越远越干。
void derive_drainage(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<std::uint8_t> wetness(n, 0);
    // 桶而非队列：湿量每步只减一，故值域就是 1..100，按值分桶后从大到小抽干，
    // 等价于单位边权的 Dijkstra，首次写入必然是可达的最大湿量。普通多源 BFS
    // 的先到先得在这里是错的 —— 源点按栅格序入队，弱源（河，湿量 10..50）会
    // 抢先占住邻格，而它往往紧邻强源（湖，湿量 100），于是每个湖口外都围出
    // 一圈排水量 ≈ 91 的「好干」地，而那圈地本该是离水一步的湿地。
    std::vector<std::vector<std::uint32_t>> buckets(kMaxWetness + 1);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            std::uint8_t start = 0;
            if (world.lake_depth.at(i) > 0)
            {
                start = kMaxWetness;
            }
            else if (world.river_width.at(i) > 0)
            {
                start = static_cast<std::uint8_t>(
                    50 * world.river_width.at(i) / 5);
            }
            if (start == 0)
            {
                continue;
            }
            wetness[i] = start;
            buckets[start].push_back(i);
        }
    }

    for (std::uint16_t level = kMaxWetness; level > 0; --level)
    {
        for (const std::uint32_t i : buckets[level])
        {
            const auto x = static_cast<int>(i % p.width);
            const auto y = static_cast<int>(i / p.width);
            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + kDx4[d];
                const int ny = y + kDy4[d];
                if (!in_bounds(p, nx, ny))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (wetness[j] != 0)
                {
                    continue;
                }
                // 每向外一格衰减一档，下限 0。
                wetness[j] = static_cast<std::uint8_t>(level - 1);
                if (level > 1)
                {
                    buckets[level - 1].push_back(j);
                }
            }
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        world.drainage.at(i) =
            static_cast<std::uint8_t>(kMaxWetness - wetness[i]);
    }
}

}  // namespace

void make_hydrology(World& world)
{
    FilledDem dem;
    fill_depressions(world, dem);
    build_downstream(world, dem);
    accumulate_flow(world, dem);
    mark_rivers(world);
    carve_lakes(world, dem);
    derive_drainage(world);
}

}  // namespace gamev2::world

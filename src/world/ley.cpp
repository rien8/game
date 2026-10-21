#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 源点至少要比海平面高这么多。低于这个高度盖层太厚，地脉埋得太深，
/// 出不了灵眼。
constexpr std::uint8_t kLeySourceRelief = 12;

/// 源点之间的最小间距（切比雪夫距离）。防止源点挤成一团、通道互相踩。
///
/// 取 6 而不是 8 或 12：间距才是地脉密度实际的约束（面积预算 1/300 比它
/// 宽，不起作用），而阶段 13 的灵脉数量是硬约束——龙脉 1、主脉 9、支脉 40
/// 共 50 条，每条要占一条通道。实测 256×256：间距 8 时最差的一批种子只
/// 出得出 43 条通道（切不出 50 段），间距 6 时同一批种子的通道数最低
/// 69 条，缺口消失。
constexpr std::uint32_t kLeySourceSpacing = 6;

/// 每格面积产出的源点数。让「地脉密度」在任何图幅上都指同一件事，
/// 与既有设计里 kElevationFeatures 的做法一致。
///
/// 这是上限而非实际密度：实际源点数由 kLeySourceSpacing 与地形决定，
/// 256×256 的图上两者给出约一百条通道。
constexpr double kLeySourcePerCell = 1.0 / 300.0;

/// 源点数量的绝对下限。小图幅上也要有几条通道才谈得上灵脉。
constexpr std::uint32_t kMinLeySources = 4;

/// 本图幅需要的地脉通道数下界。
///
/// 阶段 13 的灵脉数量是硬约束：龙脉 1、主脉 9、支脉 40 共 50 条，每条
/// 灵脉占一条通道，通道不够就切不出那么多段，品阶数量就凑不齐。多出的
/// 一条是给荒脉留的位置——荒脉也得有，否则「残余矿化」无处落地。
std::uint32_t required_ley_channels(std::uint32_t area)
{
    const auto target = vein_targets(area);
    return target.dragon + target.main + target.branch + 1u;
}

/// 单条通道的最大步数。
constexpr std::uint32_t kLeyMaxSteps = 512;

/// 埋深基准。relief 为 0（汇水区底部）时的埋深。
constexpr std::uint32_t kLeyBaseDepth = 40;

/// 埋深对 relief 的敏感度。relief 越大盖层越薄，埋得越浅。
constexpr std::uint32_t kLeyReliefShallow = 1;

/// 断层处的额外埋深。设定 2.1：「断层是裂口」。
constexpr std::uint32_t kLeyFaultDepth = 12;

/// 每个源点贡献的流量。
constexpr std::uint32_t kLeySourceFlow = 100;

/// 8 邻域偏移。顺序固定，择序与遍历才有一致的结果。
constexpr int kDx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

/// 追踪过程中的临时结果。
struct TraceResult
{
    std::vector<LeyChannel> channels;
    std::vector<LeyNode> nodes;
    /// 每格对应的节点索引。kNoNode 表示该格不在任何地脉上。
    std::vector<std::uint32_t> node_of_cell;
};

/// 汇水区底部的高程：沿 downstream 走到终点（海洋或湖底）的那一格。
///
/// 地脉埋深按「高出汇水区底部多少」反推，因此每格都要这个值。
/// 同一条链上的格子共享同一个终点，记忆化避免重复走。
std::vector<std::uint8_t> compute_basin_floor(const World& world)
{
    const std::uint32_t n = world.elevation.size();
    std::vector<std::uint8_t> floor(n, 0);
    std::vector<char> known(n, 0);

    for (std::uint32_t i = 0; i < n; ++i)
    {
        if (known[i] != 0)
        {
            continue;
        }
        std::uint32_t cur = i;
        std::uint32_t steps = 0;
        while (world.downstream.at(cur) != kNoDownstream && steps <= n)
        {
            cur = world.downstream.at(cur);
            ++steps;
        }
        const std::uint8_t base = world.elevation.at(cur);
        floor[cur] = base;
        known[cur] = 1;

        cur = i;
        steps = 0;
        while (known[cur] == 0 && steps <= n)
        {
            floor[cur] = base;
            known[cur] = 1;
            const std::uint32_t next = world.downstream.at(cur);
            if (next == kNoDownstream)
            {
                break;
            }
            cur = next;
            ++steps;
        }
    }
    return floor;
}

/// 找出地脉源点：8 邻域内无更高者，且高过海平面一个余量。
///
/// 取「无更高者」而不是「唯一最高」：等高的平台可以有多个源点，
/// 只取唯一最高会把整块高原当成一个源。
///
/// spacing 是源点之间的最小间距。调用方在通道数不够时会放宽它重选，
/// 因此这里不做任何兜底：放宽与收紧都走同一条路径。
std::vector<std::uint32_t> pick_sources(const World& world,
                                         std::uint32_t spacing)
{
    const WorldParams& p = world.params();
    std::vector<std::uint32_t> candidates;

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.elevation.at(i) < world.sea_level() + kLeySourceRelief)
            {
                continue;
            }
            bool peak = true;
            for (int d = 0; d < 8 && peak; ++d)
            {
                const int nx = static_cast<int>(x) + kDx8[d];
                const int ny = static_cast<int>(y) + kDy8[d];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                    || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (world.elevation.at(j) > world.elevation.at(i))
                {
                    peak = false;
                }
            }
            if (peak)
            {
                candidates.push_back(i);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [&world](std::uint32_t a, std::uint32_t b) {
                  if (world.elevation.at(a) != world.elevation.at(b))
                  {
                      return world.elevation.at(a) > world.elevation.at(b);
                  }
                  return a < b;
              });

    const std::uint32_t area = p.width * p.height;
    const auto wanted = static_cast<std::uint32_t>(std::max<double>(
        static_cast<double>(std::max(
            kMinLeySources, required_ley_channels(area))),
        std::lround(static_cast<double>(area) * kLeySourcePerCell)));

    std::vector<std::uint32_t> sources;
    for (const std::uint32_t cell : candidates)
    {
        if (sources.size() >= wanted)
        {
            break;
        }
        const auto x = static_cast<int>(cell % p.width);
        const auto y = static_cast<int>(cell / p.width);
        bool too_close = false;
        for (const std::uint32_t taken : sources)
        {
            const auto tx = static_cast<int>(taken % p.width);
            const auto ty = static_cast<int>(taken / p.width);
            const int dx = std::abs(x - tx);
            const int dy = std::abs(y - ty);
            if (std::max(dx, dy) < static_cast<int>(spacing))
            {
                too_close = true;
                break;
            }
        }
        if (!too_close)
        {
            sources.push_back(cell);
        }
    }
    return sources;
}

/// 埋深。盖层在山脊处薄、盆地处厚，地脉贴着盖层底走，因此埋深由
/// 「高出汇水区底部多少」反推：relief 越大，埋得越浅。
///
/// 断层另加一段埋深：设定 2.1 说「断层是裂口」，地脉在断层处沉入
/// 深部，抬升的浓度被打断——灵脉的切分边界由此而来。
std::uint32_t depth_of(const World& world, std::uint32_t cell,
                        std::uint8_t basin_floor)
{
    const WorldParams& p = world.params();
    const int relief = static_cast<int>(world.elevation.at(cell))
                       - static_cast<int>(basin_floor);

    const auto x = static_cast<int>(cell % p.width);
    const auto y = static_cast<int>(cell / p.width);
    int gradient = 0;
    for (int d = 0; d < 8; ++d)
    {
        const int nx = x + kDx8[d];
        const int ny = y + kDy8[d];
        if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
            || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        const std::uint32_t j = world.elevation.index_of(
            static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
        const int delta = static_cast<int>(world.elevation.at(cell))
                          - static_cast<int>(world.elevation.at(j));
        gradient = std::max(gradient, std::abs(delta));
    }

    const int raw = static_cast<int>(kLeyBaseDepth)
                    - static_cast<int>(kLeyReliefShallow) * relief
                    + static_cast<int>(kLeyFaultDepth) * gradient / 16;
    return static_cast<std::uint32_t>(std::clamp(
        raw, static_cast<int>(kMinLeyDepth), static_cast<int>(kMaxLeyDepth)));
}

/// 全图埋深。埋深是每格自身的属性，与路径无关，因此可以预先算好——择序
/// 第三条要比的正是它，每步现算会把追踪的代价放大九倍。
std::vector<std::uint32_t> compute_ley_depths(
    const World& world, const std::vector<std::uint8_t>& floor)
{
    const std::uint32_t n = world.elevation.size();
    std::vector<std::uint32_t> depths(n);
    for (std::uint32_t i = 0; i < n; ++i)
    {
        depths[i] = depth_of(world, i, floor[i]);
    }
    return depths;
}

/// 节点属性由流经的岩性决定（设定 2.1）。埋深可能超过世界的总格深，
/// 超出部分取最深的岩层——那本来就是「地脉在更深的地方」。
Element element_of_node(const World& world, std::uint32_t cell,
                         std::uint32_t depth)
{
    const WorldParams& p = world.params();
    const auto z = std::min<std::uint32_t>(p.z_levels - 1u,
                                            depth / p.z_cell_depth);
    return element_of_stone(world.stone[z].at(cell));
}

/// 逐源点追踪通道。
///
/// 择序：先要求严格下降（否则通道会爬坡），再按汇流量大者优先，
/// 最后按埋深浅者优先。等价条件时靠 kDx8/kDy8 的固定顺序打破平局，
/// 因此结果与遍历顺序无关。
///
/// 第三条是「埋深浅」而不是「高程低」：两者在同一汇水区内是反的
/// （高程越高 → relief 越大 → 埋得越浅）。埋深浅的邻居意味着通道贴着
/// 盖层底走而不是一头扎进低处——设计 §3.2 说「浅埋才有灵眼在地表出露
/// 的可能，灵眼若全在河口就与设定打架」。按高程低择优恰好会把通道推向
/// 河口，埋深随之变大。
TraceResult trace_channels(const World& world,
                            const std::vector<std::uint32_t>& depths,
                            std::uint32_t spacing)
{
    const WorldParams& p = world.params();
    const std::uint32_t n = world.elevation.size();

    TraceResult result;
    result.node_of_cell.assign(n, kNoNode);

    for (const std::uint32_t source : pick_sources(world, spacing))
    {
        LeyChannel channel;
        std::uint32_t cur = source;
        std::uint32_t prev = kNoNode;
        std::uint32_t steps = 0;

        while (steps < kLeyMaxSteps)
        {
            // 这一格已经在别的通道上：并入它，通道到此为止。
            // 这里的 next 覆盖通道内的边：末节点改指汇流目标。
            const std::uint32_t existing = result.node_of_cell[cur];
            if (existing != kNoNode)
            {
                if (prev != kNoNode)
                {
                    result.nodes[prev].next = existing;
                }
                ++result.nodes[existing].merges;
                break;
            }

            LeyNode node;
            node.cell = cur;
            node.depth = depths[cur];
            node.element = element_of_node(world, cur, node.depth);
            node.flow = kLeySourceFlow;
            const auto index = static_cast<std::uint32_t>(result.nodes.size());
            result.nodes.push_back(node);
            result.node_of_cell[cur] = index;
            channel.nodes.push_back(index);
            // 通道内相邻节点由 next 相连：流量沿通道累加的基础，也是
            // 「下游节点高程更低」这条无环保证的载体。走到的下一格必然
            // 更低——择序时只收严格下降的邻格。
            if (prev != kNoNode)
            {
                result.nodes[prev].next = index;
            }
            prev = index;

            const auto x = static_cast<int>(cur % p.width);
            const auto y = static_cast<int>(cur / p.width);
            std::uint32_t best = kNoNode;
            std::uint32_t best_flow = 0;
            std::uint32_t best_depth = 0;
            for (int d = 0; d < 8; ++d)
            {
                const int nx = x + kDx8[d];
                const int ny = y + kDy8[d];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                    || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                if (world.elevation.at(j) >= world.elevation.at(cur))
                {
                    continue;
                }
                // 地脉不出海：河口不是灵眼。
                if (world.elevation.at(j) < world.sea_level())
                {
                    continue;
                }
                // 已在别的通道上：并入它。
                if (result.node_of_cell[j] != kNoNode)
                {
                    best = j;
                    break;
                }
                if (best == kNoNode || world.flow.at(j) > best_flow
                    || (world.flow.at(j) == best_flow && depths[j] < best_depth))
                {
                    best = j;
                    best_flow = world.flow.at(j);
                    best_depth = depths[j];
                }
            }

            if (best == kNoNode)
            {
                break;  // 闭流死端
            }
            cur = best;
            ++steps;
        }

        if (!channel.nodes.empty())
        {
            result.channels.push_back(std::move(channel));
        }
    }
    return result;
}

/// 给少数地脉注入异质属性。
///
/// 异质不来自岩性——设定说「少数地脉夹带异质」，是地脉本身带着的。
/// 按通道整体判定，命中则整条通道的属性换成异质。整条换而不逐节点换，
/// 是为了保证「异质只整条通道出现」这条可断言的不变式。
void assign_hetero(TraceResult& trace, Pcg32& rng)
{
    constexpr std::array<Element, 7> kHeteroElements{
        Element::thunder, Element::ice,  Element::wind, Element::light,
        Element::dark,    Element::sound, Element::poison};

    for (const LeyChannel& channel : trace.channels)
    {
        if (channel.nodes.empty())
        {
            continue;
        }
        if (rng.unit() >= kHeteroChance)
        {
            continue;
        }
        const Element hetero =
            kHeteroElements[rng.below(static_cast<std::uint32_t>(
                kHeteroElements.size()))];
        for (const std::uint32_t index : channel.nodes)
        {
            trace.nodes[index].element = hetero;
        }
    }
}

/// 结算节点流量。
///
/// 节点图是一张 DAG：next 指针沿通道指向高程严格更低的节点，因此按
/// 高程降序处理时，每个节点被处理之前，它的全部上游都已把流量交给了它。
/// 这比逐通道结算省事，也不受通道创建顺序影响。
void settle_flows(const World& world, TraceResult& trace)
{
    const std::size_t n = trace.nodes.size();
    if (n == 0)
    {
        return;
    }

    std::vector<std::vector<std::uint32_t>> incoming(n);
    for (std::uint32_t v = 0; v < n; ++v)
    {
        if (trace.nodes[v].next != kNoNode)
        {
            incoming[trace.nodes[v].next].push_back(v);
        }
    }

    std::vector<std::uint32_t> order(n);
    for (std::uint32_t i = 0; i < n; ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&trace, &world](std::uint32_t a, std::uint32_t b) {
                  const std::uint8_t ea = world.elevation.at(trace.nodes[a].cell);
                  const std::uint8_t eb = world.elevation.at(trace.nodes[b].cell);
                  if (ea != eb)
                  {
                      return ea > eb;
                  }
                  return a < b;
              });

    for (const std::uint32_t u : order)
    {
        std::uint32_t inflow = 0;
        for (const std::uint32_t v : incoming[u])
        {
            inflow += trace.nodes[v].flow;
        }
        // flow 起手是本节点自己的贡献（kLeySourceFlow），加上上游汇来的。
        trace.nodes[u].flow += inflow;
    }
}

/// 把地脉图导出到栅格镜像。图是唯一的真相，镜像只写一次、只读。
void mirror_to_grids(World& world, const TraceResult& trace)
{
    for (const LeyNode& node : trace.nodes)
    {
        const std::uint32_t i = node.cell;
        world.ley_strength.at(i) = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(255u, node.flow));
        world.ley_depth.at(i) = static_cast<std::uint8_t>(node.depth);
        world.ley_element.at(i) = node.element;
    }
}

}  // namespace

void make_spirit_tide(World& world)
{
    world.spirit_tide = compute_spirit_tide(world.params().spirit_year);
}

void make_ley_network(World& world)
{
    const WorldParams& p = world.params();
    const std::vector<std::uint8_t> floor = compute_basin_floor(world);
    const std::vector<std::uint32_t> depths = compute_ley_depths(world, floor);

    // 通道数要有下界：阶段 13 的灵脉数量是硬约束，每条灵脉占一条通道，
    // 通道不够就切不出那么多段。地形平缓时按固定间距选不出这么多通道，
    // 就逐步放宽间距重选。绝大多数世界第一轮就够，长尾里的世界才重选。
    const std::uint32_t wanted = required_ley_channels(p.width * p.height);
    TraceResult trace;
    std::uint32_t best = 0;
    for (std::uint32_t spacing = kLeySourceSpacing; spacing >= 1; --spacing)
    {
        TraceResult relaxed = trace_channels(world, depths, spacing);
        if (relaxed.channels.size() > best)
        {
            best = relaxed.channels.size();
            trace = std::move(relaxed);
        }
        if (best >= wanted)
        {
            break;
        }
    }

    Pcg32 rng(stage_seed(world.params().seed,
                          static_cast<std::uint32_t>(Stream::ley)));
    assign_hetero(trace, rng);
    settle_flows(world, trace);
    mirror_to_grids(world, trace);

    world.ley_channels = std::move(trace.channels);
    world.ley_nodes = std::move(trace.nodes);
}

}  // namespace gamev2::world

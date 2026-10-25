#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::Element;
using gamev2::world::LeyChannel;
using gamev2::world::LeyNode;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::element_of_stone;
using gamev2::world::is_hetero;
using gamev2::world::kHeteroChance;
using gamev2::world::kMaxLeyDepth;
using gamev2::world::kMinLeyDepth;

WorldParams params_of(std::uint32_t width, std::uint32_t height,
                      std::uint64_t seed)
{
    WorldParams p;
    p.width = width;
    p.height = height;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

/// 沿 next 走到底，返回走过的步数。成环时步数会顶到上限（节点数 + 1）。
std::uint32_t walk_to_end(const World& w, std::uint32_t start)
{
    std::uint32_t steps = 0;
    std::uint32_t cur = start;
    const std::uint32_t limit =
        static_cast<std::uint32_t>(w.ley_nodes.size()) + 1u;
    while (w.ley_nodes[cur].next != gamev2::world::kNoNode
           && steps < limit)
    {
        cur = w.ley_nodes[cur].next;
        ++steps;
    }
    return steps;
}

TEST(ley_network_produces_channels_and_nodes)
{
    const WorldParams p = params_of(64, 64, 2026);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    CHECK(!w.ley_channels.empty());
    CHECK(!w.ley_nodes.empty());

    // 通道的节点序列必须真的存在，且首尾有序。
    for (const LeyChannel& c : w.ley_channels)
    {
        CHECK(!c.nodes.empty());
        CHECK(c.nodes.front() < w.ley_nodes.size());
        CHECK(c.nodes.back() < w.ley_nodes.size());
    }
}

TEST(ley_nodes_sit_on_land)
{
    const WorldParams p = params_of(64, 64, 77);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        // 地脉不出海：河口不是灵眼。
        CHECK(w.elevation.at(n.cell) >= w.sea_level());
    }
}

TEST(ley_next_always_points_downhill)
{
    const WorldParams p = params_of(64, 64, 5);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            const LeyNode& up = w.ley_nodes[c.nodes[i]];
            const LeyNode& down = w.ley_nodes[c.nodes[i + 1]];
            // 通道内相邻节点必须由 next 相连。只查「有 next 才断言」会在
            // next 缺失时整条空跑——那正是节点图退化成孤立点集时的样子。
            CHECK(up.next == c.nodes[i + 1]);
            // 高程严格更低是节点图无环的唯一保证。
            CHECK(w.elevation.at(down.cell) < w.elevation.at(up.cell));
            ++links;
        }
    }
    // 相邻性断言必须真的跑过，否则这条测试等于空跑。
    CHECK(links > 0);
}

TEST(ley_graph_is_acyclic)
{
    const WorldParams p = params_of(64, 64, 11);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            // 节点图的边必须沿通道铺开，否则「图」只是一堆孤立节点。
            CHECK(w.ley_nodes[c.nodes[i]].next == c.nodes[i + 1]);
            ++links;
        }
    }
    CHECK(links > 0);
    for (std::uint32_t i = 0; i < w.ley_nodes.size(); ++i)
    {
        // 走到底的步数必须小于节点总数，否则说明有环。
        CHECK(walk_to_end(w, i) < w.ley_nodes.size());
    }
}

TEST(ley_flow_grows_downstream)
{
    const WorldParams p = params_of(64, 64, 33);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t links = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        for (std::size_t i = 0; i + 1 < c.nodes.size(); ++i)
        {
            const LeyNode& up = w.ley_nodes[c.nodes[i]];
            const LeyNode& down = w.ley_nodes[c.nodes[i + 1]];
            // 通道相邻节点必须由 next 相连，否则无从谈「下游」。
            CHECK(up.next == c.nodes[i + 1]);
            // 下游承接了本节点的全部流量，还多出自己那份，因此严格更大。
            // 只断言「不少于」的话，流量恒为常量也能过——那正是
            // 「流量沿通道累加」被破坏时的样子。
            CHECK(down.flow > up.flow);
            ++links;
        }
    }
    CHECK(links > 0);
}

TEST(ley_depth_stays_inside_its_bounds)
{
    const WorldParams p = params_of(64, 64, 91);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        CHECK(n.depth >= kMinLeyDepth);
        CHECK(n.depth <= kMaxLeyDepth);
    }
}

TEST(ley_element_comes_from_the_rock_it_passes_through)
{
    const WorldParams p = params_of(64, 64, 404);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const LeyNode& n : w.ley_nodes)
    {
        const auto z = std::min<std::uint32_t>(p.z_levels - 1u,
                                                n.depth / p.z_cell_depth);
        const Element from_rock = element_of_stone(w.stone[z].at(n.cell));
        // 属性要么就是岩性给的，要么是整条通道被注入的异质。
        CHECK(n.element == from_rock || is_hetero(n.element));
    }
}

TEST(hetero_elements_cover_whole_channels_only)
{
    // fixture 选 128x128/seed 43：实测 33 条通道里 8 条带异质，覆盖面够。
    const WorldParams p = params_of(128, 128, 43);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t hetero_channels = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        Element hetero = Element::none;
        for (const std::uint32_t index : c.nodes)
        {
            const Element e = w.ley_nodes[index].element;
            if (!is_hetero(e))
            {
                continue;
            }
            if (hetero == Element::none)
            {
                hetero = e;
                continue;
            }
            // (a) 同一条通道内不得出现两种不同的异质元素。
            CHECK(e == hetero);
        }
        if (hetero == Element::none)
        {
            // 非异质通道的属性逐节点由岩性给出，同一条通道上五行不同
            // 是正常的（设计文档 §3.2），不必强求一致。
            continue;
        }
        ++hetero_channels;
        // (b) 一旦出现异质，该通道所有节点都必须是它。
        for (const std::uint32_t index : c.nodes)
        {
            CHECK(w.ley_nodes[index].element == hetero);
        }
    }
    // 守卫：fixture 一旦不再产出异质通道，上面两条断言就整条空跑，
    // 这时测试必须变红，而不是静默通过。
    CHECK(hetero_channels > 0);
}

TEST(hetero_channels_stay_a_small_fraction)
{
    // fixture 选 128x128/seed 97：实测 34 条通道里 7 条带异质，
    // 占比 7/34 在上界之内，且离「一半」还有余量。
    const WorldParams p = params_of(128, 128, 97);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::size_t hetero = 0;
    for (const LeyChannel& c : w.ley_channels)
    {
        if (is_hetero(w.ley_nodes[c.nodes.front()].element))
        {
            ++hetero;
        }
    }
    // 守卫：没有异质通道时，占比断言恒真，等于空跑。
    CHECK(hetero > 0);
    // 概率是 8%，实现在一个通道上整体判定。上界给到一半已经宽松到
    // 不可能被运气突破：真的占一半说明判定逻辑坏了。
    CHECK(hetero * 2 <= w.ley_channels.size());
}

TEST(ley_mirror_matches_the_graph)
{
    const WorldParams p = params_of(64, 64, 606);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t mirrored = 0;
    for (std::uint32_t i = 0; i < w.ley_strength.size(); ++i)
    {
        if (w.ley_strength.at(i) == 0)
        {
            continue;
        }
        ++mirrored;
        CHECK(w.ley_depth.at(i) >= kMinLeyDepth);
        CHECK(w.ley_depth.at(i) <= kMaxLeyDepth);
    }
    // 镜像上非零的格数不得超过节点数（节点可能共享汇流点，
    // 但一个格子最多一个节点）。
    CHECK(mirrored <= w.ley_nodes.size());
    // 逐节点比对：镜像的每个值都必须等于图上的对应值。
    for (const LeyNode& n : w.ley_nodes)
    {
        // flow 是 uint32，镜像是 uint8：显式转换，别让隐式窄化混过去。
        const auto expected_strength = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(255u, n.flow));
        CHECK_EQ(w.ley_strength.at(n.cell), expected_strength);
        CHECK_EQ(w.ley_depth.at(n.cell), static_cast<std::uint8_t>(n.depth));
        CHECK(w.ley_element.at(n.cell) == n.element);
    }
}

TEST(ley_layer_is_deterministic)
{
    const WorldParams p = params_of(48, 48, 1234);
    const auto a = gamev2::world::generate_world(p);
    const auto b = gamev2::world::generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    CHECK_EQ(a->ley_nodes.size(), b->ley_nodes.size());
    for (std::size_t i = 0; i < a->ley_nodes.size(); ++i)
    {
        CHECK_EQ(a->ley_nodes[i].cell, b->ley_nodes[i].cell);
        CHECK_EQ(a->ley_nodes[i].depth, b->ley_nodes[i].depth);
        CHECK_EQ(a->ley_nodes[i].flow, b->ley_nodes[i].flow);
        CHECK_EQ(a->ley_nodes[i].element, b->ley_nodes[i].element);
    }
    for (std::uint32_t i = 0; i < a->ley_strength.size(); ++i)
    {
        CHECK_EQ(a->ley_strength.at(i), b->ley_strength.at(i));
        CHECK_EQ(a->ley_element.at(i), b->ley_element.at(i));
    }
}

}  // namespace

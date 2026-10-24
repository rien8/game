#include "gamev2/world/enums.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace {

using gamev2::world::kMinCavernVolume;
using gamev2::world::World;
using gamev2::world::WorldParams;

World build(std::uint64_t seed = 33)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = 6;
    p.seed = seed;
    World w(p);
    gamev2::world::make_caverns(w);
    return w;
}

/// 地下空腔的连通域规模，按升序返回。
std::vector<std::size_t> cave_component_sizes(const World& w)
{
    const auto width = w.params().width;
    const auto height = w.params().height;
    const auto levels = w.params().z_levels;
    const std::size_t layer = static_cast<std::size_t>(width) * height;
    std::vector<bool> seen(static_cast<std::size_t>(levels) * layer, false);
    std::vector<std::size_t> sizes;

    constexpr int kDx6[6] = {1, -1, 0, 0, 0, 0};
    constexpr int kDy6[6] = {0, 0, 1, -1, 0, 0};
    constexpr int kDz6[6] = {0, 0, 0, 0, 1, -1};

    for (std::uint32_t z = 1; z < levels; ++z)
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::uint32_t i = w.cavern[z].index_of(x, y);
                if (!w.cavern[z].at(i)
                    || seen[static_cast<std::size_t>(z) * layer + i])
                {
                    continue;
                }
                std::size_t size = 0;
                std::deque<std::array<int, 3>> queue;
                queue.push_back({static_cast<int>(x), static_cast<int>(y),
                                 static_cast<int>(z)});
                seen[static_cast<std::size_t>(z) * layer + i] = true;
                while (!queue.empty())
                {
                    const auto c = queue.front();
                    queue.pop_front();
                    ++size;
                    for (int d = 0; d < 6; ++d)
                    {
                        const int nx = c[0] + kDx6[d];
                        const int ny = c[1] + kDy6[d];
                        const int nz = c[2] + kDz6[d];
                        if (nz < 1 || nz >= static_cast<int>(levels) || nx < 0
                            || ny < 0 || nx >= static_cast<int>(width)
                            || ny >= static_cast<int>(height))
                        {
                            continue;
                        }
                        const std::uint32_t j = w.cavern[static_cast<std::size_t>(
                            nz)].index_of(static_cast<std::uint32_t>(nx),
                                          static_cast<std::uint32_t>(ny));
                        const std::size_t key =
                            static_cast<std::size_t>(nz) * layer + j;
                        if (w.cavern[static_cast<std::size_t>(nz)].at(j)
                            && !seen[key])
                        {
                            seen[key] = true;
                            queue.push_back({nx, ny, nz});
                        }
                    }
                }
                sizes.push_back(size);
            }
        }
    }
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}

TEST(caverns_exist_underground_and_never_at_the_surface)
{
    const World w = build();
    std::uint32_t cells = 0;
    bool surface_clean = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.cavern[z].size(); ++i)
        {
            if (!w.cavern[z].at(i))
            {
                continue;
            }
            ++cells;
            if (z == 0)
            {
                surface_clean = false;
            }
        }
    }
    CHECK(cells > 0);
    CHECK(surface_clean);
}

TEST(caverns_differ_between_seeds)
{
    // 洞穴阶段必须吃进种子：两个不同 seed 的世界不能有一样的洞穴。
    const World a = build(33);
    const World b = build(34);
    bool same = true;
    for (std::uint32_t z = 0; z < a.cavern.size(); ++z)
    {
        for (std::uint32_t i = 0; i < a.cavern[z].size(); ++i)
        {
            same = same && a.cavern[z].at(i) == b.cavern[z].at(i);
        }
    }
    CHECK(!same);
}

TEST(caverns_are_deterministic)
{
    const World a = build();
    const World b = build();
    bool same = true;
    for (std::uint32_t z = 0; z < a.cavern.size(); ++z)
    {
        for (std::uint32_t i = 0; i < a.cavern[z].size(); ++i)
        {
            same = same && a.cavern[z].at(i) == b.cavern[z].at(i);
        }
    }
    CHECK(same);
}

TEST(underground_open_fraction_matches_the_designed_cave_ratio)
{
    // 设计文档 5.8 写死了四个数值：占据率 0.45、迭代 4 次、阈值 >=5 与 <=2。
    // 这条断言用它们的后果（地下空腔占比）把四个数一起钉住。
    //
    // 用 8 个 seed 而不是 1 个是有原因的：单 seed 时「迭代改成 10」被拦下靠
    // 的是 seed 33 恰好落在窗外（0.6872），换个 seed 就漏——8 个 seed 里有
    // 4 个落在窗内。要求全部 8 个 seed 都在窗内，这条拦截就不再依赖挑哪个
    // seed：「迭代 10」有 4 个 seed 在窗外，怎么挑都会红。
    //
    // 窗口是按当前常量实测出来的：基线 8 个 seed 是 0.6238–0.6535。将来调
    // `kCavernIterations` / `kCavernFill` / 阈值时，必须连同窗口一起重新推导。
    for (std::uint64_t seed = 33; seed <= 40; ++seed)
    {
        const World w = build(seed);
        std::uint32_t open = 0;
        std::uint32_t total = 0;
        for (std::uint32_t z = 1; z < w.params().z_levels; ++z)
        {
            for (std::uint32_t i = 0; i < w.cavern[z].size(); ++i)
            {
                ++total;
                open += w.cavern[z].at(i) ? 1u : 0u;
            }
        }
        CHECK(total > 0);
        CHECK(open * 100 > total * 60);
        CHECK(open * 100 < total * 68);
    }
}

TEST(no_cave_fragment_survives_the_cleanup)
{
    // 设计文档 5.8 第 3 条：碎屑清理之后不该再有小于阈值的空腔。
    const World w = build();
    const std::vector<std::size_t> sizes = cave_component_sizes(w);
    CHECK(!sizes.empty());

    std::size_t total = 0;
    for (const std::size_t s : sizes)
    {
        total += s;
    }
    CHECK(sizes.front() >= kMinCavernVolume);
    // 成片的洞室：主洞室占绝大多数空腔体素，而不是一堆散碎的空腔。
    CHECK(sizes.back() * 10 >= total * 9);
}

TEST(a_single_cell_world_stays_well_formed)
{
    // 退化输入：1x1 的世界，唯一那个地下格子是个碎片，碎屑清理会把它填实；
    // 地表层永远不空。
    WorldParams p;
    p.width = 1;
    p.height = 1;
    p.z_levels = 2;
    p.seed = 5;
    World w(p);
    gamev2::world::make_caverns(w);
    CHECK(!w.cavern[0].at(0));
    CHECK(!w.cavern[1].at(0));
}

}  // namespace

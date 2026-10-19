#include "gamev2/world/enums.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

/// 随机流编号全局唯一（设计文档 4.1）：同一个编号只能被一条随机流占用，
/// 否则两个阶段会从同一条流上取数，派生 seed 相同。
constexpr double kCavernFill = 0.45;
constexpr int kCavernIterations = 4;

constexpr int kDx6[6] = {1, -1, 0, 0, 0, 0};
constexpr int kDy6[6] = {0, 0, 1, -1, 0, 0};
constexpr int kDz6[6] = {0, 0, 0, 0, 1, -1};

struct Cell
{
    std::int64_t x;
    std::int64_t y;
    std::int64_t z;
};

/// 实心邻居的个数。设计文档 5.8 的元胞自动机按邻居计数分档，所以这里必须
/// 返回计数而不是布尔——返回布尔会让「>= 5」与「<= 2」同时成立。
std::int64_t solid_neighbours(const World& world, Cell c)
{
    const WorldParams& p = world.params();
    std::int64_t n = 0;
    for (int d = 0; d < 6; ++d)
    {
        const auto x = c.x + kDx6[d];
        const auto y = c.y + kDy6[d];
        const auto z = c.z + kDz6[d];
        if (z < 0 || z >= static_cast<std::int64_t>(p.z_levels))
        {
            continue;
        }
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(p.width)
            || y >= static_cast<std::int64_t>(p.height))
        {
            continue;
        }
        const std::size_t zz = static_cast<std::size_t>(z);
        const std::uint32_t i = world.cavern[zz].index_of(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        n += world.cavern[zz].at(i) ? 0 : 1;
    }
    return n;
}

/// 洪泛标记空腔连通域，剔除体素数不足的碎屑。
void drop_fragments(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<bool> seen(p.z_levels * n, false);
    std::vector<Cell> component;
    std::deque<Cell> queue;

    for (std::uint32_t z = 1; z < p.z_levels; ++z)
    {
        for (std::uint32_t y = 0; y < p.height; ++y)
        {
            for (std::uint32_t x = 0; x < p.width; ++x)
            {
                const std::uint32_t i = world.cavern[z].index_of(x, y);
                if (!world.cavern[z].at(i)
                    || seen[static_cast<std::size_t>(z) * n + i])
                {
                    continue;
                }
                component.clear();
                queue.clear();
                queue.push_back({x, y, z});
                seen[static_cast<std::size_t>(z) * n + i] = true;
                while (!queue.empty())
                {
                    const Cell c = queue.front();
                    queue.pop_front();
                    component.push_back(c);
                    for (int d = 0; d < 6; ++d)
                    {
                        const auto nx = c.x + kDx6[d];
                        const auto ny = c.y + kDy6[d];
                        const auto nz = c.z + kDz6[d];
                        if (nz < 1
                            || nz >= static_cast<std::int64_t>(p.z_levels)
                            || nx < 0 || ny < 0
                            || nx >= static_cast<std::int64_t>(p.width)
                            || ny >= static_cast<std::int64_t>(p.height))
                        {
                            continue;
                        }
                        const std::size_t zz = static_cast<std::size_t>(nz);
                        const std::uint32_t j = world.cavern[zz].index_of(
                            static_cast<std::uint32_t>(nx),
                            static_cast<std::uint32_t>(ny));
                        const std::size_t key = static_cast<std::size_t>(nz) * n + j;
                        if (world.cavern[zz].at(j) && !seen[key])
                        {
                            seen[key] = true;
                            queue.push_back({nx, ny, nz});
                        }
                    }
                }
                if (component.size() < kMinCavernVolume)
                {
                    for (const Cell c : component)
                    {
                        const std::size_t cz = static_cast<std::size_t>(c.z);
                        const std::uint32_t j = world.cavern[cz].index_of(
                            static_cast<std::uint32_t>(c.x),
                            static_cast<std::uint32_t>(c.y));
                        world.cavern[cz].at(j) = false;
                    }
                }
            }
        }
    }
}

}  // namespace

void make_caverns(World& world)
{
    const WorldParams& p = world.params();

    Pcg32 rng(stage_seed(p.seed, static_cast<std::uint32_t>(Stream::cavern)));
    for (std::uint32_t z = 1; z < p.z_levels; ++z)
    {
        for (std::uint32_t y = 0; y < p.height; ++y)
        {
            for (std::uint32_t x = 0; x < p.width; ++x)
            {
                const std::uint32_t i = world.cavern[z].index_of(x, y);
                world.cavern[z].at(i) = rng.unit() < kCavernFill;
            }
        }
    }

    // 元胞自动机把随机点磨成成片的洞室：实心邻居 >= 5 则转实心，实心邻居
    // <= 2 则转空，3~4 个之间保持原样。世界外的邻居不计入实心，所以贴边
    // 与最深层天花板的格子邻居数天然偏少，更容易被判成空腔。
    for (int iter = 0; iter < kCavernIterations; ++iter)
    {
        auto next = world.cavern;
        for (std::uint32_t z = 1; z < p.z_levels; ++z)
        {
            for (std::uint32_t y = 0; y < p.height; ++y)
            {
                for (std::uint32_t x = 0; x < p.width; ++x)
                {
                    const std::uint32_t i = world.cavern[z].index_of(x, y);
                    const std::int64_t n = solid_neighbours(world, {x, y, z});
                    bool cell = world.cavern[z].at(i);
                    if (n >= 5)
                    {
                        cell = false;
                    }
                    else if (n <= 2)
                    {
                        cell = true;
                    }
                    next[z].at(i) = cell;
                }
            }
        }
        world.cavern = std::move(next);
    }

    drop_fragments(world);
}

}  // namespace gamev2::world

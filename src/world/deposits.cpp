#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <span>

namespace gamev2::world {
namespace {

/// 三个全局尺寸档（设计文档 5.9）：小 3-6、中 10-20、大 30-60 体素。
/// allow_large 是「允许」而不是「强制」：禁大脉的矿种一律不落大档。
std::uint32_t vein_cells(bool allow_large, Pcg32& rng)
{
    const std::uint32_t roll = rng.below(10);
    if (allow_large && roll >= 7)
    {
        return 30 + rng.below(31);
    }
    if (roll >= 3)
    {
        return 10 + rng.below(11);
    }
    return 3 + rng.below(4);
}

/// 游走方向与权重。水平方向权重 2、垂直方向权重 1：矿脉因此摊成横向拉长
/// 的团块而不是球，这是设计文档 5.9 要求的「各向异性 3D blob」。
constexpr int kVeinDx[6] = {1, -1, 0, 0, 0, 0};
constexpr int kVeinDy[6] = {0, 0, 1, -1, 0, 0};
constexpr int kVeinDz[6] = {0, 0, 0, 0, 1, -1};
constexpr int kVeinDirWeight[6] = {2, 2, 2, 2, 1, 1};
constexpr int kVeinDirWeightSum = 10;

/// 矿脉团块的外接球半径按目标体素数反解，再放宽一格：目标体素数少时游走
/// 空间太小，容易在够不到母岩的地方耗尽步数。
constexpr int kVeinRadiusSlack = 1;

/// 种子搜索半径。撒点仍是均匀随机的中心点，但地下约 65% 是洞室，中心点
/// 落在洞室上的概率很高；螺旋扫描把种子吸附到附近的合法母岩格上。
constexpr int kSeedRadius = 3;

/// 每画一格允许的额外步数上限。游走只踩合法母岩格之后，画格数单调增长，
/// 这个上限只在游走反复落回「已被别的矿脉占用」的格子时才咬得住——那是
/// 裁定 G（不覆盖别的矿脉）的必然代价。它是安全阀，不是主要约束。
constexpr std::uint32_t kVeinStepSlack = 24;

int pick_vein_direction(Pcg32& rng)
{
    int roll = static_cast<int>(
        rng.below(static_cast<std::uint32_t>(kVeinDirWeightSum)));
    for (int d = 0; d < 6; ++d)
    {
        roll -= kVeinDirWeight[d];
        if (roll < 0)
        {
            return d;
        }
    }
    return 0;
}

/// 由目标体素数反解等效球半径。
int radius_for(std::uint32_t cells)
{
    const double r =
        std::cbrt(static_cast<double>(cells) * 3.0 / (4.0 * std::numbers::pi));
    return std::max(1, static_cast<int>(std::ceil(r)));
}

/// 该格能否作为矿脉的一部分：不是洞室、母岩匹配、深度在 [1, max_z]、
/// 且在地图范围内。矿脉的每一个体素都得是合法母岩，否则尺寸档无从谈起。
bool is_valid_host(const World& world, int x, int y, int z, std::uint32_t max_z,
                   std::span<const Stone> hosts)
{
    const WorldParams& p = world.params();
    // 世界边界与矿脉深度上限是两条独立的约束：前者防止越界访问 stone 网格，
    // 后者是设计文档的「只到这个 z 层为止」。分开写，改动其中一条不会牵连
    // 另一条。
    if (z < 1 || z >= static_cast<int>(p.z_levels))
    {
        return false;
    }
    if (z > static_cast<int>(max_z))
    {
        return false;
    }
    if (x < 0 || y < 0 || x >= static_cast<int>(p.width)
        || y >= static_cast<int>(p.height))
    {
        return false;
    }
    const std::size_t zz = static_cast<std::size_t>(z);
    const std::uint32_t i = world.stone[zz].index_of(
        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (world.cavern[zz].at(i))
    {
        return false;
    }
    return std::ranges::find(hosts, world.stone[zz].at(i)) != hosts.end();
}

/// 在撒点附近做固定顺序的螺旋扫描，找一颗能当矿脉种子的格。切比雪夫距离
/// 由近到远、同层按 dz/dy/dx 升序，因此扫描不消耗随机数：除矿脉层之外，
/// 世界逐格不变。
bool find_seed_cell(const World& world, std::uint32_t cx, std::uint32_t cy,
                    std::uint32_t cz, std::uint32_t max_z,
                    std::span<const Stone> hosts, int& ox, int& oy, int& oz)
{
    for (int r = 0; r <= kSeedRadius; ++r)
    {
        for (int dz = -r; dz <= r; ++dz)
        {
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    if (r > 0 && std::abs(dx) != r && std::abs(dy) != r
                        && std::abs(dz) != r)
                    {
                        continue;
                    }
                    const int x = static_cast<int>(cx) + dx;
                    const int y = static_cast<int>(cy) + dy;
                    const int z = static_cast<int>(cz) + dz;
                    if (!is_valid_host(world, x, y, z, max_z, hosts))
                    {
                        continue;
                    }
                    // 孤立格长不大：种子至少要有一个同样合法的邻居。
                    bool has_neighbour = false;
                    for (int d = 0; d < 6 && !has_neighbour; ++d)
                    {
                        has_neighbour = is_valid_host(
                            world, x + kVeinDx[d], y + kVeinDy[d],
                            z + kVeinDz[d], max_z, hosts);
                    }
                    if (!has_neighbour)
                    {
                        continue;
                    }
                    ox = x;
                    oy = y;
                    oz = z;
                    return true;
                }
            }
        }
    }
    return false;
}

/// 随机游走生长矿脉。设计文档 5.9：「各向异性 3D blob 加随机游走填充」。
/// 游走只在合法母岩格之间进行，洞室与错岩因此不再吞掉步数，尺寸档才真的
/// 落在目标区间里。已被别的矿脉占用的格子可以经过（照旧不画矿），但画
/// 不到那里去。max_z 是硬上限：矿脉不得越过它。
void fill_vein(World& world, Deposit kind, std::uint32_t cx, std::uint32_t cy,
               std::uint32_t cz, std::uint32_t cells, std::uint32_t max_z,
               Pcg32& rng, std::span<const Stone> hosts)
{
    int x = 0;
    int y = 0;
    int z = 0;
    if (!find_seed_cell(world, cx, cy, cz, max_z, hosts, x, y, z))
    {
        return;
    }
    const int sx = x;
    const int sy = y;
    const int sz = z;
    const int radius = radius_for(cells) + kVeinRadiusSlack;
    const std::uint32_t budget = cells * kVeinStepSlack + 64;
    std::uint32_t painted = 0;

    for (std::uint32_t step = 0; step < budget && painted < cells; ++step)
    {
        const std::size_t zz = static_cast<std::size_t>(z);
        const std::uint32_t i = world.stone[zz].index_of(
            static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        if (world.deposit[zz].at(i) == Deposit::none)
        {
            world.deposit[zz].at(i) = kind;
            ++painted;
        }

        // 每步挑一个加权随机的方向，目标格不合法就换一个方向试：洞室与
        // 错岩不再消耗步数。六个方向都不行说明这条矿脉长到头了。
        bool moved = false;
        for (int attempt = 0; attempt < 6 && !moved; ++attempt)
        {
            const int d = pick_vein_direction(rng);
            const int nx = x + kVeinDx[d];
            const int ny = y + kVeinDy[d];
            const int nz = z + kVeinDz[d];
            if (!is_valid_host(world, nx, ny, nz, max_z, hosts))
            {
                continue;
            }
            // 游走被限制在种子周围的球里，否则矿脉会拖成一条长蛇而不是
            // 设计文档 5.9 要的那个团块。
            const int dx = nx - sx;
            const int dy = ny - sy;
            const int dz = nz - sz;
            if (dx * dx + dy * dy + dz * dz > radius * radius)
            {
                continue;
            }
            x = nx;
            y = ny;
            z = nz;
            moved = true;
        }
        if (!moved)
        {
            break;
        }
    }
}

}  // namespace

void make_deposits(World& world)
{
    const WorldParams& p = world.params();
    if (p.z_levels < 2)
    {
        return;
    }

    Pcg32 rng(stage_seed(p.seed, static_cast<std::uint32_t>(Stream::deposit)));
    for (const auto& spec : kDepositSpecs)
    {
        const auto max_z = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(spec.max_z), p.z_levels - 1);
        for (std::uint32_t n = 0; n < spec.vein_count; ++n)
        {
            // rng 的抽取顺序必须写死：函数实参的求值顺序在 C++ 里是未指定
            // 的，把多次 rng.below() 塞进同一个实参列表会让「同 seed 同世界」
            // 在跨工具链时失效。
            const std::uint32_t cx = rng.below(p.width);
            const std::uint32_t cy = rng.below(p.height);
            const std::uint32_t cz = 1 + rng.below(max_z);
            const std::uint32_t cells = vein_cells(spec.allow_large, rng);
            fill_vein(world, spec.kind, cx, cy, cz, cells, max_z, rng,
                      spec.hosts);
        }
    }
}

void finalize_aquifers(World& world)
{
    const WorldParams& p = world.params();
    // generate_world 已经按设计文档 6 拒绝 z_cell_depth == 0，但本函数是
    // generate.hpp 里的公开声明，测试会直接调用它，不该在这里硬崩。
    if (p.z_cell_depth == 0)
    {
        return;
    }
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.aquifer_top.index_of(x, y);
            const auto top = static_cast<std::uint32_t>(world.aquifer_top.at(i));
            if (top == 0)
            {
                continue;
            }
            // 含水层是逐格深度的场，但存储按 z 层，因此取顶面所在的那一层
            // 作为起点，含水层自该层整体开始。向下取整而不是向上：顶面最高
            // 可到 depth-1，向上取整会让那一批列完全没有含水层。
            const auto first =
                static_cast<std::uint32_t>(top / p.z_cell_depth);
            for (std::uint32_t z = first; z < p.z_levels; ++z)
            {
                world.aquifer[z].at(i) = !world.cavern[z].at(i);
            }
        }
    }
}

}  // namespace gamev2::world

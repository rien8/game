#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using gamev2::world::Deposit;
using gamev2::world::DepositSpec;
using gamev2::world::kDepositSpecs;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;

World build(std::uint64_t seed = 77, std::uint32_t z_levels = 6)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = z_levels;
    p.seed = seed;
    World w(p);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_soils(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_caverns(w);
    gamev2::world::make_deposits(w);
    gamev2::world::finalize_aquifers(w);
    return w;
}

const DepositSpec* spec_of(Deposit kind)
{
    for (const auto& spec : kDepositSpecs)
    {
        if (spec.kind == kind)
        {
            return &spec;
        }
    }
    return nullptr;
}

/// 洪泛用的 6 邻域方向。
constexpr int kClusterDx[6] = {1, -1, 0, 0, 0, 0};
constexpr int kClusterDy[6] = {0, 0, 1, -1, 0, 0};
constexpr int kClusterDz[6] = {0, 0, 0, 0, 1, -1};

/// 矿脉连通团块的统计。计数语义：按 6-邻域洪泛所有非 none 的矿脉格，
/// 不区分矿种——相邻的同种矿脉会并成一个团块。
struct ClusterStats
{
    std::uint32_t clusters = 0;
    std::uint32_t tiny = 0;
    std::uint32_t cells = 0;
};

ClusterStats cluster_stats(const World& w)
{
    const WorldParams& p = w.params();
    const std::size_t layer = static_cast<std::size_t>(p.width) * p.height;
    std::vector<char> seen(layer * p.z_levels, 0);
    ClusterStats stats;

    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t y = 0; y < p.height; ++y)
        {
            for (std::uint32_t x = 0; x < p.width; ++x)
            {
                const std::size_t start =
                    (static_cast<std::size_t>(z) * p.height + y) * p.width + x;
                if (seen[start] != 0
                    || w.deposit[z].at(w.deposit[z].index_of(x, y))
                           == Deposit::none)
                {
                    continue;
                }
                std::vector<std::size_t> stack{start};
                seen[start] = 1;
                std::uint32_t size = 0;
                while (!stack.empty())
                {
                    const std::size_t cur = stack.back();
                    stack.pop_back();
                    ++size;
                    const std::uint32_t cz =
                        static_cast<std::uint32_t>(cur / layer);
                    const std::uint32_t cy =
                        static_cast<std::uint32_t>((cur / p.width) % p.height);
                    const std::uint32_t cx =
                        static_cast<std::uint32_t>(cur % p.width);
                    for (int d = 0; d < 6; ++d)
                    {
                        const int tx = static_cast<int>(cx) + kClusterDx[d];
                        const int ty = static_cast<int>(cy) + kClusterDy[d];
                        const int tz = static_cast<int>(cz) + kClusterDz[d];
                        if (tx < 0 || ty < 0 || tz < 0
                            || tx >= static_cast<int>(p.width)
                            || ty >= static_cast<int>(p.height)
                            || tz >= static_cast<int>(p.z_levels))
                        {
                            continue;
                        }
                        const std::size_t next =
                            (static_cast<std::size_t>(tz) * p.height
                             + static_cast<std::uint32_t>(ty))
                                * p.width
                            + static_cast<std::uint32_t>(tx);
                        if (seen[next] != 0
                            || w.deposit[static_cast<std::uint32_t>(tz)]
                                   .at(w.deposit[static_cast<std::uint32_t>(tz)]
                                           .index_of(
                                               static_cast<std::uint32_t>(tx),
                                               static_cast<std::uint32_t>(ty)))
                                   == Deposit::none)
                        {
                            continue;
                        }
                        seen[next] = 1;
                        stack.push_back(next);
                    }
                }
                ++stats.clusters;
                stats.cells += size;
                if (size < 3)
                {
                    ++stats.tiny;
                }
            }
        }
    }
    return stats;
}

TEST(deposits_exist_and_never_sit_inside_a_cavern)
{
    const World w = build();
    std::uint32_t cells = 0;
    bool clear_of_caverns = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            if (w.deposit[z].at(i) == Deposit::none)
            {
                continue;
            }
            ++cells;
            clear_of_caverns = clear_of_caverns && !w.cavern[z].at(i);
        }
    }
    CHECK(cells > 0);
    CHECK(clear_of_caverns);
}

TEST(every_deposit_honours_its_host_rock)
{
    const World w = build();
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            const Deposit kind = w.deposit[z].at(i);
            const DepositSpec* spec = spec_of(kind);
            if (spec == nullptr)
            {
                continue;
            }
            const Stone host = w.stone[z].at(i);
            ok = ok
                 && (std::ranges::find(spec->hosts.begin(), spec->hosts.end(),
                                       host)
                     != spec->hosts.end());
        }
    }
    CHECK(ok);
}

TEST(deposits_stay_underground)
{
    const World w = build();
    bool below_surface = true;
    for (std::uint32_t i = 0; i < w.deposit[0].size(); ++i)
    {
        below_surface = below_surface && w.deposit[0].at(i) == Deposit::none;
    }
    CHECK(below_surface);
}

TEST(deposits_respect_their_spec_depth_limit)
{
    // max_z 是「只到这个 z 层为止」。锡 / 铅 / 锌的 max_z 是 4，它们不得
    // 出现在 z=5。
    const World w = build();
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            const DepositSpec* spec = spec_of(w.deposit[z].at(i));
            if (spec == nullptr)
            {
                continue;
            }
            ok = ok && z <= static_cast<std::uint32_t>(spec->max_z);
        }
    }
    CHECK(ok);
}

TEST(aquifers_sit_below_their_top_and_outside_caverns)
{
    const World w = build();
    bool ok = true;
    std::uint32_t cells = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.aquifer_top.index_of(x, y);
            const auto top = static_cast<std::uint32_t>(w.aquifer_top.at(i));
            const auto first = top == 0 ? 0u : top / w.params().z_cell_depth;
            for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
            {
                if (!w.aquifer[z].at(i))
                {
                    continue;
                }
                ++cells;
                ok = ok && top != 0 && z >= first;
                ok = ok && !w.cavern[z].at(i);
            }
        }
    }
    CHECK(cells > 0);
    CHECK(ok);
}

TEST(aquifers_absent_where_the_top_is_zero)
{
    const World w = build();
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.aquifer[z].size(); ++i)
        {
            if (w.aquifer[z].at(i))
            {
                ok = ok && w.aquifer_top.at(i) != 0;
            }
        }
    }
    CHECK(ok);
}

TEST(deposits_respect_their_spec_depth_limit_in_a_synthetic_world)
{
    // 合成世界：整片都是砂岩（锡/铅/锌的母岩），洞室全空。
    // 96x96x6 的真实世界里 z=4、z=5 没有任何沉积母岩，深度判据在那边恒真；
    // 这里让母岩铺满，判据才真的活着。
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = 6;
    p.seed = 77;
    World w(p);
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.stone[z].size(); ++i)
        {
            w.stone[z].at(i) = Stone::sandstone;
        }
    }
    gamev2::world::make_deposits(w);
    bool ok = true;
    for (std::uint32_t z = 0; z < w.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            const DepositSpec* spec = spec_of(w.deposit[z].at(i));
            if (spec == nullptr)
            {
                continue;
            }
            ok = ok && z <= static_cast<std::uint32_t>(spec->max_z);
        }
    }
    CHECK(ok);
}

TEST(aquifers_start_at_the_layer_that_holds_their_top)
{
    // 顶面 20 格深 -> 起算层 floor(20/16) = 1：z=0 必须干，z>=1 必须湿。
    // 现有用例只断言「不在起算层之上」，向上取整也能过；这条把方向钉死。
    WorldParams p;
    p.width = 16;
    p.height = 16;
    p.z_levels = 6;
    p.seed = 77;
    World w(p);
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        w.aquifer_top.at(i) = 20;
    }
    gamev2::world::finalize_aquifers(w);
    bool dry_above = true;
    bool wet_below = true;
    for (std::uint32_t i = 0; i < w.aquifer[0].size(); ++i)
    {
        dry_above = dry_above && !w.aquifer[0].at(i);
    }
    for (std::uint32_t z = 1; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.aquifer[z].size(); ++i)
        {
            wet_below = wet_below && w.aquifer[z].at(i);
        }
    }
    CHECK(dry_above);
    CHECK(wet_below);
}

TEST(deposits_never_overwrite_another_vein)
{
    // 合成世界：整片砂岩（锡/铅/锌/煤的母岩），每格预填成金矿脉。
    // 裁定 G 要求后放的矿种不覆盖先放的，因此没有任何一格该被改写。
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.z_levels = 6;
    p.seed = 77;

    World control(p);
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < control.stone[z].size(); ++i)
        {
            control.stone[z].at(i) = Stone::sandstone;
        }
    }
    gamev2::world::make_deposits(control);
    // 对照组证明这个世界确实会落矿，否则下面那条断言就是空的。
    std::uint32_t control_cells = 0;
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < control.deposit[z].size(); ++i)
        {
            if (control.deposit[z].at(i) != Deposit::none)
            {
                ++control_cells;
            }
        }
    }
    CHECK(control_cells > 0);

    World w(p);
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.stone[z].size(); ++i)
        {
            w.stone[z].at(i) = Stone::sandstone;
        }
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            w.deposit[z].at(i) = Deposit::gold;
        }
    }
    gamev2::world::make_deposits(w);
    bool untouched = true;
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
        {
            untouched = untouched && w.deposit[z].at(i) == Deposit::gold;
        }
    }
    CHECK(untouched);
}

TEST(veins_hit_their_size_bands)
{
    // 代理断言：设计文档 5.9 的三个尺寸档在 world.deposit 里不可直接观测
    // （同种矿脉会并块），因此用两个从 world.deposit 直接可数的代理量。
    //
    // 1) 碎屑率：设计文档的最小档是 3 格，因此 1-2 格的连通团块必定是
    //    未达标的碎屑。相邻矿脉并块只会让碎片更少被计入，方向保守。
    //    修复后 5 个 seed 实测 8.3%-10.9%，修复前实测 43.7%-52.5%，
    //    阈值取 20%：两端各留 9 个百分点以上的余量。
    // 2) 落空率：每个撒点至少要落 2 格。种子吸附不成立时，大半撒点落在
    //    洞室上，矿脉根本没开始，实测只剩 1.1-1.3 格/撒点（修复后是
    //    3.5-5.2 格/撒点）。
    constexpr double kMaxTinyRatio = 0.20;
    constexpr double kMinCellsPerAttempt = 2.0;
    constexpr std::uint64_t kSeeds[] = {33, 77, 101, 202, 303};

    std::uint32_t attempts = 0;
    for (const auto& spec : kDepositSpecs)
    {
        attempts += spec.vein_count;
    }
    CHECK(attempts > 0);

    for (const std::uint64_t seed : kSeeds)
    {
        const World w = build(seed);
        const ClusterStats stats = cluster_stats(w);
        CHECK(stats.clusters > 0);
        if (stats.clusters == 0)
        {
            continue;
        }
        const double ratio =
            static_cast<double>(stats.tiny) / static_cast<double>(stats.clusters);
        CHECK(ratio <= kMaxTinyRatio);

        const double per_attempt =
            static_cast<double>(stats.cells) / static_cast<double>(attempts);
        CHECK(per_attempt >= kMinCellsPerAttempt);
    }
}

TEST(forbidden_large_veins_never_reach_the_large_band)
{
    // 均匀母岩的合成世界：整片花岗岩，洞室全空。火成母岩表含 granite，
    // 于是铜/金/银/四种宝石都能起种；在这个世界里每条矿脉都能长到自己的
    // 目标体素数，于是「每撒点的实落格数」成了可直接测量的量。
    //
    // 判别原理：禁大脉的矿种只能来自小档(3-6)或中档(10-20)，每撒点实落
    // 格数必然远低于 20；一旦它们拿到大档(30-60)，该值立刻跳到 20 上下。
    // 阈值 15：正确实现 5 个种子 x 5 个禁大脉矿种的实测上界是 12.778；
    // 去掉 allow_large 判据的变异体在 25 个（种子 x 矿种）组合里有 21 个
    // 超过 15（最小 13.286，出现在 seed 101 的 emerald），断言取全部组合
    // 的合取，因此任一组合超限即失败。种子写死，断言确定性。
    constexpr double kMaxCellsPerAttempt = 15.0;
    constexpr std::uint64_t kSeeds[] = {33, 77, 101, 202, 303};

    for (const std::uint64_t seed : kSeeds)
    {
        WorldParams p;
        p.width = 96;
        p.height = 96;
        p.z_levels = 6;
        p.seed = seed;
        World w(p);
        for (std::uint32_t z = 0; z < p.z_levels; ++z)
        {
            for (std::uint32_t i = 0; i < w.stone[z].size(); ++i)
            {
                w.stone[z].at(i) = Stone::granite;
            }
        }
        gamev2::world::make_deposits(w);

        for (const auto& spec : kDepositSpecs)
        {
            if (spec.allow_large)
            {
                continue;
            }
            std::uint32_t cells = 0;
            for (std::uint32_t z = 0; z < p.z_levels; ++z)
            {
                for (std::uint32_t i = 0; i < w.deposit[z].size(); ++i)
                {
                    if (w.deposit[z].at(i) == spec.kind)
                    {
                        ++cells;
                    }
                }
            }
            // 母岩表里没有花岗岩的矿种在这个世界里起不了种（母岩不匹配），
            // 跳过是构造决定的，不是断言空转：它由母岩表算出，不写死矿种。
            const bool can_start =
                std::ranges::find(spec.hosts.begin(), spec.hosts.end(),
                                  Stone::granite) != spec.hosts.end();
            if (!can_start)
            {
                continue;
            }
            // 起得了种的必须真的落过矿，否则断言是空的。
            CHECK(cells > 0);
            if (cells == 0)
            {
                continue;
            }
            const double per_attempt = static_cast<double>(cells)
                                       / static_cast<double>(spec.vein_count);
            CHECK(per_attempt <= kMaxCellsPerAttempt);
        }
    }
}

TEST(a_two_level_world_keeps_its_veins_below_the_surface)
{
    // 退化世界的**冒烟用例**：只有 z=0 与 z=1 两层时，z=1 的矿脉会试图往
    // z=2 走，is_valid_host 的 z 范围检查必须把它拦住。
    // 诚实标注：它证明不崩、证明矿脉不越到表层，**杀不掉**「去掉
    // z >= z_levels 判据」的变异体——那是越界访问（UB），在没有 sanitizer
    // 的环境里不可观测。
    const World w = build(77, 2);
    bool surface_clean = true;
    for (std::uint32_t i = 0; i < w.deposit[0].size(); ++i)
    {
        surface_clean = surface_clean && w.deposit[0].at(i) == Deposit::none;
    }
    CHECK(surface_clean);

    // 矿脉应当落在唯一的地下层 z=1：断言不空转。
    std::uint32_t underground = 0;
    for (std::uint32_t i = 0; i < w.deposit[1].size(); ++i)
    {
        if (w.deposit[1].at(i) != Deposit::none)
        {
            ++underground;
        }
    }
    CHECK(underground > 0);
}

TEST(finalize_aquifers_survives_a_zero_cell_depth)
{
    // generate_world 会按设计文档 6 拒绝 z_cell_depth == 0，但
    // finalize_aquifers 是 generate.hpp 里的公开声明，测试会直接调用它，
    // 不该在这里硬崩。
    WorldParams p;
    p.width = 16;
    p.height = 16;
    p.z_levels = 6;
    p.seed = 77;
    p.z_cell_depth = 0;
    World w(p);
    for (std::uint32_t i = 0; i < w.aquifer_top.size(); ++i)
    {
        w.aquifer_top.at(i) = 20;
    }
    gamev2::world::finalize_aquifers(w);
    bool all_dry = true;
    for (std::uint32_t z = 0; z < p.z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < w.aquifer[z].size(); ++i)
        {
            all_dry = all_dry && !w.aquifer[z].at(i);
        }
    }
    CHECK(all_dry);
}

}  // namespace

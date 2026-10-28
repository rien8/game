// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::environment {

std::vector<float>
run_season(ResourceField& field, const std::span<const Forager> foragers,
           const EnvironmentConfig& config)
{
    std::vector<float> satisfaction(foragers.size(), 0.0f);
    std::vector<float> got(foragers.size(), 0.0f);
    std::vector<float> want_of(foragers.size(), 0.0f);
    std::vector<float> per_cell(kCellCount, 0.0f);

    for (std::uint32_t step = 0; step < config.steps_per_season; ++step)
    {
        field.regrow();
        std::ranges::fill(got, 0.0f);
        std::ranges::fill(want_of, 0.0f);
        std::ranges::fill(per_cell, 0.0f);

        // 第一趟：按扣减前的存量算每个人的份。只算不扣。
        for (std::size_t i = 0; i < foragers.size(); ++i)
        {
            const auto& f = foragers[i];
            const auto cell = CellIndex::of(f.x, f.y);
            const auto n = field.occupants(cell);
            if (n == 0)
            {
                continue;
            }
            const float share =
                field.stock(cell) / static_cast<float>(n);
            // want 是绝对量，刻意不乘 share：乘了 share 会被 got = min(want, share)
            // 原样约掉，ratio 退化成 min(1, 1/(rate × demand)) 而与存量无关。
            const float want = config.demand_rate * f.demand;
            want_of[i] = want;
            got[i] = std::min(want, share);
            per_cell[cell.value] += got[i];
        }

        // 第二趟：按格统一扣减。必须与第一趟分开——为的是同一份存量快照，
        // 不是为了消除顺序依赖：合并成边算边扣后，被 share 截断的那部分个体
        // 读到的是 S₀ - Σ_{j<k} got_j 缩小的存量，人均份额随之递减，总扣减量
        // 系统性偏少。存量偏多 → 下一步 regrow 的基数偏大 → 承载力预算偏松。
        //
        // 推导：want = demand_rate × demand（绝对量），got = min(want, share）
        //   ratio = got / want = min(1, share / (rate × demand))
        // share 留在了分子里，于是 ratio 随存量变化。demand = 1、rate = 0.35 时
        // 闭式解给出 share = 2.34 / 0.234 / 0.05 → 1.00 / 0.67 / 0.14；而返回值
        // 是 steps_per_season 步的均值，存量在季内被持续吃空，实测（单体、8 步）
        // 比闭式解低一档：share = 0.413 / 0.206 / 0.083 / 0.066
        // → 0.400 / 0.203 / 0.081 / 0.065。两组数都随存量单调下降，而把 share
        // 乘回 want 时这五档全部输出 1.000000——这正是旧公式给不出的东西。
        //
        // 若 want 里再乘一次 share，ratio 变成 min(1, 1/(rate × demand))，阈值
        // 被钉死在 demand > 1/rate ≈ 2.86，而典型 demand ≈ 1，于是 feeding 恒为 1，
        // 饥荒永不涌现——本项目第三次栽在「f(g(x)) 把 x 约掉」这个形状上。
        //
        // 残留的顺序依赖只有一处，纯浮点：per_cell[cell] += got[i] 是同格内
        // 全体的浮点累加，输入反转会改累加顺序，量级 n × ulp(capacity) / 2。
        // 按 (x, y, demand) 规范序累加可逐位消除，但每步要付一次排序代价，
        // 本轮接受。
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            if (per_cell[c] > 0.0f)
            {
                field.draw(CellIndex{static_cast<std::uint16_t>(c)},
                           per_cell[c]);
            }
        }

        for (std::size_t i = 0; i < foragers.size(); ++i)
        {
            if (want_of[i] > 0.0f)
            {
                satisfaction[i] += got[i] / want_of[i];
            }
            else if (foragers[i].demand <= 0.0f)
            {
                // 什么都不需要的个体不该被饿死。
                satisfaction[i] += 1.0f;
            }
            // want 是绝对量（regen_rate ∈ (0,1] 且 regrow 无条件执行，故每步
            // 开始时 stock > 0），而 want = demand_rate × demand，所以
            // 「want > 0 当且仅当 demand > 0」还差一个 demand_rate > 0。
            // demand_rate = 0 是合法配置：want 恒为 0，demand > 0 的个体两个
            // 分支都不命中、全季 satisfaction 保持 0
            // （environment_system_uses_the_config_given_to_environment_world
            // 断言的就是这一点）。
            // demand_rate > 0 时落在 else 分支上的只会是「什么都不需要」的个体。
            // 真正被饿着的个体是 want > 0 而 share = 0（存量被吃空），
            // 它走上面那条分支，ratio = got / want = 0。
        }
    }

    const float steps = static_cast<float>(config.steps_per_season);
    for (auto& value : satisfaction)
    {
        value = std::clamp(value / steps, 0.0f, 1.0f);
    }
    return satisfaction;
}

std::vector<BreedingSite>
build_sites(const ResourceField& field, const ClimateField& climate,
            const std::span<const Forager> foragers,
            const std::span<const std::uint32_t> species_keys,
            const EnvironmentConfig& config)
{
    // 先把 (格, 物种) 对收集出来排序成组，再逐组出站点。排序保证输出
    // 与输入实体顺序无关；就地插入做不到这一点。
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    pairs.reserve(foragers.size());
    for (std::size_t i = 0; i < foragers.size(); ++i)
    {
        pairs.emplace_back(CellIndex::of(foragers[i].x, foragers[i].y).value,
                           species_keys[i]);
    }
    std::ranges::sort(pairs);

    std::vector<BreedingSite> sites;
    for (std::size_t begin = 0; begin < pairs.size();)
    {
        // 同一格的所有物种一起分配：出生预算是整格的预算。逐物种各自
        // lround 会让总量超过 lround(headroom)——headroom = 1.25、两个
        // 物种对半分时各拿 lround(0.625) = 1，凭空多出一胎。
        const std::uint32_t cell_index = pairs[begin].first;
        std::size_t cell_end = begin;
        while (cell_end < pairs.size()
               && pairs[cell_end].first == cell_index)
        {
            ++cell_end;
        }

        const auto cell = CellIndex{static_cast<std::uint16_t>(cell_index)};
        const auto all = static_cast<float>(field.occupants(cell));
        // 生态上限：生境承载力减去在场数，负的部分不算数。
        const float headroom = std::max(0.0f, field.capacity(cell) - all);
        const auto budget =
            static_cast<std::uint32_t>(std::lround(headroom));

        struct Group
        {
            std::uint32_t species = 0;
            std::uint32_t here = 0;
            std::uint32_t share = 0;
            float remainder = 0.0f;
        };
        std::vector<Group> groups;
        for (std::size_t g = begin; g < cell_end;)
        {
            std::size_t g_end = g;
            while (g_end < cell_end && pairs[g_end] == pairs[g])
            {
                ++g_end;
            }
            // share = here / all，按当前物种构成分配空余容量：新物种无法
            // 凭空出现在一个已经被占满的格子里。
            const auto here = static_cast<std::uint32_t>(g_end - g);
            const float exact = headroom * static_cast<float>(here) / all;
            const auto share =
                static_cast<std::uint32_t>(std::floor(exact));
            groups.push_back(Group{pairs[g].second, here, share,
                                   exact - static_cast<float>(share)});
            g = g_end;
        }

        std::uint32_t assigned = 0;
        for (const auto& group : groups)
        {
            assigned += group.share;
        }
        // 余数按最大余数法补一：预算 − Σfloor 至多等于物种数，因此每
        // 物种至多补一个。同余数时物种号小的先拿——组按 (格, 物种)
        // 升序压成，这个次序与输入实体顺序无关。
        std::vector<bool> extra(groups.size(), false);
        for (std::uint32_t left =
                 budget > assigned ? budget - assigned : 0u;
             left > 0; --left)
        {
            std::size_t best = groups.size();
            for (std::size_t i = 0; i < groups.size(); ++i)
            {
                if (extra[i])
                {
                    continue;
                }
                if (best == groups.size()
                    || groups[i].remainder > groups[best].remainder)
                {
                    best = i;
                }
            }
            if (best == groups.size())
            {
                break;
            }
            extra[best] = true;
        }

        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];
            // 繁殖上限保证一个独苗产不出满窝后代：那会是一次纯自交，
            // 直接毁掉多样性，而这正是遗传系统最不该丢掉的东西。
            const auto parent_ceiling = static_cast<std::uint32_t>(
                std::lround(static_cast<float>(group.here)
                            * config.offspring_scale));
            const auto progeny = std::min(
                group.share + (extra[i] ? 1u : 0u), parent_ceiling);
            if (progeny > 0u)
            {
                sites.push_back(BreedingSite{cell, group.species, progeny,
                                             climate.at(cell)});
            }
        }
        begin = cell_end;
    }

    return sites;
}

}  // namespace gamev2::environment

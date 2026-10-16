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
            const float want = config.demand_rate * f.demand * share;
            want_of[i] = want;
            got[i] = std::min(want, share);
            per_cell[cell.value] += got[i];
        }

        // 第二趟：按格统一扣减。必须与第一趟分开——为的是同一份存量快照，
        // 不是为了消除顺序依赖：合并成边算边扣后，第 k 个个体读到的是
        // S₀ - Σ_{j<k} got_j 缩小的存量，份额随之递减，总扣减量系统性偏少
        // （实测约 24%）。存量偏多 → 下一步 regrow 的基数偏大 → 承载力预算偏松。
        //
        // 推导：want = demand_rate × demand × share，got = min(want, share)
        //   ratio = got / want = min(1, share / want) = min(1, 1/(rate × demand))
        // share 被约掉，ratio 与存量无关，feeding 对存量尺度不变——合并前后
        // 精确算术下相同，浮点下至多相差一个 ulp，且与遍历顺序无关。
        // 拿 feeding 向量做顺序比较的测试原理上守不住两趟分离，
        // 后续者别据此认为两趟可以合并。
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
            // want 为 0 只在本格无资源时发生（capacity_base 为 0 是唯一可能），
            // 那就是饿，记 0 分。regrow 每步开头无条件执行、regen_rate ∈ (0,1]，
            // 故每步开始时 stock > 0，于是 want > 0 当且仅当 demand > 0：
            // 这一支与上面的 demand == 0 落在同一条分支上。
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
        std::size_t end = begin;
        while (end < pairs.size() && pairs[end] == pairs[begin])
        {
            ++end;
        }

        const auto cell =
            CellIndex{static_cast<std::uint16_t>(pairs[begin].first)};
        const auto here = static_cast<float>(end - begin);
        const auto all = static_cast<float>(field.occupants(cell));

        // 生态上限：生境承载力减去在场数，负的部分不算数。
        const float headroom = std::max(0.0f, field.capacity(cell) - all);
        // share = here / all，按当前物种构成分配空余容量：新物种无法
        // 凭空出现在一个已经被占满的格子里。
        const std::uint32_t eco_ceiling = static_cast<std::uint32_t>(
            std::lround(headroom * here / all));

        // 繁殖上限保证一个独苗产不出满窝后代：那会是一次纯自交，
        // 直接毁掉多样性，而这正是遗传系统最不该丢掉的东西。
        const std::uint32_t parent_ceiling = static_cast<std::uint32_t>(
            std::lround(here * config.offspring_scale));

        const auto progeny = std::min(eco_ceiling, parent_ceiling);
        if (progeny > 0)
        {
            sites.push_back(BreedingSite{cell, pairs[begin].second, progeny,
                                         climate.at(cell)});
        }
        begin = end;
    }

    return sites;
}

}  // namespace gamev2::environment

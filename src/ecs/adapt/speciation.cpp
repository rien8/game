#include "gamev2/ecs/adapt/speciation.hpp"

#include "gamev2/environment/terrain.hpp"
#include "gamev2/genetics/statistics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::ecs::adapt {

namespace {

using gamev2::environment::CellIndex;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::environment::TerrainField;
using gamev2::genetics::Genome;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesRegistry;

/// 一个连通分量。锚取**中位数**而不是质心 —— 质心会被一个远离群体的个-body
/// 拖走，中位数对此免疫。迟滞靠它跨季识别「同一个分量」（§4.3）。
struct Component
{
    std::vector<EntityId> members;
    std::vector<Genome> genomes;
    CellIndex anchor;
};

/// 分量内全部个体所在格的中位数：x 与 y 分别取中位数再合成一格。
[[nodiscard]] CellIndex median_anchor(const std::vector<EntityId>& members,
                                       World& world)
{
    std::vector<std::uint32_t> xs;
    std::vector<std::uint32_t> ys;
    xs.reserve(members.size());
    ys.reserve(members.size());
    for (const auto e : members)
    {
        const auto* pos = world.get_if<Position>(e);
        if (pos == nullptr)
        {
            continue;
        }
        const auto c = CellIndex::of(pos->x, pos->y).value;
        xs.push_back(c % kGridSide);
        ys.push_back(c / kGridSide);
    }
    if (xs.empty())
    {
        return CellIndex{0};
    }
    const auto mid = static_cast<std::ptrdiff_t>(xs.size() / 2);
    std::nth_element(xs.begin(), xs.begin() + mid, xs.end());
    std::nth_element(ys.begin(), ys.begin() + mid, ys.end());
    const auto k = static_cast<std::size_t>(mid);
    return CellIndex{static_cast<std::uint16_t>(ys[k] * kGridSide + xs[k])};
}

/// 两格的切比雪夫距离 —— 网格上「移动要几步」的天然度量。
[[nodiscard]] double anchor_distance(const CellIndex a,
                                     const CellIndex b) noexcept
{
    const auto ax = a.value % kGridSide;
    const auto ay = a.value / kGridSide;
    const auto bx = b.value % kGridSide;
    const auto by = b.value / kGridSide;
    const auto dx = static_cast<double>(ax > bx ? ax - bx : bx - ax);
    const auto dy = static_cast<double>(ay > by ? ay - by : by - ay);
    return std::max(dx, dy);
}

/// 4 邻域连通分量，不是 8：地形不可通行判定与 MovementSystem 的推进都是
/// 4 邻域的，「个体所在陆块」这个不变量只在 4 邻域下成立。8 邻域的分量能
/// 跨一道对角水缝连起来，那道缝就不再是屏障（设计文档 §5）。
///
/// 按格分桶再泛洪，而不是逐个体「找邻居再跳」—— 后者在几千个体下是 O(n²)。
[[nodiscard]] std::vector<Component>
components_of(World& world, const SpeciesRegistry& species,
              const TerrainField& terrain, const SpeciesId sid)
{
    struct Member
    {
        EntityId e;
        Genome genome;
        std::uint32_t cell;
    };
    std::vector<Member> members;
    world.for_each_entity(
        [&](const EntityId e)
        {
            const auto* tag = world.get_if<SpeciesTag>(e);
            const auto* pos = world.get_if<Position>(e);
            const auto* g = world.get_if<Genome>(e);
            // 与 EnvironmentSystem / BreedingSystem 同一把尺子：未登记的物种
            // id 各自被跳过（设计文档 §7.2 第 1 条）。
            if (tag == nullptr || pos == nullptr || g == nullptr
                || tag->species != sid || tag->species.value == 0
                || tag->species.value > species.species_count())
            {
                return;
            }
            members.push_back(
                Member{e, *g, CellIndex::of(pos->x, pos->y).value});
        });

    std::vector<std::uint32_t> head(kCellCount, ~0u);
    std::vector<std::uint32_t> next;
    next.reserve(members.size());
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        next.push_back(head[members[i].cell]);
        head[members[i].cell] = static_cast<std::uint32_t>(i);
    }

    std::vector<bool> used(kCellCount, false);
    std::vector<std::uint32_t> stack;
    std::vector<Component> out;
    for (const auto& seed : members)
    {
        if (used[seed.cell])
        {
            continue;
        }
        Component comp;
        stack.assign(1, seed.cell);
        while (!stack.empty())
        {
            const auto c = stack.back();
            stack.pop_back();
            if (used[c] || !terrain.passable(CellIndex{
                                static_cast<std::uint16_t>(c)}))
            {
                continue;
            }
            used[c] = true;
            for (auto it = head[c]; it != ~0u; it = next[it])
            {
                comp.members.push_back(members[it].e);
                comp.genomes.push_back(members[it].genome);
            }
            const auto cx = c % kGridSide;
            const auto cy = c / kGridSide;
            if (cx > 0)
            {
                stack.push_back(c - 1);
            }
            if (cx + 1 < kGridSide)
            {
                stack.push_back(c + 1);
            }
            if (cy > 0)
            {
                stack.push_back(c - kGridSide);
            }
            if (cy + 1 < kGridSide)
            {
                stack.push_back(c + kGridSide);
            }
        }
        comp.anchor = median_anchor(comp.members, world);
        out.push_back(std::move(comp));
    }
    return out;
}

}  // namespace

SpeciationSystem::SpeciationSystem(gamev2::genetics::SpeciesRegistry& species,
                                   const EnvironmentWorld& env)
    : species_{species}, env_{env}
{
}

void SpeciationSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    born_.clear();
    lost_.clear();

    // 提到物种循环**之外**：否则处理完物种 1 之后 pending_ 被清空，物种 2 的
    // 连续季数会被误归零。`claimed` 同理，它是对 pending_ 全局的占用标记。
    std::vector<Pending> next_pending;
    next_pending.reserve(pending_.size());
    std::vector<bool> claimed(pending_.size(), false);

    const auto& terrain = env_.terrain();
    const auto& config = env_.config();

    for (std::uint32_t s = 1; s <= species_.species_count(); ++s)
    {
        const auto sid = SpeciesId{s};
        const auto comps = components_of(world, species_, terrain, sid);
        if (comps.empty())
        {
            // 本季一个个体都不剩 ⇒ 该物种灭绝。**必须去重**：`species_count()`
            // 只增不减（没有注销接口），所以记过一次之后，本季 `for (s)` 的每一
            // 次迭代都会再次撞到这个 id。不去重的话玩家会看到同一支灭一整局。
            if (std::ranges::find(lost_, sid) == lost_.end())
            {
                lost_.push_back(sid);
            }
            continue;
        }
        if (comps.size() < 2)
        {
            continue;  // 没有第二个分量就无从裂变
        }

        const auto mask = species_.mask(sid);
        std::vector<Genome> all;
        for (const auto& c : comps)
        {
            all.insert(all.end(), c.genomes.begin(), c.genomes.end());
        }

        for (std::size_t ci = 0; ci < comps.size(); ++ci)
        {
            const auto& comp = comps[ci];
            // (a) min_founder
            if (comp.members.size() < config.min_founder)
            {
                continue;
            }
            // (b) 不可通行的水 —— **当前冗余**：4 邻域分量按定义就是一个陆块，
            // 而陆块之间必有水。保留它是因为阶段 4 的玩家墙不是水，那时它
            // 第一次真正起作用（设计文档 §5.1）。

            // 「其余全部」= 同物种所有基因组减去本分量。逐分量现拼：同物种的
            // 个体数在 200 季实测里约 2400，最深 11 个分量，代价可接受。
            std::vector<Genome> rest;
            rest.reserve(all.size() - comp.genomes.size());
            for (std::size_t cj = 0; cj < comps.size(); ++cj)
            {
                if (cj == ci)
                {
                    continue;
                }
                rest.insert(rest.end(), comps[cj].genomes.begin(),
                            comps[cj].genomes.end());
            }

            // (c) divergence
            const auto here = gamev2::genetics::summarize(comp.genomes, mask);
            const auto other = gamev2::genetics::summarize(rest, mask);
            if (gamev2::genetics::divergence(here, other)
                <= config.divergence_threshold)
            {
                continue;
            }

            // (d) 最近邻匹配：取距离最小且在半径内的**同物种**旧条目。平手由
            // 遍历序打破（用 `<=` 而非 `<`），因此逐位确定。
            std::size_t best = pending_.size();
            double best_d = static_cast<double>(config.hysteresis_radius_cells);
            for (std::size_t i = 0; i < pending_.size(); ++i)
            {
                if (claimed[i] || pending_[i].species != sid)
                {
                    continue;
                }
                const auto d =
                    anchor_distance(pending_[i].anchor, comp.anchor);
                if (d <= best_d)
                {
                    best_d = d;
                    best = i;
                }
            }

            std::uint32_t streak = 1;
            if (best < pending_.size())
            {
                claimed[best] = true;
                streak = pending_[best].seasons + 1;
            }

            if (streak < config.speciation_hysteresis)
            {
                next_pending.push_back(Pending{sid, comp.anchor, streak});
                continue;
            }

            const auto reg = species_.register_species(mask);
            if (!reg)
            {
                // EmptyMask / TooManySpecies：物种数到顶，不裂。
                continue;
            }
            for (const auto e : comp.members)
            {
                // 原地改值，不触发 archetype 迁移。
                world.get_if<SpeciesTag>(e)->species = *reg;
            }
            born_.push_back(
                BornSpecies{*reg, sid, comp.members.size(), comp.anchor});
            // 裂变后**不进** next_pending：同一处不该反复裂。
        }
    }

    // 本季没被任何分量匹配上的旧条目自然没有出现在 next_pending 里 ——
    // 这就是「打断即归零」。`claimed` 保证一个旧条目不会被两个分量继承。
    pending_ = std::move(next_pending);
}

}  // namespace gamev2::ecs::adapt

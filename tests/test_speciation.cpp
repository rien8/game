#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/speciation.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/terrain.hpp"

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/species.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::World;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::Genome;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::SpeciationSystem;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::genetics::AlleleCall;
using gamev2::genetics::AlleleId;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::kLocusCount;

/// 与 sim 同一份 —— 全激活时 divergence 的值域是 [0, 150]。
SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 全部位点同一个等位基因的一份基因组。裂变条件 (c) 读的就是两组的频率差，
/// 因此「一整块地纯 a、另一块纯 b」是构造出必然越过 θ 的最省事办法：
/// 每位点 |Δf| = 1，divergence = 300 / 2 = 150。
Genome pure(const AlleleId allele)
{
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i] = AlleleCall{allele, 0};
        g.b[i] = AlleleCall{allele, 0};
    }
    return g;
}

/// 挑 n 个彼此远离的可通行格。
///
/// **不用 8×8 的方块。** 水陆 70% 下 8×8 全陆地的连续块很稀有，实测只有
/// 几块，且有两块只隔 4 格 —— 相连即合成一个分量，于是只裂一次、物种数
/// 少一个。用单个可通行格则一个格按定义就是一个连通分量，夹具不再依赖
/// 地图形状。组件的真实规模（实测 500+ 格）不影响被测机制。
[[nodiscard]] std::vector<CellIndex>
spread_cells(const EnvironmentWorld& env, const std::size_t want)
{
    std::vector<CellIndex> pool;
    for (std::size_t c = 0; c < 4096; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        if (env.terrain().passable(cell))
        {
            pool.push_back(cell);
        }
    }
    // 贪心最大最小距离：每格取「离最近的那格已选格最远」的那个。
    std::vector<CellIndex> picked;
    if (pool.empty())
    {
        return picked;
    }
    picked.push_back(pool.front());
    while (picked.size() < want)
    {
        double best = -1.0;
        std::size_t best_i = pool.size();
        for (std::size_t i = 0; i < pool.size(); ++i)
        {
            const auto taken = std::ranges::any_of(
                picked,
                [&](const CellIndex p)
                { return p.value == pool[i].value; });
            if (taken)
            {
                continue;
            }
            double nearest = 1e18;
            for (const auto& p : picked)
            {
                const auto dx =
                    static_cast<double>(pool[i].value % 64 - p.value % 64);
                const auto dy = static_cast<double>(pool[i].value / 64
                                                    - p.value / 64);
                nearest = std::min(nearest, dx * dx + dy * dy);
            }
            if (nearest > best)
            {
                best = nearest;
                best_i = i;
            }
        }
        if (best_i == pool.size())
        {
            break;
        }
        picked.push_back(pool[best_i]);
    }
    return picked;
}

/// 在单个可通行格内抖 n 个个体。一个格按定义就是一个连通分量。
void place_patch(World& world, const SpeciesId sid, const Genome& genome,
                 const CellIndex cell, const int n,
                 std::vector<EntityId>& out)
{
    std::uniform_real_distribution<float> jitter{0.05f, 0.95f};
    std::mt19937_64 rng{cell.value};
    const auto bx = static_cast<float>(cell.value % 64);
    const auto by = static_cast<float>(cell.value / 64);
    for (int i = 0; i < n; ++i)
    {
        const EntityId e = world.create();
        (void)world.add<Position>(e, Position{bx + jitter(rng),
                                              by + jitter(rng)});
        (void)world.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)world.add<Genome>(e, Genome{genome});
        out.push_back(e);
    }
}

/// ecs::World 只有 add / get_if，没有 set<T> —— 改一个已有组件的值要经
/// get_if 写回。Genome 是聚合体，整体赋值即可。
void world_set_genome(World& world, const EntityId e, const Genome& genome)
{
    *world.get_if<Genome>(e) = Genome{genome};
}

[[nodiscard]] SpeciesId species_of(World& world, const EntityId e)
{
    return world.get_if<SpeciesTag>(e)->species;
}

/// 物种登记表由测试自己持有 —— `EnvironmentWorld` 只有气候 / 资源 / 地形
/// 三个场，不含登记表。`SpeciationSystem` 同时需要两者，因此测试也同时持有。
struct Setup
{
    gamev2::genetics::SpeciesRegistry species;
    EnvironmentWorld env;
    SpeciesId sid{};

    explicit Setup(const EnvironmentConfig& c)
        : env{20261016, c}
    {
        const auto reg = species.register_species(all_loci());
        CHECK(reg.has_value());
        sid = *reg;
    }
};

/// (a)(b)(c) 的公共配置。`speciation_hysteresis` 由各测试自己定。
[[nodiscard]] EnvironmentConfig speciation_config(const float water)
{
    EnvironmentConfig c{};
    c.min_founder = 40;
    c.divergence_threshold = 10.0f;
    c.speciation_hysteresis = 1;
    c.water_fraction = water;
    return c;
}

}  // namespace

TEST(components_below_min_founder_never_split)
{
    // 设计文档 §9 测试 5 的最小形式。两块地各 3 个体，远低于 min_founder
    // 40，即便 divergence 拉满（各位点 |Δf| = 1 ⇒ 150）也不该裂。
    const auto config = speciation_config(0.70f);
    Setup s{config};
    World entities;
    const auto sid = s.sid;

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto cells = spread_cells(s.env, 2);
    CHECK_EQ(cells.size(), std::size_t{2});
    place_patch(entities, sid, pure(0), cells[0], 3, left);
    place_patch(entities, sid, pure(1), cells[1], 3, right);

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(speciation.born().empty());
    CHECK_EQ(s.species.species_count(), std::size_t{1});
    for (const auto e : left)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
    for (const auto e : right)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
}

TEST(a_large_isolated_group_with_enough_divergence_splits)
{
    // 条件 (a) 与 (c) 同时满足时的正例。两块地各 60 个体（> 40），
    // 基因组一纯 a 一纯 b ⇒ divergence 150，远超 θ = 10。
    const auto config = speciation_config(0.70f);
    Setup s{config};
    World entities;
    const auto sid = s.sid;

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto cells = spread_cells(s.env, 2);
    CHECK_EQ(cells.size(), std::size_t{2});
    place_patch(entities, sid, pure(0), cells[0], 60, left);
    place_patch(entities, sid, pure(1), cells[1], 60, right);

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK_EQ(speciation.born().size(), std::size_t{2});
    for (const auto& b : speciation.born())
    {
        CHECK_EQ(b.parent, sid);
        CHECK(b.id != sid);
        CHECK(b.founders >= 40);
    }

    // 两块地必须落在**不同的**新物种上 —— 否则「两块地」这件事没被表达出来。
    const auto left_id = species_of(entities, left.front());
    const auto right_id = species_of(entities, right.front());
    CHECK(left_id != right_id);
    CHECK(left_id != sid);
    CHECK(right_id != sid);
    for (const auto e : left)
    {
        CHECK_EQ(species_of(entities, e), left_id);
    }
    for (const auto e : right)
    {
        CHECK_EQ(species_of(entities, e), right_id);
    }
}

TEST(no_speciation_without_a_second_landmass)
{
    // 设计文档 §9 的测试 6（**已改名**）。`water_fraction = 0` ⇒ 全图一个陆块
    // ⇒ 每个物种只有一个连通分量 ⇒ 没有可裂的 g。
    //
    // **这条不测隔离判据** —— 条件 (b) 当前冗余（设计文档 §5.1）：单一陆块下
    // 本就没有第二个分量，与 (b) 写成恒真还是真判无关。它守的是连通分量条件。
    const auto config = speciation_config(0.0f);
    Setup s{config};
    World entities;
    const auto sid = s.sid;

    std::vector<EntityId> ids;
    for (int i = 0; i < 120; ++i)
    {
        const auto x = static_cast<float>(i % 12) * 5.0f + 0.5f;
        const auto y = static_cast<float>(i / 12) * 5.0f + 0.5f;
        const EntityId e = entities.create();
        (void)entities.add<Position>(e, Position{x, y});
        (void)entities.add<SpeciesTag>(e, SpeciesTag{sid});
        (void)entities.add<Genome>(
            e, Genome{pure(static_cast<AlleleId>(i % 2))});
        ids.push_back(e);
    }

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(speciation.born().empty());
    CHECK_EQ(s.species.species_count(), std::size_t{1});
    for (const auto e : ids)
    {
        CHECK_EQ(species_of(entities, e), sid);
    }
}

TEST(a_new_species_inherits_the_mask_of_its_parent)
{
    // 决策 6：新物种的掩码从母物种继承。给玩家一份手挑掩码是玩家分株，
    // 属非目标。
    const auto config = speciation_config(0.70f);
    Setup s{config};
    World entities;
    const auto sid = s.sid;

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto cells = spread_cells(s.env, 2);
    CHECK_EQ(cells.size(), std::size_t{2});
    place_patch(entities, sid, pure(0), cells[0], 60, left);
    place_patch(entities, sid, pure(1), cells[1], 60, right);

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    const auto parent_mask = s.species.mask(sid);
    for (const auto& b : speciation.born())
    {
        CHECK(s.species.mask(b.id).active == parent_mask.active);
    }
}

TEST(unregistered_species_ids_are_skipped)
{
    // 与 EnvironmentSystem / BreedingSystem 同一把尺子（设计文档 §7.2 第 1
    // 条）：未登记的 id 被跳过，而不是拿越界的掩码去 summarize。
    const auto config = speciation_config(0.70f);
    Setup s{config};
    World entities;
    const auto sid = s.sid;

    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto two = spread_cells(s.env, 3);
    CHECK_EQ(two.size(), std::size_t{3});
    place_patch(entities, sid, pure(0), two[0], 60, left);
    place_patch(entities, sid, pure(1), two[1], 60, right);

    // 第三个地块挂在未登记的 id 7 上。它够大、divergence 也够，但它根本不该
    // 进入物种 1 的计算 —— 否则它会被当成一块可裂的地。
    std::vector<EntityId> ghost;
    place_patch(entities, SpeciesId{7}, pure(1), two[2], 60, ghost);

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    speciation.execute(entities, cmd);
    cmd.flush(entities);

    CHECK_EQ(s.species.species_count(), std::size_t{3});
    for (const auto e : ghost)
    {
        CHECK_EQ(species_of(entities, e), SpeciesId{7});
    }
}

TEST(speciation_waits_for_the_configured_number_of_consecutive_seasons)
{
    // 条件 (d)。同一个夹具跑两遍：hysteresis = 1 第 1 季就裂；= 4 则前 3 季
    // 安静、第 4 季一次裂出两个。
    const auto run = [](const std::uint32_t hysteresis)
    {
        auto config = speciation_config(0.70f);
        config.speciation_hysteresis = hysteresis;
        Setup s{config};
        World entities;
        std::vector<EntityId> left;
        std::vector<EntityId> right;
        const auto cells = spread_cells(s.env, 2);
        CHECK_EQ(cells.size(), std::size_t{2});
        place_patch(entities, s.sid, pure(0), cells[0], 60, left);
        place_patch(entities, s.sid, pure(1), cells[1], 60, right);

        SpeciationSystem speciation{s.species, s.env};
        CommandBuffer cmd;
        std::vector<std::size_t> born_per_season;
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            speciation.execute(entities, cmd);
            cmd.flush(entities);
            born_per_season.push_back(speciation.born().size());
        }
        return born_per_season;
    };

    const auto immediate = run(1);
    CHECK_EQ(immediate.front(), std::size_t{2});
    // 裂过之后各成一个物种，只剩 1 个分量，不再有新裂变。
    for (std::size_t i = 1; i < immediate.size(); ++i)
    {
        CHECK_EQ(immediate[i], std::size_t{0});
    }

    const auto delayed = run(4);
    for (std::size_t i = 0; i < 3; ++i)
    {
        CHECK_EQ(delayed[i], std::size_t{0});
    }
    CHECK_EQ(delayed[3], std::size_t{2});
    for (std::size_t i = 4; i < delayed.size(); ++i)
    {
        CHECK_EQ(delayed[i], std::size_t{0});
    }
}

TEST(a_season_that_breaks_the_streak_resets_the_counter)
{
    // 迟滞挡的是「群体规模在阈值上下抖时抖出一棵全是死枝的进化树」。
    // 测法：先让 (c) 连续成立三季（counter 到 3），第 4 季把 divergence 拉回
    // 阈值以下，第 5 季再拉回来 —— counter 必须从 1 重新数，凑满 4 才裂。
    // 若实现漏了「未命中即丢弃」，第 5 季就会直接裂。
    auto config = speciation_config(0.70f);
    config.speciation_hysteresis = 4;
    Setup s{config};
    World entities;
    std::vector<EntityId> left;
    std::vector<EntityId> right;
    const auto cells = spread_cells(s.env, 2);
    CHECK_EQ(cells.size(), std::size_t{2});
    place_patch(entities, s.sid, pure(0), cells[0], 60, left);
    place_patch(entities, s.sid, pure(1), cells[1], 60, right);

    SpeciationSystem speciation{s.species, s.env};
    CommandBuffer cmd;
    for (int i = 0; i < 3; ++i)
    {
        speciation.execute(entities, cmd);
        cmd.flush(entities);
        CHECK(speciation.born().empty());
    }

    // 第 4 季：右半边换成与左半边相同的基因组 ⇒ divergence 归零。
    for (const auto e : right)
    {
        world_set_genome(entities, e, pure(0));
    }
    speciation.execute(entities, cmd);
    cmd.flush(entities);
    CHECK(speciation.born().empty());

    // 第 5 季：换回来。第 4 季打断了连续，此时 counter 只有 1，凑不满 4。
    // 若实现漏了「未命中即丢弃」，旧条目会带着 seasons = 3 活到这一季，
    // 于是 streak = 4 直接裂 —— **这一行就是本条测试存在的理由**。
    for (const auto e : right)
    {
        world_set_genome(entities, e, pure(1));
    }
    speciation.execute(entities, cmd);
    cmd.flush(entities);
    CHECK(speciation.born().empty());

    // 再攒三季（1、2、3），第 8 季必须裂。
    // 只断「不裂」是不够的：漏掉丢弃的实现同样不裂（counter 被重复条目
    // 卡住），那样测试会空洞地通过。把时间线走完，裂与不裂就有了区别。
    for (int i = 0; i < 2; ++i)
    {
        speciation.execute(entities, cmd);
        cmd.flush(entities);
        CHECK(speciation.born().empty());
    }
    speciation.execute(entities, cmd);
    cmd.flush(entities);
    CHECK_EQ(speciation.born().size(), std::size_t{2});
}

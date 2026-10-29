#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace {

/// 刀 4 的夹具：256²/seed13。128² 只有太玄有活脉，六宗断供——判据 2
/// 在那里吞掉几乎所有人，很多分支读不出来（刀 3 的经济测试已因同一
/// 理由换过夹具）。
gamev2::world::WorldParams decisions_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

struct Stack
{
    std::optional<gamev2::world::World> world;
    gamev2::faction::EraReport era;
};

Stack make_stack(const gamev2::world::WorldParams& p)
{
    Stack stack;
    auto generated = gamev2::world::generate_world(p);
    if (!generated)
    {
        return stack;
    }
    stack.world = std::move(*generated);
    gamev2::history::LedgerDiagnostics ldiag;
    const auto ledger = gamev2::history::build_ledger(*stack.world, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    stack.era = gamev2::faction::simulate(*stack.world, ledger, fdiag);
    return stack;
}

/// 按 MotiveKind 数一遍活人的当年意图。
std::map<gamev2::life::MotiveKind, std::uint32_t> motive_census(
    gamev2::life::Life& life)
{
    std::map<gamev2::life::MotiveKind, std::uint32_t> out;
    life.ecs_world().view<const gamev2::life::Motive>().each(
        [&](const gamev2::life::Motive& m) { ++out[m.kind]; });
    return out;
}

}  // namespace

TEST(every_soul_gets_site_standing_and_blank_motive)
{
    // 新组件随建实体挂上。空白态在 seed 之后验：决策 system 还没跑，
    // 意图应当是空的、罪名应当是 0。跑一年之后再验一次——组件齐全要
    // 打在有新生儿的世界上（终审 B2 的先例）。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint32_t named = 0;
    std::uint32_t with_all = 0;
    std::set<std::uint32_t> cells;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId&) { ++named; });
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Site,
                          const gamev2::life::Standing,
                          const gamev2::life::Motive>().each(
        [&](const gamev2::life::SoulId&,
            const gamev2::life::Site& site,
            const gamev2::life::Standing& st,
            const gamev2::life::Motive& m)
        {
            ++with_all;
            cells.insert(site.cell);
            CHECK_EQ(st.crimes, 0u);
            CHECK_EQ(m.kind, gamev2::life::MotiveKind::none);
        });
    CHECK_EQ(with_all, named);
    CHECK(named > 900u);
    // 落格是 site_init 掷运：初始 970 人不该挤在同一个格。
    CHECK(cells.size() > 100u);

    life.run_years(1);

    std::uint32_t named2 = 0;
    std::uint32_t with_all2 = 0;
    std::uint32_t newcomers = 0;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            ++named2;
            if (id.index >= 970u)
            {
                ++newcomers;
            }
        });
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Site,
                          const gamev2::life::Standing,
                          const gamev2::life::Motive>().each(
        [&](const gamev2::life::SoulId& id, const gamev2::life::Site&,
            const gamev2::life::Standing&, const gamev2::life::Motive& m)
        {
            ++with_all2;
            if (id.index < 970u)
            {
                // 决策 system 已经跑过：老身位的意图不再是空的（末年
                // 清空是玩家的接管点，不是「永远为空」）。
                CHECK(m.kind != gamev2::life::MotiveKind::none);
            }
            else
            {
                // 新生儿赶不上这一年的决策：组件在年末 flush 才落地，
                // 意图因此是空的——下一年才轮得到他们。
                CHECK_EQ(m.kind, gamev2::life::MotiveKind::none);
            }
        });
    CHECK_EQ(with_all2, named2);
    CHECK(newcomers > 0u);
}

TEST(thick_cells_follow_the_tide_and_events_start_empty)
{
    // 厚域格表不是静态图：世界的浓度场是潮位缩放出来的，谷底期全天下
    // 没有厚域（开局年 8914 实测 0 格），灵潮期才长出来（首个非空年
    // 9061，5 格）。事件表在 seed 之后是空的。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    CHECK(life.events().empty());
    CHECK(life.thick_cells().empty());

    life.run_years(147);  // 年 9061：首个有厚域的年份
    CHECK(life.year() == 9061u);
    CHECK(!life.thick_cells().empty());
    // 格表是升序线性格号（供二分查找）。
    for (std::size_t i = 1; i < life.thick_cells().size(); ++i)
    {
        CHECK(life.thick_cells()[i - 1u] < life.thick_cells()[i]);
    }
}

TEST(same_seed_same_sites_and_strict_determinism_of_new_fields)
{
    // 两个同 seed 世界跑 2 年：站位逐位相同（新掷运的确定性钉）。
    // 跑 2 年不跑 1 年：让 site_init 也在「有新生儿」的年份上跑过。
    const Stack a = make_stack(decisions_world(13));
    const Stack b = make_stack(decisions_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics da;
    gamev2::life::LifeDiagnostics db;
    gamev2::life::Life la(*a.world, a.era, da);
    gamev2::life::Life lb(*b.world, b.era, db);
    la.seed();
    lb.seed();
    la.run_years(2);
    lb.run_years(2);

    std::map<std::uint32_t, std::uint32_t> cells_a;
    std::map<std::uint32_t, std::uint32_t> cells_b;
    const auto collect = [](gamev2::life::Life& l, auto& out)
    {
        l.ecs_world().view<const gamev2::life::SoulId,
                            const gamev2::life::Site>().each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Site& site)
            { out[id.index] = site.cell; });
    };
    collect(la, cells_a);
    collect(lb, cells_b);
    CHECK_EQ(cells_a.size(), cells_b.size());
    CHECK(cells_a == cells_b);
}

TEST(decide_covers_default_branches_after_one_year)
{
    // 判据 5 是默认：修炼/闭关/历练三类动机在 vigor 三段上都有人。
    //
    // 跑 2 年不是 1 年：第 1 年人人都撞在判据 2 上（私囊开局为 0，
    // 「灵石不够烧」成立），只有 order 为中值者续走到判据 5；第 2 年
    // 宗门弟子领到份额，判据 2 不再吞人，判据 5 才铺开。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(2);
    const auto census = motive_census(life);
    CHECK(census.at(gamev2::life::MotiveKind::cultivate) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::seclude) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::roam) > 0u);
}

TEST(decide_sends_the_starved_to_poach_or_operate)
{
    // 判据 2 的触发链：断供宗（青囊/天机/九幽 mint=0）第 1 年被克扣、
    // starved 落账，缺口成立——悖逆者盗采、守序者经营，两轴两段都要
    // 有人。判据 2 的两半触发式（断供年数、灵石不够烧）都通向这里。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(2);
    const auto census = motive_census(life);
    CHECK(census.at(gamev2::life::MotiveKind::poach) > 0u);
    CHECK(census.at(gamev2::life::MotiveKind::operate) > 0u);
}

TEST(decide_is_deterministic_under_same_seed)
{
    // 两个同 seed 世界跑 3 年：当年的意图逐位相同。3 年盖住「断供→
    // 判据 2」与「厚域寻址」两条路径。
    const Stack a = make_stack(decisions_world(13));
    const Stack b = make_stack(decisions_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics da;
    gamev2::life::LifeDiagnostics db;
    gamev2::life::Life la(*a.world, a.era, da);
    gamev2::life::Life lb(*b.world, b.era, db);
    la.seed();
    lb.seed();
    la.run_years(3);
    lb.run_years(3);

    std::map<std::uint32_t, std::pair<gamev2::life::MotiveKind,
                                      std::uint32_t>> sa;
    std::map<std::uint32_t, std::pair<gamev2::life::MotiveKind,
                                      std::uint32_t>> sb;
    const auto collect = [](gamev2::life::Life& l, auto& out)
    {
        l.ecs_world().view<const gamev2::life::SoulId,
                            const gamev2::life::Motive>().each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Motive& m)
            { out[id.index] = {m.kind, m.arg}; });
    };
    collect(la, sa);
    collect(lb, sb);
    CHECK_EQ(sa.size(), sb.size());
    CHECK(sa == sb);
}

TEST(roam_grants_extra_progress_and_no_stone)
{
    // 历练者当年年资 +2（修炼步的 +1 加行为步的 +1），不产石头（差异
    // 6），且落点必迁（他在动）。
    //
    // 逐年对比：年末的意图就是本年执行的那一个，年资的增量因此可归
    // 因到意图上。境界未变的人排除——突破成功会把年资清零，增量读数
    // 就不是 +2 了。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::map<std::uint32_t, std::uint32_t> before_progress;
    std::map<std::uint32_t, gamev2::life::Realm> before_realm;
    std::map<std::uint32_t, std::uint32_t> before_cell;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Cultivation,
                          const gamev2::life::Site>().each(
        [&](const gamev2::life::SoulId& id,
            const gamev2::life::Cultivation& c,
            const gamev2::life::Site& site)
        {
            before_progress[id.index] = c.progress;
            before_realm[id.index] = c.realm;
            before_cell[id.index] = site.cell;
        });

    life.run_years(1);

    std::uint32_t roamers = 0;
    std::uint32_t movers = 0;
    std::uint32_t cultivators = 0;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Cultivation,
                          const gamev2::life::Site,
                          const gamev2::life::Motive>().each(
        [&](const gamev2::life::SoulId& id,
            const gamev2::life::Cultivation& c,
            const gamev2::life::Site& site,
            const gamev2::life::Motive& m)
        {
            const auto it = before_progress.find(id.index);
            if (it == before_progress.end())
            {
                return;
            }
            if (before_realm[id.index] != c.realm)
            {
                return;  // 本年突破：年资清零，增量不可归因
            }
            if (m.kind == gamev2::life::MotiveKind::roam)
            {
                ++roamers;
                CHECK_EQ(c.progress - it->second, 2u);
                movers += site.cell != before_cell[id.index] ? 1u : 0u;
            }
            else if (m.kind == gamev2::life::MotiveKind::cultivate)
            {
                ++cultivators;
                CHECK_EQ(c.progress - it->second, 1u);
                CHECK_EQ(site.cell, before_cell[id.index]);
            }
        });
    CHECK(roamers > 0u);
    CHECK(cultivators > 0u);
    // 落点迁移：历练者在动（256²/seed13 首年实测 70/70 全迁）。
    CHECK_EQ(movers, roamers);
}

TEST(operate_pays_silver_and_cultivate_stays_put)
{
    // 经营者银两 +60（散修营生同额，任务赏钱）；修炼者年资恰 +1、
    // 落格不动。银两只进散修的账——宗门弟子的经营赏钱同样发（差异
    // 15：宗门弟子能攒钱从自家金库买）。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::map<std::uint32_t, std::uint32_t> before_silver;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Purse>().each(
        [&](const gamev2::life::SoulId& id,
            const gamev2::life::Purse& p)
        { before_silver[id.index] = p.silver; });

    life.run_years(1);

    std::uint32_t operators = 0;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Purse,
                          const gamev2::life::Motive>().each(
        [&](const gamev2::life::SoulId& id,
            const gamev2::life::Purse& p,
            const gamev2::life::Motive& m)
        {
            const auto it = before_silver.find(id.index);
            if (it == before_silver.end())
            {
                return;
            }
            if (m.kind != gamev2::life::MotiveKind::operate)
            {
                return;
            }
            ++operators;
            // 散修的营生与经营互斥：经济步对「当年经营过的人」让位，
            // 因此经营者的银两增额恰是赏钱 60，不是 120。
            CHECK_EQ(p.silver - it->second,
                     gamev2::life::kFreeCultivatorIncomeLiang);
        });
    CHECK(operators > 0u);
}

TEST(roam_hazard_kills_conserves_stones_and_logs_slain)
{
    // 长跑 200 年：历练遇险（5‰/年）必有死者。断言三点：事件表
    // slain 非空；死者私囊在死亡当年入 burned（恒等式不破）；死者不
    // 出现在任何后续年的 view 里（销毁落地）。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint64_t accounts = 0;
    for (std::uint32_t y = 0; y < 200; ++y)
    {
        life.run_years(1);
        accounts = life.economy().vault_total();
        life.ecs_world().view<const gamev2::life::Purse>().each(
            [&](const gamev2::life::Purse& purse)
            { accounts += purse.stone; });
        CHECK_EQ(accounts,
                 life.economy().minted_total - life.economy().burned_total);
    }
    CHECK_EQ(diag.stone_conservation_broken, 0u);

    std::uint32_t slain = 0;
    for (const auto& ev : life.events())
    {
        slain += ev.kind == gamev2::life::LifeEventKind::slain ? 1u : 0u;
    }
    CHECK(slain > 0u);

    // 横死者不出现在任何后续年的 view 里：销毁在年末 flush 落地。
    std::vector<std::uint32_t> dead;
    for (const auto& ev : life.events())
    {
        if (ev.kind == gamev2::life::LifeEventKind::slain && ev.actor != 0u)
        {
            dead.push_back(ev.actor);
        }
    }
    std::uint32_t still_here = 0;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            for (const auto d : dead)
            {
                still_here += d == id.index ? 1u : 0u;
            }
        });
    CHECK_EQ(still_here, 0u);
}

TEST(social_makes_favor_edges_and_lifts_standing)
{
    // 跑 3 年（社交机会 20%/年）：事件表 social 非空且 amount==1 的
    // 条目存在；关系网出现恩义边；存在声望升过（by_sect > 0）的活人。
    // 钳位：跑 200 年后所有声望维度都在 [-5, 5]。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(3);

    std::uint32_t favor_events = 0;
    for (const auto& ev : life.events())
    {
        favor_events += ev.kind == gamev2::life::LifeEventKind::social
                            && ev.amount == 1u
                        ? 1u
                        : 0u;
    }
    CHECK(favor_events > 0u);

    std::uint32_t favor_edges = 0;
    for (const auto& e : life.bonds().edges)
    {
        favor_edges += e.kind == gamev2::life::BondKind::favor ? 1u : 0u;
    }
    CHECK(favor_edges > 0u);

    std::uint32_t lifted = 0;
    life.ecs_world().view<const gamev2::life::Standing>().each(
        [&](const gamev2::life::Standing& st)
        {
            for (const auto v : st.by_sect)
            {
                lifted += v > 0 ? 1u : 0u;
            }
        });
    CHECK(lifted > 0u);

    life.run_years(197);
    life.ecs_world().view<const gamev2::life::Standing>().each(
        [&](const gamev2::life::Standing& st)
        {
            for (const auto v : st.by_sect)
            {
                CHECK(v >= -5);
                CHECK(v <= 5);
            }
        });
}

TEST(bullying_makes_feuds_and_grudge_deepens_them)
{
    // 立威（amount==2）事件存在；被压者的声望掉到负；仇怨边存在，
    // 且强度钳在 5 以内。
    const Stack s = make_stack(decisions_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(50);

    std::uint32_t bully = 0;
    for (const auto& ev : life.events())
    {
        bully += ev.kind == gamev2::life::LifeEventKind::social
                     && ev.amount == 2u
                     ? 1u
                     : 0u;
    }
    CHECK(bully > 0u);

    std::uint32_t feuds = 0;
    std::uint32_t deepened = 0;
    for (const auto& e : life.bonds().edges)
    {
        if (e.kind != gamev2::life::BondKind::feud)
        {
            continue;
        }
        ++feuds;
        CHECK(e.strength >= 1u);
        CHECK(e.strength <= 5u);
        deepened += e.strength >= 2u ? 1u : 0u;
    }
    CHECK(feuds > 0u);
    // 记仇/立威叠加会长出强边（强度 ≥2）。
    CHECK(deepened > 0u);

    // 被压者的声望掉到负：有人身上出现过负值。
    std::uint32_t negative = 0;
    life.ecs_world().view<const gamev2::life::Standing>().each(
        [&](const gamev2::life::Standing& st)
        {
            for (const auto v : st.by_sect)
            {
                negative += v < 0 ? 1u : 0u;
            }
        });
    CHECK(negative > 0u);
}

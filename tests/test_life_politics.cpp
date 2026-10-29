#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

/// 刀 4 的夹具：256²/seed13（刀 3 经济测试同一口径）。
gamev2::world::WorldParams politics_world(std::uint64_t seed)
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

/// 逐宗人数与职级配比。
struct Roster
{
    std::array<std::uint32_t, 8> total{};
    std::array<std::uint32_t, 8> master{};
    std::array<std::uint32_t, 8> elder{};
    std::array<std::uint32_t, 8> inner{};
    std::array<std::uint32_t, 8> outer{};
};

Roster census(gamev2::life::Life& life)
{
    Roster out;
    life.ecs_world()
        .view<const gamev2::life::SoulId, const gamev2::life::Office>()
        .each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Office& office)
            {
                const auto sect = static_cast<std::size_t>(id.sect);
                if (sect >= out.total.size())
                {
                    return;
                }
                ++out.total[sect];
                switch (office.kind)
                {
                case gamev2::life::OfficeKind::sect_master:
                    ++out.master[sect];
                    break;
                case gamev2::life::OfficeKind::elder:
                    ++out.elder[sect];
                    break;
                case gamev2::life::OfficeKind::inner:
                    ++out.inner[sect];
                    break;
                case gamev2::life::OfficeKind::outer:
                    ++out.outer[sect];
                    break;
                case gamev2::life::OfficeKind::none:
                    break;
                case gamev2::life::OfficeKind::count:
                    break;
                }
            });
    return out;
}

}  // namespace

TEST(initial_bond_web_covers_every_sect)
{
    // 初始关系网（刀 4 的 Task 7）：宗主对长老是师徒、长老对弟子是
    // 师徒、门内有亲缘、散修圈另有亲缘对。边由 bonds_init 子流派生，
    // 同 seed 逐位相同。
    //
    // 七宗各有一条宗主对长老的师徒边；散修圈不设师承（没有宗门），
    // 因此散修圈一条师徒边都没有——这是设计，不是漏洞。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::array<std::uint32_t, 8> master_edges{};
    std::uint32_t kin_edges = 0;
    for (const auto& e : life.bonds().edges)
    {
        if (e.kind == gamev2::life::BondKind::kin)
        {
            ++kin_edges;
        }
        if (e.kind != gamev2::life::BondKind::master)
        {
            continue;
        }
        for (const auto& entry : life.roster().entries)
        {
            if (entry.soul == e.a)
            {
                ++master_edges[static_cast<std::size_t>(entry.sect)];
                break;
            }
        }
    }
    for (std::size_t sect = 0; sect < 7; ++sect)
    {
        CHECK(master_edges[sect] > 0u);
    }
    CHECK_EQ(master_edges[7], 0u);
    CHECK(kin_edges > 0u);

    // 边的两端都在名册里：没有悬空边。
    std::uint32_t dangling = 0;
    for (const auto& e : life.bonds().edges)
    {
        bool a_ok = false;
        bool b_ok = false;
        for (const auto& entry : life.roster().entries)
        {
            a_ok = a_ok || entry.soul == e.a;
            b_ok = b_ok || entry.soul == e.b;
        }
        dangling += (a_ok && b_ok) ? 0u : 1u;
    }
    CHECK_EQ(dangling, 0u);
    CHECK_EQ(diag.orphan_bonds, 0u);
}

TEST(the_ladder_refills_offices_and_keeps_ratios)
{
    // 跑 50 年：晋升事件存在（新生儿都是外门，晋升必发生）；内门数
    // 跟得上在册 × kInnerNum/kInnerDen；长老数不超过缺额公式给的配额；
    // 每宗至多一个 sect_master。
    //
    // 长老数只能「不超过配额」：补位要求内门里有人到了筑基，空着就
    // 空着——位置不会凭空变出人来。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    // 峰值从开局数起算：开局的职级是按开局的在册数配的（generate
    // 用的是同一套缺额公式），第一年的普查已经晚了一步。
    std::array<std::uint32_t, 8> peak{};
    const Roster seeded = census(life);
    peak = seeded.total;
    for (std::uint32_t y = 0; y < 50; ++y)
    {
        life.run_years(1);
        const Roster r = census(life);
        for (std::size_t sect = 0; sect < 7; ++sect)
        {
            const auto n = r.total[sect];
            if (n == 0u)
            {
                continue;
            }
            CHECK(r.master[sect] <= 1u);
            const auto inner_floor =
                n * gamev2::life::kInnerNum / gamev2::life::kInnerDen - 1u;
            CHECK(r.inner[sect] + 1u >= inner_floor);
            // 长老数只能不超过缺额公式给的配额，而配额按当下在册数
            // 算——在册数会掉，掉下去之后已就的长老不会跟着退位。故
            // 上界取在册数的**峰值**上的配额：补位只在缺额时发生，
            // 长老数因此永远不超过它当初补位时的配额。
            peak[sect] = std::max(peak[sect], n);
            const auto elder_quota =
                std::max(gamev2::life::kMinElders,
                         peak[sect] / gamev2::life::kElderDivisor);
            CHECK(r.elder[sect] <= elder_quota);
        }
    }

    std::uint32_t promotions = 0;
    for (const auto& ev : life.events())
    {
        promotions += ev.kind == gamev2::life::LifeEventKind::promotion
                          ? 1u
                          : 0u;
    }
    CHECK(promotions > 0u);
}

TEST(succession_follows_master_death_within_a_year)
{
    // 长跑 300 年：succession 事件存在；宗主位不悬空——每年年末每宗
    // 至多一个宗主，且宗主缺位只出现在补位之前的那一年（政事段在
    // 同一步里补位，缺位不留到下一年）。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint32_t vacancies = 0;
    for (std::uint32_t y = 0; y < 300; ++y)
    {
        life.run_years(1);
        const Roster r = census(life);
        for (std::size_t sect = 0; sect < 7; ++sect)
        {
            CHECK(r.master[sect] <= 1u);
            if (r.total[sect] > 0u && r.master[sect] == 0u)
            {
                ++vacancies;
            }
        }
    }
    // 推举在年末补位，缺位不留到下一年：空悬只可能是补位失败（没有
    // 够格的候选人），而不是「忘了补」。
    CHECK(vacancies <= 1u);

    std::uint32_t successions = 0;
    for (const auto& ev : life.events())
    {
        successions += ev.kind == gamev2::life::LifeEventKind::succession
                           ? 1u
                           : 0u;
    }
    CHECK(successions > 0u);
}

TEST(the_starved_low_order_outer_flee_the_sect)
{
    // 断供宗（青囊/天机/九幽 mint=0）跑若干年：departure 事件存在；
    // 走者的宗籍变散修、职位清空、断供年数 ≥ 门限；出走事件落表时
    // 记的是出走前的宗门（断供宗）；出走者与原宗主结仇怨边。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(30);

    std::uint32_t departures = 0;
    for (const auto& ev : life.events())
    {
        if (ev.kind != gamev2::life::LifeEventKind::departure)
        {
            continue;
        }
        ++departures;
        // 事件落表时记的是出走前的宗门。太玄铸 260000、需求 91750，
        // 从不断供——太玄的人不该出走。
        CHECK(ev.sect != gamev2::history::Sect::taixuan);
    }
    CHECK(departures > 0u);

    // 走者仍在册的那些：宗籍已是散修、职位已清空、断供年数够门限。
    std::uint32_t checked = 0;
    life.ecs_world()
        .view<const gamev2::life::SoulId, const gamev2::life::Office>()
        .each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Office& office)
            {
                for (const auto& ev : life.events())
                {
                    if (ev.kind != gamev2::life::LifeEventKind::departure
                        || ev.actor != id.index)
                    {
                        continue;
                    }
                    ++checked;
                    CHECK_EQ(id.sect, gamev2::history::Sect::unowned);
                    CHECK_EQ(office.kind, gamev2::life::OfficeKind::none);
                    CHECK(office.starved >= gamev2::life::kStarveExitYears);
                    break;
                }
            });
    CHECK(checked > 0u);

    // 与原宗主结仇怨边：原宗主还活着时这条边在。
    std::uint32_t feuds_with_master = 0;
    for (const auto& ev : life.events())
    {
        if (ev.kind != gamev2::life::LifeEventKind::departure
            || ev.counterparty == 0u)
        {
            continue;
        }
        if (life.bonds().find(ev.actor, ev.counterparty,
                              gamev2::life::BondKind::feud)
            != nullptr)
        {
            ++feuds_with_master;
        }
    }
    CHECK(feuds_with_master > 0u);
    CHECK_EQ(diag.office_without_sect, 0u);
}

TEST(starved_counts_consecutive_shortfall_and_resets)
{
    // 断供计数：断供宗的成员连续断供年数逐年递增；一旦某年足额发放，
    // 计数清零。
    //
    // 256²/seed13：太玄（铸 260000）从不断供；丹霞（铸 80000、需求
    // 86750）头几年断供、金库攒够后第 5 年起足额发放、计数归零；其余
    // 五宗始终断供。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const auto peak_starved = [&](std::array<std::uint32_t, 8>& out)
    {
        out.fill(0u);
        life.ecs_world()
            .view<const gamev2::life::SoulId, const gamev2::life::Office>()
            .each(
                [&](const gamev2::life::SoulId& id,
                    const gamev2::life::Office& office)
                {
                    const auto sect = static_cast<std::size_t>(id.sect);
                    if (sect >= out.size())
                    {
                        return;
                    }
                    if (office.starved > out[sect])
                    {
                        out[sect] = office.starved;
                    }
                });
    };

    std::array<std::uint32_t, 8> peak{};
    for (std::uint32_t y = 1; y <= 4; ++y)
    {
        life.run_years(1);
        peak_starved(peak);
        CHECK_EQ(peak[0], 0u);  // 太玄从不断供
        for (std::size_t sect = 1; sect < 7; ++sect)
        {
            CHECK_EQ(peak[sect], y);
        }
    }
    // 第 5 年：丹霞攒够了金库，足额发放 → 计数归零。
    life.run_years(1);
    peak_starved(peak);
    CHECK_EQ(peak[0], 0u);
    CHECK_EQ(peak[1], 0u);
    for (std::size_t sect = 2; sect < 7; ++sect)
    {
        CHECK_EQ(peak[sect], 5u);
    }
    // 第 6 年：丹霞继续足额，其余五宗继续递增。
    life.run_years(1);
    peak_starved(peak);
    CHECK_EQ(peak[0], 0u);
    CHECK_EQ(peak[1], 0u);
    for (std::size_t sect = 2; sect < 7; ++sect)
    {
        CHECK_EQ(peak[sect], 6u);
    }
}

TEST(births_stop_in_shorted_sects_and_continue_elsewhere)
{
    // 收徒受供给约束：断供宗第 2 年起停收（断供年数断供的是「上年
    // 有没有克扣」，第 1 年 Economy 还没跑、shorted 全 false，照常收
    // 徒）；供给恢复的宗重新开收。
    //
    // 出生数按序号数：新生者的 SoulId::index 单调递增且从初始人口
    // 之后起算，因此「序号落在哪一段」就是「哪一年生的」。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint32_t max_index = 969u;
    const auto births_this_year = [&](std::array<std::uint32_t, 8>& out)
    {
        out.fill(0u);
        life.ecs_world().view<const gamev2::life::SoulId>().each(
            [&](const gamev2::life::SoulId& id)
            {
                const auto sect = static_cast<std::size_t>(id.sect);
                if (sect >= out.size() || id.index <= max_index)
                {
                    return;
                }
                ++out[sect];
            });
    };

    std::array<std::uint32_t, 8> born{};
    births_this_year(born);
    for (const auto b : born)
    {
        CHECK_EQ(b, 0u);  // 开局没有新生儿
    }

    // 第 1 年：七宗各收一徒（shorted 尚未置位）。
    life.run_years(1);
    births_this_year(born);
    for (std::size_t sect = 0; sect < 7; ++sect)
    {
        CHECK_EQ(born[sect], 1u);
    }
    CHECK_EQ(born[7], 0u);
    max_index = 976u;

    // 第 2–4 年：只有太玄（从未断供）收徒。
    for (std::uint32_t y = 2; y <= 4; ++y)
    {
        life.run_years(1);
        births_this_year(born);
        CHECK_EQ(born[0], 1u);
        for (std::size_t sect = 1; sect < 7; ++sect)
        {
            CHECK_EQ(born[sect], 0u);
        }
        max_index += born[0];
    }

    // 第 5、6 年：丹霞第 5 年足额发放 → 第 6 年起恢复收徒。
    life.run_years(1);
    births_this_year(born);
    CHECK_EQ(born[0], 1u);
    CHECK_EQ(born[1], 0u);
    max_index += born[0];
    life.run_years(1);
    births_this_year(born);
    CHECK_EQ(born[0], 1u);
    CHECK_EQ(born[1], 1u);
}

TEST(sect_members_buy_stones_from_their_own_vault)
{
    // 宗门弟子能从自家金库买进（差异 15）：攒够两千两换一枚下品，
    // 划转不是铸币——金库与私囊在恒等式的两侧同增同减，守恒不动。
    //
    // 场地：太玄外门弟子（铸 260000，份额 150，年烧毁 37）。私囊给
    // 足两千两，年底银两应当正好花光（只买得起一枚），石头多出那一
    // 枚。
    const Stack s = make_stack(politics_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    gamev2::ecs::EntityId target{};
    life.ecs_world().for_each_entity(
        [&](const gamev2::ecs::EntityId e)
        {
            if (target != gamev2::ecs::EntityId{})
            {
                return;
            }
            const auto* id = life.ecs_world().get_if<gamev2::life::SoulId>(e);
            if (id == nullptr || id->sect != gamev2::history::Sect::taixuan)
            {
                return;
            }
            const auto* office =
                life.ecs_world().get_if<gamev2::life::Office>(e);
            if (office == nullptr
                || office->kind != gamev2::life::OfficeKind::outer)
            {
                return;
            }
            target = e;
        });
    CHECK(target != gamev2::ecs::EntityId{});
    if (target == gamev2::ecs::EntityId{})
    {
        return;
    }

    auto* cult = life.ecs_world().get_if<gamev2::life::Cultivation>(target);
    auto* mortal = life.ecs_world().get_if<gamev2::life::Mortal>(target);
    auto* purse = life.ecs_world().get_if<gamev2::life::Purse>(target);
    auto* temper = life.ecs_world().get_if<gamev2::life::Temper>(target);
    CHECK(cult != nullptr && mortal != nullptr && purse != nullptr
          && temper != nullptr);
    if (cult == nullptr || mortal == nullptr || purse == nullptr
        || temper == nullptr)
    {
        return;
    }
    cult->realm = gamev2::life::Realm::yinqi;
    cult->progress = 0u;
    mortal->age = 30u;
    // 夹具动了私囊，账就得跟着动：恒等式的两侧要一起改，否则守恒
    // 诊断会把「夹具的改动」记成破账（刀 3 的死人不营生测试同一手法）。
    const auto original = purse->stone;
    purse->stone = 100u;
    if (purse->stone > original)
    {
        life.economy().minted_total += purse->stone - original;
    }
    else
    {
        life.economy().burned_total += original - purse->stone;
    }
    purse->silver = gamev2::life::kLiangPerStone;
    // 性格轴拨成守序中值：判据落到修炼，不发赏钱也不动落点——私囊
    // 的账因此只有「份额 + 买入 − 年烧毁」。
    temper->order = 5u;
    temper->valor = 3u;
    temper->mercy = 3u;
    temper->vigor = 3u;

    life.run_years(1);

    // 份额 150 + 买入 1 − 年烧毁 37 = 214；银两正好花光。
    CHECK_EQ(purse->stone, 100u + 150u + 1u - 37u);
    CHECK_EQ(purse->silver, 0u);
    CHECK_EQ(diag.stone_conservation_broken, 0u);

    // 长跑：非散修手里有灵石（经营攒钱买进）——下界钉死。
    life.run_years(199);
    std::uint32_t with_stone = 0;
    life.ecs_world()
        .view<const gamev2::life::SoulId, const gamev2::life::Purse>()
        .each(
            [&](const gamev2::life::SoulId& id,
                const gamev2::life::Purse& p)
            {
                if (id.sect != gamev2::history::Sect::unowned
                    && p.stone > 0u)
                {
                    ++with_stone;
                }
            });
    CHECK(with_stone > 0u);
}

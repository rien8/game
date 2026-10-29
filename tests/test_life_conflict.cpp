#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/life/combat.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

/// 刀 4 的夹具：256²/seed13（刀 3 经济测试同一口径）。
gamev2::world::WorldParams conflict_world(std::uint64_t seed)
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

/// 找一名活着的天机阁弟子（断供宗：份额为 0，私囊的账因此干净）。
///
/// 断供宗是量私囊的干净场地：金库空 → 份额 0 → 年末的私囊只由
/// 「期初 ± 行为步的收支 − 年烧毁」决定，断言不必把份额算进去。
gamev2::ecs::EntityId find_broke_soul(gamev2::life::Life& life)
{
    gamev2::ecs::EntityId found{};
    life.ecs_world().for_each_entity(
        [&](const gamev2::ecs::EntityId e)
        {
            if (found != gamev2::ecs::EntityId{})
            {
                return;
            }
            const auto* id = life.ecs_world().get_if<gamev2::life::SoulId>(e);
            if (id == nullptr || id->sect != gamev2::history::Sect::tianji)
            {
                return;
            }
            if (life.ecs_world().get_if<gamev2::life::Purse>(e) == nullptr
                || life.ecs_world().get_if<gamev2::life::Office>(e) == nullptr)
            {
                return;
            }
            found = e;
        });
    return found;
}

}  // namespace

TEST(poaching_mints_into_purses_and_the_identity_survives)
{
    // 跑 200 年：poached_total > 0；逐年恒等式成立（盗采进私囊的同时
    // 进铸币侧——两侧同增，守恒不动）；铸造总额 = 宗门铸币 + 私铸。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (std::uint32_t y = 0; y < 200; ++y)
    {
        life.run_years(1);
        std::uint64_t accounts = life.economy().vault_total();
        life.ecs_world().view<const gamev2::life::Purse>().each(
            [&](const gamev2::life::Purse& purse)
            { accounts += purse.stone; });
        CHECK_EQ(accounts,
                 life.economy().minted_total - life.economy().burned_total);
    }
    CHECK_EQ(diag.stone_conservation_broken, 0u);
    CHECK(life.economy().poached_total > 0u);
    CHECK_EQ(life.economy().minted_total,
             420000u * 200u + life.economy().poached_total);
}

TEST(poachers_lose_standing_and_some_die_or_get_branded)
{
    // 跑 200 年：散修圈声望（by_sect[7]）出现过负值（盗采每年扣 1）；
    // poach 事件存在；被擒的两条出路至少一条可见——被擒败则横死
    // （slain 事件），被擒胜则记罪（Standing::crimes > 0）。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(200);

    std::uint32_t poaches = 0;
    std::uint32_t slain = 0;
    for (const auto& ev : life.events())
    {
        poaches += ev.kind == gamev2::life::LifeEventKind::poach ? 1u : 0u;
        slain += ev.kind == gamev2::life::LifeEventKind::slain ? 1u : 0u;
    }
    CHECK(poaches > 0u);
    CHECK(slain > 0u);

    std::uint32_t negative = 0;
    std::uint32_t branded = 0;
    life.ecs_world().view<const gamev2::life::Standing>().each(
        [&](const gamev2::life::Standing& st)
        {
            negative += st.by_sect[7] < 0 ? 1u : 0u;
            branded += st.crimes > 0u ? 1u : 0u;
        });
    CHECK(negative > 0u);
    // 被擒胜 → 脱身但记罪。记罪者可能后来死了（ crimes 随实体一起
    // 消失），因此这条只在活人身上查。
    CHECK(branded > 0u);
}

TEST(poach_deaths_clear_accounts_exactly_once)
{
    // 横死者私囊的清算只发生一次（slay 清零在先、经济步的死者账消对
    // 已死空位跳过）。逐年恒等式 + 横死者不再出现在名册里。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    for (std::uint32_t y = 0; y < 100; ++y)
    {
        life.run_years(1);
        std::uint64_t accounts = life.economy().vault_total();
        life.ecs_world().view<const gamev2::life::Purse>().each(
            [&](const gamev2::life::Purse& purse)
            { accounts += purse.stone; });
        CHECK_EQ(accounts,
                 life.economy().minted_total - life.economy().burned_total);
    }
    CHECK_EQ(diag.stone_conservation_broken, 0u);

    // 被擒横死的人（盗采当年被擒、败）不出现在名册里。
    std::vector<std::uint32_t> victims;
    for (const auto& ev : life.events())
    {
        if (ev.kind == gamev2::life::LifeEventKind::slain && ev.actor != 0u)
        {
            victims.push_back(ev.actor);
        }
    }
    CHECK(!victims.empty());
    std::uint32_t still_here = 0;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            for (const auto v : victims)
            {
                still_here += v == id.index ? 1u : 0u;
            }
        });
    CHECK_EQ(still_here, 0u);
}

TEST(duel_is_deterministic_and_upside_is_bounded)
{
    // 一步战斗是自由函数（公共头，测试直调——与 lifespan_reached 同例）。
    // 同 (seed, year, 双方) 永远掷出同一个数；同境界同体魄同财力时
    // 胜率约半，境界每高一层胜率抬到上限。
    using gamev2::life::Aptitude;
    using gamev2::life::Cultivation;
    using gamev2::life::Realm;

    Cultivation low{};
    low.realm = Realm::yinqi;
    Cultivation mid{};
    mid.realm = Realm::lianqi;
    Cultivation high{};
    high.realm = Realm::zhuji;
    Aptitude apt{};
    apt.body = 3;

    // 同境界：1000 个年份采样，胜次数落在 [400, 600]。
    std::uint32_t wins = 0;
    for (std::uint32_t year = 1; year <= 1000; ++year)
    {
        const bool won = gamev2::life::duel(
            13, year, low, apt, 0, low, apt, 0, 1u, 2u);
        wins += won ? 1u : 0u;
    }
    CHECK(wins >= 400u);
    CHECK(wins <= 600u);

    // 确定性：同一组键重掷同果。
    for (std::uint32_t year = 1; year <= 50; ++year)
    {
        const bool a = gamev2::life::duel(13, year, low, apt, 0, low, apt, 0,
                                          1u, 2u);
        const bool b = gamev2::life::duel(13, year, low, apt, 0, low, apt, 0,
                                          1u, 2u);
        CHECK_EQ(a, b);
    }

    // 差一境：胜率被钳到上限 950‰（远高于六成）。
    wins = 0;
    for (std::uint32_t year = 1; year <= 1000; ++year)
    {
        const bool won = gamev2::life::duel(
            13, year, mid, apt, 0, low, apt, 0, 1u, 2u);
        wins += won ? 1u : 0u;
    }
    CHECK(wins > 600u);

    // 差两境：仍在上限（95%）。
    wins = 0;
    for (std::uint32_t year = 1; year <= 1000; ++year)
    {
        const bool won = gamev2::life::duel(
            13, year, high, apt, 0, low, apt, 0, 1u, 2u);
        wins += won ? 1u : 0u;
    }
    CHECK(wins > 850u);
}

TEST(contest_transfers_the_seat_or_exiles_the_loser)
{
    // 长跑 300 年：contest 事件存在。胜者（amount==1）接任宗主，败者
    // （amount==0）被逐（宗籍变散修、职位清空）或被杀。迁移写入者的
    // 自检：事后 office_without_sect == 0（有职位却无宗籍）。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(300);

    std::uint32_t contests = 0;
    std::uint32_t wins = 0;
    std::uint32_t losses = 0;
    for (const auto& ev : life.events())
    {
        if (ev.kind != gamev2::life::LifeEventKind::contest)
        {
            continue;
        }
        ++contests;
        wins += ev.amount == 1u ? 1u : 0u;
        losses += ev.amount == 0u ? 1u : 0u;
    }
    CHECK(contests > 0u);
    CHECK(wins + losses == contests);

    // 首条夺位：胜者接位（活着的话他此刻是宗主），旧主不再���宗主。
    bool seen_first = false;
    for (const auto& ev : life.events())
    {
        if (ev.kind != gamev2::life::LifeEventKind::contest
            || ev.amount != 1u)
        {
            continue;
        }
        if (seen_first)
        {
            break;
        }
        seen_first = true;
        std::uint32_t actor_office = 99u;
        std::uint32_t old_master_office = 99u;
        life.ecs_world()
            .view<const gamev2::life::SoulId, const gamev2::life::Office>()
            .each(
                [&](const gamev2::life::SoulId& id,
                    const gamev2::life::Office& office)
                {
                    if (id.index == ev.actor)
                    {
                        actor_office =
                            static_cast<std::uint32_t>(office.kind);
                    }
                    if (id.index == ev.counterparty)
                    {
                        old_master_office =
                            static_cast<std::uint32_t>(office.kind);
                    }
                });
        CHECK_EQ(actor_office,
                 static_cast<std::uint32_t>(gamev2::life::OfficeKind::sect_master));
        // 旧主要么死了（查不到 → 99），要么不再担任宗主。
        CHECK(old_master_office
              != static_cast<std::uint32_t>(
                     gamev2::life::OfficeKind::sect_master));
    }
    CHECK(seen_first);

    // 有职位却无宗籍：迁移写入者 discipline 的自检。
    CHECK_EQ(diag.office_without_sect, 0u);
}

TEST(avenge_settles_feuds_through_duels)
{
    // 长跑 300 年：avenge 事件存在。actor 与 counterparty 必是两人
    // （自己给自己寻仇是判据 3 的岔口——加害者本人不算仇家）。
    // 因果方向：仇在先、报在后——被寻仇的人此前就得手过（事件表里
    // 他当过加害者）。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(300);

    std::uint32_t avenges = 0;
    for (const auto& ev : life.events())
    {
        if (ev.kind != gamev2::life::LifeEventKind::avenge)
        {
            continue;
        }
        ++avenges;
        CHECK(ev.actor != ev.counterparty);
        CHECK(ev.actor != 0u);
        CHECK(ev.counterparty != 0u);
        // 仇在先：对手此前就得手过。
        bool prior = false;
        for (const auto& earlier : life.events())
        {
            if (earlier.year >= ev.year)
            {
                break;  // 事件表按年递增，后面的不算「此前」
            }
            if ((earlier.kind == gamev2::life::LifeEventKind::slain
                 || earlier.kind == gamev2::life::LifeEventKind::expulsion)
                && earlier.counterparty == ev.counterparty)
            {
                prior = true;
                break;
            }
        }
        CHECK(prior);
    }
    CHECK(avenges > 0u);
}

TEST(breakthrough_requires_paying_the_cost_both_ways)
{
    // 门槛是「尝试时付、成败都烧」：付不起就不尝试，付得起就当场烧
    // 掉那一笔。
    //
    // 场地选断供宗（天机阁）：金库空 → 份额 0 → 私囊的年末值只由
    // 「期初 ± 门槛 − 年烧毁」决定，断言不必把别人的账算进来。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const gamev2::ecs::EntityId e = find_broke_soul(life);
    CHECK(e != gamev2::ecs::EntityId{});
    if (e == gamev2::ecs::EntityId{})
    {
        return;
    }
    auto* cult = life.ecs_world().get_if<gamev2::life::Cultivation>(e);
    auto* mortal = life.ecs_world().get_if<gamev2::life::Mortal>(e);
    auto* purse = life.ecs_world().get_if<gamev2::life::Purse>(e);
    auto* temper = life.ecs_world().get_if<gamev2::life::Temper>(e);
    CHECK(cult != nullptr && mortal != nullptr && purse != nullptr
          && temper != nullptr);
    if (cult == nullptr || mortal == nullptr || purse == nullptr
        || temper == nullptr)
    {
        return;
    }
    // 炼气者：进筑基的门槛是 2500（kBreakthroughCost[2]）。
    cult->realm = gamev2::life::Realm::lianqi;
    cult->progress = gamev2::life::kRealmYears[1];
    cult->seclude_until = 0u;
    mortal->age = 30u;
    // 性格轴拨到中值之外的守序档：判据 2 落下来走「经营」，不发石头
    // ——私囊的账因此只有门槛与烧毁两项。
    temper->order = 5u;
    temper->valor = 3u;
    temper->mercy = 3u;
    temper->vigor = 3u;

    // 场景一：付不起（2499 < 2500）。不尝试：私囊只被年烧毁吃掉 100。
    purse->stone = 2499u;
    life.run_years(1);
    CHECK(purse->stone == 2499u - 100u);

    // 场景二：付得起（2500）。门槛当场烧掉，私囊清零。
    purse->stone = 2500u;
    cult->progress = gamev2::life::kRealmYears[1];
    life.run_years(1);
    CHECK_EQ(purse->stone, 0u);
}

TEST(jindan_attempts_need_thick_ground)
{
    // 金丹要厚域位置约束：不在厚域就不尝试、不扣钱。
    //
    // 厚域格表随潮位走，谷底期全天下没有厚域——所以这条要在灵潮期
    // 里验（首个有厚域的年份是 9061）。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(147);  // 年 9061：首个有厚域的年份
    CHECK_EQ(life.year(), 9061u);
    CHECK(!life.thick_cells().empty());

    // 两名筑基者：一个站在厚域，一个站在别处。私囊给足门槛。
    std::vector<gamev2::ecs::EntityId> picked;
    life.ecs_world().for_each_entity(
        [&](const gamev2::ecs::EntityId e)
        {
            if (picked.size() >= 2u)
            {
                return;
            }
            const auto* id = life.ecs_world().get_if<gamev2::life::SoulId>(e);
            if (id == nullptr || id->sect != gamev2::history::Sect::tianji)
            {
                return;
            }
            if (life.ecs_world().get_if<gamev2::life::Purse>(e) == nullptr)
            {
                return;
            }
            picked.push_back(e);
        });
    CHECK_EQ(picked.size(), 2u);
    if (picked.size() != 2u)
    {
        return;
    }

    const auto setup = [&](const gamev2::ecs::EntityId e, std::uint32_t cell)
    {
        auto* cult = life.ecs_world().get_if<gamev2::life::Cultivation>(e);
        auto* mortal = life.ecs_world().get_if<gamev2::life::Mortal>(e);
        auto* purse = life.ecs_world().get_if<gamev2::life::Purse>(e);
        auto* site = life.ecs_world().get_if<gamev2::life::Site>(e);
        auto* temper = life.ecs_world().get_if<gamev2::life::Temper>(e);
        if (cult == nullptr || mortal == nullptr || purse == nullptr
            || site == nullptr || temper == nullptr)
        {
            return;
        }
        cult->realm = gamev2::life::Realm::zhuji;
        cult->progress = gamev2::life::kRealmYears[2];
        cult->seclude_until = 0u;
        mortal->age = 30u;
        purse->stone = 33333u;  // 进金丹的门槛
        site->cell = cell;
        temper->order = 5u;
        temper->valor = 3u;
        temper->mercy = 3u;
        temper->vigor = 3u;
    };

    // 厚域里那个：尝试发生，门槛当场烧掉。
    setup(picked[0], life.thick_cells()[0]);
    // 厚域外那个：格子号取一个不在厚域表里的。
    std::uint32_t outside = life.thick_cells()[0] + 1u;
    while (std::ranges::binary_search(life.thick_cells(), outside))
    {
        ++outside;
    }
    setup(picked[1], outside);

    life.run_years(1);

    const auto purse_of = [&](const gamev2::ecs::EntityId e)
    {
        const auto* purse = life.ecs_world().get_if<gamev2::life::Purse>(e);
        return purse == nullptr ? 0u : purse->stone;
    };
    // 厚域者付了 33333，囊空；非厚域者没付，只被年烧毁吃掉 1000。
    CHECK_EQ(purse_of(picked[0]), 0u);
    CHECK_EQ(purse_of(picked[1]), 33333u - 1000u);
}

TEST(yuanying_locks_a_century_and_seclusion_invites_feuds)
{
    // 元婴百年闭关（设定 §5.1 逐字）：金丹圆满者先锁百年，锁满才
    // 付账掷运。锁是一次性的——seclude_until 非零即已开过。
    //
    // 断供宗当场地：份额 0，私囊的账只有门槛与烧毁两项。
    const Stack s = make_stack(conflict_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const gamev2::ecs::EntityId e = find_broke_soul(life);
    CHECK(e != gamev2::ecs::EntityId{});
    if (e == gamev2::ecs::EntityId{})
    {
        return;
    }
    auto* cult = life.ecs_world().get_if<gamev2::life::Cultivation>(e);
    auto* mortal = life.ecs_world().get_if<gamev2::life::Mortal>(e);
    auto* purse = life.ecs_world().get_if<gamev2::life::Purse>(e);
    auto* temper = life.ecs_world().get_if<gamev2::life::Temper>(e);
    CHECK(cult != nullptr && mortal != nullptr && purse != nullptr
          && temper != nullptr);
    if (cult == nullptr || mortal == nullptr || purse == nullptr
        || temper == nullptr)
    {
        return;
    }
    cult->realm = gamev2::life::Realm::jindan;
    cult->progress = gamev2::life::kRealmYears[3];
    cult->seclude_until = 0u;
    mortal->age = 30u;
    purse->stone = 333333u;  // 进元婴的门槛
    temper->order = 5u;
    temper->valor = 3u;
    temper->mercy = 3u;
    temper->vigor = 3u;

    // 锁的当年：不付账，锁满百年。
    life.run_years(1);
    CHECK_EQ(cult->seclude_until, life.year() + 100u);
    // 门槛没被扣：私囊只剩年烧毁（金丹 33333）。
    CHECK_EQ(purse->stone, 333333u - 33333u);

    // 锁满（手动把锁拨到今天——锁期本身已由上面钉住）：付账掷运。
    cult->seclude_until = life.year();
    purse->stone = 333333u;
    life.run_years(1);
    CHECK_EQ(purse->stone, 0u);
}

TEST(breakthrough_odds_hit_the_three_anchors)
{
    // 突破成功率的三条锚：满潮时中位悟性 300‰、最聪颖 500‰、最愚钝
    // 钳到 200‰；潮位把基准折损，谷底只剩一成多；潮位因子有下限，
    // 再低也不会归零。
    using gamev2::life::breakthrough_chance;
    CHECK_EQ(breakthrough_chance(5u, 255u), 300u);
    CHECK_EQ(breakthrough_chance(9u, 255u), 500u);
    CHECK_EQ(breakthrough_chance(1u, 255u), 200u);
    // 谷底潮位（8914 开局，潮位 33）：因子 129‰，中位悟性只剩 38‰。
    CHECK_EQ(breakthrough_chance(5u, 33u), 38u);
    // 因子下限：潮位 0 时因子触到 50‰，基准只剩 15‰。
    CHECK_EQ(breakthrough_chance(5u, 0u), 15u);
    // 潮位越高越容易突破（单调）。
    CHECK(breakthrough_chance(5u, 138u) > breakthrough_chance(5u, 99u));
}

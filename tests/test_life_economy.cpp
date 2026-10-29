#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/economy.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

/// 刀 3 夹具：256²。128² 只有太玄有活脉，分配与断供测不出贫富差；
/// 256²/seed13 实测覆盖富宗（太玄）、小宗（丹霞/寒江/万兽各有 1 条
/// 主脉）、断供宗（青囊/天机/九幽 0 脉）三态（计划探针读数）。
gamev2::world::WorldParams economy_world(std::uint64_t seed)
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

/// 曾经盗采过的身位序号（按 SoulId::index）。
///
/// 刀 4 起私囊里多了一块来路：盗采私铸直接进私囊（不进金库）。判据 2
/// （「灵石不够烧」）在开局人人成立——私囊全是 0——于是悖逆者（order ≤2）
/// 头一年就集体起盗采心，分配类断言若不摘掉他们，量到的就不是分配
/// 次序而是私铸。
///
/// 摘的依据是事件表：它是「谁在何年做了什么」的唯一权威（设计 §8）。
/// 逐帧看 Motive 不够——私囊里的石头是历年攒下的，今年没盗采的人身上
/// 照样有往年盗来的石头。
std::vector<char> ever_poached(const gamev2::life::Life& life)
{
    std::uint32_t max_index = 0;
    for (const auto& ev : life.events())
    {
        max_index = std::max(max_index, ev.actor);
    }
    std::vector<char> flags(max_index + 1u, 0);
    for (const auto& ev : life.events())
    {
        if (ev.kind == gamev2::life::LifeEventKind::poach)
        {
            flags[ev.actor] = 1;
        }
    }
    return flags;
}

}  // namespace

TEST(every_soul_carries_an_empty_purse)
{
    // 有供给就得有账本。私囊挂在每个活人身上——初始 970 人开局两手
    // 空空（设定没给开局身家，0 是「从零开始」的默认）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    CHECK(!s.era.veins.empty());
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    std::uint32_t purses = 0;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Purse>().each(
        [&](const gamev2::life::SoulId&,
            const gamev2::life::Purse& purse)
        {
            ++purses;
            CHECK_EQ(purse.stone, 0u);
            CHECK_EQ(purse.silver, 0u);
        });
    CHECK_EQ(purses, 970u);

    // 每个有名有姓的都得有账本——包括本年新入门的孩子。少了私囊的
    // 实体在经济步里隐身：不领份额、不营生、不烧毁，而这一切在别处
    // 看不出破绽（终审 B2：这条断言必须打在有新生儿的世界上，否则
    // 恒真）。跑一年让收徒落地——新生儿组件随年末 flush 落地。
    life.run_years(1);

    std::uint32_t with_purse = 0;
    std::uint32_t with_name = 0;
    std::uint32_t newcomers = 0;
    life.ecs_world().view<const gamev2::life::SoulId,
                          const gamev2::life::Purse>().each(
        [&](const gamev2::life::SoulId&,
            const gamev2::life::Purse&)
        { ++with_purse; });
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            ++with_name;
            if (id.index >= 970u)
            {
                ++newcomers;
            }
        });
    // 新生儿确实在册。判据是「新生儿在册」而非「总人口增长」——刀 4
    // 起众生会横死（历练遇险、盗采被擒、寻仇败北），开局那一年就有人
    // 死，总人口不增反减（256²/seed13 实测：970 → 957，另有 7 名新生
    // 儿落地，序号 970–976）。
    CHECK(newcomers > 0u);
    CHECK_EQ(with_purse, with_name);
}

TEST(the_mint_matches_the_vein_anchors)
{
    // 铸币表两头钉：逐宗读数与天下合计 == 420000 下品（设定 §6.1.1
    // 的天下年产 4200 中品折算）。铸币基数是当代活脉产能快照（口径
    // 裁定 1），刀 5 换 FactionOutcome 后「天下合计」断言不变。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const auto& mint = life.economy().mint_per_year;
    CHECK_EQ(mint[0], 260000u);  // 太玄：1 龙脉 + 4 主脉
    CHECK_EQ(mint[1], 80000u);   // 丹霞：2 主脉
    CHECK_EQ(mint[2], 40000u);   // 寒江：1 主脉
    CHECK_EQ(mint[3], 40000u);   // 万兽：1 主脉
    CHECK_EQ(mint[4], 0u);       // 青囊：失脉
    CHECK_EQ(mint[5], 0u);       // 天机阁：无地
    CHECK_EQ(mint[6], 0u);       // 九幽宫：失脉
    CHECK_EQ(mint[7], 0u);       // 无主：支脉全枯
    std::uint64_t total = 0;
    for (const auto m : mint)
    {
        total += m;
    }
    CHECK_EQ(total, 420000u);
}

TEST(the_identity_holds_every_year)
{
    // 设计 §3.5 的角色层恒等式：Σ(所有金库 + 所有私囊) = 累计铸造 −
    // 累计烧毁。逐年结算后校验，破一次诊断 +1。跑 300 年——跨度盖过
    // 初始引气队列的寿元死亡（死者的私囊随死烧毁，口径裁定 13），
    // 也盖过「同年寿尽又突破」的漏损窗口（终审 B1：那个漏损在 128²
    // 的 seed 13 第 291 年、seed 15 约第 196 年出现，100 年跑不到
    // 它）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (std::uint32_t y = 0; y < 300; ++y)
    {
        life.run_years(1);

        std::uint64_t accounts = life.economy().vault_total();
        life.ecs_world().view<const gamev2::life::SoulId,
                              const gamev2::life::Purse>().each(
            [&](const gamev2::life::SoulId&,
                const gamev2::life::Purse& purse)
            { accounts += purse.stone; });
        CHECK_EQ(accounts,
                 life.economy().minted_total
                     - life.economy().burned_total);
    }
    CHECK_EQ(diag.stone_conservation_broken, 0u);
    CHECK_EQ(diag.office_without_sect, 0u);
    // 铸币表逐年入账：420000 × 300，逐年精确。盗采的私铸走同一条铸币
    // 侧（恒等式两侧同增，守恒不动），因此铸造总额 = 宗门铸币 + 私铸。
    CHECK(life.economy().poached_total > 0u);
    CHECK_EQ(life.economy().minted_total,
             126000000u + life.economy().poached_total);
}

TEST(the_same_seed_gives_the_same_ledger)
{
    // 经济全是确定性算术（无掷运），同 seed 两个世界跑 100 年，账本
    // 逐字段逐位相同；私囊的「有钱人计数」作为私囊分布的代表比对。
    const Stack a = make_stack(economy_world(13));
    const Stack b = make_stack(economy_world(13));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();
    life1.run_years(100);
    life2.run_years(100);

    CHECK_EQ(life1.economy().minted_total, life2.economy().minted_total);
    CHECK_EQ(life1.economy().burned_total, life2.economy().burned_total);
    CHECK(life1.economy().vault == life2.economy().vault);

    using gamev2::life::Purse;
    std::array<std::uint64_t, 2> h1{};
    std::array<std::uint64_t, 2> h2{};
    life1.ecs_world().view<const Purse>().each(
        [&](const Purse& p)
        { ++h1[p.stone > 0u ? 1u : 0u]; });
    life2.ecs_world().view<const Purse>().each(
        [&](const Purse& p)
        { ++h2[p.stone > 0u ? 1u : 0u]; });
    CHECK(h1 == h2);
}

TEST(the_rich_sect_pays_and_accumulates)
{
    // 设计 §3.3：宗主最高优先、长老高份额。首年太玄铸 260000、需求
    // 91750——全员足额，金库结余 168250（main 上的活脉分布：1 龙脉
    // + 4 主脉）。
    // 私囊实得要与「份额 − 当年烧毁」对得上：宗主引气级不可能，
    // 拿外门引气弟子的账做精确断言（份额 150 − 烧毁 37 = 113）。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    // 金库结余：260000 − 91750 = 168250。刀 3 读数 164600 已作废——
    // 刀 4 的政事段改变了分配结构：晋升阶梯改写职级配比（外门 150
    // → 内门 500），横死者不留职位因而不领份额（活人的份额照发）。
    CHECK_EQ(life.economy().vault[0], 168250u);

    using gamev2::life::Office;
    using gamev2::life::OfficeKind;
    using gamev2::life::Purse;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    const std::vector<char> poached = ever_poached(life);
    std::uint32_t checked = 0;
    life.ecs_world().view<const SoulId, const Office, const Purse,
                          const gamev2::life::Cultivation>().each(
        [&](const SoulId& id, const Office& office, const Purse& purse,
            const gamev2::life::Cultivation& cult)
        {
            if (id.index >= 970u)  // 初始队列之外是本年新生儿：组件随
            {                      // 年末 flush 才落地，经济步发不到，
                return;            // 探针表 168250 也按初始名单算
            }
            if (id.sect != gamev2::history::Sect::taixuan
                || office.kind != OfficeKind::outer
                || cult.realm != Realm::yinqi)
            {
                return;
            }
            // 盗采者的私囊里另有私铸的一块：量分配次序时摘掉他们。
            if (id.index < poached.size() && poached[id.index] != 0)
            {
                return;
            }
            // 外门引气：份额 150 − 烧毁 37。
            CHECK_EQ(purse.stone, 150u - 37u);
            ++checked;
        });
    CHECK(checked > 0u);
}

TEST(the_short_sect_cuts_from_the_bottom)
{
    // 设计 §3.3 的克扣次序：外门先饿、内门次之、长老宗主不动。丹霞
    // 首年铸 80000（main 上两条主脉）、需求 86750——宗主足额 35000、
    // 长老足额各 3000、内门足额各 500，剩下 3000 给 65 名外门：每人
    // 46（份额 150 的三成），余 10 入库。钱不够时先摊薄底层。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    CHECK_EQ(life.economy().vault[1], 10u);

    using gamev2::life::Office;
    using gamev2::life::OfficeKind;
    using gamev2::life::Purse;
    using gamev2::life::SoulId;
    const std::vector<char> poached = ever_poached(life);
    std::uint32_t outer_seen = 0;
    std::uint32_t inner_with_stones = 0;
    std::uint32_t elders_seen = 0;
    life.ecs_world().view<const SoulId, const Office, const Purse,
                          const gamev2::life::Cultivation>().each(
        [&](const SoulId& id, const Office& office, const Purse& purse,
            const gamev2::life::Cultivation&)
        {
            if (id.index >= 970u)  // 同太玄测试：断言只对初始队列，
            {
                return;            // 新生儿年末 flush 后才有组件
            }
            if (id.sect != gamev2::history::Sect::daxia)
            {
                return;
            }
            // 盗采者的私囊里另有私铸的一块：量分配次序时摘掉他们。
            if (id.index < poached.size() && poached[id.index] != 0)
            {
                return;
            }
            if (office.kind == OfficeKind::outer)
            {
                // 外门先饿：份额 150 被摊薄到 46（实得还要再减烧毁）。
                ++outer_seen;
                CHECK(purse.stone <= 46u);
            }
            else if (office.kind == OfficeKind::inner)
            {
                // 内门次之，但这里仍够：实得减烧毁不超过标准份 500。
                CHECK(purse.stone <= 500u);
                inner_with_stones += purse.stone > 0u ? 1u : 0u;
            }
            else if (office.kind == OfficeKind::elder)
            {
                ++elders_seen;
                // 长老不动：拿到足额 3000（减各自境界的烧毁）。
                CHECK(purse.stone <= 3000u);
            }
            else if (office.kind == OfficeKind::sect_master)
            {
                CHECK(purse.stone <= 35000u);
            }
        });
    CHECK(outer_seen > 0u);
    CHECK(inner_with_stones > 0u);
    CHECK(elders_seen > 0u);
}

TEST(the_empty_vault_leaves_everyone_with_nothing)
{
    // 断供的另一端：金库一滴不剩（金库空），全宗分文不得。天机阁无地
    // 无脉（铸币 0），首年便是这一态——它没有份额可发。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(1);

    CHECK_EQ(life.economy().vault[5], 0u);

    using gamev2::life::Purse;
    using gamev2::life::SoulId;
    const std::vector<char> poached = ever_poached(life);
    std::uint32_t members = 0;
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& purse)
        {
            if (id.sect != gamev2::history::Sect::tianji)
            {
                return;
            }
            ++members;
            // 盗采是私铸：石头直接进私囊不过公账，断供宗里也有人靠
            // 它攒下灵石。断供宗「分文不得」指的是份额，私铸不算。
            if (id.index < poached.size() && poached[id.index] != 0)
            {
                return;
            }
            CHECK_EQ(purse.stone, 0u);
        });
    CHECK(members > 0u);
}

TEST(the_free_cultivator_needs_thirty_three_years)
{
    // 设定 §6.4 逐字的绝望数学：年入约六十两、一枚下品折两千两——
    // 2000/60 = 33.33，第 33 年末 1980 两买不起，第 34 年末 2040 两
    // 才够一次（且公账无钱、买不进——见下一条）。三十三年一枚不调参。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::Purse;
    using gamev2::life::SoulId;

    // 散修人数逐年变（十年一批），银两总量不是 60×N 的定值——断言
    // 「最富散修」：33 年时最富的散修也买不起一枚（1980 < 2000），
    // 34 年 2040 两够了但公账为 0、买不进。
    life.run_years(33);
    std::uint32_t people = 0;
    std::uint64_t richest = 0;
    std::uint32_t over_stone = 0;
    std::uint32_t over_stone_plain = 0;
    std::vector<char> poached = ever_poached(life);
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect != gamev2::history::Sect::unowned)
            {
                return;
            }
            ++people;
            richest = richest > p.silver ? richest : p.silver;
            over_stone += p.stone > 0u ? 1u : 0u;
            const bool ever = id.index < poached.size()
                              && poached[id.index] != 0;
            over_stone_plain += (p.stone > 0u && !ever) ? 1u : 0u;
        });
    CHECK(people > 0u);
    // 营生只发银两：没盗采过的散修，第 33 年一枚灵石也没有
    //（33×60 = 1980 < 2000），且营生不产 stone——铸币是灵石的宗门侧
    // 唯一来源（设计 §3.5「没有第二个来源」；无主金库为 0，买入永不
    // 成交）。盗采的私铸是第二个来源，但那只进盗采者的私囊。
    CHECK_EQ(over_stone_plain, 0u);
    // 摘人不掏空断言：盗采者手里确实有灵石（实测 165 人）。
    CHECK(over_stone > 0u);
    CHECK_EQ(richest, 33u * gamev2::life::kFreeCultivatorIncomeLiang);

    life.run_years(1);  // 第 34 年
    std::uint64_t richest34 = 0;
    over_stone = 0;
    over_stone_plain = 0;
    poached = ever_poached(life);
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect != gamev2::history::Sect::unowned)
            {
                return;
            }
            richest34 = richest34 > p.silver ? richest34 : p.silver;
            over_stone += p.stone > 0u ? 1u : 0u;
            const bool ever = id.index < poached.size()
                              && poached[id.index] != 0;
            over_stone_plain += (p.stone > 0u && !ever) ? 1u : 0u;
        });
    // 34×60 = 2040 ≥ 2000：够了——但参考世界无主脉全枯、公账为 0，
    // 买不进，银两留着（口径裁定 8：买入从无主金库划转，不铸币）。
    CHECK_EQ(richest34, 34u * gamev2::life::kFreeCultivatorIncomeLiang);
    CHECK_EQ(over_stone_plain, 0u);
    CHECK(over_stone > 0u);
    CHECK_EQ(life.economy().vault[7], 0u);
}

TEST(the_dead_neither_work_nor_buy)
{
    // 寿尽者的账消在年末 flush 之后，经济步当帧仍看得见他们。营生与
    // 买入都必须过寿元判据：否则死者领完银两、把公账灵石买走，随实体
    // 销毁一起烧掉——公账在账外少一枚。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::ecs::EntityId;
    using gamev2::life::Cultivation;
    using gamev2::life::Mortal;
    using gamev2::life::Purse;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;

    // 两名初始散修推到寿元线上，账户分别刚好够一枚（2000 两）与差
    // 一年（1940 两，靠今年营生才够）。两条合起来同时钉住「不营生」
    // 与「不买入」：任一条失守，公账都会少一枚。
    const std::array<std::uint32_t, 2> silver{2000u, 1940u};
    std::size_t found = 0;
    life.ecs_world().for_each_entity(
        [&](const EntityId e)
        {
            if (found == silver.size())
            {
                return;
            }
            const auto* id = life.ecs_world().get_if<SoulId>(e);
            if (id == nullptr || id->index >= 970u
                || id->sect != gamev2::history::Sect::unowned)
            {
                return;
            }
            auto* cult = life.ecs_world().get_if<Cultivation>(e);
            auto* mortal = life.ecs_world().get_if<Mortal>(e);
            auto* purse = life.ecs_world().get_if<Purse>(e);
            CHECK(cult != nullptr && mortal != nullptr && purse != nullptr);
            if (cult == nullptr || mortal == nullptr || purse == nullptr)
            {
                return;
            }
            cult->realm = Realm::yinqi;
            mortal->age = gamev2::life::kRealmLifespan[
                static_cast<std::size_t>(Realm::yinqi)];
            purse->silver = silver[found];
            ++found;
        });
    CHECK_EQ(found, 2u);
    if (found != silver.size())
    {
        return;
    }

    // 公账预置两枚，并同步记进铸造总额：守恒对账不因夹具而破。
    life.economy().vault[7] = 2;
    life.economy().minted_total += 2;

    life.run_years(1);

    CHECK_EQ(life.economy().vault[7], 2u);
    CHECK_EQ(diag.stone_conservation_broken, 0u);
}

TEST(income_has_exactly_one_source)
{
    // 设计 §3.5：「当年 Σ收入 = 脉产出 × kVeinToStone（没有第二个
    // 来源）」。银两不进灵石守恒：营生发银两，灵石只能从铸币来。
    // 断言：跑 100 年，铸造 == 420000×100，且恒等式（Task 2）仍平——
    // 营生若把银两错记进 stone，恒等式与铸造额至少破一头。
    //
    // 刀 4 补了第二条灵石的路：盗采私铸（差异 7——私铸直接进私囊，
    // 同时在铸币侧同增，恒等式两侧一起动）。它单列在 poached_total，
    // 因此「铸币额 == 脉产出」仍以差值的形式精确成立。
    const Stack s = make_stack(economy_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(100);

    CHECK(life.economy().poached_total > 0u);
    CHECK_EQ(life.economy().minted_total,
             42000000u + life.economy().poached_total);
    CHECK_EQ(diag.stone_conservation_broken, 0u);
    // 烧毁非死代码的显式钉（R2：本针从 T2 移来——全经济下 100 年
    // 必然有人烧过灵石）。
    CHECK(life.economy().burned_total > 0u);

    // 银两确实发过：散修攒了 100 年营生（部分人已用 2000 两尝试
    // 买入失败，银两不会凭空消失——无主金库为 0 时买入不成交）。
    std::uint64_t silver = 0;
    using gamev2::life::Purse;
    using gamev2::life::SoulId;
    life.ecs_world().view<const SoulId, const Purse>().each(
        [&](const SoulId& id, const Purse& p)
        {
            if (id.sect == gamev2::history::Sect::unowned)
            {
                silver += p.silver;
            }
        });
    CHECK(silver > 100u * gamev2::life::kFreeCultivatorIncomeLiang);
}


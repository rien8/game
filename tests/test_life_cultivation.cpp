#include "gamev2/ecs/world.hpp"
#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

/// 刀 2 的夹具：与刀 1 同口径（128×128、z3、seed 13/14）。
gamev2::world::WorldParams roster_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

/// 金字塔上移的夹具：256²。128² 只有太玄有活脉，其余六宗无地可种、
/// 无脉可采——经济退化到只剩一宗能养人（刀 3 的经济测试已用同一
/// 理由换过夹具）。刀 4 的判据 2 让「缺钱」成为常态，退化世界的
/// 众生大半被逼去盗采与横死，筑基以上的存量因此只降不升
/// （128² 实测：开局 133 → 100 年 125），金字塔上移在那里读不出来。
gamev2::world::WorldParams pyramid_world(std::uint64_t seed)
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
    const auto ledger =
        gamev2::history::build_ledger(*stack.world, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    stack.era = gamev2::faction::simulate(*stack.world, ledger, fdiag);
    return stack;
}

/// 数一数活着的、谓词命中的众生（视图里能读到 SoulId 的就是活的）。
template<typename F>
std::uint32_t count_alive(gamev2::life::Life& life, F&& pred)
{
    std::uint32_t n = 0;
    life.ecs_world().view<const gamev2::life::SoulId>().each(
        [&](const gamev2::life::SoulId& id)
        {
            if (pred(id))
            {
                ++n;
            }
        });
    return n;
}

std::uint32_t initial_population()
{
    std::uint32_t n = 0;
    for (const auto& c : gamev2::life::kPopulation)
    {
        n += c;
    }
    return n;
}

}  // namespace

TEST(the_clock_starts_at_the_era_and_advances)
{
    // 时钟从当代起算（设计 §1.4：8914 开局），每 run_years(1) 推一格。
    // 名册的 year 是开局快照，不随推演变（差异与补齐 §4）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    CHECK(!s.era.veins.empty());
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    CHECK_EQ(life.year(), s.era.now);
    life.run_years(5);
    CHECK_EQ(life.year(), s.era.now + 5u);
    CHECK_EQ(life.roster().year, s.era.now);
}

TEST(nobody_ever_outlives_their_realm)
{
    // 寿元表逐字来自设定 §5.1（kRealmLifespan 是唯一权威，组件里不存
    // 上限）。逐年断言：活着的众生年龄严格小于该境寿元——死发生在
    // 「满寿元」那一年，所以年末在册者都还差一岁。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (std::uint32_t y = 0; y < 150; ++y)
    {
        life.run_years(1);
        std::uint32_t alive = 0;
        life.ecs_world().view<const gamev2::life::Mortal,
                              const gamev2::life::Cultivation>().each(
            [&](const gamev2::life::Mortal& m,
                const gamev2::life::Cultivation& c)
            {
                ++alive;
                const auto r = static_cast<std::size_t>(c.realm);
                CHECK(m.age < gamev2::life::kRealmLifespan[r]);
            });
        CHECK(alive > 0u);
    }
}

TEST(the_cohort_shrinks_as_the_years_pass)
{
    // 寿元死亡不是死代码：跑 200 年，初始队列（index < 初始人口数）
    // 必须有人死去，同时世界不能空。
    //
    // 上下界由探针实测钉死（刀 4 重钉，2026-10-29）：实测环境与读数
    // 128×128 / seed 13 / 200 年 / 初始队列存活 80。刀 3 的读数是 900
    // ——「九成初始人口活到长寿境界（金丹/元婴寿 600/1200）」，刀 4
    // 把它打散了：众生现在会横死（历练遇险 5‰、盗采被擒 30‰、寻仇
    // 败北），200 年里初始队列掉到 80。下界取 80（防世界掏空），上界
    // 断言证明死亡不是死代码。
    // 刀 2 旧读数「存活 87」与实测不符（除数与经济均已排除为成因），
    // 已按本次探针订正。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    life.run_years(200);

    const std::uint32_t survivors = count_alive(
        life, [&](const gamev2::life::SoulId& id)
        { return id.index < initial_population(); });
    const std::uint32_t total = count_alive(
        life, [](const gamev2::life::SoulId&) { return true; });
    CHECK(survivors < initial_population());
    CHECK(survivors >= 80u);
    CHECK(total > 0u);
}

TEST(the_populace_refills_from_below)
{
    // 补员的两条底线：引气不被晋升抽干（新生儿全是引气）、新队列
    // 确实在长（index ≥ 初始人口数的在册者出现）。设计 §1.4 的
    // 「金字塔随年演化」就是这个：底部持续有水进来。
    //
    // 总量上下界由探针钉死（刀 3 重标后复测，2026-10-27）：实测环境
    // 与读数 128×128 / seed 13 / 100 年实测 total 1923（200 年 2850）
    // ——不塌（>500，初始筑基以上 133 人全活）也不炸。刀 2 旧读数
    // 3554 已过时；上界 5400 沿用（较新读数宽松，只防爆炸不误报）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    life.run_years(100);

    std::uint32_t yinqi_count = 0;
    life.ecs_world().view<const gamev2::life::Cultivation>().each(
        [&](const gamev2::life::Cultivation& c)
        {
            if (c.realm == gamev2::life::Realm::yinqi)
            {
                ++yinqi_count;
            }
        });
    // 引气层必须一直有人：晋升会把老人抽走，只有新生儿（全是引气）
    // 能补上。这条为 0 就是补员断了。
    CHECK(yinqi_count > 0u);

    const auto newcomers = count_alive(
        life, [&](const gamev2::life::SoulId& id)
        { return id.index >= initial_population(); });
    CHECK(newcomers > 0u);

    const std::uint32_t total = count_alive(
        life, [](const gamev2::life::SoulId&) { return true; });
    CHECK(total > 500u);
    CHECK(total < 5400u);
}

TEST(the_same_seed_gives_the_same_populace_over_time)
{
    // 设计 §8 确定性：同 seed 跑 100 年，活着的总人数与逐境直方图
    // 逐位相同。补员的随机（收徒姓名/属性）一并被钉住——它按
    // (seed, 流号, 序号) 派生，与遍历顺序无关。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(13));
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

    using gamev2::life::Cultivation;
    using gamev2::life::SoulId;
    std::uint32_t n1 = 0;
    std::uint32_t n2 = 0;
    std::array<std::uint32_t, 9> realms1{};
    std::array<std::uint32_t, 9> realms2{};
    life1.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId&, const Cultivation& c)
        {
            ++n1;
            ++realms1[static_cast<std::size_t>(c.realm)];
        });
    life2.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId&, const Cultivation& c)
        {
            ++n2;
            ++realms2[static_cast<std::size_t>(c.realm)];
        });
    CHECK_EQ(n1, n2);
    CHECK(realms1 == realms2);
}

TEST(the_pyramid_rises_with_the_years)
{
    // 设计 §11 刀 2 验收：无决策注入跑百年，人口金字塔随年演化。
    // 演化的方向是「上移」：新生队列里有人凭年资与悟性爬上筑基以上、
    // 初始引气队列被晋升抽走——合起来才叫修炼在运转。
    //
    // 夹具换成 256²（理由见 pyramid_world 的注释）：128² 里经济退化，
    // 金字塔读不出上移。256² 实测：开局 133 → 100 年 139。
    const Stack s = make_stack(pyramid_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::Cultivation;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    const auto count_realm_plus = [&](gamev2::life::Life& l,
                                      Realm floor, bool newcomers_only)
    {
        std::uint32_t n = 0;
        l.ecs_world().view<const SoulId, const Cultivation>().each(
            [&](const SoulId& id, const Cultivation& c)
            {
                const bool over =
                    static_cast<std::size_t>(c.realm)
                    >= static_cast<std::size_t>(floor);
                if (over
                    && (!newcomers_only
                        || id.index >= initial_population()))
                {
                    ++n;
                }
            });
        return n;
    };

    const std::uint32_t zhuji_before =
        count_realm_plus(life, Realm::zhuji, false);
    life.run_years(100);

    // 晋升潮：筑基以上比开局多（初始 133 人全活，多出来的全是新晋）。
    CHECK(count_realm_plus(life, Realm::zhuji, false) > zhuji_before);
    // 新队列里有人爬上筑基：他们的门槛之和是 20 + 20 年，加上掷运。
    CHECK(count_realm_plus(life, Realm::zhuji, true) > 0u);
    // 结构性上限：100 年内到不了化神（前四境门槛之和 120 年）。
    CHECK(count_realm_plus(life, Realm::huashen, false) == 0u);
}

TEST(the_same_seed_gives_the_same_realm_histogram)
{
    // 突破掷运的无状态派生：(seed, 流号, 年, 实体槽号)。本测试钉住同
    // seed 端到端确定性（含掷运管线不引入非确定性）：两个独立世界跑
    // 100 年，逐境直方图逐位相同；不同 seed 初始名册不同，必有一境不
    // 同。掷运的变异钉住（恒成功/恒失败各打红一条断言）是刀 3 重标时
    // 的验收项。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(14));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!b.era.veins.empty());
    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::LifeDiagnostics d3;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*a.world, a.era, d2);
    gamev2::life::Life life3(*b.world, b.era, d3);
    life1.seed();
    life2.seed();
    life3.seed();
    life1.run_years(100);
    life2.run_years(100);
    life3.run_years(100);

    using gamev2::life::Cultivation;
    const auto histogram = [](gamev2::life::Life& l)
    {
        std::array<std::uint32_t, 9> h{};
        l.ecs_world().view<const Cultivation>().each(
            [&](const Cultivation& c)
            {
                ++h[static_cast<std::size_t>(c.realm)];
            });
        return h;
    };
    const auto h1 = histogram(life1);
    const auto h2 = histogram(life2);  // 与 h1 同 seed：必须相等
    const auto h3 = histogram(life3);  // seed 14：掷运不同，必须不同
    CHECK(h1 == h2);
    CHECK(h1 != h3);
}

TEST(the_breakthrough_roll_can_pass_and_fail)
{
    // 掷运的变异钉住（刀 2 计划携带项 ②）：
    //   恒成功变异 → 晋级即清零，快照里无人进度达标 → 「有人掷输」
    //   断言红；
    //   恒失败变异 → 无人晋级，新队列永远到不了筑基 → 「有人突破」
    //   断言红。
    // 正常掷运（中位 300‰）两头都不触界。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();
    life.run_years(200);

    using gamev2::life::Cultivation;
    using gamev2::life::Realm;
    using gamev2::life::SoulId;
    using gamev2::life::kRealmYears;
    std::uint32_t breakthroughs = 0;   // 新队列上筑基的人数
    std::uint32_t failed_this_year = 0;  // 进度达标而未晋级的人数
    life.ecs_world().view<const SoulId, const Cultivation>().each(
        [&](const SoulId& id, const Cultivation& c)
        {
            if (id.index >= initial_population()
                && static_cast<std::size_t>(c.realm)
                       >= static_cast<std::size_t>(Realm::zhuji))
            {
                ++breakthroughs;
            }
            if (c.realm != Realm::dacheng
                && c.progress
                       >= kRealmYears[static_cast<std::size_t>(
                           c.realm)])
            {
                ++failed_this_year;
            }
        });
    CHECK(breakthroughs > 0u);        // 掷运会成功（恒失败时红）
    CHECK(failed_this_year > 0u);     // 掷运会失败（恒成功时红）
}

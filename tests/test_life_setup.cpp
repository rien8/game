#include "gamev2/ecs/component.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

/// 本刀的主夹具：128×128。history 层已在 128² 上验证过台账可用；
/// seed 13 与 14 的当代格局由测试里的 veins 守卫钉住（见夹具节）。
gamev2::world::WorldParams roster_world(std::uint64_t seed)
{
    gamev2::world::WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = seed;
    return p;
}

/// world + 台账 + 当代格局的一整套。world 存 optional：EraReport 不
/// 拥有世界，而 Life 还要读它，整套必须活到测试结束。
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

std::uint32_t total_population()
{
    std::uint32_t n = 0;
    for (const auto& c : gamev2::life::kPopulation)
    {
        n += c;
    }
    return n;
}

}  // namespace

TEST(the_life_components_satisfy_the_ecs_constraints)
{
    // register_component 的 static_assert 守着「默认可构造、nothrow
    // 可移动、对齐不超限」。这条测试再守一层：六件组件必须是六个
    // **互不相同**的组件——共用一个 id 会让两个字段挤进同一列，
    // 静默串数据。
    using gamev2::ecs::component_id_of;
    const std::array ids{
        component_id_of<gamev2::life::SoulId>(),
        component_id_of<gamev2::life::Cultivation>(),
        component_id_of<gamev2::life::Aptitude>(),
        component_id_of<gamev2::life::Temper>(),
        component_id_of<gamev2::life::Mortal>(),
        component_id_of<gamev2::life::Office>(),
    };
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ids.size(); ++j)
        {
            CHECK(!(ids[i] == ids[j]));
        }
    }
}

TEST(the_same_seed_gives_the_same_roster)
{
    // 设计 §4.1 的断言形态：同 seed 重跑，全部个体的全部字段逐位相同。
    const auto p = roster_world(13);
    const Stack a = make_stack(p);
    const Stack b = make_stack(p);
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!a.era.veins.empty());

    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();

    const auto& r1 = life1.roster();
    const auto& r2 = life2.roster();
    // 规模是常数表给的，不是碰巧相等。
    CHECK_EQ(r1.entries.size(), total_population());
    CHECK_EQ(r1.entries.size(), r2.entries.size());
    CHECK_EQ(r1.year, a.era.now);
    for (std::size_t i = 0; i < r1.entries.size(); ++i)
    {
        const auto& x = r1.entries[i];
        const auto& y = r2.entries[i];
        CHECK_EQ(x.soul, y.soul);
        CHECK(x.name == y.name);
        CHECK(x.sect == y.sect);
        CHECK(x.office == y.office);
        CHECK(x.realm == y.realm);
        CHECK_EQ(x.age, y.age);
        CHECK(x.temper.greed == y.temper.greed);
        CHECK(x.temper.valor == y.temper.valor);
        CHECK(x.temper.order == y.temper.order);
        CHECK(x.temper.mercy == y.temper.mercy);
        CHECK(x.temper.vigor == y.temper.vigor);
        CHECK_EQ(x.aptitude.comprehension, y.aptitude.comprehension);
        CHECK_EQ(x.aptitude.sense, y.aptitude.sense);
        CHECK_EQ(x.aptitude.body, y.aptitude.body);
        CHECK_EQ(x.aptitude.fortitude, y.aptitude.fortitude);
    }
    CHECK_EQ(d1.no_contemporary_state, 0u);
    CHECK_EQ(d2.no_contemporary_state, 0u);
}

TEST(the_different_seed_gives_a_different_roster)
{
    // 守「随机真的被用过」：把 Stream::life 换成常量，上面那条同 seed
    // 的测试照样绿——必须有一条测试红在「换 seed 名册不变」上。
    // 两条 veins 守卫是 seed 14 的探针：红了就换种子（见夹具节）。
    const Stack a = make_stack(roster_world(13));
    const Stack b = make_stack(roster_world(14));
    CHECK(a.world.has_value() && b.world.has_value());
    if (!a.world || !b.world)
    {
        return;
    }
    CHECK(!a.era.veins.empty());
    CHECK(!b.era.veins.empty());

    gamev2::life::LifeDiagnostics d1;
    gamev2::life::LifeDiagnostics d2;
    gamev2::life::Life life1(*a.world, a.era, d1);
    gamev2::life::Life life2(*b.world, b.era, d2);
    life1.seed();
    life2.seed();

    const auto& r1 = life1.roster();
    const auto& r2 = life2.roster();
    CHECK_EQ(r1.entries.size(), r2.entries.size());
    bool differs = false;
    for (std::size_t i = 0; i < r1.entries.size(); ++i)
    {
        if (r1.entries[i].name != r2.entries[i].name
            || r1.entries[i].age != r2.entries[i].age
            || r1.entries[i].temper.greed != r2.entries[i].temper.greed)
        {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST(the_population_pyramid_matches_the_constants)
{
    // 设计 §1.4：境界分布是消耗金字塔的反推——引气最多、逐境递减、
    // 元婴寥寥、化神以上一个都没有（谷底期养不起）。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::OfficeKind;
    using gamev2::life::Realm;
    constexpr std::size_t kSectKinds = 7;  // 七宗；第 8 位是无主
    std::array<std::uint32_t, kSectKinds> sect_population{};
    std::uint32_t free_count = 0;

    for (const auto& e : life.roster().entries)
    {
        const auto r = static_cast<std::size_t>(e.realm);
        // 化神及以上：谷底期的当代一个都容不下。分布表写 0，这里从
        // 名册侧钉住它真的没被生成——七宗与散修一起查。
        CHECK(r <= static_cast<std::size_t>(Realm::yuanying));
        if (e.sect == gamev2::history::Sect::unowned)
        {
            CHECK(e.office == OfficeKind::none);
            ++free_count;
            continue;
        }
        ++sect_population[static_cast<std::size_t>(e.sect)];
    }

    // 七宗逐宗核对规模与逐境递减。
    for (std::size_t sct = 0; sct < kSectKinds; ++sct)
    {
        const std::uint32_t n = gamev2::life::kPopulation[sct];
        CHECK_EQ(sect_population[sct], n);

        std::array<std::uint32_t, static_cast<std::size_t>(Realm::count)>
            counts{};
        for (const auto& e : life.roster().entries)
        {
            if (static_cast<std::size_t>(e.sect) == sct)
            {
                ++counts[static_cast<std::size_t>(e.realm)];
            }
        }
        for (std::size_t r = 1; r < static_cast<std::size_t>(Realm::count);
             ++r)
        {
            CHECK(counts[r - 1] >= counts[r]);
        }
        // 引气至少三成，否则金字塔是倒着长的。
        CHECK(counts[0] >= n * 3u / 10u);
        // 元婴寥寥：一宗至多一个。
        CHECK(counts[static_cast<std::size_t>(Realm::yuanying)] <= 1u);
    }

    // 散修按常数表单列。
    CHECK_EQ(free_count,
             gamev2::life::kPopulation[static_cast<std::size_t>(
                 gamev2::history::Sect::unowned)]);
}

TEST(nobody_outlives_their_realm)
{
    // 九境寿元逐字来自设定 §5.1。年龄在生成时就折半封顶，这里再从
    // 名册侧独立复算一遍——生成器写错（越界下标、忘了封顶）时红的
    // 是这条，而不是一条莫名其妙的越界崩溃。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    for (const auto& e : life.roster().entries)
    {
        CHECK(e.age >= gamev2::life::kMinAge);
        const auto r = static_cast<std::size_t>(e.realm);
        CHECK(e.age < gamev2::life::kRealmLifespan[r]);
    }
}

TEST(sect_leaders_are_zhuji_or_above)
{
    // 职位是资源与权力的阶梯（设计 §2.3）：宗主长老必须从筑基以上挑，
    // 金丹才脱离得了宗门供给体系（设定 §5.1）。外门内门不设门槛。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    using gamev2::life::OfficeKind;
    using gamev2::life::Realm;
    std::array<std::uint32_t, 7> masters{};
    std::array<std::uint32_t, 7> elders{};
    for (const auto& e : life.roster().entries)
    {
        const bool leading = e.office == OfficeKind::sect_master
                             || e.office == OfficeKind::elder;
        if (leading)
        {
            CHECK(e.realm >= Realm::zhuji);
        }
        if (e.sect == gamev2::history::Sect::unowned)
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(e.sect);
        if (sect >= masters.size())
        {
            continue;
        }
        masters[sect] += (e.office == OfficeKind::sect_master) ? 1u : 0u;
        elders[sect] += (e.office == OfficeKind::elder) ? 1u : 0u;
    }
    for (std::size_t i = 0; i < masters.size(); ++i)
    {
        const std::uint32_t n = gamev2::life::kPopulation[i];
        const std::uint32_t by_divisor = n / gamev2::life::kElderDivisor;
        const std::uint32_t expect_elders =
            (by_divisor > gamev2::life::kMinElders)
                ? by_divisor
                : gamev2::life::kMinElders;
        CHECK_EQ(masters[i], 1u);
        CHECK_EQ(elders[i], expect_elders);
    }
}

TEST(a_world_before_the_epoch_has_no_populace)
{
    // 退化不是错误（设计 §7）：当代早于纪元 8300 时台账为空、格局为空，
    // 没有当代格局就没有众生可初始化，诊断报出，名册为空。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 5;
    p.spirit_year = 8299;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics ldiag;
    const auto ledger = gamev2::history::build_ledger(*generated, ldiag);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto era = gamev2::faction::simulate(*generated, ledger, fdiag);
    CHECK(era.veins.empty());

    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*generated, era, diag);
    life.seed();
    CHECK(life.roster().entries.empty());
    CHECK_EQ(diag.no_contemporary_state, 1u);
}

TEST(the_roster_is_exportable_as_text)
{
    // 名册是玩家选身位的界面（设计 §5.1），导不出文本就选不了。
    // 行数必须精确：表头一行 + 逐人一行。上界式的行数断言在这里
    // 不够——本文件没有别的段落，行数就是内容的全集。
    const Stack s = make_stack(roster_world(13));
    CHECK(s.world.has_value());
    if (!s.world)
    {
        return;
    }
    gamev2::life::LifeDiagnostics diag;
    gamev2::life::Life life(*s.world, s.era, diag);
    life.seed();

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "life_roster_test.txt";
    CHECK(gamev2::life::export_roster(life.roster(), path));

    std::ifstream in(path);
    CHECK(in.good());
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        lines.push_back(line);
    }
    CHECK_EQ(lines.size(), life.roster().entries.size() + 1u);
    // 表头要能说出这是哪一年、共多少人。
    CHECK(lines[0].contains("灵历 8914"));
    CHECK(lines[0].contains("970 人"));
    // 名册里必须看得见宗主那一行——玩家开局选宗主要靠它。
    bool saw_master = false;
    for (const auto& line : lines)
    {
        if (line.contains("宗主"))
        {
            saw_master = true;
            break;
        }
    }
    CHECK(saw_master);
}

TEST(an_empty_roster_still_exports)
{
    // 退化路径（设计 §7）：没有众生的名册照样是一份可读的文件，
    // 只是没有人。写不出文件才是错误。
    const gamev2::life::Roster empty;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "life_roster_empty.txt";
    CHECK(gamev2::life::export_roster(empty, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string first;
    CHECK(static_cast<bool>(std::getline(in, first)));
    CHECK(first.contains("共 0 人"));
}

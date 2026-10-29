#include "gamev2/ecs/view.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

/// 涌现验收的夹具：256²/seed13，300 年，无注入。
gamev2::world::WorldParams emergence_world(std::uint64_t seed)
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

/// 一次 300 年长跑的产物：事件表、逐年人口、诊断与经济账。
struct Emergence
{
    std::vector<gamev2::life::LifeEvent> events;
    /// 逐年活人数。下标 i 对应开局年之后的第 i + 1 年（8915 起）。
    std::vector<std::uint32_t> population;
    /// 仍在册的出走者的性格轴值（order）。出走是悖逆者的门：order
    /// ≤2 的人才走得了，走了之后轴只往下推（差异 18），因此当下读到
    /// 的值必然也 ≤2——轴若退出判定，这条就红。
    std::vector<std::uint8_t> departer_order;
    gamev2::life::LifeDiagnostics diagnostics;
    gamev2::life::EconomyLedger economy;
};

/// 四条招牌共用一次推演：事件表与人口采样必须是同一段历史的两个
/// 侧面，分开跑就没有可比性。
const Emergence& emergence()
{
    static const Emergence data = []
    {
        Emergence out;
        const Stack s = make_stack(emergence_world(13));
        if (!s.world)
        {
            return out;
        }
        gamev2::life::LifeDiagnostics diag;
        gamev2::life::Life life(*s.world, s.era, diag);
        life.seed();
        for (std::uint32_t y = 0; y < 300; ++y)
        {
            life.run_years(1);
            std::uint32_t alive = 0;
            life.ecs_world().view<const gamev2::life::SoulId>().each(
                [&](const gamev2::life::SoulId&) { ++alive; });
            out.population.push_back(alive);
        }
        out.events = life.events();
        out.diagnostics = diag;
        out.economy = life.economy();

        // 仍在册的出走者的性格轴。
        life.ecs_world()
            .view<const gamev2::life::SoulId, const gamev2::life::Temper>()
            .each(
                [&](const gamev2::life::SoulId& id,
                    const gamev2::life::Temper& temper)
                {
                    for (const auto& ev : out.events)
                    {
                        if (ev.kind == gamev2::life::LifeEventKind::departure
                            && ev.actor == id.index)
                        {
                            out.departer_order.push_back(temper.order);
                            break;
                        }
                    }
                });
        return out;
    }();
    return data;
}

std::uint32_t count_events(const Emergence& e, gamev2::life::LifeEventKind kind)
{
    std::uint32_t n = 0;
    for (const auto& ev : e.events)
    {
        n += ev.kind == kind ? 1u : 0u;
    }
    return n;
}

}  // namespace

TEST(banner_1_unscripted_succession_or_contest)
{
    // 招牌①：没有剧本的换代也会发生——宗主推举与夺位两类事件存在，
    // 但换代不是「每年都换」的噪声（300 年里换代类事件落在 [1, 30]，
    // 探针实测 15：推举 4 + 夺位 11）。
    const Emergence& e = emergence();
    const std::uint32_t succession = count_events(
        e, gamev2::life::LifeEventKind::succession);
    const std::uint32_t contest =
        count_events(e, gamev2::life::LifeEventKind::contest);
    const std::uint32_t changes = succession + contest;
    CHECK(changes >= 1u);
    CHECK(changes <= 30u);
    CHECK(succession > 0u);
    CHECK(contest > 0u);
}

TEST(banner_2_starvation_drives_departures)
{
    // 招牌②：断供逼人出走。出走事件存在，且三个无地无脉的宗
    // （青囊/天机/九幽）都有人走过——制度性逃生门对谁都开着。
    // 出走事件落表时记的是出走前的宗门，因此事件里的 sect 就是
    // 「当年断供的宗」。
    const Emergence& e = emergence();
    const std::uint32_t departures =
        count_events(e, gamev2::life::LifeEventKind::departure);
    CHECK(departures > 0u);

    bool qingnang = false;
    bool tianji = false;
    bool jiuyou = false;
    for (const auto& ev : e.events)
    {
        if (ev.kind != gamev2::life::LifeEventKind::departure)
        {
            continue;
        }
        // 太玄铸 260000、需求 91750，从不断供——它的人不该因断供出走。
        CHECK(ev.sect != gamev2::history::Sect::taixuan);
        qingnang = qingnang || ev.sect == gamev2::history::Sect::qiangnang;
        tianji = tianji || ev.sect == gamev2::history::Sect::tianji;
        jiuyou = jiuyou || ev.sect == gamev2::history::Sect::jiuyou;
    }
    CHECK(qingnang);
    CHECK(tianji);
    CHECK(jiuyou);

    // 出走是悖逆者的门：order ≤ 2 的人才走得了（判据之外、制度性的
    // 逃生门读性格轴），走了之后轴只往下推。因此当下读到的性格轴
    // 必然也 ≤ 2——性格轴若退出判定，这条就红。
    CHECK(!e.departer_order.empty());
    for (const auto order : e.departer_order)
    {
        CHECK(order <= 2u);
    }
}

TEST(banner_3_conflicts_of_the_poach_contest_avenge_kind)
{
    // 招牌③：冲突涌现。盗采、夺位、寻仇三类事件都出现（探针实测：
    // 盗采 21491、夺位 11、寻仇 1，合计 21503）。上界不设——冲突是
    // 涌现不是配额。
    const Emergence& e = emergence();
    const std::uint32_t poach =
        count_events(e, gamev2::life::LifeEventKind::poach);
    const std::uint32_t contest =
        count_events(e, gamev2::life::LifeEventKind::contest);
    const std::uint32_t avenge =
        count_events(e, gamev2::life::LifeEventKind::avenge);
    CHECK(poach > 0u);
    CHECK(contest > 0u);
    CHECK(avenge > 0u);
    CHECK(poach + contest + avenge >= 21503u);
}

TEST(banner_4_population_follows_the_tide)
{
    // 招牌④：人口随灵潮走。断言只钉形状，不钉绝对数——绝对数随标定
    // 变，形状是结构。
    //
    // 探针钉死的年份段（256²/seed13）：谷底段从 8970 年的 986 人降到
    // 9049 年的 630 人；灵潮期（9000 年起）之后有人口回到谷底之上
    // （9050 年 662 人）。
    const Emergence& e = emergence();
    CHECK(e.population.size() == 300u);
    const std::uint32_t before = e.population[55];  // 年 8970
    const std::uint32_t trough = e.population[134];  // 年 9049
    CHECK(trough < before);
    // 衰退期修士总量实实在在地下滑：谷底不到段初的七成。绝对值随
    // 标定变，比例是形状——潮位因子一旦从突破判定里拿掉，谷底期的
    // 人多活一半（实测比例 630/986 → 751/990），这条就红。
    CHECK(trough * 10u < before * 7u);

    bool recovered = false;
    for (std::size_t i = 135; i < e.population.size(); ++i)
    {
        recovered = recovered || e.population[i] > trough;
    }
    CHECK(recovered);
}

TEST(full_pipeline_determinism_300_years)
{
    // 设计 §8 确定性类在刀 4 形态下的完整形态：两个同 seed 世界跑
    // 300 年，事件表逐条相同（年份/类型/actor/counterparty/amount/
    // sect），按 index 收齐的众生状态逐位相同。
    const Stack a = make_stack(emergence_world(13));
    const Stack b = make_stack(emergence_world(13));
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
    la.run_years(300);
    lb.run_years(300);

    CHECK_EQ(la.events().size(), lb.events().size());
    const std::size_t n = std::min(la.events().size(), lb.events().size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& x = la.events()[i];
        const auto& y = lb.events()[i];
        CHECK_EQ(static_cast<int>(x.kind), static_cast<int>(y.kind));
        CHECK_EQ(x.year, y.year);
        CHECK_EQ(x.actor, y.actor);
        CHECK_EQ(x.counterparty, y.counterparty);
        CHECK_EQ(x.amount, y.amount);
        CHECK_EQ(static_cast<int>(x.sect), static_cast<int>(y.sect));
    }

    // 众生状态：按 SoulId::index 收齐再比对（视图遍历序不保证一致）。
    struct State
    {
        std::uint8_t realm = 0;
        std::uint32_t progress = 0;
        std::uint64_t stone = 0;
        std::uint32_t silver = 0;
        std::uint8_t office = 0;
        std::uint8_t sect = 0;

        [[nodiscard]] bool operator==(const State& o) const noexcept
        {
            return realm == o.realm && progress == o.progress
                   && stone == o.stone && silver == o.silver
                   && office == o.office && sect == o.sect;
        }
    };
    const auto collect = [](gamev2::life::Life& l,
                            std::vector<std::pair<std::uint32_t, State>>& out)
    {
        l.ecs_world()
            .view<const gamev2::life::SoulId,
                  const gamev2::life::Cultivation,
                  const gamev2::life::Purse, const gamev2::life::Office>()
            .each(
                [&](const gamev2::life::SoulId& id,
                    const gamev2::life::Cultivation& c,
                    const gamev2::life::Purse& p,
                    const gamev2::life::Office& o)
                {
                    State st;
                    st.realm = static_cast<std::uint8_t>(c.realm);
                    st.progress = c.progress;
                    st.stone = p.stone;
                    st.silver = p.silver;
                    st.office = static_cast<std::uint8_t>(o.kind);
                    st.sect = static_cast<std::uint8_t>(id.sect);
                    out.emplace_back(id.index, st);
                });
        std::sort(out.begin(), out.end(),
                  [](const auto& x, const auto& y)
                  { return x.first < y.first; });
    };
    std::vector<std::pair<std::uint32_t, State>> sa;
    std::vector<std::pair<std::uint32_t, State>> sb;
    collect(la, sa);
    collect(lb, sb);
    CHECK_EQ(sa.size(), sb.size());
    CHECK(sa == sb);
}

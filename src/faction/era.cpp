/// @file era.cpp
/// @brief 派系演化层的入口与产物：当代格局的汇总与文本导出。
#include "gamev2/faction/era.hpp"
// 私有头与本文件同目录：不在 faction 目标的包含路径里，也不能进它——
// SimConfig / Simulation 是实现细节，测试与调用方都不该看见。
#include "systems.hpp"
#include "gamev2/history/quota.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace gamev2::faction {

EraReport simulate(const gamev2::world::World& world,
                   const gamev2::history::Ledger& ledger,
                   FactionDiagnostics& diagnostics)
{
    EraReport report;
    report.epoch = ledger.epoch;
    report.now = ledger.now;

    if (ledger.veins.empty() || ledger.now < ledger.epoch)
    {
        // 没有基线就没有推演。与台账同规：当代早于纪元时台账为空。
        diagnostics.no_baseline = 1u;
        return report;
    }

    // 跨度上限。必须在潮位积分之前：积分本身就是一次逐年循环，
    // 超大跨度会先在这里把机器拖死。
    if (ledger.now - ledger.epoch > gamev2::history::kMaxSpanYears)
    {
        diagnostics.span_too_large = 1u;
        return report;
    }

    // 标定系数：与台账逐字相同的算法，取自共享原语。
    //
    // 绝不在这里重写一遍公式——那正是 Task 1 存在的理由。
    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral =
        gamev2::history::tide_integral(ledger.epoch, ledger.now);
    const std::uint64_t conversion =
        gamev2::history::quota_conversion(sum_total, sum_rate, integral);
    if (conversion == 0u)
    {
        diagnostics.conversion_collapsed = 1u;
        return report;
    }

    SimConfig config;
    config.epoch = ledger.epoch;
    config.now = ledger.now;
    config.conversion = conversion;

    std::vector<EraEvent> events;
    Simulation sim(world, ledger, config, events);
    sim.seed_entities();
    sim.run();
    sim.collect(report, diagnostics);
    return report;
}

namespace {

/// 派系的个数：七宗加无主。
constexpr std::size_t kSectCount = static_cast<std::size_t>(Sect::count);

/// 事件类型的个数。
///
/// 事件类型是封闭集合（era.hpp：「只有这几种：多了就说明有行为没有被想清楚」）。
/// 计数表按它定长：将来加第九种事件，表跟着长，越界写在编译期就不可能
/// 发生——写坏相邻内存而不报错是最难查的一类错。
constexpr std::size_t kEventKindCount =
    static_cast<std::size_t>(EraEventKind::count);

/// 品阶的汉字名。与台账导出的那一份同名：两份文档的脉级表要能对上。
const char* rank_name(gamev2::world::VeinRank rank)
{
    switch (rank)
    {
    case gamev2::world::VeinRank::dragon: return "龙";
    case gamev2::world::VeinRank::main:   return "主";
    case gamev2::world::VeinRank::branch: return "支";
    default:                               return "?";
    }
}

/// 一宗的品阶构成。
///
/// 只数当代持有的脉：这一段比的是「现在归谁」。
struct Composition
{
    std::uint32_t dragon = 0;
    std::uint32_t main = 0;
    std::uint32_t branch = 0;
};

/// 名下当代脉的品阶构成。
Composition composition_of(const EraReport& report, Sect sect)
{
    Composition c;
    for (const auto& vein : report.veins)
    {
        if (vein.owner != sect)
        {
            continue;
        }
        switch (vein.rank)
        {
        case gamev2::world::VeinRank::dragon:
            ++c.dragon;
            break;
        case gamev2::world::VeinRank::main:
            ++c.main;
            break;
        case gamev2::world::VeinRank::branch:
            ++c.branch;
            break;
        default:
            break;
        }
    }
    return c;
}

/// 品阶构成的写法：龙/主/支三个数。
std::string composition_text(const Composition& c)
{
    return std::to_string(c.dragon) + "/" + std::to_string(c.main) + "/"
           + std::to_string(c.branch);
}

/// 设定 §8.1 的参照持有。
///
/// 只记品阶构成，不记绝对条数：条数随图幅变（64×64 只有一条主脉、
/// 三条支脉），构成才可比。
struct ReferenceHolding
{
    Composition held;
    /// 散修的支脉有多少条由图幅定：这一格没有定数可比。
    bool branch_unfixed = false;
    /// 设定对这一行的注解（阴脉、不登记、无地……）。
    const char* note = "";
};

/// 参照持有表。索引即宗门编号，与 Sect 枚举同序。
constexpr std::array<ReferenceHolding, kSectCount> kReferenceHoldings{{
    {Composition{1, 3, 0}, false, ""},
    {Composition{0, 2, 0}, false, ""},
    {Composition{0, 1, 0}, false, "阴脉"},
    {Composition{0, 1, 0}, false, ""},
    {Composition{0, 1, 0}, false, ""},
    {Composition{0, 0, 0}, false, "无地"},
    {Composition{0, 1, 0}, false, "不登记"},
    {Composition{0, 0, 0}, true, "散修的支脉，数目随图幅"},
}};

/// 参照持有的写法：龙/主/支，附设定注解。
std::string reference_text(const ReferenceHolding& ref)
{
    std::string text = std::to_string(ref.held.dragon) + "/"
                       + std::to_string(ref.held.main) + "/";
    text += ref.branch_unfixed ? "随图幅" : std::to_string(ref.held.branch);
    if (ref.note[0] != '\0')
    {
        text += "（";
        text += ref.note;
        text += "）";
    }
    return text;
}

/// 事件类型的中文说法。
///
/// 穷举全部类型，且没有 default：新增一种事件类型而忘了给它说法，
/// 下面的 static_assert 会让编译过不去。
constexpr const char* event_label(EraEventKind kind)
{
    switch (kind)
    {
    case EraEventKind::redraft_proposed:    return "提出重勘";
    case EraEventKind::redraft_passed:      return "重勘通过";
    case EraEventKind::redraft_rejected:    return "重勘否决";
    case EraEventKind::vein_transferred:    return "脉易主";
    case EraEventKind::vein_depleted:       return "脉枯竭";
    case EraEventKind::vein_publicized:    return "公有化";
    case EraEventKind::overmine_discovered: return "越采败露";
    case EraEventKind::cave_heaven_lost:    return "洞天失效";
    case EraEventKind::count:                return "";
    }
    return "";
}

/// 有中文说法的事件类型数。
constexpr std::size_t named_event_kinds()
{
    std::size_t named = 0;
    for (std::size_t i = 0; i < kEventKindCount; ++i)
    {
        if (event_label(static_cast<EraEventKind>(i))[0] != '\0')
        {
            ++named;
        }
    }
    return named;
}

/// 每一种事件类型都要有中文说法。
///
/// 这一句是「多了就说明没想清楚」的强制点：新增第九种事件类型而忘了给
/// 它说法，编译就过不去——而不是等到导出时多出一行谁也读不懂的记录。
static_assert(named_event_kinds() == kEventKindCount,
              "每一种事件类型都要有中文说法");

/// 事件涉及的脉。提案与表决不涉及具体的脉。
std::string event_vein_text(const EraEvent& event)
{
    switch (event.kind)
    {
    case EraEventKind::redraft_proposed:
    case EraEventKind::redraft_passed:
    case EraEventKind::redraft_rejected:
        return "-";
    default:
        return std::to_string(event.vein);
    }
}

/// 事件的主体：谁做的。
///
/// 脉枯竭与公有化没有主体：前者是物理事实，后者是物理程序，都不是
/// 谁做的事。
std::string event_actor_text(const EraEvent& event)
{
    switch (event.kind)
    {
    case EraEventKind::vein_depleted:
    case EraEventKind::vein_publicized:
        return "-";
    case EraEventKind::vein_transferred:
        // 主体是**接手方**。列名只说「主体」，不点破就会被读反
        // （「8900 脉易主 13 丹霞谷 太玄宗」很容易读成相反）。
        return std::string(gamev2::history::sect_name(event.actor)) + "（接手）";
    default:
        return gamev2::history::sect_name(event.actor);
    }
}

/// 事件的对方：谁交出了脉、谁被夺。
std::string event_counterparty_text(const EraEvent& event)
{
    switch (event.kind)
    {
    case EraEventKind::vein_transferred:
    case EraEventKind::vein_publicized:
        return std::string(gamev2::history::sect_name(event.counterparty))
               + "（原持有）";
    default:
        return "-";
    }
}

/// 事件数额的中文说法。
///
/// 各个事件的「数额」含义不同（转手几条脉、赞成权重多少、少采多少），
/// 印一个裸数字读者得回头查表，所以写成短语。
std::string event_amount_text(const EraEvent& event)
{
    switch (event.kind)
    {
    case EraEventKind::redraft_passed:
        return "转手 " + std::to_string(event.amount) + " 条脉";
    case EraEventKind::redraft_rejected:
        return "赞成权重 " + std::to_string(event.amount);
    case EraEventKind::vein_transferred:
        // 试算值，不是实采值：重勘的清单比的是「若转手，当年会少抽多少」
        // （设计 §3.11.3）。写成「试算少采」，读者才不会把它当成已发生
        // 的差额。
        return "试算少采 " + std::to_string(event.amount);
    case EraEventKind::overmine_discovered:
        return "越采 " + std::to_string(event.amount);
    default:
        return "-";
    }
}

/// 用度与缺口：魔修与散修用度无上限，这两个数对它们没有意义。
///
/// 印 0 会被读成「它们不用度」，所以印成文字。
std::string demand_text(std::uint32_t demand, bool unbounded)
{
    return unbounded ? std::string("无上限") : std::to_string(demand);
}

/// 缺口。用度无上限的派系没有可比的缺口。
std::string deficit_text(std::uint32_t deficit, bool unbounded)
{
    return unbounded ? std::string("不适用") : std::to_string(deficit);
}

/// 洞天那一列的列名。
///
/// 洞天挂在脉上，脉转手时只改 VeinOwner，洞天的归属不动——所以它记的
/// 还是 8300 的持有者。列名要写明，否则读者会以为它与同一行的当代口径
/// 是一回事。
constexpr const char* kCaveHeavenColumn = "洞天(8300 归属)";

/// 枯竭年。0 表示这条脉还没枯。
std::string depletion_text(std::uint32_t year)
{
    return year == 0u ? std::string("未枯") : std::to_string(year);
}

}  // namespace

bool export_era(const EraReport& report,
                const FactionDiagnostics& diagnostics,
                const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        return false;
    }
    out << "当代格局\n";
    // 年数只有一个口径：实际推演了多少年。世界的跨度（now − epoch）在
    // 没有基线时是 614，而实际推演是 0——两个数并排印出来会打架，读
    // 者会以为有一个算错了。推演年数是唯一的事实来源。
    out << "纪元 " << report.epoch << " —— 当代 " << report.now << '\n';
    out << "推演 " << report.years_simulated << " 年，当代潮位 "
        << report.current_level << '\n';
    // 「脉易主」是六百年里转手的次数，「持有格局变动」是当代与 8300 的
    // 持有格局**不同**的脉数。两个数不是一回事：一条脉转手之后若又
    // 枯竭公有化，它回到无主——若它 8300 年本来就是无主的（散修的支
    // 脉），当代的持有者与 8300 相同，就不计入格局变动。两个数并排
    // 印出来会让人以为另一个坏了，所以口径写在这里。
    std::size_t transfers = 0;
    for (const auto& event : report.events)
    {
        if (event.kind == EraEventKind::vein_transferred)
        {
            ++transfers;
        }
    }
    out << "脉 " << report.veins.size() << " 条，事件 " << report.events.size()
        << " 笔，脉易主 " << transfers << " 次，持有格局变动 "
        << report.owner_changes << " 条\n";
    out << "全脉累计开采 " << report.total_extracted << "，当代余额 "
        << report.total_balance << '\n';

    out << "\n=== 各宗格局 ===\n";
    // 这一行的 13 列分属五种口径。不写出来，读者会把「开采」当成当代
    // 名下脉的开采量，看到「开采 > 配额」就判定模型坏了——而那正是
    // 六百年里转过手的脉上的旧账（设计 §8.10：这份文档是给人裁决
    // 哪个格局算数的，读者不能被误导去改模型）。
    out << "（口径：持有脉/配额/余额 = 当代名下脉；初始脉/表决权重 = 8300；"
        << "开采/越采/未登记 = 六百年累计，含后来转出的脉，可大于当代配额；"
        << "未登记 = 落在不登记在册的脉上的开采（与台账同名同义），记在开采"
        << "当时的持有者名下，散修在登记在册的脉上的私采不计这一列；"
        << "用度/供给/缺口 = 当年；洞天 = 8300 归属）\n";
    out << "宗门\t持有脉\t初始脉\t配额\t余额\t用度\t供给\t缺口\t开采\t越采\t"
        << "未登记\t表决权重\t" << kCaveHeavenColumn << '\n';
    for (const auto& faction : report.factions)
    {
        const bool unbounded = faction.unbounded_demand;
        out << gamev2::history::sect_name(faction.sect) << '\t'
            << faction.vein_count << '\t'
            << faction.initial_vein_count << '\t'
            << faction.quota_total << '\t'
            << faction.balance << '\t'
            << demand_text(faction.demand, unbounded) << '\t'
            << faction.supply << '\t'
            << deficit_text(faction.deficit, unbounded) << '\t'
            << faction.extracted << '\t'
            << faction.overmined << '\t'
            << faction.unregistered << '\t'
            << faction.vote_weight << '\t'
            << faction.cave_heavens << '\n';
    }

    out << "\n=== 脉级格局 ===\n";
    // 「阴阳」是脉的物理属性，不进品阶构成：寒江剑府那条阴脉在参照表
    // 里按主脉记，没有这一列读者在本世界一侧认不出它。
    out << "脉\t初始持有者\t当代持有者\t品阶\t阴阳\t配额\t已采\t合法开采\t"
        << "余额\t枯竭年\t登记\n";
    for (const auto& vein : report.veins)
    {
        out << vein.vein << '\t'
            << gamev2::history::sect_name(vein.initial_owner) << '\t'
            << gamev2::history::sect_name(vein.owner) << '\t'
            << rank_name(vein.rank) << '\t'
            << (vein.yin ? "阴" : "阳") << '\t'
            << vein.quota_total << '\t'
            << vein.extracted << '\t'
            << vein.legal_extracted << '\t'
            << vein.balance << '\t'
            << depletion_text(vein.depleted_year) << '\t'
            << (vein.registered ? "是" : "否") << '\n';
    }

    out << "\n=== 与设定参照持有表的对照 ===\n";
    // 推演出来的当代格局与设定 §8.1 的参照持有表不一致（设计 §8.10）。
    // 差异摆在这里由人裁决哪个算数，代码不去迎合那张表。
    out << "（此对照只在九条主脉齐备的默认图幅（256×256）上有意义："
        << "条数随图幅变，可比的是品阶构成。）\n";
    out << "宗门\t本世界（龙/主/支）\t设定参照（龙/主/支）\n";
    for (const auto& faction : report.factions)
    {
        const auto index = static_cast<std::size_t>(faction.sect);
        const Composition held = composition_of(report, faction.sect);
        out << gamev2::history::sect_name(faction.sect) << '\t'
            << composition_text(held) << '\t'
            << reference_text(kReferenceHoldings[index]) << '\n';
    }

    out << "\n=== 事件 ===\n";
    // 小结先行：枯竭与公有化占了大半笔数，不点破的话，读者会被它们
    // 淹掉真正决定格局的那几十笔。
    out << "（事件小结：";
    {
        std::array<std::size_t, kEventKindCount> counts{};
        for (const auto& event : report.events)
        {
            ++counts[static_cast<std::size_t>(event.kind)];
        }
        bool first = true;
        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            if (counts[i] == 0u)
            {
                continue;
            }
            if (!first)
            {
                out << "、";
            }
            first = false;
            out << event_label(static_cast<EraEventKind>(i)) << ' '
                << counts[i];
        }
    }
    out << "）\n";
    out << "年份\t事件\t脉\t主体\t对方\t数额\n";
    for (const auto& event : report.events)
    {
        out << event.year << '\t'
            << event_label(event.kind) << '\t'
            << event_vein_text(event) << '\t'
            << event_actor_text(event) << '\t'
            << event_counterparty_text(event) << '\t'
            << event_amount_text(event) << '\n';
    }

    out << "\n=== 诊断 ===\n";
    // 退化不是错误，但必须看得见：格局是给人读的文档，读者要能在里面
    // 找到每一处对不上的地方。逐项一行，与其他区段同一张表的样式。
    out << "项目\t计数\n";
    out << "无基线\t" << diagnostics.no_baseline << '\n';
    out << "换算塌缩\t" << diagnostics.conversion_collapsed << '\n';
    out << "跨度超限\t" << diagnostics.span_too_large << '\n';
    out << "重勘通过\t" << diagnostics.redraft_passed << '\n';
    out << "重勘否决\t" << diagnostics.redraft_rejected << '\n';
    out << "重勘空转\t" << diagnostics.redraft_empty << '\n';
    out << "公有化残留\t" << diagnostics.public_release_mismatch << '\n';
    out << "守恒破坏\t" << diagnostics.conservation_broken << '\n';
    return static_cast<bool>(out);
}

}  // namespace gamev2::faction

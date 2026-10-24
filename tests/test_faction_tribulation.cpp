#include "gamev2/faction/era.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

/// 读回导出的文本。
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/// 劫损咬得动的世界。
///
/// **种子是 0**：它是 demo 的默认世界，也是实测里劫损最重的一个（九条脉
/// 被劫，合计 1027）。它之所以能当判据，不是因为「恰好有劫损」，而是因为
/// 被劫的脉里**有落在可比集里的**——太玄宗的龙脉（vein 0）被抽走 405，
/// 台账记它 8859 年枯竭。
///
/// 别的种子不成立：种子 21（本层既有的等价性测试用的那个）恰好有三条被劫
/// 的脉，而它们全是无主脉——落在设计 §6.1 的排除项里，比到的脉一条都没被
/// 劫过。这就是这条缺陷能活到今天的原因。
///
/// 生成一次就够：本文件的所有测试读的是同一个世界，而生成一份 256² 世界
/// 要半秒上下。世界是纯函数——同样的参数生成同样的世界，读它的测试互不
/// 影响。
const gamev2::world::World* toll_world()
{
    static const auto generated = [] {
        gamev2::world::WorldParams p;
        p.z_levels = 3;
        p.seed = 0;
        return gamev2::world::generate_world(p);
    }();
    return generated ? &*generated : nullptr;
}

/// 可比集：设计 §6.1 的三类排除。
///
/// 台账越采脉：台账的越采是按年抽样的定额，本层是缺口驱动的连续行为；
/// 无主脉    ：台账里无主脉仍受自己的总量配额约束，本层的散修用度无上限；
/// 阴脉      ：台账不建模「采出即衰」，本层建模。
bool comparable(const gamev2::history::QuotaEntry& entry,
                const gamev2::faction::VeinReport& vein)
{
    return entry.overmined_amount == 0u
           && entry.holder != gamev2::history::Sect::unowned
           && !vein.yin;
}

/// 台账一侧的合法开采（不含越采）。从事件表里按脉汇总。
std::vector<std::uint32_t> ledger_legal(
    const gamev2::history::Ledger& ledger)
{
    std::vector<std::uint32_t> legal(ledger.veins.size(), 0u);
    for (const auto& event : ledger.events)
    {
        if (event.over_quota)
        {
            continue;
        }
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            if (ledger.veins[i].vein == event.vein)
            {
                legal[i] += event.amount;
            }
        }
    }
    return legal;
}

TEST(the_toll_is_charged_to_the_vein)
{
    // 劫损是天地抽走的：没有配额、没有持有者、没有人该负责。
    //
    // 台账已经把它记在 `QuotaEntry::tribulation_loss` 上了，而它的当代余额
    // 要减掉这一项。本层从台账读 8300 的快照起算六百年，却**不认这一项**——
    // 于是同一本账在两层里对同一条脉给出两个余额：台账说太玄宗的龙脉
    // 8859 年被抽干，纪元报告说它还剩 387、还没枯。
    //
    // 这条测试守的是「本层认账」：可比集上每条脉的劫损必须等于台账记的数。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    CHECK_EQ(report.veins.size(), ledger.veins.size());
    std::size_t compared = 0u;
    std::size_t tolled = 0u;
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        CHECK_EQ(report.veins[i].vein, ledger.veins[i].vein);
        if (!comparable(ledger.veins[i], report.veins[i]))
        {
            continue;
        }
        ++compared;
        CHECK_EQ(report.veins[i].tribulation_loss,
                 ledger.veins[i].tribulation_loss);
        if (ledger.veins[i].tribulation_loss != 0u)
        {
            ++tolled;
        }
    }
    // 比上的脉要够多，否则这条测试等于没测。不用「占比」当判据：无主的
    // 支脉占了 50 条里的 40 条。下限取实测值 6。
    CHECK(compared >= 6u);
    // 比上的脉里必须有**被劫过的**：全是没被劫的脉，这条测试守的就只是
    // 本层原有的账，劫损接没接上它一点都不管。
    CHECK(tolled >= 4u);
}

TEST(the_toll_keeps_the_conservation)
{
    // 守恒恒等式要跟着口径一起改：余额 + 开采 + 劫损 == 配额。
    //
    // 劫损不是开采——它不记在 `extracted` 上（台账的口径：天地抽走的没有
    // 人该负责）。所以它只能自己占一格，而守恒式必须把它算进去，否则
    // 余额会比配额多出劫损那一份。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    for (const auto& vein : report.veins)
    {
        CHECK_EQ(vein.balance + vein.extracted + vein.tribulation_loss,
                 vein.quota_total);
    }
    // 本层自己的对账量也要干净：它报的就是上面那条恒等式。
    CHECK_EQ(fdiag.conservation_broken, 0u);
}

TEST(the_two_layers_agree_on_a_tolled_vein)
{
    // 缺陷的正面：被劫过的脉，两层必须说同一件事。
    //
    // 余额这一列只能在**两层都没有越采**的脉上比：本层越采压低余额，而
    // 两层的越采模型不同（设计 §6.1：台账的是按年抽样的定额，本层的是
    // 缺口驱动的连续行为）。差的就是这一份，比不了。
    //
    // 剩下的脉上，两层的合法开采必须逐位相等（设计 §6.1），余额与枯竭年
    // 也就跟着相等。劫损接进来之前，龙脉那一行是 332 vs 347、余额 0 vs 387。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    const std::vector<std::uint32_t> legal = ledger_legal(ledger);
    std::size_t compared = 0u;
    std::size_t tolled_compared = 0u;
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        const auto& vein = report.veins[i];
        const auto& entry = ledger.veins[i];
        if (!comparable(entry, vein))
        {
            continue;
        }
        // 本层越采过的脉：越采压低余额，两层的越采模型又不同，余额这一列
        // 本来就该差一份（见上面的说明）。合法开采仍要相等——除非余额被
        // 吃穿，那正是劫损要守住的场合。
        if (vein.extracted != vein.legal_extracted)
        {
            continue;
        }
        ++compared;
        if (entry.tribulation_loss != 0u)
        {
            ++tolled_compared;
        }
        CHECK_EQ(vein.legal_extracted, legal[i]);
        CHECK_EQ(vein.balance, entry.balance);
    }
    // 排除之后至少还剩下一批脉，否则这条测试等于没测。下限取实测值 2。
    CHECK(compared >= 2u);
    // 比到的脉里必须有被劫过的，守的还是劫损这条线。
    CHECK(tolled_compared >= 1u);

    // 被抽干的脉：台账说它余额归零、记了枯竭年，纪元报告必须说同一件事。
    // 缺陷发作时这一行是「余额 387、枯竭年未枯」。
    //
    // 这一段**不排除本层越采过的脉**：越采与劫损争抢同一条脉的余额，谁先
    // 拿走哪一份两层的分配本就可能不同，但「这条脉被抽干了」这个事实
    // 两层必须一致。
    //
    // 枯竭**年**可以差几年：劫损抽走的量若没把井抽空，之后是开采把它抽
    // 空的，而本层的开采与台账的在余数刻度上略有出入（本层还多了一次越
    // 采），差几年是正常的。要对的是「枯了没有」这个事实。
    std::size_t drained = 0u;
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        const auto& vein = report.veins[i];
        const auto& entry = ledger.veins[i];
        if (!comparable(entry, vein)
            || entry.tribulation_loss == 0u
            || entry.depleted_year == 0u)
        {
            continue;
        }
        ++drained;
        CHECK_EQ(vein.balance, 0u);
        CHECK(vein.depleted_year != 0u);
    }
    // 至少有一条被劫到抽干的脉：只有余额归零的脉才谈得上「两层说同一件
    // 事」。下限取实测值 3。
    CHECK(drained >= 3u);
}

TEST(the_toll_is_an_event_on_the_timeline)
{
    // 事件表是给人读的史官记录。余额莫名少一截却没有事件，就是那种
    // 「谁也读不懂的记录」——读者无从知道哪一年、哪条脉被抽走了。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    // 事件与账要对得上：每一笔劫损事件都要有一笔对应的脉级账。
    std::uint32_t from_events = 0u;
    std::size_t events = 0u;
    for (const auto& event : report.events)
    {
        if (event.kind != gamev2::faction::EraEventKind::tribulation_toll)
        {
            continue;
        }
        ++events;
        from_events += event.amount;
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        // 劫云没有主体：它是天地，不是谁做的事。
        CHECK(event.actor == gamev2::history::Sect::unowned);
    }
    std::uint32_t from_books = 0u;
    for (const auto& vein : report.veins)
    {
        from_books += vein.tribulation_loss;
    }
    CHECK_EQ(from_events, from_books);
    CHECK(events >= 4u);

    // 可比集上的每一笔台账劫损，本层都要有同年同脉的一笔事件。
    std::size_t matched = 0u;
    for (const auto& ledger_event : ledger.tribulation_events)
    {
        bool found = false;
        for (std::size_t i = 0; i < report.veins.size() && !found; ++i)
        {
            if (report.veins[i].vein != ledger_event.vein
                || !comparable(ledger.veins[i], report.veins[i]))
            {
                continue;
            }
            for (const auto& event : report.events)
            {
                if (event.kind
                        == gamev2::faction::EraEventKind::tribulation_toll
                    && event.year == ledger_event.year
                    && event.vein == ledger_event.vein)
                {
                    found = true;
                    break;
                }
            }
        }
        if (found)
        {
            ++matched;
        }
    }
    CHECK(matched >= 4u);
}

TEST(the_shortfall_is_reported)
{
    // 两层对某些脉的余额就是不一样，这一条必须看得见。
    //
    // 无主脉上散修在本层采尽为止（设计 §3.8），井常常先被采空；台账那边
    // 散修受脉自身的总量配额约束，还剩下一点给劫云。于是那一笔劫损本层
    // 抽不走——井是空的。两层的账因此差这一笔。
    //
    // 这是两套模型的差异，不是缺陷；但它必须由诊断量报出来，不能让读者
    // 以为两层算的是同一件事。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    CHECK(fdiag.tribulation_shortfall > 0u);
    // 差在可比集之外：可比集上每条脉的劫损都与台账相等（见
    // the_toll_is_charged_to_the_vein），两层的账在那里不差。
    std::uint32_t total = 0u;
    for (const auto& vein : report.veins)
    {
        total += vein.tribulation_loss;
    }
    CHECK(total < static_cast<std::uint32_t>(ledger.tribulation_deviation));
}

TEST(the_exports_show_the_toll)
{
    // 两份文档都要有「劫损」一列。
    //
    // 读者要能把「配额 − 已采 − 劫损 = 余额」对出来。没有这一列，被劫过
    // 的脉在表里看着像是凭空少了一截——台账那边「配额 737、已采 332、
    // 余额 0」中间那 405 无处可查，正是这个毛病。
    const gamev2::world::World* w = toll_world();
    CHECK(w != nullptr);
    if (w == nullptr)
    {
        return;
    }
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(*w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(*w, ledger, fdiag);

    const std::filesystem::path era_path =
        std::filesystem::temp_directory_path() / "faction_era_toll.txt";
    CHECK(gamev2::faction::export_era(report, fdiag, era_path));
    const std::string era = read_file(era_path);

    const std::filesystem::path ledger_path =
        std::filesystem::temp_directory_path() / "history_ledger_toll.txt";
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, ledger_path));
    const std::string book = read_file(ledger_path);

    // 纪元格局：脉级表与各宗表各一列。
    CHECK(era.find("脉\t初始持有者\t当代持有者\t品阶\t阴阳\t配额\t已采\t"
                   "合法开采\t劫损\t余额") != std::string::npos);
    CHECK(era.find("宗门\t持有脉\t初始脉\t配额\t余额\t用度\t供给\t缺口\t"
                   "开采\t越采\t未登记\t劫损") != std::string::npos);
    // 事件表里要有「劫损」这一种。
    CHECK(era.find("劫损") != std::string::npos);
    // 诊断段要报出封顶的笔数。
    CHECK(era.find("劫损未足额") != std::string::npos);
    // 台账：脉级表也要有这一列——它的余额同样减了劫损。
    CHECK(book.find("脉\t宗门\t品阶\t8300配额\t安全流速\t已采\t越采\t劫损\t"
                    "当代余额") != std::string::npos);
}

}  // namespace

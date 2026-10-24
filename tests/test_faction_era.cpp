#include "gamev2/faction/era.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using gamev2::faction::EraEventKind;

/// 取对照段里某宗门那一行的末格：设定 §8.1 的参照持有。
///
/// 取末格而不是找子串：「\t1/3/0\n」这种匹配会依赖「本世界列 ≠ 参照值」
/// 这个与断言本意无关的前提——推演结果哪天恰好与参照表吻合，测试就会
/// 假失败。
std::string reference_cell(const std::string& text, const std::string& sect)
{
    const std::size_t section = text.find("=== 与设定参照持有表的对照 ===");
    if (section == std::string::npos)
    {
        return "";
    }
    // 行首匹配：对照段的每一行都以「宗门\t」开头。
    const std::size_t begin = text.find("\n" + sect + "\t", section);
    if (begin == std::string::npos)
    {
        return "";
    }
    const std::size_t end = text.find('\n', begin + 1u);
    if (end == std::string::npos)
    {
        return "";
    }
    const std::string line = text.substr(begin + 1u, end - begin - 1u);
    const std::size_t last = line.rfind('\t');
    if (last == std::string::npos)
    {
        return "";
    }
    return line.substr(last + 1u);
}

TEST(the_redraft_period_is_two_hundred_years)
{
    // 设定 §6.5：脉律每二百年重勘一次。
    CHECK_EQ(gamev2::faction::kRedraftPeriod, 200u);
}

TEST(the_early_redraft_interval_is_fifty_years)
{
    // 提案与表决是大事，不能年年开。
    CHECK_EQ(gamev2::faction::kEarlyRedraftInterval, 50u);
}

TEST(a_depleted_vein_is_publicized)
{
    // 枯竭的脉不可复生（设定 §2.3：枯眼不可复生），它随即公有化：
    // 一个抽干的脉对原持有者没有价值，也不该继续占着一个名字。
    // 公有化是物理程序，不需要表决。
    //
    // 这条断言属于 Task 7：公有化要改 VeinOwner，而 VeinOwner 的唯一
    // 写入者是 ResolveSystem——它在本任务里才存在。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    std::size_t depleted = 0;
    for (const auto& vein : report.veins)
    {
        if (vein.depleted_year == 0u)
        {
            continue;
        }
        ++depleted;
        CHECK(vein.owner == gamev2::history::Sect::unowned);
    }
    CHECK(depleted > 0u);
    // 内部一致性：抽干的脉不该还挂在原主人名下。
    CHECK_EQ(fdiag.public_release_mismatch, 0u);
}

TEST(a_vein_changes_hands_at_least_once)
{
    // 涌现性验收（设计 §6.4）：当代的「脉 — 持有者」映射必须与 8300
    // 的映射不同，且变化不是代码里写死的。
    //
    // 判据是「两张表不同」，不是「太玄宗失去了某条脉」：后者会在
    // 世界变了之后仍然通过，而它其实什么都没测。
    //
    // 用默认图幅：小图幅凑不齐七宗的持有格局（见「测试图幅」一节）。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK(report.owner_changes > 0u);
    // 变化必须真的落在事件表里，不能只有计数。
    std::size_t transferred = 0;
    for (const auto& event : report.events)
    {
        if (event.kind == EraEventKind::vein_transferred)
        {
            ++transferred;
        }
    }
    CHECK(transferred > 0u);
}

TEST(the_events_are_ordered_by_year)
{
    // 事件表是史官读的东西，乱序就没人读了。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    for (std::size_t i = 1; i < report.events.size(); ++i)
    {
        CHECK(report.events[i - 1].year <= report.events[i].year);
    }
}

TEST(the_redrafts_are_fifty_years_apart)
{
    // 重勘不是年年开的事：两次重勘至少隔 50 年（设计 §3.11.3）。
    //
    // 8300 年那一次《脉律》也算数——它就是上一次重勘。这一条守的就是
    // 「距上次重勘」的基准：8300 年的格局没有被记进 proposed_year（那一
    // 栏记的是「谁提过案」），所以拿纪元当基准这件事必须对，否则 8301
    // 到 8349 之间都能开一次提前重勘，与上一次只隔一两年。
    //
    // 例行重勘（纪元 + 200k）由历法定，不受这个间隔约束——它撞上提前
    // 重勘时两者的间距可以不足 50 年。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    std::vector<std::uint32_t> years;
    for (const auto& event : report.events)
    {
        if (event.kind == EraEventKind::redraft_proposed)
        {
            years.push_back(event.year);
        }
    }
    CHECK(years.size() > 1u);
    // 第一次重勘也不早于纪元 + 50 年：纪元那一次就是上一次重勘。
    CHECK(years.front() >= report.epoch + 50u);
    for (std::size_t i = 1; i < years.size(); ++i)
    {
        const std::uint32_t year = years[i];
        if ((year - report.epoch) % 200u == 0u)
        {
            continue;
        }
        CHECK(year - years[i - 1] >= 50u);
    }
}

TEST(the_routine_redraft_years_have_a_proposal)
{
    // 例行重勘年一定要开过会：脉律每二百年一勘（设定 §6.5）。
    //
    // 这条守的是「重勘年至少有一条 redraft 事件」那一半——非重勘年不该
    // 冒出重勘事件，重勘年则一个都不能少。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    for (std::uint32_t year = report.epoch + 200u; year < report.now;
         year += 200u)
    {
        std::size_t proposals = 0;
        for (const auto& event : report.events)
        {
            if (event.kind == EraEventKind::redraft_proposed
                && event.year == year)
            {
                ++proposals;
            }
        }
        CHECK_EQ(proposals, 1u);
    }
}

TEST(the_redraft_events_all_happen_in_a_redraft_year)
{
    // 逐事件扫一遍：非重勘年不得冒出任何 redraft_* 事件。
    //
    // 「重勘年」从外面看就是「有提案的那一年」——所以这条断言拆成两句：
    // 结果事件（通过、否决）必定与提案同年出现；一年至多一条提案。
    // 少了后半句，一次重勘记两条提案就能混过去。
    //
    // 这条守的是事件表的**位置**，与「重勘了几次、清单多长」无关。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    // 提案年。事件表按年份有序，提案是它的一个子序列，所以它也是有序的。
    std::vector<std::uint32_t> proposals;
    for (const auto& event : report.events)
    {
        if (event.kind != EraEventKind::redraft_proposed)
        {
            continue;
        }
        // 一年至多一条提案。
        CHECK(proposals.empty() || proposals.back() != event.year);
        proposals.push_back(event.year);
    }
    CHECK(!proposals.empty());

    for (const auto& event : report.events)
    {
        if (event.kind != EraEventKind::redraft_passed
            && event.kind != EraEventKind::redraft_rejected)
        {
            continue;
        }
        // 有结果必有提案：结果事件不能落在非重勘年。
        CHECK(std::ranges::binary_search(proposals, event.year));
    }
}

TEST(an_empty_redraft_is_not_a_passed_one)
{
    // 空清单的那一次不记通过：提案发生了，但没有东西被投过票
    // （设计 §3.11.5 的第二种僵持：没有可重划的东西，提案空转）。
    //
    // 这条守的是「空转」这个状态本身。把空清单改回记通过（旧行为），
    // 这里的 redraft_empty > 0 就会红——而顺序指纹一个数都不会动，
    // 因为这条规则不碰开采。这正是「改错了没人发现」的那一类盲区。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    // 有提案但没有表决结果的年份 = 空转的年份。
    std::size_t empty_years = 0;
    for (const auto& event : report.events)
    {
        if (event.kind != EraEventKind::redraft_proposed)
        {
            continue;
        }
        bool has_outcome = false;
        for (const auto& other : report.events)
        {
            if (other.year != event.year)
            {
                continue;
            }
            if (other.kind == EraEventKind::redraft_passed
                || other.kind == EraEventKind::redraft_rejected)
            {
                has_outcome = true;
            }
        }
        if (has_outcome)
        {
            continue;
        }
        ++empty_years;
        // 空转的那一年不得有「通过」事件。
        for (const auto& other : report.events)
        {
            if (other.year != event.year)
            {
                continue;
            }
            CHECK(other.kind != EraEventKind::redraft_passed);
        }
    }
    // 诊断量与事件表对得上：空转就是「提了案却没有表决结果」。
    CHECK_EQ(fdiag.redraft_empty, static_cast<std::uint32_t>(empty_years));
    // 这个世界里确实有空转：把空清单记成通过的话，这一格会变成 0。
    CHECK(fdiag.redraft_empty > 0u);
}

TEST(the_report_covers_every_faction_and_vein)
{
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.factions.size(),
             static_cast<std::size_t>(gamev2::history::Sect::count));
    CHECK_EQ(report.veins.size(), ledger.veins.size());
    CHECK_EQ(report.epoch, 8300u);
    CHECK_EQ(report.now, ledger.now);
}

TEST(the_era_is_exportable_as_text)
{
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "faction_era_test.txt";
    CHECK(gamev2::faction::export_era(report, fdiag, path));
    std::ifstream in(path, std::ios::binary);
    CHECK(static_cast<bool>(in));
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("当代格局") != std::string::npos);
    CHECK(text.find("太玄宗") != std::string::npos);
    CHECK(text.find("诊断") != std::string::npos);
}

TEST(the_report_totals_are_filled_in)
{
    // 报告里的汇总数不能只靠眼睛看：它们要能由脉级数据重新算出来。
    //
    // 守的是「宗级汇总真的做了」这件事。此前 FactionReport 的持有脉、
    // 初始脉、配额、余额与 EraReport 的全账开采、余额都是 0，导出会把
    // 一份空账印成一份账。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);

    std::uint64_t extracted = 0;
    std::uint64_t rest = 0;
    for (const auto& vein : report.veins)
    {
        extracted += vein.extracted;
        rest += vein.balance;
    }
    CHECK_EQ(report.total_extracted, extracted);
    CHECK_EQ(report.total_balance, rest);

    // 每条脉有一个当代主人、一个初始主人：两栏脉数加起来都得等于
    // 脉的总数。这两条同时排除了「全是 0」这种凑巧自洽。
    std::uint32_t owned = 0;
    std::uint32_t initial = 0;
    for (const auto& faction : report.factions)
    {
        std::uint32_t veins = 0;
        std::uint32_t initial_veins = 0;
        std::uint32_t quota = 0;
        std::uint32_t balance = 0;
        for (const auto& vein : report.veins)
        {
            if (vein.owner == faction.sect)
            {
                ++veins;
                quota += vein.quota_total;
                balance += vein.balance;
            }
            if (vein.initial_owner == faction.sect)
            {
                ++initial_veins;
            }
        }
        CHECK_EQ(faction.vein_count, veins);
        CHECK_EQ(faction.initial_vein_count, initial_veins);
        CHECK_EQ(faction.quota_total, quota);
        CHECK_EQ(faction.balance, balance);
        owned += faction.vein_count;
        initial += faction.initial_vein_count;
    }
    CHECK_EQ(owned, static_cast<std::uint32_t>(report.veins.size()));
    CHECK_EQ(initial, static_cast<std::uint32_t>(report.veins.size()));
}

TEST(the_export_names_every_section_and_the_reference_table)
{
    // 导出文档是给人裁决哪个格局算数的（设计 §8.10）。它的五段结构、
    // 参照表与诊断项都要钉住：参照表若被误改一个数，对照段就会悄悄
    // 说错话，而那正是这份文档要摆在台面上的东西。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const gamev2::world::World& w = *generated;
    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(w, diagnostics);
    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "faction_era_sections.txt";
    CHECK(gamev2::faction::export_era(report, fdiag, path));
    std::ifstream in(path, std::ios::binary);
    CHECK(static_cast<bool>(in));
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    // 五段结构都在。
    CHECK(text.find("=== 各宗格局 ===") != std::string::npos);
    CHECK(text.find("=== 脉级格局 ===") != std::string::npos);
    CHECK(text.find("=== 与设定参照持有表的对照 ===") != std::string::npos);
    CHECK(text.find("=== 事件 ===") != std::string::npos);
    CHECK(text.find("=== 诊断 ===") != std::string::npos);
    // 诊断逐项列出，空转这一项不能少。
    CHECK(text.find("重勘空转") != std::string::npos);
    // 设定参照表（设定 §8.1）：太玄宗龙 1 + 主 3，寒江那条是阴脉。
    // 比的是对照段的末格——「本世界」那一列是推演出来的，不该参与断言。
    CHECK_EQ(reference_cell(text, "太玄宗"), std::string("1/3/0"));
    CHECK_EQ(reference_cell(text, "寒江剑府"), std::string("0/1/0（阴脉）"));
    // 各宗格局的段首要写清每一列的口径：混了五种口径的一行表，读者
    // 会把「开采 > 配额」当成模型坏了。
    CHECK(text.find("开采/越采/未登记 = 六百年累计") != std::string::npos);
}

}  // namespace

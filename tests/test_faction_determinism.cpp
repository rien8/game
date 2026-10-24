#include "gamev2/faction/era.hpp"
#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

/// 等价性测试用默认图幅。
///
/// 无主的支脉占了大半（256x256 下是 50 条里的 40 条），它们被排除在
/// 比较之外，所以「比过的脉占多大比例」不是个有用的判据；判据是
/// 「比过的脉够不够多」。
///
/// **种子是 21**：全层唯一的随机源是「越采被发现」，它必须真的被走过，
/// 否则「可复算」守的是一条没跑过的路径——把随机源换成常量，这条测试
/// 照样绿。这个世界上有一笔：寒江剑府 8368 年在 vein 7 上被发现 1 单位。
///
/// 别的测试文件里的金标准用的是各自的 seed 7，与此无关。
gamev2::world::WorldParams full_world()
{
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 21;
    return p;
}

TEST(the_same_seed_gives_the_same_era)
{
    // 可复算是这一刀的第一条验收标准（设计 §6.4）。
    //
    // **跑在默认图幅上**：小图幅上（64x64）全年流速截断为 0、越采根本不
    // 触发，全层唯一的随机源「越采被发现」一次都不会被调用——守着一个
    // 不产生随机的世界，等于没守。默认图幅还不够：还得是一个随机源
    // 真的命中过的种子（见 full_world 的说明与下面那条断言）。
    const auto p = full_world();
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
    gamev2::faction::FactionDiagnostics f1;
    gamev2::faction::FactionDiagnostics f2;
    const auto a = gamev2::faction::simulate(w, ledger, f1);
    const auto b = gamev2::faction::simulate(w, ledger, f2);
    CHECK_EQ(a.veins.size(), b.veins.size());
    for (std::size_t i = 0; i < a.veins.size(); ++i)
    {
        CHECK_EQ(a.veins[i].extracted, b.veins[i].extracted);
        CHECK(a.veins[i].owner == b.veins[i].owner);
        CHECK_EQ(a.veins[i].depleted_year, b.veins[i].depleted_year);
    }
    CHECK_EQ(a.events.size(), b.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i)
    {
        CHECK(a.events[i].kind == b.events[i].kind);
        CHECK_EQ(a.events[i].year, b.events[i].year);
        CHECK_EQ(a.events[i].vein, b.events[i].vein);
        // 主体与数额也要比：事件表是给人读的史官记录，「8900 脉易主 13
        // 丹霞谷 太玄宗 试算少采 2」这一行的每一格都是内容。只比到脉编
        // 号的话，主体与数额错了没人知道。
        CHECK(a.events[i].actor == b.events[i].actor);
        CHECK(a.events[i].counterparty == b.events[i].counterparty);
        CHECK_EQ(a.events[i].amount, b.events[i].amount);
    }
    // 被比对的表里必须有来自**随机源**的行。全层唯一的随机源是「越采被
    // 发现」（设计 §3.14），它按 (年, 派系, 脉) 无状态派生。表里若一行
    // 都没有，把随机源换成常量，这条测试照样绿——守住随机路径的可复算，
    // 先得保证随机路径真的被走过。
    //
    // 这不是把测试绑在概率上：种子是定的，发现与否随之确定（这个世界
    // 上一笔：8368）。
    std::size_t discovered = 0;
    for (const auto& event : a.events)
    {
        if (event.kind == gamev2::faction::EraEventKind::overmine_discovered)
        {
            ++discovered;
        }
    }
    CHECK(discovered > 0u);
    CHECK_EQ(a.owner_changes, b.owner_changes);
    // 逐宗汇总也是格局的一部分：demo 印的第一张表就是它，读者最先看到
    // 的「谁手里还剩多少」全在这些数里。它是唯一没被上面的逐项比较
    // 覆盖的一层，所以单独比一遍。
    CHECK_EQ(a.factions.size(), b.factions.size());
    for (std::size_t i = 0; i < a.factions.size(); ++i)
    {
        CHECK(a.factions[i].sect == b.factions[i].sect);
        CHECK_EQ(a.factions[i].vein_count, b.factions[i].vein_count);
        CHECK_EQ(a.factions[i].initial_vein_count,
                 b.factions[i].initial_vein_count);
        CHECK_EQ(a.factions[i].quota_total, b.factions[i].quota_total);
        CHECK_EQ(a.factions[i].balance, b.factions[i].balance);
        CHECK_EQ(a.factions[i].demand, b.factions[i].demand);
        CHECK_EQ(a.factions[i].supply, b.factions[i].supply);
        CHECK_EQ(a.factions[i].deficit, b.factions[i].deficit);
        CHECK_EQ(a.factions[i].extracted, b.factions[i].extracted);
        CHECK_EQ(a.factions[i].overmined, b.factions[i].overmined);
        CHECK_EQ(a.factions[i].unregistered, b.factions[i].unregistered);
        CHECK_EQ(a.factions[i].vote_weight, b.factions[i].vote_weight);
        CHECK_EQ(a.factions[i].cave_heavens, b.factions[i].cave_heavens);
        CHECK(a.factions[i].unbounded_demand == b.factions[i].unbounded_demand);
    }
    // 报告级的汇总与计数：demo 也印它们，一并钉住。
    CHECK_EQ(a.years_simulated, b.years_simulated);
    CHECK_EQ(a.current_year, b.current_year);
    CHECK_EQ(a.current_level, b.current_level);
    CHECK_EQ(a.total_extracted, b.total_extracted);
    CHECK_EQ(a.total_balance, b.total_balance);
    CHECK_EQ(a.redraft_passed, b.redraft_passed);
    CHECK_EQ(a.redraft_rejected, b.redraft_rejected);
}

TEST(the_legal_extraction_matches_the_ledger)
{
    // 与台账的等价性（设计 §6.1）：本层的**合法开采**（不含越采）
    // 必须与台账的合法开采逐位相等。
    //
    // 比较范围是**两边都没有理由分岔**的脉。排除的三类各有理由——
    // 这三类排除不是放宽判据，而是承认两套模型在那些情形下**本来就
    // 该不同**：台账没有派系（越采是按年抽样的定额），本层有（越采是
    // 缺口驱动的连续行为），阴脉则是一边建模「采出即衰」另一边不。
    //
    // 台账那侧的合法开采从事件表里取：over_quota == false 的事件
    // 按脉汇总。
    const auto p = full_world();
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
    CHECK_EQ(report.veins.size(), ledger.veins.size());

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

    std::size_t compared = 0;
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        const auto& vein = report.veins[i];
        const auto& entry = ledger.veins[i];
        // 下标对齐是这条循环的前提：报告的脉序按台账的脉序建（建脉时
        // 按 ledger.veins 走一遍，填报告时按 veins_ 走一遍）。把这条
        // 前提显式钉住，将来谁改了遍历顺序，红的会是这一条，而不是一次
        // 莫名其妙的开采量不等。
        CHECK_EQ(vein.vein, entry.vein);
        // 比较范围：设计 §6.1 的三类排除。
        //   台账越采脉 ：台账的越采是一笔按年抽样的定额，本层是缺口驱动
        //                 的连续行为，两者本就不同；
        //   无主脉     ：台账里无主脉仍受它自己的总量配额约束，本层的
        //                 散修用度无上限，会越过配额开采；
        //   阴脉       ：台账不建模「采出即衰」，本层建模。
        //
        // 还有两类**不该**排除，因为它们不是合法开采的分岔来源：
        //   本层越采过的脉：越采压低余额只在它吃穿余额或配额时才会改变
        //                    合法开采。余额没被吃穿时，合法开采分毫未变。
        //   换过手的脉    ：合法开采与归属无关——配额是这条脉自己的，
        //                    谁来采都是同一个数（见 ResolveSystem 的
        //                    试算）。转手只改越采的分摊，不改合法开采。
        //
        // 已知的分岔模式：若哪天两套模型在一条七宗名下的脉上分岔
        // （本层的越采吃穿了那条脉的余额，压低了它的合法开采），这条
        // 测试会红——那不是 bug，是「模型真的分岔了」的信号。看到红的
        // 人不必从头推一遍，先看是不是这一种。
        if (entry.overmined_amount != 0u
            || entry.holder == gamev2::history::Sect::unowned
            || vein.yin)
        {
            continue;
        }
        ++compared;
        CHECK_EQ(vein.legal_extracted, legal[i]);
    }
    // 排除之后至少还剩下一批脉，否则这条测试等于没测。
    // 不用「占比」当判据：无主的支脉占了 50 条里的 40 条。
    // 下限取实测值 9：某条排除规则误伤、比较集缩到 7 条时，这条要红
    // ——覆盖收缩必须是看得见的，不能悄悄发生。
    //
    // 用度与供给的定点化（本波次）没有动这个数的性质：三类排除的依据
    // 分别在台账的越采量、持有者是不是无主、阴脉判定上，全在台账与地
    // 脉那一侧，本层改口径不改它们。换种子（7 → 21）之后实测从 8 变成
    // 9，下限随之重定。
    CHECK(compared >= 9u);
}

TEST(the_tide_drives_the_supply)
{
    // 潮位是这一刀全部戏剧性的来源：8300 年是峰值 110，谷底期 33。
    // 供给缩到三成而用度不变，缺口才会出现。
    CHECK_EQ(gamev2::world::compute_spirit_tide(8300).level, 110u);
    CHECK_EQ(gamev2::world::compute_spirit_tide(8801).level, 33u);
    CHECK_EQ(gamev2::world::compute_spirit_tide(8914).level, 33u);

    // 上面三个数只是窗口的锚点。这条断言守的是因果链的另一半：
    // 潮位掉了 → 供给缩了 → 缺口出现。
    //
    // 用度就是**纪元年的供给**：8300 年潮位 110，当年可采量恰好等于
    // 用度（建脉时按纪元潮位算出来，见 setup.cpp 的 demands）。所以
    // 「当代供给 < 用度」就是「潮位把供给压到了用度以下」——而不是
    // 别的原因。
    const auto p = full_world();
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

    // 推演停在灵历 8913 年，谷底期，潮位 33——当代的供给是在这个潮位
    // 上算出来的。
    CHECK_EQ(report.current_level, 33u);
    bool saw_deficit = false;
    std::uint32_t supply_total = 0u;
    std::uint32_t demand_total = 0u;
    for (const auto& faction : report.factions)
    {
        if (faction.unbounded_demand || faction.demand == 0u)
        {
            // 无上限用度（魔修与散修）没有可比的用度，缺口对它们是
            // 「不适用」；无地的天机阁用度恒为零。两者都不参与这条断言。
            continue;
        }
        // 缺口是用度减供给，饱和到 0：这一栏是算出来的，不是抄来的。
        //
        // 饱和是必须的：用度是 8300 定下的常量（设计 §3.3），派系在重勘
        // 里接到脉之后供给会涨过它那份用度——那时缺口就该是零。「供给追
        // 上了用度」不是错，是格局变过了。
        const std::uint32_t gap = faction.demand > faction.supply
                                      ? faction.demand - faction.supply
                                      : 0u;
        CHECK_EQ(faction.deficit, gap);
        supply_total += faction.supply;
        demand_total += faction.demand;
        if (faction.deficit > 0u)
        {
            saw_deficit = true;
        }
    }
    // 潮位把**天下**的供给压到了用度以下。
    //
    // 这一条按总量写，不能逐派系写：总供给是脉的事，与归属无关——脉给
    // 谁都是那个产出（设计 §3.11.1）。派系之间的分配随重勘变，总量不变。
    // 逐派系断言「供给 < 用度」在格局变过之后就不成立：接到脉的那一宗
    // 供给涨过它 8300 定下的用度，缺口按设计就该消失。
    CHECK(supply_total < demand_total);
    // 谷底期必须有缺口。这一层最要紧的那条因果链就在这里：没有缺口，
    // 六百年就只是把同一个格局重算了六百遍。
    CHECK(saw_deficit);
}

}  // namespace

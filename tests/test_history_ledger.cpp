#include "gamev2/history/ledger.hpp"
#include "gamev2/history/quota.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace {

using gamev2::history::Ledger;
using gamev2::history::LedgerDiagnostics;
using gamev2::history::Sect;
using gamev2::world::generate_world;
using gamev2::world::VeinRank;
using gamev2::world::World;
using gamev2::world::WorldParams;

TEST(sect_count_matches_the_setting)
{
    // 七宗 + 无主池。少一个就意味着有一宗在台账里无处安放。
    CHECK_EQ(static_cast<std::size_t>(Sect::count), 8u);
    CHECK(static_cast<std::size_t>(Sect::unowned) == 7u);
}

TEST(an_empty_world_yields_an_empty_ledger)
{
    // 没有灵脉的世界不是错误：返回空台账，跨度照记。
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 2;
    const World w(p);
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.veins.empty());
    CHECK(ledger.events.empty());
    CHECK(ledger.sects.empty());
    CHECK_EQ(ledger.epoch, 8300u);
    CHECK_EQ(ledger.now, p.spirit_year);
    CHECK_EQ(ledger.unregistered_extracted, 0u);
}

TEST(a_year_before_the_epoch_yields_an_empty_ledger)
{
    // now < epoch：跨度为负，台账为空（设计 §5）。
    // fixture 必须是有灵脉的世界，否则 ledger.veins.empty() 对任何年份都
    // 成立，这条断言就跟年份无关了——等于没测。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    p.spirit_year = 8000;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.veins.empty());
    CHECK_EQ(ledger.epoch, 8300u);
    CHECK_EQ(ledger.now, 8000u);
}

TEST(a_year_after_the_epoch_yields_a_populated_ledger)
{
    // 上一条的反面：同一个有灵脉的世界，年份正常时台账必须有内容。
    // 两条放在一起，「now < epoch 让台账变空」才是被测到的行为，
    // 而不是因为世界本身没有灵脉。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.veins.empty());
    CHECK_EQ(ledger.now, 8914u);
}

TEST(diagnostics_are_optional)
{
    // 单参版本必须存在且不崩；双参版本把诊断填出来。
    WorldParams p;
    p.width = 8;
    p.height = 8;
    p.z_levels = 2;
    const World w(p);
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(ledger.veins.empty());
    CHECK_EQ(diagnostics.barren_veins, 0u);
}

TEST(the_surveyed_total_is_the_reserve_for_an_unmined_vein)
{
    // 64x64 足够快，且必然产出支脉（灵气层按面积缩放支脉数量）。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 台账为空的话下面的循环一条都不跑，这条断言会把它变成空跑失败。
    CHECK(!ledger.veins.empty());

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            ++checked;
            CHECK_EQ(entry.surveyed_total, vein.reserve);
            // 非越采脉没有反推区间。
            CHECK_EQ(entry.surveyed_total_hi, entry.surveyed_total);
        }
    }
    // 守卫：fixture 一旦不再产出未越采的脉，上面两条断言就整条空跑。
    CHECK(checked > 0);
}

TEST(an_overmined_vein_reverses_the_forty_percent_penalty)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    // fixture 选 128x128/seed 26：台账里有 4 条越采脉，其中 2 条的 reserve
    // 是奇数（另 4 条越采脉是荒脉，不进台账）。
    // 奇数 reserve 才是「固定 +3」与精确上界分道扬镳的地方（宽度是 2
    // 还是 3 取决于奇偶），选偶数 reserve 的种子这条断言就白写了。
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            continue;
        }
        ++checked;
        // 反推区间的下界：reserve = floor(original * 40/100)
        // ⟹ original >= ceil(reserve * 100/40)。
        const std::uint32_t lo = (vein.reserve * 100u + 39u) / 40u;
        CHECK_EQ(entry.surveyed_total, lo);
        // 开区间上界：候选原值落在
        // [ceil(2.5·reserve), ceil(2.5·(reserve+1)))，上界取后者。
        const std::uint32_t hi = ((vein.reserve + 1u) * 100u + 39u) / 40u;
        CHECK_EQ(entry.surveyed_total_hi, hi);
        // 区间的宽度是 2 或 3（取决于 reserve 的奇偶），3 是上确界。
        CHECK(entry.surveyed_total_hi > entry.surveyed_total);
        CHECK(entry.surveyed_total_hi - entry.surveyed_total <= 3u);
        // 上界是「紧」的：hi−1 仍是合法原值，hi 已经越界。
        // 断言作用在实现字段上，不是局部算出来的 hi——否则改了公式
        // 这两条会跟着一起漂移，失去独立性。
        CHECK_EQ((entry.surveyed_total_hi - 1u) * 40u / 100u, vein.reserve);
        CHECK((entry.surveyed_total_hi * 40u / 100u) != vein.reserve);
        // 越采量 = 8300 承载上限 − 当代物理储量。
        CHECK_EQ(entry.overmined_amount,
                 entry.surveyed_total - vein.reserve);
    }
    // 守卫：这条断言一旦整条空跑，说明 fixture 不再产出越采脉。
    CHECK(checked > 0);
}

TEST(barren_veins_stay_out_of_the_ledger)
{
    // 荒脉没有灵眼、不可采，不进台账，只计数。
    // fixture 选 128x128/seed 26：实测世界里有 4 条荒脉。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK(diagnostics.barren_veins > 0u);
    // 台账里的脉数 = 世界里的脉数 − 荒脉数。一条不多，一条不少。
    CHECK_EQ(ledger.veins.size(),
             w.veins.size() - diagnostics.barren_veins);
    for (const auto& entry : ledger.veins)
    {
        CHECK(w.veins[entry.vein].rank != VeinRank::barren);
    }
}

TEST(a_depleted_eye_still_reports_its_original_throughput)
{
    // 枯眼的 eye.flow 被置零，但原始吞吐还在 ley_nodes 里。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 21;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    std::size_t alive = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        const auto& eye = w.eyes[vein.eye];
        if (!eye.depleted)
        {
            // 未枯竭的眼：流速就是灵眼自己的 flow，不用回查节点。
            ++alive;
            CHECK_EQ(entry.surveyed_rate, eye.flow);
            continue;
        }
        ++checked;
        CHECK_EQ(eye.flow, 0u);
        CHECK_EQ(entry.surveyed_rate, w.ley_nodes[eye.node].flow);
    }
    CHECK(checked > 0);
    // 未枯竭的那一支也要真的跑到，否则上面那条断言是空的。
    CHECK(alive > 0);
}

TEST(the_balance_starts_at_the_surveyed_total)
{
    // Task 4 的守恒恒等式从这条起算：余额的初始值就是封顶基数，
    // 而且是保守下界 surveyed_total，不是区间上界。
    //
    // fixture 必须含越采脉才有意义：非越采脉两者相等，断言恒真。
    //
    // spirit_year 取 8300（= epoch）：推演没有年份可走，余额停在初值。
    // 换成默认的当代年份时余额已经被逐年扣减，这条断言守的就不再是
    // 「初值」而是「推演后的余额」——那是另一条测试的事。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    p.spirit_year = 8300;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t overmined = 0;
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance, entry.surveyed_total);
        if (w.veins[entry.vein].overmined)
        {
            ++overmined;
            // 越采脉的基数是反推出来的，余额必须跟着基数走，
            // 不能是当代的物理储量——否则越采的痕迹就凭空消失了。
            CHECK(entry.balance > w.veins[entry.vein].reserve);
        }
    }
    CHECK(overmined > 0);
}

TEST(the_diagnostics_are_reset_between_runs)
{
    // 诊断量是「本次推演的结果」，不是累计值：调用方复用同一个结构时，
    // 不该看到上一次的计数叠上来。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;

    LedgerDiagnostics diagnostics;
    const Ledger first = gamev2::history::build_ledger(w, diagnostics);
    const std::uint32_t barren = diagnostics.barren_veins;
    CHECK(barren > 0u);

    const Ledger second = gamev2::history::build_ledger(w, diagnostics);
    // 同一个结构跑两遍，计数不能翻倍。
    CHECK_EQ(diagnostics.barren_veins, barren);
    CHECK_EQ(first.veins.size(), second.veins.size());
}

TEST(the_epoch_year_itself_yields_a_populated_ledger)
{
    // 闸门判的是 now < epoch，不是 now <= epoch。差一格是另一回事：
    // 8300 年是快照瞬间，台账正是从这一年起算，8300 本身必须有内容。
    //
    // 每个年份各生成一次世界：spirit_year 会改潮位，但潮位只被随后的
    // 浓度场阶段读取，灵脉生成不读它——两年的脉集合完全一致，
    // 「台账有没有内容」的差异只能来自年份本身。
    auto world_for_year = [](std::uint32_t year)
    {
        WorldParams p;
        p.width = 64;
        p.height = 64;
        p.z_levels = 3;
        p.seed = 7;
        p.spirit_year = year;
        return generate_world(p);
    };

    const auto before = world_for_year(8299);
    CHECK(before.has_value());
    if (before)
    {
        const Ledger ledger = gamev2::history::build_ledger(*before);
        CHECK(ledger.veins.empty());
        CHECK_EQ(ledger.now, 8299u);
    }

    const auto at_epoch = world_for_year(8300);
    CHECK(at_epoch.has_value());
    if (at_epoch)
    {
        const Ledger ledger = gamev2::history::build_ledger(*at_epoch);
        CHECK(!ledger.veins.empty());
        CHECK_EQ(ledger.now, 8300u);
    }
}

TEST(the_nine_main_veins_are_all_assigned)
{
    // 256x256 才能凑齐九条主脉：灵气层按面积缩放，小图幅凑不齐。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::vector<std::uint32_t> mains;
    std::vector<Sect> holders;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        mains.push_back(entry.vein);
        holders.push_back(entry.holder);
    }
    CHECK_EQ(mains.size(), 9u);

    // 无遗漏无重复：九条主脉归到六宗手里。设定 8.1 里主脉只归六宗
    // （太玄三主、丹霞两主、其余各一主），所以「九条脉九个持有者」
    // 不成立——成立的是「九条脉，六宗持有，且一宗不缺」。
    // Sect 是强类型枚举，没有内建的 operator<，排序得自己给比较器。
    std::sort(holders.begin(), holders.end(),
              [](Sect a, Sect b) {
                  return static_cast<std::uint8_t>(a)
                         < static_cast<std::uint8_t>(b);
              });
    std::vector<Sect> distinct;
    for (const auto& holder : holders)
    {
        if (distinct.empty() || distinct.back() != holder)
        {
            distinct.push_back(holder);
        }
    }
    CHECK_EQ(distinct.size(), 6u);
    // 天机阁无地：无主脉。
    CHECK(std::find(distinct.begin(), distinct.end(), Sect::tianji)
          == distinct.end());
    // 也没有主脉落进无主池：九条脉都有主。
    CHECK(std::find(distinct.begin(), distinct.end(), Sect::unowned)
          == distinct.end());
}

TEST(the_sects_hold_the_setting_six_veins)
{
    // 设定 8.1：太玄 1 龙 + 3 主，丹霞 2 主，寒江/万兽/青囊/九幽各 1 主。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t taixuan_main = 0;
    std::size_t taixuan_dragon = 0;
    std::size_t daxia = 0;
    std::size_t hanjiang = 0;
    std::size_t wanshou = 0;
    std::size_t qiangnang = 0;
    std::size_t jiuyou = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::dragon)
        {
            CHECK(entry.holder == Sect::taixuan);
            ++taixuan_dragon;
            continue;
        }
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        switch (entry.holder)
        {
        case Sect::taixuan: ++taixuan_main; break;
        case Sect::daxia: ++daxia; break;
        case Sect::hanjiang: ++hanjiang; break;
        case Sect::wanshou: ++wanshou; break;
        case Sect::qiangnang: ++qiangnang; break;
        case Sect::jiuyou: ++jiuyou; break;
        default: break;
        }
    }
    CHECK_EQ(taixuan_dragon, 1u);
    CHECK_EQ(taixuan_main, 3u);
    CHECK_EQ(daxia, 2u);
    CHECK_EQ(hanjiang, 1u);
    CHECK_EQ(wanshou, 1u);
    CHECK_EQ(qiangnang, 1u);
    CHECK_EQ(jiuyou, 1u);
}

TEST(branch_veins_belong_to_the_unowned_pool)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 5;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    std::size_t branches = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::branch)
        {
            continue;
        }
        ++branches;
        CHECK(entry.holder == Sect::unowned);
    }
    CHECK(branches > 0);
}

// 这条脉的占格里有没有阴地。
//
// 测试自己算一遍，不复用实现的判据：复用的话实现判错时测试也跟着错。
bool vein_has_yin_cell(const World& w, std::uint32_t vein)
{
    for (std::uint32_t cell = 0; cell < w.vein_id.size(); ++cell)
    {
        if (w.vein_id.at(cell) != vein)
        {
            continue;
        }
        if (w.land_type.at(cell) == gamev2::world::LandType::yin)
        {
            return true;
        }
    }
    return false;
}

TEST(the_hanjiang_vein_is_the_only_yin_main_vein)
{
    // 设定 8.1：寒江剑府握的是「一条阴脉（采出即衰）」。
    //
    // fixture 选 256x256/seed 7：实测九条主脉里只有一条占着阴地，
    // 而它排在第 0 槽（太玄的），寒江原本的槽位不是阴脉——所以
    // 「对调」这条分支必然被走到。
    //
    // 断言不写死脉编号，只断言「九条主脉里唯一的阴脉归寒江」。这样写
    // 既能抓住对调没发生，也能抓住整条 QuotaEntry 被换掉——那会让
    // 寒江拿着一条不阴的脉，而太玄拿着阴脉。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    std::size_t yin_mains = 0;
    std::size_t hanjiang_mains = 0;
    std::size_t hanjiang_yin = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        const bool yin = vein_has_yin_cell(w, entry.vein);
        if (yin)
        {
            ++yin_mains;
        }
        if (entry.holder == Sect::hanjiang)
        {
            ++hanjiang_mains;
            if (yin)
            {
                ++hanjiang_yin;
            }
        }
    }
    // fixture 的前提：九条主脉里恰好一条阴脉。
    CHECK_EQ(yin_mains, 1u);
    CHECK_EQ(hanjiang_mains, 1u);
    CHECK_EQ(hanjiang_yin, 1u);
    // 拿到了阴脉，不该再报「缺阴脉」。
    CHECK_EQ(diagnostics.missing_yin_vein, 0u);
    // 对调不改变各宗的脉数：换成谁，那条脉就是谁的。
    std::size_t taixuan_main = 0;
    std::size_t daxia = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        if (entry.holder == Sect::taixuan)
        {
            ++taixuan_main;
        }
        else if (entry.holder == Sect::daxia)
        {
            ++daxia;
        }
    }
    CHECK_EQ(taixuan_main, 3u);
    CHECK_EQ(daxia, 2u);
}

TEST(a_world_without_a_yin_vein_reports_the_missing_yin_vein)
{
    // 256x256/seed 1：九条主脉里一条阴地都没有，寒江拿不到阴脉。
    // 退化不是错误（设计 5），由诊断量报出。
    //
    // 但也不能留空缺：寒江仍然按序分到一条主脉。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 1;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK_EQ(diagnostics.missing_yin_vein, 1u);
    std::size_t hanjiang_mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main && entry.holder == Sect::hanjiang)
        {
            ++hanjiang_mains;
        }
    }
    CHECK_EQ(hanjiang_mains, 1u);
}

TEST(a_small_world_reports_short_main_veins)
{
    // 128x128/seed 5：实测只有两条主脉。分派照做（两条都归太玄），
    // 但差了几条要报出来——持有格局是设定给定的，凑不齐就是退化。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 5;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    // 九条主脉只有两条，缺七条。
    CHECK_EQ(diagnostics.short_main_veins, 7u);
    // 现有的主脉照样分派。
    std::size_t mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main)
        {
            ++mains;
            CHECK(entry.holder != Sect::unowned);
        }
    }
    CHECK_EQ(mains, 2u);
}

TEST(a_reserve_tie_is_broken_by_the_vein_id)
{
    // 同 reserve 的两条脉换个顺序就换一个持有者，所以排序必须全序：
    // reserve 降序，同值按脉编号升序。
    //
    // fixture 选 256x256/seed 32：实测 vein 3 与 vein 4 的 reserve 同为
    // 652，排在 slot 2 与 slot 3——正是太玄与丹霞的分界，所以同值比较
    // 的方向一变，两个持有者就互换。这条 fixture 没有阴脉（实测
    // yin_slots=0），所以阴脉对调不会掩盖 tie-break 的效果。
    //
    // 别的 fixture 测不到这条：绝大多数世界里主脉的 reserve 两两不等，
    // 同值比较那个分支从来没被走到。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 32;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    Sect holder_of_three = Sect::unowned;
    Sect holder_of_four = Sect::unowned;
    std::size_t mains = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank == VeinRank::main)
        {
            ++mains;
        }
        if (entry.vein == 3u)
        {
            holder_of_three = entry.holder;
        }
        if (entry.vein == 4u)
        {
            holder_of_four = entry.holder;
        }
    }
    CHECK_EQ(mains, 9u);
    // 编号小的占靠前的槽位：vein 3 在 slot 2（太玄），vein 4 在 slot 3
    // （丹霞）。同值比较反过来写，这两条就互换。
    CHECK(holder_of_three == Sect::taixuan);
    CHECK(holder_of_four == Sect::daxia);
}

TEST(the_jiuyou_vein_is_not_registered)
{
    // 《脉律》册上无名：九幽宫那条脉不登记在册（设定 8.1）。
    //
    // 这条只守「谁不登记」；「登记跟最终持有者走、而不是跟槽位走」由
    // 下面 seed 62 那条守——阴脉不落在第 8 槽时两种写法结果一样。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t jiuyou = 0;
    std::size_t registered = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.holder == Sect::jiuyou)
        {
            ++jiuyou;
            CHECK(!entry.registered);
            continue;
        }
        ++registered;
        CHECK(entry.registered);
    }
    // 守卫：fixture 一旦不再分到九幽宫，上面两条断言就整条空跑。
    CHECK_EQ(jiuyou, 1u);
    CHECK(registered > 0u);
}

TEST(the_unregistered_vein_follows_the_final_holder)
{
    // 登记状态跟最终持有者走，不是跟排序槽位走。
    //
    // fixture 选 256x256/seed 62：实测九条主脉里唯一那条阴脉正好落在
    // 第 8 槽——九幽宫的槽位。这是唯一能区分两种写法的情形：阴脉对调
    // 把第 8 槽的阴脉换给寒江，寒江原来那条落到九幽宫手里。
    //
    // 「按槽位登记」的错误写法会让阴脉那条（现在是寒江的）不登记、
    // 而九幽宫那条（现在是寒江原来的）登记——正好颠倒。
    //
    // 其余 fixture 测不到这条：阴脉不落在第 8 槽时，九幽宫拿的仍然是
    // 排序槽位 8 那条，两种写法结果一样。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 62;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    std::size_t jiuyou = 0;
    std::size_t hanjiang = 0;
    std::size_t jiuyou_yin = 0;
    std::size_t hanjiang_yin = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.rank != VeinRank::main)
        {
            continue;
        }
        const bool yin = vein_has_yin_cell(w, entry.vein);
        if (entry.holder == Sect::jiuyou)
        {
            ++jiuyou;
            CHECK(!entry.registered);
            if (yin)
            {
                ++jiuyou_yin;
            }
        }
        if (entry.holder == Sect::hanjiang)
        {
            ++hanjiang;
            CHECK(entry.registered);
            if (yin)
            {
                ++hanjiang_yin;
            }
        }
    }
    CHECK_EQ(jiuyou, 1u);
    CHECK_EQ(hanjiang, 1u);
    // 阴脉在寒江手里，不在九幽宫手里。
    CHECK_EQ(jiuyou_yin, 0u);
    CHECK_EQ(hanjiang_yin, 1u);
    // 拿到了阴脉，就不该再报「缺阴脉」。
    CHECK_EQ(diagnostics.missing_yin_vein, 0u);
}

TEST(balances_never_go_negative)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.veins.empty());
    for (const auto& entry : ledger.veins)
    {
        // balance 是无符号的，「不为负」是类型保证，写出来只是恒真。
        // 真正要守的是两条边界：采量不超基数，余额不超基数。
        CHECK(entry.extracted <= entry.surveyed_total);
        CHECK(entry.balance <= entry.surveyed_total);
    }
}

TEST(conservation_holds_at_every_year)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 逐年守恒：任何一年都不许把余额采成负数，且最后一年的年末余额
    // 恰好等于台账记的当代余额。
    //
    // 台账没有公开每一年的余额，所以用事件表复算：余额(t) = 8300 承载
    // 上限 − 截至 t 年的开采累加。事件按年份递增，顺序遍历即时间序。
    std::map<std::uint32_t, std::uint32_t> running;
    for (const auto& entry : ledger.veins)
    {
        running[entry.vein] = entry.surveyed_total;
    }
    // 事件必须落在推演区间内，且年份不倒退——否则「顺序遍历即时间序」
    // 这个前提就不成立，后面的复算全部作废。劫损表的年份升序由
    // the_tribulation_events_land_inside_the_window 负责。
    std::uint32_t last_year = ledger.epoch;
    bool year_went_backwards = false;
    for (const auto& event : ledger.events)
    {
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        if (event.year < last_year)
        {
            year_went_backwards = true;
        }
        last_year = event.year;
    }
    CHECK(!year_went_backwards);

    // 守卫：这个 fixture 必须真的有劫损。
    CHECK(!ledger.tribulation_events.empty());

    // 逐年内次序与实现一致：① 越采 → ② 劫损 → ③ 正常开采；
    // 跨年按年份升序。三段过滤而不是按事件表下标交错：两张表各自按年份
    // 递增，但谁在前谁在后只有按年重排才看得出来。
    for (std::uint32_t year = ledger.epoch; year < ledger.now; ++year)
    {
        for (const auto& event : ledger.events)
        {
            if (event.year == year && event.over_quota)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
        for (const auto& event : ledger.tribulation_events)
        {
            if (event.year == year)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
        for (const auto& event : ledger.events)
        {
            if (event.year == year && !event.over_quota)
            {
                CHECK(running[event.vein] >= event.amount);
                running[event.vein] -= event.amount;
            }
        }
    }
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(running[entry.vein], entry.balance);
    }
    // 守卫：没有事件的话上面两条断言整条空跑。
    CHECK(!ledger.events.empty());
}

TEST(final_balance_matches_the_ledger)
{
    WorldParams p;
    p.width = 128;                 // 64² 的纪元渡劫场 40/40 全空，验不到劫损
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    // 守卫：这个 fixture 必须真的有劫损，否则新公式在它身上恒 0、空跑。
    CHECK(!ledger.tribulation_events.empty());
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance,
                 entry.surveyed_total - entry.extracted
                     - entry.tribulation_loss);
    }
}

TEST(the_events_account_for_every_extraction)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 非空守卫：没有事件时下面那条「事件总和 == 开采量」两边都是 0，
    // 恒真——这条测试会变成空跑。事件表必须真的记着六百年的开采。
    CHECK(!ledger.events.empty());

    // 逐脉核对：事件总和 == extracted。
    std::vector<std::uint32_t> sums(ledger.veins.size(), 0u);
    for (const auto& event : ledger.events)
    {
        CHECK(event.amount > 0u);
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        bool found = false;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            if (ledger.veins[i].vein == event.vein)
            {
                sums[i] += event.amount;
                found = true;
                break;
            }
        }
        CHECK(found);
    }
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        CHECK_EQ(sums[i], ledger.veins[i].extracted);
    }
}

TEST(the_ledger_is_deterministic)
{
    // 同 (seed, params) 两次构建，账本逐字段相同。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto a = generate_world(p);
    const auto b = generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    const Ledger la = gamev2::history::build_ledger(*a);
    const Ledger lb = gamev2::history::build_ledger(*b);
    // 守卫：确定性比较要真的比较到渡劫事件，否则新字段没被测到。
    CHECK(!la.tribulation_events.empty());
    CHECK_EQ(la.tribulation_events.size(), lb.tribulation_events.size());
    for (std::size_t i = 0; i < la.tribulation_events.size(); ++i)
    {
        CHECK_EQ(la.tribulation_events[i].vein,
                 lb.tribulation_events[i].vein);
        CHECK_EQ(la.tribulation_events[i].year,
                 lb.tribulation_events[i].year);
        CHECK_EQ(la.tribulation_events[i].amount,
                 lb.tribulation_events[i].amount);
        CHECK_EQ(la.tribulation_events[i].site,
                 lb.tribulation_events[i].site);
    }
    CHECK_EQ(la.tribulation_failed, lb.tribulation_failed);
    CHECK_EQ(la.tribulation_deviation, lb.tribulation_deviation);
    CHECK_EQ(la.veins.size(), lb.veins.size());
    for (std::size_t i = 0; i < la.veins.size(); ++i)
    {
        CHECK_EQ(la.veins[i].vein, lb.veins[i].vein);
        CHECK_EQ(la.veins[i].holder, lb.veins[i].holder);
        CHECK_EQ(la.veins[i].surveyed_total, lb.veins[i].surveyed_total);
        CHECK_EQ(la.veins[i].surveyed_rate, lb.veins[i].surveyed_rate);
        CHECK_EQ(la.veins[i].extracted, lb.veins[i].extracted);
        CHECK_EQ(la.veins[i].balance, lb.veins[i].balance);
    }
    CHECK_EQ(la.events.size(), lb.events.size());
    for (std::size_t i = 0; i < la.events.size(); ++i)
    {
        CHECK_EQ(la.events[i].vein, lb.events[i].vein);
        CHECK_EQ(la.events[i].year, lb.events[i].year);
        CHECK_EQ(la.events[i].amount, lb.events[i].amount);
    }
}

TEST(every_overmined_vein_is_claimed)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 13;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        if (!vein.overmined)
        {
            continue;
        }
        ++checked;
        // 越采必须足额认领：认领不全的话守恒恒等式 3 会塌。
        CHECK_EQ(entry.overmined_amount, entry.surveyed_total - vein.reserve);
        std::uint32_t claimed = 0;
        for (const auto& event : ledger.events)
        {
            if (event.vein == entry.vein && event.over_quota)
            {
                claimed += event.amount;
            }
        }
        CHECK_EQ(claimed, entry.overmined_amount);
    }
    CHECK(checked > 0);
}

TEST(ledger_never_double_counts_overmining)
{
    // 守恒恒等式 3：reserve − balance = 正常开采量 ≥ 0。
    // 越采被重复计一次的话，这条会变成负。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 台账为空的话下面的循环一条都不跑，这条断言会变成空跑失败。
    CHECK(!ledger.veins.empty());
    // 守卫：seed 13 的纪元池 1 处但失败 0 次，换 seed 11 才有劫损可验。
    CHECK(!ledger.tribulation_events.empty());

    std::size_t overmined = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        // 不含越采的那一份开采量：越采那一笔在事件表里单独标着。
        std::uint32_t normal = 0;
        for (const auto& event : ledger.events)
        {
            if (event.vein == entry.vein && !event.over_quota)
            {
                normal += event.amount;
            }
        }
        const std::int64_t gap =
            static_cast<std::int64_t>(vein.reserve) - entry.balance;
        CHECK(gap >= 0);
        // 等号那一半：越采脉的基数是反推出来的，reserve = 基数 − 越采量，
        // 所以 reserve − balance 恰好等于「不含越采的开采量 + 劫损」。
        CHECK_EQ(gap,
                 static_cast<std::int64_t>(normal)
                     + static_cast<std::int64_t>(entry.tribulation_loss));
        if (vein.overmined)
        {
            ++overmined;
        }
    }
    // 守卫：fixture 一旦不再产出越采脉，上面那条等式在非越采脉上恒成立
    // （基数就是 reserve），越采的那一份就没有被测到。
    CHECK(overmined > 0);
}

TEST(every_depleted_eye_is_claimed)
{
    // worldgen 标了枯眼的脉，台账里必有归零年。
    //
    // fixture 选 256x256/seed 3：这个世界上既有台账真的抽干的脉，也有
    // worldgen 标了枯、账上没枯的脉——两种情形都要走到，测试才不是
    // 只验一种。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    std::size_t claimed = 0;
    std::size_t drained = 0;
    std::size_t unbacked = 0;
    for (const auto& entry : ledger.veins)
    {
        const auto& vein = w.veins[entry.vein];
        const bool marked = w.eyes[vein.eye].depleted;
        const bool emptied = entry.balance == 0u;
        if (marked)
        {
            ++claimed;
        }
        if (marked && emptied)
        {
            ++drained;
            // 台账确实抽干了它：归零年必须是推演得出的那一年，
            // 而且落在推演区间之内。
            CHECK(entry.depleted_year != 0u);
            CHECK(entry.depleted_year < ledger.now);
        }
        else if (marked && !emptied)
        {
            ++unbacked;
            // worldgen 说枯了、账上没枯：归零年只能记成当代，意思是
            // 「到当代为止没有归零过」。
            CHECK_EQ(entry.depleted_year, ledger.now);
        }
    }
    CHECK(claimed > 0);
    // 守卫：fixture 一旦不再抽出枯竭脉，「归零年 < 当代」这一侧就整条
    // 空跑，整条测试变成只查一个恒真的字段。
    CHECK(drained > 0);
    // 守卫：fixture 一旦不再产出「标了枯而账上没枯」的脉，下面那个分支
    // 整条空跑，哨兵年那一侧就没人验了。
    CHECK(unbacked > 0);
    // 诊断量要跟实际数出来的冲突数一致：这个字段才真的被用上了。
    CHECK_EQ(diagnostics.depletion_claim_unbacked, unbacked);
}

TEST(the_deviation_is_not_trivial)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 至少有一条脉的配额与当代余额不相等。
    bool any = false;
    for (const auto& entry : ledger.veins)
    {
        if (entry.surveyed_total != entry.balance)
        {
            any = true;
        }
    }
    CHECK(any);

    // 宗级汇总必须是八项、索引即宗门编号。把每一项的 sect 写成同一个
    // 值或者少几项，上面的逐脉断言照样通过，所以形状得在这里钉死。
    CHECK_EQ(ledger.sects.size(), static_cast<std::size_t>(Sect::count));
    for (std::size_t i = 0; i < ledger.sects.size(); ++i)
    {
        CHECK(ledger.sects[i].sect == static_cast<Sect>(i));
    }

    // 这个 fixture 有脉、有额度：宗级汇总里必须有内容，否则上面那两条
    // 形状断言对一份空壳照样成立。
    std::uint32_t sect_quota = 0;
    for (const auto& sect : ledger.sects)
    {
        sect_quota += sect.quota_total;
    }
    CHECK(sect_quota > 0u);
}

TEST(the_structural_deviation_dominates)
{
    // 招牌断言：8300 是峰值年、8914 是谷底年，口径差必然压过另两类。
    // 潮位公式若改了，这条会先变红——那正是它存在的意义。
    //
    // fixture 选 128x128/seed 11：实测纪元池 3 处、失败 2 次（8733、
    // 8571），劫损非零（合计 50）。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.structural_deviation > 0);
    // 前提：这个 fixture 真的有越采、有劫损。少了任何一个，下面的比较
    // 就成了 0 与正数的比较，等于没测。
    CHECK(ledger.overmining_deviation > 0);
    CHECK(ledger.tribulation_deviation > 0);
    CHECK(ledger.structural_deviation >= ledger.overmining_deviation);
    CHECK(ledger.structural_deviation >= ledger.tribulation_deviation);
}

TEST(the_tribulation_toll_rolls_up_per_sect)
{
    // 劫损要能指名到宗门：「太玄宗的地被天地抽走了多少」才是配额之争里
    // 有人会拿来说话的数，只有一个全账总数就没法归因。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(ledger.tribulation_deviation > 0);

    // 逐宗归因：每宗的劫损必须等于它名下那些脉的劫损之和。只验总和的话，
    // 「全部堆进同一宗」的错误归因照样能过。
    for (const auto& sect : ledger.sects)
    {
        std::int64_t expected = 0;
        for (const auto& entry : ledger.veins)
        {
            if (entry.holder == sect.sect)
            {
                expected += entry.tribulation_loss;
            }
        }
        CHECK_EQ(sect.tribulation_loss, expected);
    }

    std::int64_t sum = 0;
    for (const auto& sect : ledger.sects)
    {
        sum += sect.tribulation_loss;
    }
    // 宗级合计必须逐项加得回全账口径，否则归因就是假的。
    CHECK_EQ(sum, ledger.tribulation_deviation);
}

TEST(unregistered_extraction_is_separate)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    std::uint32_t unregistered = 0;
    std::uint32_t unregistered_current = 0;
    for (const auto& entry : ledger.veins)
    {
        if (!entry.registered)
        {
            unregistered += entry.extracted;
            unregistered_current += entry.balance;
        }
    }
    // 守卫：没有未登记的脉，上面两条断言就整条空跑。
    CHECK(unregistered > 0u);
    // 余额也要非零：否则「登记宗余额 == 登记脉余额之和」这条断言在未登记
    // 余额为 0 时恒真，登记宗被偷偷并入未登记余额也照样过。
    CHECK(unregistered_current > 0u);
    CHECK_EQ(ledger.unregistered_extracted, unregistered);

    // 未登记的脉不计入任何登记宗的汇总。
    std::uint32_t registered_current = 0;
    for (const auto& sect : ledger.sects)
    {
        if (!sect.registered)
        {
            continue;
        }
        registered_current += sect.current_total;
    }
    std::uint32_t all_registered = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.registered)
        {
            all_registered += entry.balance;
        }
    }
    CHECK_EQ(registered_current, all_registered);
}

TEST(the_rate_deviation_is_the_tide_shortfall)
{
    // 8300 是峰值年、当代是谷底年：流速偏差是「把核定的流速按当代潮位
    // 折下来」差的那一份。对每一宗，偏差都不超过核定流速，且不为负。
    //
    // 这条守住 deviation_rate 的定义：它不是存量减存量，也不是恒零。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    const std::uint32_t level_epoch =
        gamev2::world::compute_spirit_tide(ledger.epoch).level;
    const std::uint32_t level_now =
        gamev2::world::compute_spirit_tide(ledger.now).level;
    // 前提：8300 是峰值、当代是谷底。哪天潮位公式改了，这里先红。
    CHECK(level_epoch > level_now);

    std::size_t checked = 0;
    bool any_positive = false;
    for (const auto& sect : ledger.sects)
    {
        if (sect.quota_rate == 0u)
        {
            continue;
        }
        ++checked;
        // 核定的流速不会低于当代潮位能支撑的流速，所以偏差非负。
        CHECK(sect.deviation_rate >= 0);
        // 潮位比差小于 1，所以偏差不会超过核定流速本身。
        CHECK(sect.deviation_rate
              <= static_cast<std::int64_t>(sect.quota_rate));
        if (sect.deviation_rate > 0)
        {
            any_positive = true;
        }
    }
    CHECK(checked > 0);
    // 只给上下界的话，恒零的 deviation_rate 也照样过；这个 fixture 里
    // 确有宗级流速偏差，把「非零」钉住。
    CHECK(any_positive);
}

/// 台账区段内的列数是否一致。
///
/// 返回 false 时先把区段名与两边的列数打出来：CHECK 只印表达式，印不出
/// 运行时的区段名，而「哪张表错位」正是排错时要找的东西。
bool columns_agree(const std::string& section,
                    std::size_t expected_tabs,
                    std::size_t actual_tabs)
{
    if (expected_tabs == actual_tabs)
    {
        return true;
    }
    std::println("  FAIL 台账区段「{}」列数错位：本段首行 {} 列，该行 {} 列",
                 section, expected_tabs + 1u, actual_tabs + 1u);
    return false;
}

TEST(the_ledger_is_exportable_as_text)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    // 守卫：这个 fixture 必须真的有劫损，否则渡劫清单那一段整条空跑。
    CHECK(!ledger.tribulation_events.empty());

    // 文件名带上种子：并发跑测试时各写各的，不互相覆盖。
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("gamev2_ledger_test_seed" + std::to_string(p.seed) + ".txt");
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    std::size_t lines = 0;
    // 每个 === 区段内，带 tab 的行必须列数一致：按表头取列的解析器会
    // 照着标签读值，标签多一个就整体错位。
    std::string section;
    std::size_t section_tabs = 0;
    bool section_measured = false;
    while (std::getline(in, line))
    {
        ++lines;
        if (line.starts_with("=== "))
        {
            section = line;
            section_measured = false;
            continue;
        }
        const auto tabs = static_cast<std::size_t>(
            std::ranges::count(line, '\t'));
        if (tabs == 0u)
        {
            // 空行与不带 tab 的行不参与列数比较。
            continue;
        }
        if (!section_measured)
        {
            // 区段里第一条带 tab 的行（本就是表头）定下列数。
            section_tabs = tabs;
            section_measured = true;
            continue;
        }
        CHECK(columns_agree(section, section_tabs, tabs));
    }
    in.close();
    std::filesystem::remove(path);

    // 表格行数 = 脉数 + 事件数 + 越采案数 + 枯竭脉数，加上固定的表头
    // 行数。越采案与枯竭脉是另外两段清单，它们把事件与脉各抄了一遍，
    // 所以行数里要把这两份重复算进去。
    std::size_t overmine_rows = 0;
    for (const auto& event : ledger.events)
    {
        if (event.over_quota)
        {
            ++overmine_rows;
        }
    }
    std::size_t depleted_rows = 0;
    std::size_t disputed_rows = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.depleted_year == 0u)
        {
            continue;
        }
        // 枯竭脉清单只收真抽干的那；depleted_year == ledger.now 是哨兵，
        // 那条脉归到争议段去了。
        if (entry.depleted_year == ledger.now)
        {
            ++disputed_rows;
        }
        else
        {
            ++depleted_rows;
        }
    }
    const std::size_t rows = ledger.veins.size() + ledger.events.size()
                           + ledger.tribulation_events.size()
                           + overmine_rows + depleted_rows + disputed_rows;
    CHECK(lines > rows);
    // 固定表头 = 原有的 24 行 + 越采案清单 3 行 + 枯竭脉清单 3 行
    // + 枯竭争议 3 行 + 渡劫事件清单 3 行 + 诊断 12 行（标记 + 表头 + 9 项）
    // + 偏差分解的劫损 1 行 = 49 行，余量 1 行留给导出格式的小变动。
    //
    // 越采案、枯竭脉、枯竭争议与渡劫清单是另外四段，它们把事件与脉各抄了
    // 一遍，所以行数里要把这几份重复算进去——只按「脉 + 事件」算，边界会
    // 差出几十行。
    CHECK(lines < rows + 50u);
}

TEST(the_sects_are_populated_even_at_the_epoch)
{
    // now == epoch 是一条合法的配置：台账有脉有配额，只是没有年份可走。
    //
    // 这里守的是「脉非空 ⟹ 宗级汇总齐备」这条不变式：按持有者下标的
    // 调用方（实现自己就这么用）会读到越界，宗级汇总少一截就出事。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 26;
    p.spirit_year = 8300;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);

    // 前提：fixture 真的有脉。没有脉的话「脉非空 ⟹ 宗级汇总齐备」
    // 这条不变式就整条空跑。
    CHECK(!ledger.veins.empty());
    CHECK_EQ(ledger.sects.size(), static_cast<std::size_t>(Sect::count));
    for (std::size_t i = 0; i < ledger.sects.size(); ++i)
    {
        CHECK(ledger.sects[i].sect == static_cast<Sect>(i));
    }
    // 没有任何一年被推演，余额还停在基数上，于是余量为 0。
    for (const auto& sect : ledger.sects)
    {
        CHECK_EQ(sect.deviation_total, 0);
        CHECK_EQ(sect.deviation_rate, 0);
    }
    CHECK_EQ(ledger.structural_deviation, 0);
}

TEST(a_span_beyond_the_limit_is_refused)
{
    // 跨度上限：年份是个 uint32，不设上限就能要出四十亿次循环与一份
    // 几百 GB 的余额轨迹。超限的台账按「没有推演」处理并报出退化。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 11;
    p.spirit_year = 8300u + 100001u;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK_EQ(diagnostics.span_too_large, 1u);
    // 没有推演就没有渡劫：与事件表、余额同一个口径。
    CHECK(ledger.tribulation_events.empty());
    CHECK_EQ(ledger.tribulation_deviation, 0);
    CHECK_EQ(ledger.tribulation_failed, 0u);
    // 没有推演：事件表是空的，余额还停在基数上。
    CHECK(ledger.events.empty());
    CHECK(!ledger.veins.empty());
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance, entry.surveyed_total);
        CHECK_EQ(entry.extracted, 0u);
    }
    // 宗级汇总仍然齐备：不变式不因退化而破。
    CHECK_EQ(ledger.sects.size(), static_cast<std::size_t>(Sect::count));
    for (std::size_t i = 0; i < ledger.sects.size(); ++i)
    {
        CHECK(ledger.sects[i].sect == static_cast<Sect>(i));
    }
}

/// 按 tab 切分一行。
std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true)
    {
        const auto tab = line.find('\t', start);
        if (tab == std::string::npos)
        {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

TEST(the_export_marks_the_unregistered_sect_and_the_degradations)
{
    // 九幽宫那条脉不在册：导出的宗表必须把它标出来。否则读者只看得见
    // 「未登记开采」这个总数，不知道是哪一宗，也看不出这一宗仍在开采。
    //
    // 诊断量也必须进到文件里：这份文档就是被读、被争的地方，退化不能
    // 只留在内存里。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    // fixture 的前提：确实有一宗不登记。
    std::size_t unregistered_sects = 0;
    for (const auto& sect : ledger.sects)
    {
        if (!sect.registered)
        {
            ++unregistered_sects;
        }
    }
    CHECK_EQ(unregistered_sects, 1u);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("gamev2_ledger_mark_seed" + std::to_string(p.seed) + ".txt");
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    bool in_sect_table = false;
    bool saw_diagnostics = false;
    std::size_t marked_no = 0;
    std::size_t marked_yes = 0;
    while (std::getline(in, line))
    {
        if (line.starts_with("=== "))
        {
            in_sect_table = line.find("七宗") != std::string::npos;
            saw_diagnostics |= line.find("诊断") != std::string::npos;
            continue;
        }
        if (!in_sect_table)
        {
            continue;
        }
        const auto fields = split_tabs(line);
        if (fields.size() < 2u)
        {
            continue;
        }
        if (fields[1] == "是")
        {
            ++marked_yes;
        }
        else if (fields[1] == "否")
        {
            ++marked_no;
        }
    }
    in.close();
    std::filesystem::remove(path);

    CHECK(saw_diagnostics);
    CHECK_EQ(marked_no, 1u);
    CHECK(marked_yes > 0u);
}

/// 台账导出文件的区段：=== 标记到该区段的行。
std::map<std::string, std::vector<std::string>> read_sections(
    const std::filesystem::path& path)
{
    std::map<std::string, std::vector<std::string>> sections;
    std::ifstream in(path);
    std::string line;
    std::string current;
    while (std::getline(in, line))
    {
        if (line.starts_with("=== "))
        {
            current = line;
            continue;
        }
        if (current.empty())
        {
            continue;
        }
        sections[current].push_back(line);
    }
    return sections;
}

/// 取一个区段的数据行：跳过表头与空行。
std::vector<std::string> data_rows(
    const std::map<std::string, std::vector<std::string>>& sections,
    const std::string& marker)
{
    std::vector<std::string> rows;
    const auto it = sections.find(marker);
    if (it == sections.end())
    {
        return rows;
    }
    for (std::size_t i = 0; i < it->second.size(); ++i)
    {
        if (i == 0u || it->second[i].empty())
        {
            continue;
        }
        rows.push_back(it->second[i]);
    }
    return rows;
}

TEST(the_export_separates_a_disputed_vein_from_a_drained_one)
{
    // worldgen 标了枯、账上没枯的脉，不能混进枯竭脉清单：它还有余额，
    // 读者却会以为它在当年跑干了。争议也不许被藏起来——它得被点名。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 3;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    // 争议脉：哨兵年 == 当代，且余额不为零。
    std::map<std::uint32_t, std::uint32_t> disputed;
    for (const auto& entry : ledger.veins)
    {
        if (entry.depleted_year != 0u && entry.depleted_year == ledger.now)
        {
            CHECK(entry.balance > 0u);
            disputed[entry.vein] = entry.balance;
        }
    }
    // 守卫：fixture 一旦不再产出争议脉，下面三段就整条空跑。
    CHECK(!disputed.empty());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("gamev2_ledger_dispute_seed" + std::to_string(p.seed) + ".txt");
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, path));
    const auto sections = read_sections(path);
    std::filesystem::remove(path);

    // 枯竭脉清单：每一条都是真抽干的，枯竭年在当代之前，余额为零。
    for (const auto& line : data_rows(sections, "=== 枯竭脉清单 ==="))
    {
        const auto fields = split_tabs(line);
        CHECK_EQ(fields.size(), std::size_t{4});
        CHECK(std::stoul(fields[2]) < ledger.now);
        CHECK_EQ(std::stoul(fields[3]), std::size_t{0});
    }

    // 争议段：一条不多一条不少，余额与账上的一致。
    const auto rows = data_rows(sections, "=== 枯竭争议 ===");
    CHECK_EQ(rows.size(), disputed.size());
    for (const auto& line : rows)
    {
        const auto fields = split_tabs(line);
        CHECK_EQ(fields.size(), std::size_t{4});
        const auto vein = static_cast<std::uint32_t>(std::stoul(fields[0]));
        const auto it = disputed.find(vein);
        CHECK(it != disputed.end());
        if (it != disputed.end())
        {
            CHECK_EQ(std::stoul(fields[3]), it->second);
        }
    }

    // 脉级台账：争议脉的枯竭年印 0，与它非零的当代余额自洽。
    for (const auto& line : data_rows(sections, "=== 脉级台账 ==="))
    {
        const auto fields = split_tabs(line);
        CHECK_EQ(fields.size(), std::size_t{9});
        const auto vein = static_cast<std::uint32_t>(std::stoul(fields[0]));
        if (disputed.contains(vein))
        {
            CHECK_EQ(std::stoul(fields[8]), std::size_t{0});
        }
    }
}

TEST(the_epoch_sites_are_derived_exactly)
{
    // 候选池不是「重新生成一份世界」，而是「拷贝 + 只重跑潮相关层」。
    // 两者的相等是唯一挡住「worldgen 在域/疤/地貌之间插了阶段」的东西：
    // 插了阶段，派生出来的池子就会静默过时，而台账不会报任何错。
    //
    // 64² 的池子 40/40 全空，两边都空的「逐处一致」是空比较，所以必须
    // 用 128²；seed 11 实测池子 3 处。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto host = generate_world(p);
    CHECK(host.has_value());
    if (!host)
    {
        return;
    }
    // 只读边界：调用前后调用方的世界必须不变。派生靠「拷贝 + 改拷贝」
    // 才不写输入——这个约定要有测试守着，否则有人把入参改成 World& 时
    // 套件不会红，只会在推演里把当代潮位悄悄写回世界。
    const std::size_t sites_before = host->tribulation_sites.size();
    const auto tide_before = host->spirit_tide;
    const auto derived =
        gamev2::history::epoch_tribulation_sites(*host);
    CHECK(!derived.empty());
    CHECK_EQ(host->tribulation_sites.size(), sites_before);
    CHECK_EQ(host->spirit_tide.level, tide_before.level);
    CHECK_EQ(host->spirit_tide.year, tide_before.year);

    WorldParams pe = p;
    pe.spirit_year = 8300;
    const auto full = generate_world(pe);
    CHECK(full.has_value());
    if (!full)
    {
        return;
    }
    CHECK_EQ(derived.size(), full->tribulation_sites.size());
    for (std::size_t i = 0; i < derived.size(); ++i)
    {
        CHECK_EQ(derived[i].cell, full->tribulation_sites[i].cell);
        CHECK_EQ(derived[i].radius, full->tribulation_sites[i].radius);
        CHECK_EQ(derived[i].vein, full->tribulation_sites[i].vein);
    }
}

/// 某条脉在某处劫云圈内覆盖的节点数占它总节点数的量。照实现的公式
/// 从 World 反算——基数写在账里，但基数由谁提供要靠测试自己推，否则
/// 改了覆盖口径两边一起漂，断言永远绿。
std::pair<std::uint32_t, std::uint32_t> coverage_of(
    const World& w, std::uint32_t vein_index, std::uint32_t site_cell,
    std::uint32_t radius)
{
    const gamev2::world::SpiritVein& vein = w.veins[vein_index];
    const auto& nodes = w.ley_channels[vein.channel].nodes;
    const std::uint32_t width = w.params().width;
    const std::int64_t sx = static_cast<std::int64_t>(site_cell % width);
    const std::int64_t sy = static_cast<std::int64_t>(site_cell / width);
    const std::int64_t r2 = static_cast<std::int64_t>(radius)
                            * static_cast<std::int64_t>(radius);
    std::uint32_t inside = 0;
    for (std::uint32_t j = vein.first_node; j <= vein.last_node; ++j)
    {
        const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
        const std::int64_t dx = static_cast<std::int64_t>(cell % width) - sx;
        const std::int64_t dy = static_cast<std::int64_t>(cell / width) - sy;
        if (dx * dx + dy * dy <= r2)
        {
            ++inside;
        }
    }
    const std::uint32_t total = vein.last_node - vein.first_node + 1u;
    return {inside, total};
}

/// 这笔劫损执行那一刻的余额——劫损的基数。
///
/// 基数 = 上一年年末余额 − 该年越采 − 该年更早劫心（同脉同年多笔时，后
/// 一笔要减掉前一笔已经抽走的那份）。只用两张事件表就能复算，不需要台账
/// 公开逐年余额。
std::uint32_t balance_before_toll(const Ledger& ledger, std::uint32_t vein,
                                  std::uint32_t year, std::uint32_t site)
{
    std::uint32_t balance = 0;
    bool started = false;
    for (const auto& entry : ledger.veins)
    {
        if (entry.vein == vein)
        {
            balance = entry.surveyed_total;
            started = true;
            break;
        }
    }
    if (!started)
    {
        return 0;
    }
    for (std::uint32_t y = ledger.epoch; y < year; ++y)
    {
        for (const auto& e : ledger.events)
        {
            if (e.vein == vein && e.year == y)
            {
                balance -= e.amount;
            }
        }
        for (const auto& e : ledger.tribulation_events)
        {
            if (e.vein == vein && e.year == y)
            {
                balance -= e.amount;
            }
        }
    }
    // 同一脉同年多笔劫损时，后一笔的基数要减掉前一笔已抽走的。事件表按
    // (年, 劫心) 升序，同脉同年劫心更小的那些就是已经施加过的。
    for (const auto& e : ledger.tribulation_events)
    {
        if (e.vein == vein && e.year == year && e.site < site)
        {
            balance -= e.amount;
        }
    }
    for (const auto& e : ledger.events)
    {
        if (e.vein == vein && e.year == year && e.over_quota)
        {
            balance -= e.amount;
        }
    }
    return balance;
}

TEST(a_failed_tribulation_takes_the_covered_share)
{
    // 损失 = 当年余额 × 圈内节点占比，先乘后除。覆盖比 1.00 时整条脉
    // 被抽干，这正是设定 5.2「抽干方图百里」的几何读法。
    //
    // 这一条同时兑现设计 §6.2 的 #2 与 #6：`balance_before_toll` 把该年
    // **已经发生的越采**也扣掉，所以基数一旦不含越采（次序挪了）或含了
    // 当年正常开采，下面的等式立刻红。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());

    const auto pool =
        gamev2::history::epoch_tribulation_sites(w);
    std::size_t checked = 0;
    for (const auto& event : ledger.tribulation_events)
    {
        // 找到这笔事件对应的候选池条目，拿它的劫心与半径。
        std::uint32_t radius = 0;
        bool found = false;
        for (const auto& site : pool)
        {
            if (site.cell == event.site)
            {
                radius = site.radius;
                found = true;
                break;
            }
        }
        CHECK(found);
        if (!found)
        {
            continue;
        }
        const auto [covered, total] =
            coverage_of(w, event.vein, event.site, radius);
        CHECK(covered > 0u);
        // 基数：这笔劫损执行那一刻的余额，扣掉同年更早劫心已经抽走的。
        const std::uint32_t base =
            balance_before_toll(ledger, event.vein, event.year, event.site);
        ++checked;
        CHECK_EQ(event.amount, base * covered / total);
    }
    CHECK(checked > 0);
}

TEST(tribulation_loss_never_exceeds_the_balance)
{
    // 无符号恒等式的前提：每一笔扣减都 ≤ 扣减时刻的余额。劫损取余额的
    // 覆盖比例，比例 ≤ 1 所以天然成立——这条断言守的是「天然」没被改坏。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());
    for (const auto& entry : ledger.veins)
    {
        CHECK_EQ(entry.balance,
                 entry.surveyed_total - entry.extracted
                     - entry.tribulation_loss);
        // 累计劫损不得超过「基数减去已采」：余额 = 基数 − 已采 − 劫损 ≥ 0
        // 正是这条不等式的等价形式。比「劫损 ≤ 基数」强，也正是注释说的
        // 「损失不超过扣减时刻的余额」。
        CHECK(entry.tribulation_loss
              <= entry.surveyed_total - entry.extracted);
    }
}

TEST(the_tribulation_events_land_inside_the_window)
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    // fixture 选 128x128/seed 28：实测失败 1 处、事件 2 笔、不同劫心 1 处
    // ——一处劫心圈住两条台账脉，逐脉各记一笔。
    //
    // 不用 seed 11：它那处失败的劫心只圈住一条余额非零的脉，事件 1 笔、
    // 不同劫心 1 处，下面那条「事件笔数 > 不同劫心数」的守卫在它身上恒假。
    //
    // fixture 随 Stream::tribulation 的编号走：派系层抢走了 18 号，本层让号
    // 改占 19，抽样结果就全变了——上次选 seed 26 就是因为这个失效的。改号
    // 之后要重扫，不能沿用旧种子。
    p.seed = 28;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const Ledger ledger = gamev2::history::build_ledger(w);
    CHECK(!ledger.tribulation_events.empty());
    // 失败候选数与事件笔数不是同一个量：一处劫心可以圈住多条台账脉，
    // 逐脉各记一笔（设计 §3.4），所以事件笔数可能比失败次数多。成立的
    // 不变式是「出现在事件表里的不同劫心数 ≤ 失败候选数」——空圈失败
    // 只计数，不入账。
    std::vector<std::uint32_t> distinct_sites;
    for (const auto& event : ledger.tribulation_events)
    {
        if (std::ranges::find(distinct_sites, event.site)
            == distinct_sites.end())
        {
            distinct_sites.push_back(event.site);
        }
    }
    CHECK(static_cast<std::size_t>(ledger.tribulation_failed)
          >= distinct_sites.size());
    // 守卫：seed 28 实测失败 1 处、事件 2 笔、不同劫心 1 处。这条断言在
    // 「一处劫心圈住多条脉」时才有牙。
    CHECK(ledger.tribulation_events.size() > distinct_sites.size());

    std::uint32_t last_year = ledger.epoch;
    std::uint32_t last_site = 0;
    bool first = true;
    for (const auto& event : ledger.tribulation_events)
    {
        CHECK(event.year >= ledger.epoch);
        CHECK(event.year < ledger.now);
        CHECK(event.amount > 0u);
        // 脉编号是 worldgen 的编号，台账只收非荒脉，两者下标不对应，
        // 不能拿台账长度判界。
        bool known = false;
        for (const auto& entry : ledger.veins)
        {
            if (entry.vein == event.vein)
            {
                known = true;
                break;
            }
        }
        CHECK(known);
        if (!first)
        {
            CHECK(event.year >= last_year);
        }
        // 同年按劫心升序（设计 §4）。
        if (!first && event.year == last_year)
        {
            CHECK(event.site >= last_site);
        }
        first = false;
        last_year = event.year;
        last_site = event.site;
    }
}

TEST(the_export_carries_the_tribulation_toll)
{
    // 台账是给人读的：劫损若只在内存里，配额之争的第三方成因读者看不见，
    // 那它跟没算过一样。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.z_levels = 3;
    p.seed = 11;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(!ledger.tribulation_events.empty());

    // 诊断量对着世界数一遍：它报的是「抽了个空圈」的候选数。只断言导出里
    // 有这一行的话，计数写成 0 或写成别的数都没人发现。
    const auto pool = gamev2::history::epoch_tribulation_sites(w);
    std::size_t empty_circles = 0;
    for (const auto& site : pool)
    {
        bool touched = false;
        for (const auto& entry : ledger.veins)
        {
            const auto [covered, total] =
                coverage_of(w, entry.vein, site.cell, site.radius);
            (void)total;
            if (covered > 0u)
            {
                touched = true;
                break;
            }
        }
        if (!touched)
        {
            ++empty_circles;
        }
    }
    CHECK_EQ(diagnostics.tribulation_no_vein, empty_circles);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("gamev2_ledger_toll_seed" + std::to_string(p.seed) + ".txt");
    CHECK(gamev2::history::export_ledger(ledger, diagnostics, path));
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    bool saw_toll_row = false;
    bool saw_toll_section = false;
    bool saw_no_vein = false;
    std::size_t toll_rows = 0;
    while (std::getline(in, line))
    {
        saw_toll_row |= line.starts_with("劫损（天地抽走）");
        saw_toll_section |= line.starts_with("=== 渡劫事件清单 ===");
        saw_no_vein |= line.find("空圈") != std::string::npos;
        if (saw_toll_section && !line.starts_with("=== ")
            && !line.starts_with("脉\t") && !line.empty()
            && line[0] >= '0' && line[0] <= '9')
        {
            ++toll_rows;
        }
    }
    in.close();
    std::filesystem::remove(path);
    CHECK(saw_toll_row);
    CHECK(saw_toll_section);
    CHECK(saw_no_vein);
    CHECK_EQ(toll_rows, ledger.tribulation_events.size());
}

TEST(the_shared_primitives_match_their_golden_values)
{
    // 原语层的 golden：固定输入 → 固定期望值。
    //
    // 这一层抓的是「原语内部算错」：区间端点取错、潮位求和漏一年、
    // 或者 quota_conversion 里的 255 被写成 256，这里立刻红。端到端
    // 那层抓的是「原语组合起来的结果变了」，两层抓的错不一样。
    //
    // 期望值不是凭记忆写的：按潮位公式独立算过一遍（逐年的 level 求
    // 和），再与实现的输出交叉核对，两边一致。
    CHECK_EQ(gamev2::history::tide_integral(8300, 8300), 0u);
    CHECK_EQ(gamev2::history::tide_integral(8300, 8301), 110u);
    CHECK_EQ(gamev2::history::tide_integral(8300, 8310), 1093u);
    CHECK_EQ(gamev2::history::tide_integral(8300, 8914), 39552u);
    CHECK_EQ(gamev2::history::tide_integral(8301, 8914), 39442u);
    CHECK_EQ(gamev2::history::tide_integral(1, 2), 77u);

    // 换算系数：分子分母同时给，期望值是这一步除法的整数部分。
    CHECK_EQ(gamev2::history::quota_conversion(1000, 100, 1000), 167116u);
    CHECK_EQ(gamev2::history::quota_conversion(1147, 3900, 39552), 124u);
    // 退化：潮位积分全零、流速全零、储量全零，一律给 0（不除零）。
    CHECK_EQ(gamev2::history::quota_conversion(1000, 100, 0), 0u);
    CHECK_EQ(gamev2::history::quota_conversion(1000, 0, 1000), 0u);
    CHECK_EQ(gamev2::history::quota_conversion(0, 100, 1000), 0u);

    // 余数累加器：小数余数跨年累加，整数部分才不会被逐年截没。
    //
    // 每年 1000×128×124 的有效流量小于分母，第一年取 0；攒到第二年够
    // 一个整数量级，于是取 1。reset() 把余数清零，取值回到 0。
    gamev2::history::QuotaRemainder remainder;
    CHECK_EQ(remainder.take(1000u, 128u, 124u), 0u);
    CHECK_EQ(remainder.take(1000u, 128u, 124u), 1u);
    CHECK_EQ(remainder.take(1000u, 128u, 124u), 1u);
    remainder.reset();
    CHECK_EQ(remainder.take(1000u, 128u, 124u), 0u);
}

TEST(the_shared_conversion_matches_the_ledger_extraction)
{
    // 台账与派系演化层必须用同一套原语，否则两套推演的数字不可比，
    // 对照表会变成「说不清的差」。这一条用台账自己的推演结果反过来
    // 验共享原语：共享原语算出的逐年开采量，必须与台账记的账一致。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);
    CHECK(!ledger.veins.empty());

    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral =
        gamev2::history::tide_integral(ledger.epoch, ledger.now);
    CHECK(integral > 0u);
    const std::uint64_t conversion =
        gamev2::history::quota_conversion(sum_total, sum_rate, integral);
    CHECK(conversion > 0u);

    // 用共享原语重算每一条非越采脉的累计开采。
    //
    // 越采脉必须跳过：台账最终记下的 extracted 含越采那一笔，而这里
    // 重算的是不含越采的正常开采，两者必然不等——那是台账的另一条
    // 分支，不是共享原语对不对。跳过越采脉，这条测试的绿就只取决于
    // 原语。
    std::size_t checked = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.overmined_amount != 0u)
        {
            continue;
        }
        gamev2::history::QuotaRemainder remainder;
        std::uint64_t extracted = 0;
        for (std::uint32_t y = ledger.epoch; y < ledger.now; ++y)
        {
            const std::uint32_t level =
                gamev2::world::compute_spirit_tide(y).level;
            const std::uint64_t eff =
                remainder.take(entry.surveyed_rate, level, conversion);
            const std::uint32_t quota_left =
                entry.surveyed_total - static_cast<std::uint32_t>(extracted);
            const std::uint32_t take =
                (eff >= quota_left) ? quota_left : static_cast<std::uint32_t>(eff);
            extracted += take;
            if (eff >= quota_left)
            {
                remainder.reset();
            }
        }
        CHECK_EQ(extracted, entry.extracted);
        ++checked;
    }
    CHECK(checked > 0u);
}

TEST(the_ledger_numbers_are_pinned)
{
    // 端到端的 golden：把这个世界（64x64/seed 7/当代灵历 8914）的台账
    // 数字钉住。
    //
    // 这些数字是「搬家前」的值：标定系数、阴脉判定、有效流速三样原语
    // 从台账里搬进公共头之前，跑出来的就是它们。它们是「数值一字不变」
    // 在仓库内唯一的守卫——worldgen 的生成结果若变了，或者原语被改坏
    // 了，这条会红。那是故意的：数值变了就该有人看见，而不是悄悄过去。
    //
    // 期望值不是凭记忆写的：搬家前的台账源码单独编译成一份 dump 程序，
    // 跑同一个世界导出的台账文本与逐脉数值转储，与这里的期望值
    // 逐字节比过。
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    CHECK_EQ(ledger.epoch, 8300u);
    CHECK_EQ(ledger.now, 8914u);
    CHECK_EQ(ledger.veins.size(), 5u);
    CHECK_EQ(ledger.events.size(), 848u);
    CHECK_EQ(ledger.unregistered_extracted, 0u);
    CHECK_EQ(ledger.structural_deviation, 801);
    CHECK_EQ(ledger.overmining_deviation, 90);

    // 标定系数：这个世界的 kNum。两个数一钉，原语层与端到端层就对上了。
    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    CHECK_EQ(sum_total, 1147u);
    CHECK_EQ(sum_rate, 3900u);
    CHECK_EQ(gamev2::history::tide_integral(ledger.epoch, ledger.now), 39552u);
    CHECK_EQ(gamev2::history::quota_conversion(sum_total, sum_rate, 39552u),
             124u);

    // 前两条脉的累计开采与当代余额。脉编号与世界一一对应。
    CHECK_EQ(ledger.veins[0].vein, 0u);
    CHECK_EQ(ledger.veins[0].extracted, 322u);
    CHECK_EQ(ledger.veins[0].balance, 129u);
    CHECK_EQ(ledger.veins[1].vein, 1u);
    CHECK_EQ(ledger.veins[1].extracted, 264u);
    CHECK_EQ(ledger.veins[1].balance, 81u);
}

TEST(the_shared_yin_vein_matches_the_ledger_assignment)
{
    // 阴脉判定必须只有一份实现：台账按「这条脉占的格子里有没有阴地」
    // 判，本层也要按同一条规则判。两边各判一次，寒江剑府拿到的
    // 可能就不是同一条脉。
    //
    // fixture 选 256x256/seed 7：九条主脉里只有一条占着阴地，而它排
    // 在太玄的槽位上，台账要把它对调给寒江。小图幅的主脉不够六条，
    // 寒江连脉都分不到，这条断言就落空了。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 3;
    p.seed = 7;
    const auto generated = generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    LedgerDiagnostics diagnostics;
    const Ledger ledger = gamev2::history::build_ledger(w, diagnostics);

    // 共享原语认出的阴脉主脉。
    std::uint32_t yin_main = 0;
    std::uint32_t yin_mains = 0;
    for (const auto& vein : w.veins)
    {
        if (vein.rank != VeinRank::main)
        {
            continue;
        }
        if (gamev2::history::is_yin_vein(w, vein.id))
        {
            ++yin_mains;
            yin_main = vein.id;
        }
    }
    CHECK_EQ(yin_mains, 1u);

    // 拿到了阴脉，就不该再报「缺阴脉」。
    CHECK_EQ(diagnostics.missing_yin_vein, 0u);

    // 台账分给寒江剑府的那条脉，必须就是共享原语认出的那条阴脉。
    std::uint32_t hanjiang_vein = 0;
    std::uint32_t hanjiang_count = 0;
    for (const auto& entry : ledger.veins)
    {
        if (entry.holder == Sect::hanjiang)
        {
            ++hanjiang_count;
            hanjiang_vein = entry.vein;
        }
    }
    CHECK_EQ(hanjiang_count, 1u);
    CHECK_EQ(hanjiang_vein, yin_main);
}

}  // namespace

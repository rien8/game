#include "gamev2/faction/era.hpp"
#include "gamev2/history/quota.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

// 后三个头是后续任务要用的：quota.hpp 出共享原语，generate.hpp 出世界
// 生成入口，spirit_tables.hpp 出潮位函数。era.hpp 只带出 world.hpp，
// 不带这三样。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::faction::apportion_deficit;
using gamev2::faction::deficit_share_of;
using gamev2::faction::FactionBalance;
using gamev2::faction::FactionIdentity;
using gamev2::faction::identity;
using gamev2::faction::Role;
using gamev2::faction::Stance;
using gamev2::history::Sect;

TEST(the_identity_table_covers_every_sect)
{
    // 七宗 + 无主，一个都不能少。少一个就意味着有派系在推演里无处安放，
    // 而它会在推演中途才崩。
    for (std::size_t i = 0; i < static_cast<std::size_t>(Sect::count); ++i)
    {
        const auto sect = static_cast<Sect>(i);
        const auto& id = identity(sect);
        CHECK(id.sect == sect);
    }
}

TEST(the_identity_table_follows_the_setting)
{
    // 这张表是设定给定的常量：太玄执掌盟约、天机无地、九幽是魔修。
    // 改它等于改设定，必须先改设定文档。
    //
    // 八行逐行钉死，role / stance / may_propose 一项都不能漏：把 daxia
    // 误写成魔修这类错误会静默通过测试，一路错到经济推演里。
    CHECK(identity(Sect::taixuan).role == Role::sect);
    CHECK(identity(Sect::taixuan).stance == Stance::contesting);
    CHECK(identity(Sect::taixuan).may_propose);

    CHECK(identity(Sect::daxia).role == Role::sect);
    CHECK(identity(Sect::daxia).stance == Stance::contesting);
    CHECK(!identity(Sect::daxia).may_propose);

    CHECK(identity(Sect::hanjiang).role == Role::sect);
    CHECK(identity(Sect::hanjiang).stance == Stance::contesting);
    CHECK(!identity(Sect::hanjiang).may_propose);

    CHECK(identity(Sect::wanshou).role == Role::sect);
    CHECK(identity(Sect::wanshou).stance == Stance::isolationist);
    CHECK(!identity(Sect::wanshou).may_propose);

    CHECK(identity(Sect::qiangnang).role == Role::sect);
    CHECK(identity(Sect::qiangnang).stance == Stance::neutral);
    CHECK(!identity(Sect::qiangnang).may_propose);

    CHECK(identity(Sect::tianji).role == Role::landless);
    CHECK(identity(Sect::tianji).stance == Stance::neutral);
    CHECK(!identity(Sect::tianji).may_propose);

    CHECK(identity(Sect::jiuyou).role == Role::demonic);
    CHECK(identity(Sect::jiuyou).stance == Stance::contesting);
    CHECK(!identity(Sect::jiuyou).may_propose);

    CHECK(identity(Sect::unowned).role == Role::free_cultivator);
    CHECK(identity(Sect::unowned).stance == Stance::neutral);
    CHECK(!identity(Sect::unowned).may_propose);
}

TEST(a_default_identity_matches_the_unowned_row)
{
    // 默认构造出来的身份不能与设定矛盾：无主就是散修、中立、无提案权。
    const FactionIdentity id;
    CHECK(id.sect == Sect::unowned);
    CHECK(id.role == Role::free_cultivator);
    CHECK(id.stance == Stance::neutral);
    CHECK(!id.may_propose);
}

TEST(the_faction_balance_defaults_to_a_bounded_demand)
{
    // 无上限用度必须由 unbounded_demand 明说，不能靠 demand 取一个大数
    // 蒙混：减法里会溢出。
    FactionBalance balance;
    CHECK(!balance.unbounded_demand);
    CHECK_EQ(balance.demand, 0u);
    CHECK_EQ(balance.vote_weight, 0u);
}

TEST(the_report_starts_from_the_8300_snapshot)
{
    // 建实体这一步决定了后续一切：脉的条数必须与台账一致，
    // 初始余额必须等于 8300 的总量配额，初始归属必须等于台账的
    // 持有格局。这三条错了，后面的推演算的是另一个世界的历史。
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
    CHECK(!ledger.veins.empty());

    gamev2::faction::FactionDiagnostics fdiag;
    const auto report = gamev2::faction::simulate(w, ledger, fdiag);
    CHECK_EQ(report.veins.size(), ledger.veins.size());
    CHECK_EQ(report.factions.size(),
             static_cast<std::size_t>(gamev2::history::Sect::count));
    for (std::size_t i = 0; i < report.veins.size(); ++i)
    {
        CHECK_EQ(report.veins[i].vein, ledger.veins[i].vein);
        CHECK_EQ(report.veins[i].quota_total, ledger.veins[i].surveyed_total);
        CHECK(report.veins[i].initial_owner == ledger.veins[i].holder);
    }
}

TEST(a_world_before_the_epoch_yields_an_empty_era)
{
    // 当代早于纪元：没有「从 8300 推到当代」这件事，硬推出来的格局
    // 没有意义（与台账同规）。
    gamev2::world::WorldParams p;
    p.width = 64;
    p.height = 64;
    p.z_levels = 3;
    p.seed = 7;
    p.spirit_year = 8000;
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
    CHECK(report.veins.empty());
    CHECK_EQ(fdiag.no_baseline, 1u);
}

TEST(a_plain_vein_does_not_decay)
{
    // 只有阴脉采出即衰。其余脉的衰减率恒为一。
    gamev2::faction::VeinProfile profile;
    profile.yin = false;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    gamev2::faction::VeinStock stock;
    stock.extracted = 500u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 65536u);
}

TEST(a_yin_vein_decays_with_the_extraction)
{
    // 阴脉的产出随累计开采线性衰减：采了一半，产出减半。
    // 这条是寒江剑府的结构性赤字的唯一来源：它没有专属参数，
    // 赤字是从地脉的性质里长出来的。
    gamev2::faction::VeinProfile profile;
    profile.yin = true;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    gamev2::faction::VeinStock stock;
    stock.extracted = 250u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 49152u);
    stock.extracted = 1000u;
    CHECK_EQ(gamev2::faction::decay_of(profile, right, stock), 0u);
}

TEST(the_effective_rate_equals_the_quota_rate_at_the_peak_year)
{
    // 8300 年是第九轮灵潮的峰值年，脉律的基数就编在这一年。
    // 把标定系数取成「使峰值年折出等于流速配额」的那个值，
    // 有效流速就应当等于流速配额。
    gamev2::faction::VeinProfile profile;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000u;
    right.quota_rate = 40u;
    gamev2::faction::VeinStock stock;
    stock.stock = 1000u;

    const std::uint32_t level_8300 =
        gamev2::world::compute_spirit_tide(8300).level;
    CHECK_EQ(level_8300, 110u);
    // 整除截掉 den/110 的小数部分会让峰值年差一格（39 而不是 40）：
    // 标定系数要取「使 110 × conversion ≥ kQuotaFixedDen」的最小整数，
    // 才真是注释里说的那个值。
    const std::uint64_t conversion =
        (gamev2::history::kQuotaFixedDen + 109u) / 110u;
    gamev2::faction::VeinRemainder remainder;
    const std::uint64_t eff = gamev2::faction::take_effective(
        profile, right, stock, level_8300, conversion, remainder);
    CHECK_EQ(eff, 40u);
}

TEST(the_deficit_is_the_gap_between_demand_and_supply)
{
    CHECK_EQ(gamev2::faction::deficit_of(100u, 30u), 70u);
    CHECK_EQ(gamev2::faction::deficit_of(30u, 100u), 0u);
}

TEST(the_demand_and_the_vote_weight_come_from_the_ledger)
{
    // 三个决定推演骨架的量，一个都不能错，而它们此前一条测试都没触达：
    //
    // - **用度**：缺口是全层唯一的驱动力（设计 §3.5）。用度错了，缺口
    //   就错，越采、重勘、放弃全跟着错。
    // - **表决权重**：重勘能不能过，看的就是它（设计 §3.11.4）。
    // - **无主的权重恒为 0**：它不在盟约里，没有席位。这一行决定了
    //   这个世界里所有重勘的成败——若无主计票，提案会被否。
    //
    // 用度按设计 §3.2/§3.3 的公式**独立**算一遍：这里走的是浮点的
    // 连续公式（F = 总量 / (流速 × 潮位积分)，eff = 流速 × 潮位 × F），
    // 层里走的是 2^16 定点。两条路只在「恰好落在 .5 上」时才可能差
    // 一格，本世界上没有发生这种情况（实测差 0）。
    //
    // 纪元年的衰减率是满值（累计开采为 0，阴脉也一样），所以公式里
    // 不带衰减因子——这正是设计 §3.3 把用度锚在 8300 的用意。
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
    CHECK(sum_rate > 0u);
    const double conversion = static_cast<double>(sum_total)
                              / (static_cast<double>(sum_rate)
                                 * static_cast<double>(integral));
    const double level = static_cast<double>(
        gamev2::world::compute_spirit_tide(ledger.epoch).level);

    std::array<double, static_cast<std::size_t>(Sect::count)> demand_real{};
    for (const auto& entry : ledger.veins)
    {
        const double eff = static_cast<double>(entry.surveyed_rate) * level
                           * conversion;
        demand_real[static_cast<std::size_t>(entry.holder)] +=
            std::min(eff, static_cast<double>(entry.surveyed_total));
    }

    for (const auto& faction : report.factions)
    {
        const auto index = static_cast<std::size_t>(faction.sect);
        if (faction.unbounded_demand)
        {
            // 魔修与散修的用度无上限，没有可比较的数（设计 §3.3）。
            CHECK_EQ(faction.demand, 0u);
            continue;
        }
        // 用度：按 §3.3 的公式独立算出的值，四舍五入成整数。
        CHECK_EQ(faction.demand,
                 static_cast<std::uint32_t>(std::lround(demand_real[index])));
    }

    // 表决权重：七宗之和 = 台账的七宗配额之和（去掉无主）。
    std::uint32_t ledger_quota = 0;
    for (const auto& sect : ledger.sects)
    {
        if (sect.sect == Sect::unowned)
        {
            continue;
        }
        ledger_quota += sect.quota_total;
    }
    std::uint32_t faction_weight = 0;
    for (const auto& faction : report.factions)
    {
        if (faction.sect == Sect::unowned)
        {
            // 无主不在盟约里，没有席位：权重恒为 0。
            CHECK_EQ(faction.vote_weight, 0u);
            continue;
        }
        faction_weight += faction.vote_weight;
    }
    CHECK(faction_weight > 0u);
    CHECK_EQ(faction_weight, ledger_quota);
}

TEST(the_fixed_rate_is_the_whole_rate_at_twice_the_scale)
{
    // eff_fixed 是有效流速的 2^16 定点值：它的整数部分正是 take_effective
    // 的结果（余数结转之前），小数部分是这一层原来逐脉丢掉的那部分。
    //
    // 这条把两个原语钉在一起。定点化若把分母写错一层（少乘或多乘一个
    // 2^16），整数部分就会差 65536 倍——而用度是缺口、越采、表决的源
    // 头，源头的单位错了，后面每一处数字都错，且错得很有说服力。
    gamev2::faction::VeinProfile profile;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000000u;
    right.quota_rate = 40u;
    gamev2::faction::VeinStock stock;
    stock.stock = 1000000u;

    const std::uint32_t level = 100u;
    const std::uint64_t conversion = 12345u;
    gamev2::faction::VeinRemainder remainder;
    const std::uint64_t whole = gamev2::faction::take_effective(
        profile, right, stock, level, conversion, remainder);
    const std::uint64_t fixed = gamev2::faction::eff_fixed(
        profile, right, stock, level, conversion);
    CHECK_EQ(fixed / 65536u, whole);
    // 小数部分不为零：这一层原来就是把它逐脉扔掉的。
    CHECK(fixed % 65536u != 0u);
}

TEST(the_simulation_covers_every_year)
{
    // 推演必须逐年跑完 [8300, 当代)。
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
    CHECK_EQ(report.epoch, 8300u);
    CHECK_EQ(report.now, ledger.now);
    CHECK_EQ(report.years_simulated, ledger.now - ledger.epoch);
    CHECK_EQ(report.current_year, ledger.now - 1u);
    CHECK_EQ(report.current_level,
             gamev2::world::compute_spirit_tide(ledger.now - 1u).level);
}

TEST(the_extraction_never_breaks_the_conservation)
{
    // 守恒恒等式：quota_total == stock + extracted。
    // 越采也是从 stock 里扣的，所以它不会让这条恒等式崩；
    // 但如果越采的上限误写成「剩余**合法**配额」，累计开采会超过
    // 总量配额、余额变负，这条恒等式就崩了——所以另有一条专测越采的。
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
    CHECK_EQ(fdiag.conservation_broken, 0u);
    // 公有化残留也要是 0：抽干的脉不再挂在原持有者名下。
    //
    // 这条守的是 collect 的对账本身没算错——公有化由 ResolveSystem 执行
    // （Task 7 起就有），残留不为 0 就说明「余额为零却还挂着主人」这条
    // 一致性判据破了。
    CHECK_EQ(fdiag.public_release_mismatch, 0u);
    std::uint64_t quota = 0;
    std::uint64_t extracted = 0;
    std::uint64_t balance = 0;
    for (const auto& vein : report.veins)
    {
        quota += vein.quota_total;
        extracted += vein.extracted;
        balance += vein.balance;
    }
    CHECK_EQ(extracted + balance, quota);
    CHECK(extracted > 0u);
}

TEST(the_overmining_actually_happens)
{
    // 越采路径必须有一条自动化断言。
    //
    // 上面那条守恒测试抓不住「越采的上限误写成剩余配额」这个错：越采
    // 永不发生时守恒照样成立，测试照样绿。而简报自己许诺过「另有一条
    // 专测越采的」——整个计划里却没有。越采一旦回归，全绿的测试
    // 会给出虚假的安心。
    //
    // 用**默认图幅**：64x64 上年流速截断为 0、越采根本不触发（小图幅
    // 退化），只有默认图幅才演得出越采。信号是公共入口能读到的：
    // 某条脉的 extracted > legal_extracted。
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
    std::uint32_t overmined = 0u;
    for (const auto& vein : report.veins)
    {
        if (vein.extracted > vein.legal_extracted)
        {
            ++overmined;
        }
    }
    CHECK(overmined > 0u);
}

TEST(the_faction_accounts_add_up_the_extraction)
{
    // 派系级的累计开采必须与脉级对得上：每一份开采都记在某个派系
    // 名下——合法开采由 ExtractSystem 记，越采由 OvertakeSystem 记。
    // 两处都是 `+=`，所以派系之和应当逐位等于脉级之和；派系级恒为 0
    // 就说明有一处漏了写入者。
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

    std::uint64_t vein_sum = 0;
    for (const auto& vein : report.veins)
    {
        vein_sum += vein.extracted;
    }
    std::uint64_t faction_sum = 0;
    for (const auto& faction : report.factions)
    {
        faction_sum += faction.extracted;
    }
    CHECK(faction_sum > 0u);
    CHECK_EQ(faction_sum, vein_sum);
}

TEST(the_effective_rate_carries_the_fractional_remainder)
{
    // 余数真的在跨年结转：整数结果仍对时，只有余数能暴露「取模分母
    // 写错一层」这类错——整数与余数各自还原分子，两个错互相掩盖时
    // 这里就会对不上。
    gamev2::faction::VeinProfile profile;
    gamev2::faction::VeinRight right;
    right.quota_total = 1000000u;
    right.quota_rate = 40u;
    gamev2::faction::VeinStock stock;
    stock.stock = 1000000u;

    const std::uint32_t level = 100u;
    const std::uint64_t conversion = 12345u;
    const std::uint64_t den = gamev2::history::kQuotaFixedDen
                              * gamev2::history::kQuotaFixedScale;
    // 非阴脉的衰减率恒为一，分子就是这条公开的乘积。
    const std::uint64_t num = static_cast<std::uint64_t>(right.quota_rate)
                              * level * conversion
                              * gamev2::history::kQuotaFixedScale;

    gamev2::faction::VeinRemainder remainder;
    const std::uint64_t first = gamev2::faction::take_effective(
        profile, right, stock, level, conversion, remainder);
    CHECK(remainder.value != 0u);
    CHECK_EQ(first * den + remainder.value, num);

    // 第二次调用复用同一个余数器：小数部分真的被结转进了下一年。
    const std::uint64_t carried = remainder.value;
    const std::uint64_t second = gamev2::faction::take_effective(
        profile, right, stock, level, conversion, remainder);
    CHECK_EQ(second * den + remainder.value, num + carried);
}

TEST(a_depleted_vein_is_marked)
{
    // 枯竭的脉要被认出来：余额归零的那一年记进 depleted_year。
    //
    // **公有化的断言不在这里**：公有化要改 VeinOwner，而 VeinOwner 的
    // 唯一写入者是 ResolveSystem。那条断言在 tests/test_faction_era.cpp
    // 的 `a_depleted_vein_is_publicized` 里——它守的是「抽干的脉不再挂
    // 在原主人名下」，与本条守的「枯竭年被记下来」是两件事。
    //
    // **用默认图幅**：默认图幅上六百年里被抽干的脉最多，断言最有力。
    //
    // 两套口径的数字别混。**台账**（seed 7）：64x64 是 5 条脉有 3 条
    // 余额归零，256x256 是 50 条脉有 38 条余额归零。**派系演化层**
    // （seed 7、默认图幅 256x256）：50 条脉有 37 条余额归零、37 条枯竭。
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
        // 枯竭之后余额就是零。
        CHECK_EQ(vein.balance, 0u);
        // 枯竭年必须落在被推演的年份里。
        CHECK(vein.depleted_year >= report.epoch);
        CHECK(vein.depleted_year < report.now);
    }
    // 至少有一条脉该在这六百年里枯竭：潮位从 110 掉到 33。
    // 台账的口径（64x64 / seed 7）是「5 条脉有 3 条余额归零」；派系演化层
    // 在默认图幅上是 45 条（见上面的注释）。
    CHECK(depleted > 0u);
}

TEST(a_cave_heaven_is_lost_with_its_vein)
{
    // 这条守的是「默认图幅上不许凭空冒出洞天失效事件」。
    //
    // 当代是谷底期（灵历 8914，潮位 33），没有福地就没有洞天——洞天数
    // 为 0 这件事已被 tests/test_world_heavens.cpp 钉成规格。所以这条
    // 用例在默认图幅上恒绿：它验不到洞天失效本身，只验「不该有的时候
    // 没有」。
    //
    // 洞天失效的真正覆盖在下一条用例里（那条把图取到灵潮期末，那里
    // 才有洞天）。
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
    std::size_t lost = 0;
    for (const auto& event : report.events)
    {
        if (event.kind != gamev2::faction::EraEventKind::cave_heaven_lost)
        {
            continue;
        }
        ++lost;
        // 洞天失效必须挂在那条真的枯了的脉上：事件的脉编号要能在报告里
        // 找到，且那条脉的枯竭年不为零。
        bool found = false;
        for (const auto& vein : report.veins)
        {
            if (vein.vein == event.vein)
            {
                found = true;
                CHECK(vein.depleted_year != 0u);
            }
        }
        CHECK(found);
    }
    // 洞天失效数不超过洞天总数——上面已逐条验过脉的对应关系，这里再钉
    // 一次数量上界，防止同一个洞天被反复记失效。上界取洞天数而不是
    // 脉数：多个福地格可以落在同一条脉上，脉数不是洞天数的上界。
    CHECK(lost <= w.cave_heavens.size());
}

TEST(a_cave_heaven_is_lost_when_its_vein_runs_out)
{
    // 上一条洞天用例在默认图幅上是空跑的：当代是谷底期（潮位 33），
    // 福地格数为 0，洞天数也是 0，于是「洞天失效」无从谈起——它永远
    // 绿，也永远没验过任何东西。
    //
    // 这条把图取到灵潮期末（灵历 9300）：那里有福地、有洞天，也有
    // 抽得干的脉，「脉枯 → 洞天失效」这条链才真的被走一遍。图幅仍是
    // 默认的 256x256——挪的是年份，不是尺寸。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 11;
    p.spirit_year = 9300;
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

    std::size_t lost = 0;
    for (const auto& event : report.events)
    {
        if (event.kind != gamev2::faction::EraEventKind::cave_heaven_lost)
        {
            continue;
        }
        ++lost;
        // 失效年必须落在被推演的年份里。
        CHECK(event.year >= report.epoch);
        CHECK(event.year < report.now);
        // 必须挂在那条真的枯了的脉上，而且就在那一年失效：脉枯与洞天
        // 失效都发生在 DepleteSystem 的同一次执行里，年份必然相同。
        bool found = false;
        for (const auto& vein : report.veins)
        {
            if (vein.vein == event.vein)
            {
                found = true;
                CHECK(vein.depleted_year != 0u);
                CHECK_EQ(event.year, vein.depleted_year);
            }
        }
        CHECK(found);
    }
    // 这一条是本用例的全部意义所在：洞天确实失效过。
    CHECK(lost > 0u);
    // 一座洞天至多记一次失效，所以失效数不超过洞天总数。
    CHECK(lost <= w.cave_heavens.size());
}

TEST(the_depletion_years_pin_the_system_order)
{
    // 把 system 的**执行顺序**钉进断言。
    //
    // 枯竭年取决于 DepleteSystem 排在开采与越采之后还是之前：排在之前，
    // 被越采抽干的脉要等到下一年才被认出来。实测把 DepleteSystem 的
    // `order` 从 6 改成 4（让它排在 OvertakeSystem 之前），默认图幅上
    // 枯竭年的和从 330495 变成别的值——而**没有任何其它测试会红**。
    //
    // 这条测试就是那个红灯。它守的是执行顺序，不是任何经济数值。
    //
    // 顺序原先靠注册顺序兑现，现在靠每个 system 写死的 `order`（设计
    // §3.13）。变异的手法因此也跟着变：早先是调换 `scheduler.add()` 的
    // 先后，现在是改 `order` 常量。上面那句「实测」就是按新手法重测的
    // 结果——改动 `order` 仍然只有这一条测试变红。
    //
    // **指纹随本波次（最终整枝审阅后的统一修复波次）重算过**，后来又随
    // 劫损接进派系层重算了一次。当前值是枯竭 39 条、枯竭年之和 330495。
    // 新值与改动逐条对应如下——每一条都是实测的对照，不是「数值变了就
    // 接受」：
    //
    // 1. **用度的定点化**（设计 §3.3：逐脉算出实数、按 2^16 定点累加、
    //    最后取整；旧口径是逐脉取整再相加，把每宗两到四条脉的小数全扔
    //    掉）。旧模型里各宗的用度是：太玄 5、丹霞 2、寒江 1、万兽 1、
    //    青囊 1；新模型是：太玄 8、丹霞 3、寒江 1、万兽 2、青囊 1。
    //    **用度变大 → 缺口变大 → 越采的分摊与重勘清单的试算都变了**。
    //    单独量这一项（用度定点、供给仍旧）：枯竭 38 → 37 条，枯竭年之和
    //    318998 → 312886，而且**逐脉的枯竭年清单与最终态完全一致**。
    // 2. **供给的定点化**（设计 §3.4：供给与用度同源）。旧模型里谷底期
    //    的供给逐脉截断成 0，于是所有宗的缺口都等于用度；新模型里太玄
    //    当代供给是 5（定点求和后取整），缺口因此从 8 落回 3。
    //    **这一项没有动这个指纹**：只用供给定点、用度仍旧时枯竭是
    //    39 条、枯竭年之和 327576（脉 11 从 8565 挪到 8497，脉 13 新增
    //    枯竭于 8646，其余不动）。两项都定点时逐脉清单回到「只用用度
    //    定点」的那一版：仍是 37 / 312886。
    // 3. **未登记口径**（派系演化层的「未登记」改成未登记脉上的全部
    //    开采，与台账同名同义）：**它不影响任何行为**。那一列是只写
    //    不读的——写进 `FactionBalance::unregistered`，只被汇总读出来报
    //    数，不参与任何判据。所以它改的是报数，不是格局：九幽宫的「未
    //    登记」从 372（只算越采）变成 186（它那条不登记的脉在新格局里
    //    更早转手，它自己总共只开采了 186），太玄宗接手后记了 392。
    // 4. **确定性的图幅**：那条断言一直在默认图幅上，没动。
    //
    // 5. **劫损接进派系层**（这一波）：台账记的劫损原本没被本层认账，本层
    //    的余额比台账多出劫损那一份（缺陷：太玄宗的龙脉在台账里 8859 年
    //    被抽干，本层却报「还剩 387、还没枯」）。接进来之后，被劫的脉在
    //    劫年当场枯竭：枯竭 37 → **39** 条，枯竭年之和 312886 → **330495**。
    //    逐脉看：多出来的那两条是被劫当年就抽干的脉；其余被劫的脉，枯竭年
    //    从「采到空」改成「被抽干」——后者通常更早，所以总和上升。
    //    指纹的**鉴别力**也重测过：把 DepleteSystem 的 `order` 从 6 改成 4
    //    （让它排在 OvertakeSystem 之前），枯竭年之和不再是 330495，
    //    **没有任何其它测试会红**——这条红灯还在。
    //
    // 前两项链在一起把格局推到了另一个样子：重勘通过 7 → 3 次、空转
    // 3 → 6 次、转手 19 → 32 次、全脉累计开采 15529 → 14885、当代余额
    // 2887 → 3531。当代持有也跟着换了一批（太玄 4 → 12 条、丹霞 7 → 0
    // 条、无主 38 → 37 条）。
    //
    // 逐脉看枯竭年的移动：脉 11（旧 8565）不再枯竭，其余变动的脉一律
    // **推迟**（脉 10 从 8433 到 8645、脉 21 从 8449 到 8737、脉 26 从
    // 8379 到 8441……）。少掉的那条枯竭得晚，推迟的那些加起来只多
    // 2453，所以总和从 318998 降到 312886 —— 指纹变小不等于「抽得更
    // 快」，它是「少枯了一条」与「其余推迟」两件事相抵的结果。
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
    std::uint64_t year_sum = 0;
    for (const auto& vein : report.veins)
    {
        if (vein.depleted_year == 0u)
        {
            continue;
        }
        ++depleted;
        year_sum += vein.depleted_year;
    }
    CHECK_EQ(depleted, 39u);
    CHECK_EQ(year_sum, 330495u);
}

TEST(the_deficit_is_apportioned_in_whole_units)
{
    // 分摊的结果是整数单位，各脉之和恰好等于缺口。
    //
    // 逐脉截断的老做法在这个例子里把每一脉都截成 0：缺口 3 分给 12 条
    // 脉，每脉 0.25，截断之后一份都不剩——缺口凭空消失，正派于是永不
    // 越采，「越采被发现」那条事件线也永不触发。
    const std::vector<std::uint64_t> weights(12, 1u);
    const auto units = apportion_deficit(3u, weights);
    CHECK_EQ(units.size(), 12u);
    std::uint32_t sum = 0u;
    for (const std::uint32_t u : units)
    {
        sum += u;
        // 各脉至多多拿一个单位：它的比例份额是 0.25。
        CHECK(u <= 1u);
    }
    CHECK_EQ(sum, 3u);
}

TEST(the_apportioned_shares_follow_the_weights)
{
    // 恰好整除时，各脉的份额就是它的比例份额。
    const std::vector<std::uint64_t> weights{1u, 1u, 1u};
    const auto units = apportion_deficit(12u, weights);
    CHECK_EQ(units[0], 4u);
    CHECK_EQ(units[1], 4u);
    CHECK_EQ(units[2], 4u);
}

TEST(a_vein_without_flow_gets_no_share)
{
    // 权重为零的脉分不到份额：它采不出东西（余额为零的脉不进分摊）。
    const std::vector<std::uint64_t> weights{0u, 4u};
    const auto units = apportion_deficit(4u, weights);
    CHECK_EQ(units[0], 0u);
    CHECK_EQ(units[1], 4u);
}

TEST(the_proportional_share_rounds_to_the_nearest_unit)
{
    // 试算用的比例份额：四舍五入，不是截断。
    // 0.25 → 0，0.75 → 1，4 → 4。
    CHECK_EQ(deficit_share_of(3u, 1u, 12u), 0u);
    CHECK_EQ(deficit_share_of(3u, 3u, 12u), 1u);
    CHECK_EQ(deficit_share_of(12u, 1u, 3u), 4u);
}

TEST(the_sects_overmine_their_deficit)
{
    // 缺口必须真的变成越采（设计 §3.7）：缺口是这一层唯一的驱动力，
    // 缺口落不到越采上，「铤而走险」就只是报告里的一个数。
    //
    // 判据：**有缺口的正派，累计越采至少盖过提案次数**——重勘年是要开
    // 程序的年份，程序走完，缺口就该被采出来，累积下来每年至少一个
    // 单位。取整的残渣当不了这个数：逐脉截断分摊时，缺口小于脉数就整
    // 段消失，累计越采是个位数。
    //
    // 种子 21：那个世界上随机源（越采被发现）真的被走过（见
    // test_faction_determinism.cpp 的 full_world）。
    gamev2::world::WorldParams p;
    p.z_levels = 3;
    p.seed = 21;
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

    std::uint32_t proposals = 0;
    for (const auto& event : report.events)
    {
        if (event.kind == gamev2::faction::EraEventKind::redraft_proposed)
        {
            ++proposals;
        }
    }
    CHECK(proposals > 0u);
    std::uint32_t best = 0;
    for (const auto& faction : report.factions)
    {
        if (faction.unbounded_demand || faction.demand == 0u)
        {
            // 无上限用度（魔修与散修）本来就在采尽为止，看它们的越采说明
            // 不了任何事。
            continue;
        }
        best = std::max(best, faction.overmined);
    }
    CHECK(best >= proposals);
}

}  // namespace

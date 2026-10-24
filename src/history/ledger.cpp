/// @file ledger.cpp
/// @brief 时序层的推演实现。
#include "gamev2/history/ledger.hpp"
#include "gamev2/history/quota.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

namespace gamev2::history {
namespace {

/// 越采削减比例取共享的那一份：worldgen 改了这个数，台账的反推区间
/// 必须跟着变，否则两侧会静默错位。
using gamev2::world::kOverminePenalty;

/// 《脉律》核定的纪元：灵历 8300 年。脉律只有这一版是既定的。
///
/// 不放进公共头文件：它是本模块的内部纪元常量，调用方只该从
/// Ledger::epoch 读它。
constexpr std::uint32_t kQuotaEpoch = 8300;

/// 纪元年的候选池。推演用它当渡劫的候选，取完就丢，不留副本。
std::vector<gamev2::world::TribulationSite> derive_epoch_sites(
    const gamev2::world::World& world)
{
    // 拷贝整个世界：下面四个阶段要写 domain_grade、scars、land_type、
    // tribulation_sites，写在输入世界上就破了只读边界。
    gamev2::world::World derived = world;
    derived.spirit_tide = gamev2::world::compute_spirit_tide(kQuotaEpoch);
    gamev2::world::make_spirit_domains(derived);
    gamev2::world::make_historical_scars(derived);
    gamev2::world::make_land_types(derived);
    gamev2::world::make_tribulation_sites(derived);
    return derived.tribulation_sites;
}

/// 一处渡劫在一条台账脉上的覆盖：圈内节点数与该脉节点总数。
///
/// 先把覆盖算出来放着：它只依赖几何与脉的段范围，与推演的年份无关，
/// 两遍推演都要用同一份，算两遍是白费。
struct Coverage
{
    std::uint32_t site = 0;    ///< 候选池下标
    std::uint32_t vein = 0;    ///< 台账下标（不是 worldgen 脉编号）
    std::uint32_t covered = 0;
    std::uint32_t total = 0;
};

/// 一次待结算的渡劫。amount 在推演里算，因为基数是执行那一刻的余额。
struct TollPlan
{
    std::uint32_t site = 0;    ///< 候选池下标
    std::uint32_t year = 0;
};

/// 逐候选的覆盖。圈内一条台账脉都没有时，候选不产生任何事件，
/// 但要计数——「抽了个空圈」是读者该看得见的退化。
void build_coverages(const gamev2::world::World& world,
                     const Ledger& ledger,
                     const std::vector<gamev2::world::TribulationSite>& pool,
                     std::vector<Coverage>& out,
                     LedgerDiagnostics& diagnostics)
{
    const std::uint32_t width = world.params().width;
    for (std::size_t s = 0; s < pool.size(); ++s)
    {
        const gamev2::world::TribulationSite& site = pool[s];
        const std::int64_t sx = static_cast<std::int64_t>(site.cell % width);
        const std::int64_t sy = static_cast<std::int64_t>(site.cell / width);
        const std::int64_t r2 = static_cast<std::int64_t>(site.radius)
                                * static_cast<std::int64_t>(site.radius);
        std::uint32_t touched = 0;
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            const QuotaEntry& entry = ledger.veins[i];
            const auto& vein = world.veins[entry.vein];
            const auto& nodes = world.ley_channels[vein.channel].nodes;
            std::uint32_t inside = 0;
            for (std::uint32_t j = vein.first_node;
                 j <= vein.last_node; ++j)
            {
                const std::uint32_t cell = world.ley_nodes[nodes[j]].cell;
                const std::int64_t dx =
                    static_cast<std::int64_t>(cell % width) - sx;
                const std::int64_t dy =
                    static_cast<std::int64_t>(cell / width) - sy;
                if (dx * dx + dy * dy <= r2)
                {
                    ++inside;
                }
            }
            if (inside == 0u)
            {
                continue;
            }
            ++touched;
            Coverage c;
            c.site = static_cast<std::uint32_t>(s);
            c.vein = static_cast<std::uint32_t>(i);
            c.covered = inside;
            c.total = vein.last_node - vein.first_node + 1u;
            out.push_back(c);
        }
        if (touched == 0u)
        {
            ++diagnostics.tribulation_no_vein;
        }
    }
}

/// 排期：每处候选至多一次，年份在 [epoch, now) 内均匀抽，成败按
/// kTribulationFailChance 判。成功不入账（净 0），只计失败。
///
/// 随机流独立成一条（Stream::tribulation）：混进 Stream::ledger 的话，
/// 调渡劫参数会连带扰动越采年，把既有 fixture 全部推翻。
///
/// 排序必须拿到候选池：同年两笔的次序按劫心格定，而格号只有池子知道。
std::vector<TollPlan> sample_tolls(
    const std::vector<gamev2::world::TribulationSite>& pool,
    std::uint32_t years, std::uint64_t seed, Ledger& ledger)
{
    const std::uint64_t trib_seed = gamev2::world::stage_seed(
        seed,
        static_cast<std::uint32_t>(gamev2::world::Stream::tribulation));
    std::vector<TollPlan> plans;
    plans.reserve(pool.size());
    for (std::size_t s = 0; s < pool.size(); ++s)
    {
        gamev2::world::Pcg32 rng(
            gamev2::world::stage_seed(trib_seed,
                                      static_cast<std::uint32_t>(s)));
        const std::uint32_t year = kQuotaEpoch + rng.below(years);
        if (rng.unit() >= kTribulationFailChance)
        {
            continue;
        }
        TollPlan plan;
        plan.site = static_cast<std::uint32_t>(s);
        plan.year = year;
        plans.push_back(plan);
        ++ledger.tribulation_failed;
    }
    std::sort(plans.begin(), plans.end(),
              [&pool](const TollPlan& a, const TollPlan& b) {
                  if (a.year != b.year)
                  {
                      return a.year < b.year;
                  }
                  return pool[a.site].cell < pool[b.site].cell;
              });
    return plans;
}

/// 主脉下标按 reserve 降序排。同值按脉编号升序——排序必须全序，否则
/// 同 reserve 的两条脉换个顺序就会换一个持有者。
void sort_main_indices(std::vector<std::size_t>& mains,
                       const std::vector<QuotaEntry>& veins,
                       const gamev2::world::World& world)
{
    std::sort(mains.begin(), mains.end(),
              [&veins, &world](std::size_t a, std::size_t b) {
                  const auto& va = world.veins[veins[a].vein];
                  const auto& vb = world.veins[veins[b].vein];
                  if (va.reserve != vb.reserve)
                  {
                      return va.reserve > vb.reserve;
                  }
                  return veins[a].vein < veins[b].vein;
              });
}

/// 宗级汇总。
///
/// 调用前先清零：推演会改写 balance 与 extracted，汇总必须能重跑而
/// 不把两次的结果叠起来。
void aggregate_sects(Ledger& ledger)
{
    ledger.sects.assign(static_cast<std::size_t>(Sect::count), SectLedger{});
    for (std::size_t s = 0; s < ledger.sects.size(); ++s)
    {
        ledger.sects[s].sect = static_cast<Sect>(s);
    }
    for (const auto& entry : ledger.veins)
    {
        const auto index = static_cast<std::size_t>(entry.holder);
        SectLedger& sect = ledger.sects[index];
        if (!entry.registered)
        {
            sect.registered = false;
        }
        sect.quota_total += entry.surveyed_total;
        sect.quota_rate += entry.surveyed_rate;
        sect.current_total += entry.balance;
        sect.overmined_amount += entry.overmined_amount;
        sect.tribulation_loss += entry.tribulation_loss;
    }
    for (auto& sect : ledger.sects)
    {
        // deviation_total = quota_total − current_total，此刻等于
        // 「累计开采 + 累计劫损」——劫损也从余额里扣掉了。它恒 ≥ 0
        // 仍然成立：配额封顶让余额不会低于 0。
        sect.deviation_total =
            static_cast<std::int64_t>(sect.quota_total)
            - static_cast<std::int64_t>(sect.current_total);
    }
}

/// 偏差分解：口径差（结构）、越采差（人为）、劫损（天地抽走），以及逐宗
/// 的流速偏差。
///
/// 同样先清零：口径差不随推演变，流速偏差却要按潮位差算，重算时不能
/// 叠在旧值上。
void compute_deviations(Ledger& ledger)
{
    ledger.structural_deviation = 0;
    ledger.overmining_deviation = 0;
    ledger.tribulation_deviation = 0;
    for (auto& sect : ledger.sects)
    {
        sect.deviation_rate = 0;
    }

    const std::uint32_t level_epoch =
        gamev2::world::compute_spirit_tide(ledger.epoch).level;
    const std::uint32_t level_now =
        gamev2::world::compute_spirit_tide(ledger.now).level;
    if (level_epoch == 0u)
    {
        // 潮位基准为 0：口径差与流速差无从折算，劫损不用潮位，仍要算。
        for (const auto& entry : ledger.veins)
        {
            ledger.tribulation_deviation += entry.tribulation_loss;
        }
        return;
    }
    // 潮位差必须先转成有符号再相减：level_now > level_epoch 时，
    // uint32 相减会回绕成一个巨大的正数。
    const std::int64_t level_gap =
        static_cast<std::int64_t>(level_epoch)
        - static_cast<std::int64_t>(level_now);
    for (const auto& entry : ledger.veins)
    {
        ledger.structural_deviation +=
            static_cast<std::int64_t>(entry.surveyed_total)
            * level_gap / static_cast<std::int64_t>(level_epoch);
        ledger.overmining_deviation += entry.overmined_amount;
        ledger.tribulation_deviation += entry.tribulation_loss;

        // 流量侧的口径差：与上面的存量侧同构，作用在流速上。
        SectLedger& sect =
            ledger.sects[static_cast<std::size_t>(entry.holder)];
        sect.deviation_rate +=
            static_cast<std::int64_t>(entry.surveyed_rate)
            * level_gap / static_cast<std::int64_t>(level_epoch);
    }
}

}  // namespace

std::vector<gamev2::world::TribulationSite>
epoch_tribulation_sites(const gamev2::world::World& world)
{
    return derive_epoch_sites(world);
}

Ledger build_ledger(const gamev2::world::World& world)
{
    LedgerDiagnostics ignored;
    return build_ledger(world, ignored);
}

Ledger build_ledger(const gamev2::world::World& world,
                    LedgerDiagnostics& diagnostics)
{
    // 诊断量是「本次推演的结果」，不是累计值：调用方复用同一个结构时
    // 不该看到上一次的计数叠上来。
    diagnostics = LedgerDiagnostics{};

    Ledger ledger;
    ledger.epoch = kQuotaEpoch;
    ledger.now = world.params().spirit_year;

    // 当代早于脉律核定年时台账为空（设计 §5）：没有「从 8300 推到当代」
    // 这件事，硬推出来的账没有意义。
    //
    // 这道闸门必须在入账之前，不能等推演那一步再判——那时脉已经进账了。
    //
    // 副作用：闸门在计数之前返回，所以 now < epoch 时 barren_veins 恒为 0。
    // 这是有意的：台账本身为空，「这个世界的荒脉有多少」不在台账要回答的
    // 问题范围内，诊断量跟着台账一起为空。
    if (ledger.now < ledger.epoch)
    {
        return ledger;
    }

    for (const auto& vein : world.veins)
    {
        // 荒脉无灵眼、不可采：不进台账，只计数。
        if (vein.rank == gamev2::world::VeinRank::barren)
        {
            ++diagnostics.barren_veins;
            continue;
        }
        const auto& eye = world.eyes[vein.eye];

        QuotaEntry entry;
        entry.vein = vein.id;
        entry.rank = vein.rank;
        if (vein.overmined)
        {
            // worldgen 把越采脉的储量按 kOverminePenalty 削减，8300 年的原值
            // 只能反推：reserve = floor(original * (100 - kOverminePenalty) / 100)
            // ⟹ original >= ceil(reserve * 100 / (100 - kOverminePenalty))。
            // 取区间下界做基数：真实配额落在开区间里，用真值扣余额会回绕。
            // reserve 是各节点得分之和（单节点 ≤ 255），乘 100 溢出需 168431 个节点。
            entry.surveyed_total =
                (vein.reserve * 100u + (100u - kOverminePenalty) - 1u)
                / (100u - kOverminePenalty);
            // 开区间的排他上界：候选原值落在 [下界, 上界) 里，真值不含上界。
            entry.surveyed_total_hi =
                ((vein.reserve + 1u) * 100u + (100u - kOverminePenalty) - 1u)
                / (100u - kOverminePenalty);
            entry.overmined_amount = entry.surveyed_total - vein.reserve;
        }
        else
        {
            entry.surveyed_total = vein.reserve;
            entry.surveyed_total_hi = vein.reserve;
        }
        // 枯眼的 flow 被置零，原始吞吐在 ley_nodes 里。
        entry.surveyed_rate =
            eye.flow > 0 ? eye.flow : world.ley_nodes[eye.node].flow;
        entry.balance = entry.surveyed_total;
        ledger.veins.push_back(entry);
    }

    // 持有格局是设定给定的，不是推演出来的（设计 8.1）。
    // 太玄宗拿龙脉 + 前三条主脉，其余按序分派。
    std::vector<std::size_t> mains;
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        if (ledger.veins[i].rank == gamev2::world::VeinRank::main)
        {
            mains.push_back(i);
        }
    }
    sort_main_indices(mains, ledger.veins, world);
    if (mains.size() < 9u)
    {
        diagnostics.short_main_veins =
            static_cast<std::uint32_t>(9u - mains.size());
    }

    for (auto& entry : ledger.veins)
    {
        if (entry.rank == gamev2::world::VeinRank::dragon)
        {
            entry.holder = Sect::taixuan;
        }
        else if (entry.rank == gamev2::world::VeinRank::branch)
        {
            entry.holder = Sect::unowned;
        }
    }
    for (std::size_t i = 0; i < mains.size() && i < 9u; ++i)
    {
        // 九个槽位对上六宗：0-2 太玄、3-4 丹霞、5 寒江、6 万兽、
        // 7 青囊、8 九幽（天机阁无地，不分脉）。循环上限是 i < 9u，
        // 所以下面的 case 覆盖了全部可能取值，不需要 default。
        switch (i)
        {
        case 0: case 1: case 2:
            ledger.veins[mains[i]].holder = Sect::taixuan; break;
        case 3: case 4:
            ledger.veins[mains[i]].holder = Sect::daxia; break;
        case 5:
            ledger.veins[mains[i]].holder = Sect::hanjiang; break;
        case 6:
            ledger.veins[mains[i]].holder = Sect::wanshou; break;
        case 7:
            ledger.veins[mains[i]].holder = Sect::qiangnang; break;
        case 8:
            ledger.veins[mains[i]].holder = Sect::jiuyou; break;
        }
    }

    // 寒江剑府要的是阴脉。第 6 条不是阴地就在主脉里另找一条，
    // 找到则与原持有者对调——对调而不是留空缺：九条主脉必须无遗漏
    // 无重复。
    if (mains.size() >= 6u)
    {
        const std::size_t hanjiang_slot = 5;
        if (!is_yin_vein(world, ledger.veins[mains[hanjiang_slot]].vein))
        {
            std::size_t yin_slot = mains.size();
            for (std::size_t i = 0; i < mains.size(); ++i)
            {
                // 跳过寒江自己的槽位：外层已经证明那条不是阴脉，这里
                // 再挡一次是为了让「找阴脉」这一段自己站得住。
                if (i != hanjiang_slot
                    && is_yin_vein(world, ledger.veins[mains[i]].vein))
                {
                    yin_slot = i;
                    break;
                }
            }
            if (yin_slot < mains.size())
            {
                // 只换 holder，不换整条 QuotaEntry：整条交换会把脉编号
                // 也换掉，台账的脉编号必须与 worldgen 一一对应。
                std::swap(ledger.veins[mains[hanjiang_slot]].holder,
                          ledger.veins[mains[yin_slot]].holder);
            }
            else
            {
                diagnostics.missing_yin_vein = 1u;
            }
        }
    }

    // 九幽宫那条脉不登记在册。放在阴脉对调之后：登记状态跟着最终
    // 持有者走，而不是跟着排序槽位走。
    for (auto& entry : ledger.veins)
    {
        if (entry.holder == Sect::jiuyou)
        {
            entry.registered = false;
        }
    }

    if (ledger.veins.empty())
    {
        // 空世界没有台账可汇总：宗级汇总必须有内容才有意义。
        return ledger;
    }

    // 宗级汇总与偏差分解，先算一遍。
    //
    // now == epoch 时没有任何一年被推演，余额还停在基数上，于是偏差为
    // 0——这是一个「还没开采过」的世界的实账，而不是空壳。推演之后还要
    // 再算一遍：推演改写了 balance 与 extracted。
    aggregate_sects(ledger);
    compute_deviations(ledger);

    if (ledger.now <= ledger.epoch)
    {
        return ledger;
    }

    // 跨度上限。必须在潮位积分之前：积分本身就是一次逐年循环，超大跨度
    // 会先在这里把机器拖死。
    const std::uint32_t years = ledger.now - ledger.epoch;
    if (years > kMaxSpanYears)
    {
        diagnostics.span_too_large = 1u;
        return ledger;
    }

    // 换算系数：流速（累计流量，无量纲归一化得分的来源）与储量
    // （段上归一化得分的积分）不是同一个量纲，必须折算。锚点取世界
    // 总量：若无配额封顶，全世界的存量恰好在当代耗尽；配额封顶把
    // 枯竭摊在六百年里，当代仍有余额。
    std::uint64_t sum_total = 0;
    std::uint64_t sum_rate = 0;
    for (const auto& entry : ledger.veins)
    {
        sum_total += entry.surveyed_total;
        sum_rate += entry.surveyed_rate;
    }
    const std::uint64_t integral = tide_integral(ledger.epoch, ledger.now);
    if (integral == 0 || sum_rate == 0)
    {
        // 系数算不出来：潮位积分全为 0，或全世界的流速都是 0。这一种
        // 退化按设计 §5 由诊断量报出，不静默返回一份「什么都没采」的
        // 台账。
        diagnostics.flow_conversion_collapsed = 1u;
        return ledger;
    }
    // kFlowToStock = sum_total * 255 / (integral * sum_rate)
    //              = kNum / kQuotaFixedScale
    const std::uint64_t kNum =
        quota_conversion(sum_total, sum_rate, integral);
    if (kNum == 0)
    {
        // 换算系数被整除成了 0：流速与储量的比例离谱到这个地步，
        // 算不出任何开采量。这是一种退化，按设计 §5 由诊断量报出，
        // 不静默返回一份「什么都没采」的台账。
        diagnostics.flow_conversion_collapsed = 1u;
        return ledger;
    }

    // 纪元候选池与排期。放在所有闸门之后：now <= epoch、跨度超限、换算
    // 塌缩都不推演，也就没有可结算的渡劫。
    const std::vector<gamev2::world::TribulationSite> pool =
        derive_epoch_sites(world);
    std::vector<Coverage> coverages;
    build_coverages(world, ledger, pool, coverages, diagnostics);
    const std::vector<TollPlan> tolls =
        sample_tolls(pool, years, world.params().seed, ledger);

    // 逐年推演。余额被配额封顶，所以余额与累计开采量都落在 uint32 内。

    // 内部状态：推演过程的可变部分。
    struct VeinState
    {
        std::uint32_t overmine_year = 0;  ///< 越采落在哪一年
        std::uint32_t drain_year = 0;     ///< 余额归零那年
    };

    std::vector<VeinState> states(ledger.veins.size());
    std::vector<std::uint32_t> trajectory(ledger.veins.size() * years, 0u);

    // 推演一遍。with_overmining 为真时才插越采。
    // 轨迹按年记录，供第二遍定越采的可行窗口。
    const auto advance = [&](std::vector<std::uint32_t>* trace,
                             bool with_overmining)
    {
        std::vector<QuotaRemainder> remainder(ledger.veins.size());
        for (std::size_t i = 0; i < ledger.veins.size(); ++i)
        {
            ledger.veins[i].extracted = 0;
            ledger.veins[i].tribulation_loss = 0;
            ledger.veins[i].balance = ledger.veins[i].surveyed_total;
            states[i].drain_year = 0;
        }
        ledger.events.clear();
        ledger.tribulation_events.clear();

        for (std::uint32_t year = ledger.epoch; year < ledger.now; ++year)
        {
            const std::uint32_t level =
                gamev2::world::compute_spirit_tide(year).level;
            for (std::size_t i = 0; i < ledger.veins.size(); ++i)
            {
                QuotaEntry& entry = ledger.veins[i];
                if (entry.extracted >= entry.surveyed_total)
                {
                    continue;
                }
                // 越采在该年的正常开采之前扣除：第一遍算出的
                // 「该年起点的余额」在第二遍里原样成立。
                if (with_overmining && states[i].overmine_year == year)
                {
                    const std::uint32_t over =
                        std::min(entry.balance, entry.overmined_amount);
                    if (over > 0u)
                    {
                        entry.extracted += over;
                        entry.balance = entry.surveyed_total
                                        - entry.extracted
                                        - entry.tribulation_loss;
                        ExtractionEvent event;
                        event.vein = entry.vein;
                        event.year = year;
                        event.amount = over;
                        event.over_quota = true;
                        ledger.events.push_back(event);
                    }
                }
                // ② 劫损：基数是越采之后、正常开采之前的余额。
                // 只动 balance 与 tribulation_loss，不动 extracted——
                // extracted 的语义是「人采走了多少」，天地抽走的不算。
                for (const TollPlan& plan : tolls)
                {
                    if (plan.year != year)
                    {
                        continue;
                    }
                    for (const Coverage& cov : coverages)
                    {
                        if (cov.site != plan.site || cov.vein != i)
                        {
                            continue;
                        }
                        // 中间量升到 uint64：单脉段要超过约 3400 个节点
                        // 才可能把 uint32 乘爆，但这一步不花钱。
                        const std::uint32_t loss =
                            static_cast<std::uint32_t>(
                                static_cast<std::uint64_t>(entry.balance)
                                * cov.covered / cov.total);
                        if (loss == 0u)
                        {
                            continue;
                        }
                        entry.tribulation_loss += loss;
                        entry.balance = entry.surveyed_total
                                        - entry.extracted
                                        - entry.tribulation_loss;
                        TribulationEvent event;
                        event.vein = entry.vein;
                        event.year = plan.year;
                        event.amount = loss;
                        event.site = pool[plan.site].cell;
                        ledger.tribulation_events.push_back(event);
                    }
                }
                const std::uint64_t eff =
                    remainder[i].take(entry.surveyed_rate, level, kNum);

                // 配额余量要减掉劫损：天地抽走的那部分人也采不到了。
                // 不减的话，劫损之后那一年仍按原配额扣满，balance 会减成
                // 负数并回绕成巨大正数（无符号），整本账静默错乱。
                const std::uint32_t quota_left =
                    entry.surveyed_total - entry.extracted
                    - entry.tribulation_loss;
                const std::uint32_t take =
                    (eff >= quota_left)
                        ? quota_left
                        : static_cast<std::uint32_t>(eff);
                if (eff >= quota_left)
                {
                    // 被配额卡住：未消费的小数没有意义，余数清零。
                    remainder[i].reset();
                }
                entry.extracted += take;
                entry.balance = entry.surveyed_total
                                - entry.extracted
                                - entry.tribulation_loss;
                if (take > 0u)
                {
                    ExtractionEvent event;
                    event.vein = entry.vein;
                    event.year = year;
                    event.amount = take;
                    event.over_quota = false;
                    ledger.events.push_back(event);
                }
                // 枯竭年在正常开采之后判定：越采把余额打空的那一年
                // 同样算枯竭年。
                if (entry.balance == 0u && states[i].drain_year == 0u)
                {
                    states[i].drain_year = year;
                }
            }
            // 年末余额入轨迹
            if (trace != nullptr)
            {
                for (std::size_t i = 0; i < ledger.veins.size(); ++i)
                {
                    (*trace)[i * years + (year - ledger.epoch)]
                        = ledger.veins[i].balance;
                }
            }
        }
    };

    // ① 第一遍：只算正常开采，产出余额轨迹。
    advance(&trajectory, false);

    // 越采年采样。窗口 = 「该年起点的余额仍装得下这笔越采」的年份。
    //
    // 脉律是 8300 编的：越采案只能落在核定年之后，所以窗口从 8301 起。
    // 窗口为空时只能落在 8300（余额必装得下），报诊断。
    const std::uint64_t ledger_seed = gamev2::world::stage_seed(
        world.params().seed,
        static_cast<std::uint32_t>(gamev2::world::Stream::ledger));

    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        const QuotaEntry& entry = ledger.veins[i];
        if (entry.overmined_amount == 0u)
        {
            continue;
        }
        // 该年起点的余额 = 上一年年末的余额（t = 0 时是 8300 的快照）。
        std::uint32_t last_ok = 0;
        for (std::uint32_t t = 0; t < years; ++t)
        {
            const std::uint32_t start_balance =
                (t == 0) ? entry.surveyed_total
                          : trajectory[i * years + t - 1u];
            if (start_balance >= entry.overmined_amount)
            {
                last_ok = t;
            }
        }
        if (last_ok == 0)
        {
            // 只有 8300 的快照装得下这笔越采：窗口 [epoch+1, now) 里没有
            // 合法年份，只能回落到核定年 8300 本身，并报诊断。
            states[i].overmine_year = ledger.epoch;
            ++diagnostics.overmine_before_epoch;
            continue;
        }
        gamev2::world::Pcg32 rng(gamev2::world::stage_seed(
            ledger_seed, entry.vein));
        states[i].overmine_year =
            ledger.epoch + 1u + rng.below(last_ok);
    }

    // ③ 第二遍：带越采跑一遍。
    //
    // 不再写轨迹：越采窗口已经定完，第二遍写出来的余额轨迹没有人读。
    advance(nullptr, true);

    // 事件表按 (年份, 劫心) 升序：推演时事件是按「年 → 台账下标」追加的，
    // 同年跨劫心时不是劫心序。设计 §4 把顺序列为确定性要求，这里排一次。
    // 排序不改变同一年的扣减次序——同一脉同年多笔劫损在推演时已按劫心
    // 序施加（tolls 按 (年, 劫心) 排过），跨脉的先后与余额无关。
    // 三级比较 (年, 劫心, 脉)：同年同劫心可以有多笔——一处劫心圈住多条
    // 台账脉时逐脉各记一笔（seed 26 实测）。缺第三级就是「等价元素」，
    // ranges::sort 非 stable，跨标准库版本次序不保证，而设计 §4 把顺序
    // 列为确定性要求、导出文本照这个顺序印。
    std::ranges::sort(ledger.tribulation_events,
                      [](const TribulationEvent& a, const TribulationEvent& b) {
                          if (a.year != b.year)
                          {
                              return a.year < b.year;
                          }
                          if (a.site != b.site)
                          {
                              return a.site < b.site;
                          }
                          return a.vein < b.vein;
                      });

    // 枯竭认领：台账的枯竭年是推演得出（余额归零那年），
    // worldgen 的 eye.depleted 标记作交叉校验。强行抽干会打破
    // 守恒恒等式 3，所以不抽干，只报冲突。
    for (std::size_t i = 0; i < ledger.veins.size(); ++i)
    {
        QuotaEntry& entry = ledger.veins[i];
        const auto& vein = world.veins[entry.vein];
        const bool claimed = world.eyes[vein.eye].depleted;
        const bool drained = entry.balance == 0u;
        if (claimed && drained)
        {
            entry.depleted_year = states[i].drain_year;
        }
        else if (claimed && !drained)
        {
            ++diagnostics.depletion_claim_unbacked;
            entry.depleted_year = ledger.now;
        }
        else if (!claimed && drained)
        {
            ++diagnostics.depletion_unmarked;
            entry.depleted_year = states[i].drain_year;
        }
    }

    // 宗级汇总与偏差分解，推演之后再算一遍：推演改写了 balance 与
    // extracted，余额变了，余量与口径差都得跟着变。
    aggregate_sects(ledger);
    compute_deviations(ledger);

    // 未登记开采不计入任何登记宗的余额，只单列一笔。
    //
    // 放在偏差分解之后、且不受潮位基准的守卫约束：它是开采量的合计，
    // 与潮位口径无关。
    ledger.unregistered_extracted = 0;
    for (const auto& entry : ledger.veins)
    {
        if (!entry.registered)
        {
            ledger.unregistered_extracted += entry.extracted;
        }
    }

    return ledger;
}

namespace {

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

/// 事件所属脉的持有宗门。脉编号在台账里唯一，与 worldgen 一一对应。
Sect holder_of(const Ledger& ledger, std::uint32_t vein)
{
    for (const auto& entry : ledger.veins)
    {
        if (entry.vein == vein)
        {
            return entry.holder;
        }
    }
    return Sect::unowned;
}

/// 枯竭年是不是台账真的抽干的那一年。
///
/// 归零年由推演得出，必然落在 [epoch, now) 里；而 ledger.now 是哨兵，
/// 表示 worldgen 标了枯而账上没枯（余额没归零）。哨兵不是枯竭年：
/// 当代还没过去，脉跑到今天都没干，谈不上哪一年枯的。
bool is_drain_year(std::uint32_t year, std::uint32_t now)
{
    return year != 0u && year != now;
}

/// 争议脉：worldgen 说枯、账上没枯。哨兵年记的是 worldgen 的主张，
/// 当代余额是账上的反证。
bool is_disputed(std::uint32_t year, std::uint32_t now)
{
    return year != 0u && year == now;
}

}  // namespace

const char* sect_name(Sect sect)
{
    switch (sect)
    {
    case Sect::taixuan:   return "太玄宗";
    case Sect::daxia:     return "丹霞谷";
    case Sect::hanjiang:  return "寒江剑府";
    case Sect::wanshou:   return "万兽山";
    case Sect::qiangnang: return "青囊谷";
    case Sect::tianji:    return "天机阁";
    case Sect::jiuyou:    return "九幽宫";
    case Sect::unowned:   return "无主";
    default:              return "?";
    }
}

bool export_ledger(const Ledger& ledger,
                   const LedgerDiagnostics& diagnostics,
                   const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        return false;
    }
    out << "灵脉台账\n";
    // 跨度可能为负（now < epoch）：台账为空时不能拿它去减。
    const std::uint32_t span =
        (ledger.now > ledger.epoch) ? (ledger.now - ledger.epoch) : 0u;
    out << "纪元 " << ledger.epoch << " —— 当代 " << ledger.now
        << "（" << span << " 年）\n";
    out << "脉 " << ledger.veins.size() << " 条，事件 "
        << ledger.events.size() << " 笔，未登记开采 "
        << ledger.unregistered_extracted << "\n";
    out << "\n=== 七宗与无主配额与当代余额 ===\n";
    // 没有「脉数」这一列：SectLedger 不记脉数，台账里查不到这一项。
    //
    // 「配额与余额差」是逐宗的余量，「偏差分解」里的口径差是全账口径
    // 的量，两者不是同一回事、也不能相加，所以这里不用「偏差」二字。
    //
    // 「登记」列标出哪一宗的脉律配额不在册：九幽宫那条不登记，读者不能
    // 只从「未登记开采」那一行反推是哪一宗。
    out << "宗门\t登记\t8300配额\t8300流速\t流速偏差\t当代余额\t"
        << "配额与余额差\t越采\t劫损\n";
    for (const auto& sect : ledger.sects)
    {
        out << sect_name(sect.sect) << '\t'
            << (sect.registered ? "是" : "否") << '\t'
            << sect.quota_total << '\t'
            << sect.quota_rate << '\t'
            << sect.deviation_rate << '\t'
            << sect.current_total << '\t'
            << sect.deviation_total << '\t'
            << sect.overmined_amount << '\t'
            << sect.tribulation_loss << '\n';
    }
    out << "\n=== 偏差分解 ===\n";
    // 口径差是全账口径的量，与逐宗的「配额与余额差」不是同一回事，也
    // 不能相加——括注写在这里，是因为这份文档就是被读、被争的地方。
    out << "口径差（8300 峰值口径 vs 当代谷底口径，全账口径，"
        << "不等于各宗「配额与余额差」之和）\t"
        << ledger.structural_deviation << '\n';
    out << "越采差（人为多采）\t" << ledger.overmining_deviation << '\n';
    out << "劫损（天地抽走）\t" << ledger.tribulation_deviation << '\n';
    out << "\n=== 脉级台账 ===\n";
    out << "脉\t宗门\t品阶\t8300配额\t安全流速\t已采\t越采\t当代余额\t枯竭年\n";
    for (const auto& entry : ledger.veins)
    {
        // 争议脉的哨兵年不是枯竭年，这里印 0：账上的算法是「没枯」，
        // 与同行非零的当代余额自洽。争议本身在「枯竭争议」段里点名。
        const std::uint32_t drain_year =
            is_drain_year(entry.depleted_year, ledger.now)
                ? entry.depleted_year
                : 0u;
        out << entry.vein << '\t'
            << sect_name(entry.holder) << '\t'
            << rank_name(entry.rank) << '\t'
            << entry.surveyed_total << '\t'
            << entry.surveyed_rate << '\t'
            << entry.extracted << '\t'
            << entry.overmined_amount << '\t'
            << entry.balance << '\t'
            << drain_year << '\n';
    }
    out << "\n=== 开采事件 ===\n";
    out << "脉\t年份\t数额\t越采\n";
    for (const auto& event : ledger.events)
    {
        out << event.vein << '\t'
            << event.year << '\t'
            << event.amount << '\t'
            << (event.over_quota ? 1 : 0) << '\n';
    }
    out << "\n=== 越采案清单 ===\n";
    out << "脉\t宗门\t年份\t数额\n";
    for (const auto& event : ledger.events)
    {
        if (!event.over_quota)
        {
            continue;
        }
        out << event.vein << '\t'
            << sect_name(holder_of(ledger, event.vein)) << '\t'
            << event.year << '\t'
            << event.amount << '\n';
    }
    out << "\n=== 枯竭脉清单 ===\n";
    out << "脉\t宗门\t枯竭年\t当代余额\n";
    for (const auto& entry : ledger.veins)
    {
        // 只列台账真的抽干的脉。depleted_year == ledger.now 是哨兵：
        // worldgen 标了枯而账上没枯，把它当枯竭年印出去，读者会以为那条
        // 脉在当代跑干了——而它还有余额。
        if (!is_drain_year(entry.depleted_year, ledger.now))
        {
            continue;
        }
        out << entry.vein << '\t'
            << sect_name(entry.holder) << '\t'
            << entry.depleted_year << '\t'
            << entry.balance << '\n';
    }
    out << "\n=== 枯竭争议 ===\n";
    // 争议脉在枯竭脉清单里被剔除、在脉级台账里枯竭年印 0，两头都看不出
    // 有人跟谁不对付，所以要在这里点名：这份文档就是被读、被争的地方。
    out << "脉\t宗门\t枯竭年\t当代余额\n";
    for (const auto& entry : ledger.veins)
    {
        if (!is_disputed(entry.depleted_year, ledger.now))
        {
            continue;
        }
        out << entry.vein << '\t'
            << sect_name(entry.holder) << '\t'
            << "灵历 " << ledger.now << " 已枯" << '\t'
            << entry.balance << '\n';
    }
    out << "\n=== 渡劫事件清单 ===\n";
    // 与越采案清单同形：越采是「人多采了」，劫损是「天地抽走了」，
    // 两者在政治上要分得开，账上就得分得开。
    out << "脉\t宗门\t年份\t数额\t劫心格\n";
    for (const auto& event : ledger.tribulation_events)
    {
        out << event.vein << '\t'
            << sect_name(holder_of(ledger, event.vein)) << '\t'
            << event.year << '\t'
            << event.amount << '\t'
            << event.site << '\n';
    }
    out << "\n=== 诊断 ===\n";
    // 退化不是错误，但必须看得见：台账是给人读的文档，读者要能在里面
    // 找到每一处对不上的地方。逐项一行，与其他区段同一张表的样式。
    out << "项目\t计数\n";
    out << "枯眼无账\t" << diagnostics.depletion_claim_unbacked << '\n';
    out << "枯竭未标\t" << diagnostics.depletion_unmarked << '\n';
    out << "缺阴脉\t" << diagnostics.missing_yin_vein << '\n';
    out << "主脉不足\t" << diagnostics.short_main_veins << '\n';
    out << "越采早于纪元\t" << diagnostics.overmine_before_epoch << '\n';
    out << "荒脉\t" << diagnostics.barren_veins << '\n';
    out << "换算塌缩\t" << diagnostics.flow_conversion_collapsed << '\n';
    out << "跨度超限\t" << diagnostics.span_too_large << '\n';
    out << "空圈\t" << diagnostics.tribulation_no_vein << '\n';
    return static_cast<bool>(out);
}

}  // namespace gamev2::history

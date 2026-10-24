/// @file resolve.cpp
/// @brief 派系演化层的重勘：公有化、清单生成、表决与归属变更。
#include "systems.hpp"
#include "gamev2/ecs/view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::faction {
namespace {

/// 派系的个数：七宗加无主。
constexpr std::size_t kFactionCount = static_cast<std::size_t>(Sect::count);

/// 一条待转移的脉。
struct Transfer
{
    std::uint32_t vein = 0;
    Sect from = Sect::unowned;
    Sect to = Sect::unowned;
    /// 转移后这条脉当年的开采量（试算值）。
    std::uint32_t take_after = 0;
    /// 转移前这条脉当年的开采量。
    std::uint32_t take_before = 0;
};

/// 年末的脉。
///
/// 试算要读别的脉的数（候选的分摊分母、余力），所以先把整年的账取成
/// 快照：视图里的指针只在一次 each 之内有效。
struct VeinSnapshot
{
    std::uint32_t vein = 0;
    Sect owner = Sect::unowned;
    std::uint32_t quota_total = 0;
    /// 年末余额。
    std::uint32_t stock = 0;
    /// 累计开采。
    std::uint32_t extracted = 0;
    /// 当年开采量（合法 + 越采）。
    std::uint32_t taken = 0;
    /// 当年有效流速。
    std::uint64_t eff = 0;
};

/// 年末的派系。
struct FactionSnapshot
{
    Sect sect = Sect::unowned;
    std::uint32_t deficit = 0;
    std::uint32_t vote_weight = 0;
    bool unbounded_demand = false;
    FactionIntent intent;
    /// 余力：名下脉的年末余额之和（井里还剩多少）。
    std::uint32_t slack = 0;
    /// 名下**还采得出的**脉的当年有效流速之和。余额为零的脉不算：
    /// 它分不到份额，也不该占分摊的分母。
    std::uint64_t sum_eff = 0;
};

/// 试算：候选接手这条脉之后，当年的开采量是多少。
///
/// 试算只涉及两个派系与这一条脉，不重跑全年（设计 §3.11.3）。它比的是
/// 「同一年、同一条脉」的两个数，所以两边都用**年初口径**：这一年的开采
/// 已经按旧归属算过了，归属变更自次年生效。
///
/// 年初口径的三个数都能从年末的账里倒推：余额加回当年的开采量，累计
/// 开采减去当年的开采量。
std::uint32_t trial_take_after(const VeinSnapshot& vein,
                                const FactionSnapshot& candidate)
{
    const std::uint32_t stock_start = vein.stock + vein.taken;
    const std::uint32_t extracted_start = vein.extracted - vein.taken;
    const std::uint32_t quota_left =
        vein.quota_total > extracted_start ? vein.quota_total - extracted_start
                                           : 0u;
    // 合法开采与归属无关：配额是这条脉自己的，谁来采都是这个数。
    const std::uint32_t legal = static_cast<std::uint32_t>(
        std::min({vein.eff, static_cast<std::uint64_t>(quota_left),
                  static_cast<std::uint64_t>(stock_start)}));
    // 越采的上限是余额：抽不穿井底。
    const std::uint64_t room = stock_start - legal;
    std::uint32_t over = 0u;
    if (candidate.unbounded_demand)
    {
        // 无上限用度不经缺口分摊：随时可以，采尽为止。
        over = static_cast<std::uint32_t>(
            std::min(vein.eff * kOvertakeFactor, room));
    }
    else if (candidate.deficit > 0u)
    {
        // C 接手后多了这条脉的合法开采，它的缺口就少了这么多。
        const std::uint32_t deficit =
            candidate.deficit > legal ? candidate.deficit - legal : 0u;
        const std::uint64_t sum_eff = candidate.sum_eff + vein.eff;
        if (sum_eff > 0u)
        {
            // 试算只涉及这一条脉，算不出 C 名下其他脉的下界，给不出最大
            // 余额法的最终份额，只能给比例份额的四舍五入——与真实分摊相差
            // 至多一个单位（设计 §3.11.3：小试算，不是重跑全年）。
            const std::uint32_t share =
                deficit_share_of(deficit, vein.eff, sum_eff);
            over = static_cast<std::uint32_t>(
                std::min({static_cast<std::uint64_t>(share),
                          vein.eff * kOvertakeFactor, room}));
        }
    }
    return legal + over;
}

}  // namespace

bool redraft_this_year(std::uint32_t year, std::uint32_t epoch,
                       const FactionIntent& covenant,
                       std::uint32_t covenant_deficit)
{
    if (year < epoch)
    {
        return false;
    }
    const std::uint32_t since = year - epoch;
    // 纪元那一年不算重勘：8300 年的格局就是《脉律》本身，下一次重勘
    // 在二百年后（设定 §6.5：8300 + 200 = 8500，再 +200 = 8700、8900）。
    if (since > 0u && since % kRedraftPeriod == 0u)
    {
        return true;
    }
    if (!covenant.may_propose || covenant_deficit == 0u)
    {
        // 提前重勘要有人提得出理由：执掌盟约者自己没有缺口，
        // 它就没有改分配的动机。
        return false;
    }
    // 距上次重勘未满最小间隔：不开。
    //
    // 上次重勘是纪元那一次：8300 年的《脉律》就是上一次重勘的结果。
    // 它没有进 proposed_year（那一栏记的是「谁提过案」），所以这里要
    // 拿纪元当基准——否则 8301 到 8349 之间都能开一次提前重勘，与上一次
    // 只隔一两年，违反「两次重勘至少隔 50 年」。
    const std::uint32_t last = covenant.proposed_year == 0u ? epoch
                                                             : covenant.proposed_year;
    return year > epoch && year - last >= kEarlyRedraftInterval;
}

ResolveSystem::ResolveSystem(gamev2::ecs::EntityId clock, std::uint32_t epoch,
                             std::vector<EraEvent>& events)
    : clock_{clock}, epoch_{epoch}, events_{events}
{
}

void ResolveSystem::execute(gamev2::ecs::World& world,
                            gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<SimClock>(clock_);
    if (clock == nullptr)
    {
        return;
    }
    const std::uint32_t year = clock->year;

    // 公有化：抽干的脉没有主人。物理程序，不需要表决。
    //
    // 「余额为零」而不是「枯竭年不为零」：枯竭年是余额归零的那一年，
    // 判据要落在事实上——余额为零的脉对原持有者已经毫无价值。
    //
    // 事件与公有化年份对每一条抽干的脉都记，不分它原先挂在谁名下：
    // 抽干就是抽干，这条脉从这一年起不再是谁的私产。已经在公有池里的
    // 支脉同样要记一笔——它的名字空出来了，这件事本身就该在事件表里。
    // public_year 是「记过没有」的凭据：一条脉只公有化一次。
    world.view<const VeinId, VeinOwner, VeinStock>().each(
        [&](const VeinId& id, VeinOwner& owner, VeinStock& stock) {
            if (stock.stock != 0u || stock.public_year != 0u)
            {
                return;
            }
            const Sect former = owner.owner;
            owner.owner = Sect::unowned;
            stock.public_year = year;
            events_.push_back(EraEvent{EraEventKind::vein_publicized, year,
                                        id.id, Sect::unowned, former, 0u});
        });

    // 派系的年末账。
    std::array<FactionSnapshot, kFactionCount> factions{};
    world.view<const FactionId, const FactionBalance, const FactionIntent>()
        .each(
            [&](const FactionId& id, const FactionBalance& balance,
                const FactionIntent& intent) {
                FactionSnapshot& f = factions[static_cast<std::size_t>(id.sect)];
                f.sect = id.sect;
                f.deficit = balance.deficit;
                f.vote_weight = balance.vote_weight;
                f.unbounded_demand = balance.unbounded_demand;
                f.intent = intent;
            });

    // 脉的年末账。
    std::vector<VeinSnapshot> veins;
    world.view<const VeinId, const VeinOwner, const VeinRight,
               const VeinStock>().each(
        [&](const VeinId& id, const VeinOwner& owner, const VeinRight& right,
            const VeinStock& stock) {
            veins.push_back(VeinSnapshot{id.id, owner.owner, right.quota_total,
                                        stock.stock, stock.extracted,
                                        stock.taken, stock.eff});
        });
    // 清单按脉编号升序生成：视图的遍历顺序不保证与编号一致。
    std::ranges::sort(veins, {}, &VeinSnapshot::vein);

    // 余力与流速之和：余力是候选的排序依据，不是表决的依据。
    //
    // 余力 = 名下脉的**年末余额**之和（设计 §3.11.2：年初存量减去当年
    // 开采量之后剩下的，就是年末余额）。这里不能再减一遍开采量：减两遍
    // 算出来的不是余力，而且无符号减法会回绕——一条脉这一年被抽掉的量
    // 超过它年初余额的一半时，它的贡献就是负的（余额 0、当年开采大于 0），
    // 一个把自家井抽到只剩残余额的宗门（九幽宫、丹霞谷这类）累加起来
    // 会被顶成负数，回绕成约 4.29e9，于是它成了所有脉的首选候选——
    // 而「余力为 0 跳过」拦不住那个数（它是最大值，不是 0）。
    //
    // 年末余额恒非负，回绕从根上消失：余力就是「井里还剩多少」。
    // （散修不在其中：它的立场是中立，压根不是候选，回绕伤不到它。）
    std::array<std::uint32_t, kFactionCount> stock_sum{};
    for (const auto& v : veins)
    {
        const auto index = static_cast<std::size_t>(v.owner);
        stock_sum[index] += v.stock;
        // 分摊的分母只算还采得出的脉：余额为零的脉按流速分到的那一份会被
        // 余额封顶成 0，把它算进分母只会稀释别人那份（与
        // overtake_for_faction 里的同一口径）。
        if (v.stock != 0u)
        {
            factions[index].sum_eff += v.eff;
        }
    }
    for (std::size_t i = 0; i < kFactionCount; ++i)
    {
        factions[i].slack = stock_sum[i];
    }

    // 执掌盟约者：唯一有提案权的那一宗。
    std::size_t covenant = kFactionCount;
    for (std::size_t i = 0; i < kFactionCount; ++i)
    {
        if (factions[i].intent.may_propose)
        {
            covenant = i;
            break;
        }
    }
    const FactionSnapshot covenant_faction =
        covenant < kFactionCount ? factions[covenant] : FactionSnapshot{};
    if (!redraft_this_year(year, epoch_, covenant_faction.intent,
                           covenant_faction.deficit))
    {
        return;
    }
    const Sect proposer =
        covenant < kFactionCount ? covenant_faction.sect : Sect::unowned;

    // 提案：执掌盟约者召集重勘。提案年写进 FactionIntent，提前重勘的
    // 间隔就是拿它算的。
    world.view<FactionIntent>().each(
        [&](FactionIntent& intent) {
            if (intent.may_propose)
            {
                intent.proposed_year = year;
            }
        });
    events_.push_back(EraEvent{EraEventKind::redraft_proposed, year, 0u,
                                proposer, Sect::unowned, 0u});

    // 清单生成。逐条脉评估，谁都不能被夺、谁都不能当候选，各有各的
    // 规矩（设计 §3.11.3）。
    std::vector<Transfer> list;
    for (const auto& v : veins)
    {
        if (v.stock == 0u)
        {
            // 抽干的脉没有可重划的东西：它已经公有化了，转手只会让一
            // 个空名字挂回某个宗的名下。
            continue;
        }
        const Sect from = v.owner;
        if (from != Sect::unowned && identity(from).stance == Stance::neutral)
        {
            // 中立立场不可被夺：夺它要过青囊谷那一关。
            continue;
        }
        // 候选：立场 contesting、不是现任持有者、余力非零。按余力降序、
        // 派系编号升序取第一个。
        std::size_t pick = kFactionCount;
        std::uint32_t best_slack = 0u;
        for (std::size_t i = 0; i < kFactionCount; ++i)
        {
            const FactionSnapshot& f = factions[i];
            if (f.sect == from
                || identity(f.sect).stance != Stance::contesting)
            {
                continue;
            }
            if (f.slack <= 0u)
            {
                // 余力为零：井底子空了，接不住这条脉。
                continue;
            }
            if (f.slack > best_slack)
            {
                best_slack = f.slack;
                pick = i;
            }
        }
        if (pick == kFactionCount)
        {
            // 没有人接得住这条脉。
            continue;
        }
        const std::uint32_t take_after = trial_take_after(v, factions[pick]);
        if (take_after >= v.taken)
        {
            // 转手之后抽得更多：不列入清单。
            continue;
        }
        list.push_back(
            Transfer{v.vein, from, factions[pick].sect, take_after, v.taken});
    }

    if (list.empty())
    {
        // 清单为空：没有可重划的东西，提案空转（设计 §3.11.5）。
        //
        // 空转不是通过：没有东西被投过票，所以不记 redraft_passed。
        // 提案本身发生过，redraft_proposed 已经记下了。
        return;
    }

    // 表决。权重是 8300 年名下脉的总量配额之和：散修不在盟约里，没有
    // 席位，权重恒为 0。
    std::uint64_t total_weight = 0u;
    for (const auto& f : factions)
    {
        total_weight += f.vote_weight;
    }

    std::array<bool, kFactionCount> gains{};
    std::array<bool, kFactionCount> loses{};
    for (const auto& t : list)
    {
        gains[static_cast<std::size_t>(t.to)] = true;
        loses[static_cast<std::size_t>(t.from)] = true;
    }

    // 清单生效后当年的总开采量：列入清单的用试算值，其余用实采值。
    std::vector<std::uint32_t> after(veins.size(), 0u);
    for (std::size_t i = 0; i < veins.size(); ++i)
    {
        after[i] = veins[i].taken;
    }
    for (const auto& t : list)
    {
        const auto it =
            std::ranges::lower_bound(veins, t.vein, {}, &VeinSnapshot::vein);
        after[static_cast<std::size_t>(it - veins.begin())] = t.take_after;
    }
    std::uint64_t total_before = 0u;
    std::uint64_t total_after = 0u;
    for (std::size_t i = 0; i < veins.size(); ++i)
    {
        total_before += veins[i].taken;
        total_after += after[i];
    }
    const bool total_not_increased = total_after <= total_before;

    std::uint64_t approved = 0u;
    for (std::size_t i = 0; i < kFactionCount; ++i)
    {
        bool approve = false;
        if (gains[i])
        {
            // 受益方赞成。它在这一轮里得了脉，提案对它有利——哪怕它
            // 同时也失了脉：清单是整份通过的，不能拆开挑。
            approve = true;
        }
        else if (loses[i])
        {
            // 受损方反对。它没有否决权：盟约是多数决，不是全体一致。
            approve = false;
        }
        else
        {
            // 无利害方：清单不增加总开采量才赞成（世界少抽一点，存量
            // 多留一点）。
            approve = total_not_increased;
        }
        if (approve)
        {
            approved += factions[i].vote_weight;
        }
    }
    // 赞成 × 分母 > 总权重 × 分子 ⟺ 赞成 > 总权重 / 2。
    const bool passed = approved * kVoteThresholdDen
                        > total_weight * kVoteThresholdNum;

    if (!passed)
    {
        // 数额是赞成权重：否决的理由就在这个数里——它没过半。
        //
        // 设计 §3.11.5 提到的另一个理由（「总开采将增加」）在本实现里
        // 不会出现：清单的构造规则只收「转移后开采量下降」的条目，所以
        // 无利害方那一条恒成立，能挡下提案的只有权重。
        events_.push_back(EraEvent{EraEventKind::redraft_rejected, year, 0u,
                                    proposer, Sect::unowned,
                                    static_cast<std::uint32_t>(approved)});
        return;
    }
    // 数额是这一轮转手了几条脉。
    events_.push_back(EraEvent{EraEventKind::redraft_passed, year, 0u, proposer,
                                Sect::unowned,
                                static_cast<std::uint32_t>(list.size())});

    // 应用转移。变更自次年生效——本年的开采已经按旧归属算过了。
    world.view<const VeinId, VeinOwner>().each(
        [&](const VeinId& id, VeinOwner& owner) {
            const auto it =
                std::ranges::lower_bound(list, id.id, {}, &Transfer::vein);
            if (it != list.end() && it->vein == id.id)
            {
                owner.owner = it->to;
            }
        });
    for (const auto& t : list)
    {
        // 数额是试算里这条脉当年少抽的量。
        events_.push_back(EraEvent{EraEventKind::vein_transferred, year,
                                    t.vein, t.to, t.from,
                                    t.take_before - t.take_after});
    }
}

}  // namespace gamev2::faction

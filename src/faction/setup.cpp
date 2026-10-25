/// @file setup.cpp
/// @brief 派系演化层：设定身份表与实体的搭建。
#include "gamev2/faction/faction.hpp"
// 私有头与本文件同目录，不在也不该在 faction 目标的包含路径里。
#include "systems.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/view.hpp"
#include "gamev2/history/quota.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::faction {
namespace {

/// 派系的个数：七宗加无主。
constexpr std::size_t kFactionCount = static_cast<std::size_t>(Sect::count);

/// 设定给定的身份表。顺序与 Sect 枚举一致，索引即宗门编号。
///
/// 这张表是常量：角色与立场来自设定，不是调参项。
constexpr std::array<FactionIdentity, 8> kIdentities{{
    {Sect::taixuan, Role::sect, Stance::contesting, true},
    {Sect::daxia, Role::sect, Stance::contesting, false},
    {Sect::hanjiang, Role::sect, Stance::contesting, false},
    {Sect::wanshou, Role::sect, Stance::isolationist, false},
    {Sect::qiangnang, Role::sect, Stance::neutral, false},
    {Sect::tianji, Role::landless, Stance::neutral, false},
    {Sect::jiuyou, Role::demonic, Stance::contesting, false},
    {Sect::unowned, Role::free_cultivator, Stance::neutral, false},
}};

static_assert(kIdentities.size() == static_cast<std::size_t>(Sect::count));

/// 台账里编号为 vein 的脉的持有者。查不到（无脉格、荒脉）时无主：
/// 台账只收可采的脉，洞天却可以开在任何福地格上。
Sect holder_of(const gamev2::history::Ledger& ledger, std::uint32_t vein)
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

}  // namespace

const FactionIdentity& identity(Sect sect)
{
    // Sect::count 是哨兵值，不是宗门：越界是调用方的错，不是要兜住的
    // 运行时情形。
    assert(static_cast<std::size_t>(sect) < kIdentities.size());
    return kIdentities[static_cast<std::size_t>(sect)];
}

Simulation::Simulation(const gamev2::world::World& world,
                       const gamev2::history::Ledger& ledger,
                       SimConfig config,
                       std::vector<EraEvent>& events)
    : world_(world),
      ledger_(ledger),
      config_(config),
      events_(events)
{
}

void Simulation::seed_entities()
{
    // 用度与表决权重锚在 8300 的持有格局上：脉律是 8300 编的，它规定的
    // 年采额就是那年的实际开采量。8300 年余额 = 总量配额、累计开采 = 0，
    // 阴脉的衰减率因此是满值——有效流速退化成台账的 take()。
    const std::uint32_t level_epoch =
        gamev2::world::compute_spirit_tide(config_.epoch).level;

    // 用度按 2^16 定点逐脉累加、整层取整（设计 §3.3）。逐脉取整再相加
    // 是错的：8300 年每脉的有效流速只有零点几个单位，逐脉取整会把每宗
    // 两到四条脉的小数全扔掉，用度系统性低估两到四倍，某些派系甚至归
    // 零——用度归零的派系永远不会有缺口，也就不会行动。
    std::array<std::uint64_t, kFactionCount> demand_fixed{};
    std::array<std::uint32_t, kFactionCount> weights{};

    // 脉。台账已把荒脉排除在外：ledger.veins 的每一项就是一条可采的脉。
    for (const auto& entry : ledger_.veins)
    {
        const gamev2::ecs::EntityId e = ecs_world_.create();
        veins_.push_back(e);
        (void)ecs_world_.add<VeinId>(e, VeinId{entry.vein});

        const auto& vein = world_.veins[entry.vein];
        const VeinProfile profile{vein.rank, vein.element,
                                  history::is_yin_vein(world_, entry.vein),
                                  gamev2::world::kNoCaveHeaven};
        const VeinRight right{entry.surveyed_total, entry.surveyed_rate,
                              entry.registered, entry.holder};
        VeinStock stock;
        stock.stock = entry.surveyed_total;

        // 用度：min(有效流速, 总量配额)。两者都换算到 2^16 定点再比。
        const std::uint64_t eff = eff_fixed(profile, right, stock,
                                            level_epoch, config_.conversion);
        const std::uint64_t capped =
            std::min(eff, static_cast<std::uint64_t>(entry.surveyed_total) << 16);
        const auto index = static_cast<std::size_t>(entry.holder);
        demand_fixed[index] += capped;
        weights[index] += entry.surveyed_total;

        (void)ecs_world_.add<VeinProfile>(e, profile);
        (void)ecs_world_.add<VeinRight>(e, right);
        (void)ecs_world_.add<VeinStock>(e, stock);
        (void)ecs_world_.add<VeinOwner>(e, VeinOwner{entry.holder});
        (void)ecs_world_.add<VeinRemainder>(e, VeinRemainder{});
    }

    // 洞天。归属与挂靠都从所在格子的脉反查：台账是唯一的持有格局来源。
    for (const auto& cave : world_.cave_heavens)
    {
        const std::uint32_t vein = world_.vein_id.at(cave.cell);
        const gamev2::ecs::EntityId e = ecs_world_.create();
        (void)ecs_world_.add<CaveHeavenId>(e, CaveHeavenId{cave.cell, vein});
        (void)ecs_world_.add<CaveHeavenState>(e,
            CaveHeavenState{holder_of(ledger_, vein), true});
    }

    // 无主不在盟约里，没有席位。这条要显式清零：支脉有四十条，权重
    // 加起来比七宗还大，靠「支脉权重小」蒙混会算出一个无主压倒七宗
    // 的盟约。天机阁无地，权重自然是 0。
    weights[static_cast<std::size_t>(Sect::unowned)] = 0u;

    for (std::size_t i = 0; i < static_cast<std::size_t>(Sect::count); ++i)
    {
        const auto sect = static_cast<Sect>(i);
        const FactionIdentity& id = identity(sect);
        const gamev2::ecs::EntityId e = ecs_world_.create();
        factions_.push_back(e);
        (void)ecs_world_.add<FactionId>(e, FactionId{sect});

        // 无上限用度（魔修、散修）没有可比较的数：demand 置 0，
        // 改由 unbounded_demand 明说。
        const bool unbounded =
            id.role == Role::demonic || id.role == Role::free_cultivator;
        FactionBalance balance;
        balance.unbounded_demand = unbounded;
        balance.demand = unbounded ? 0u : round_fixed(demand_fixed[i]);
        balance.vote_weight = weights[i];
        (void)ecs_world_.add<FactionBalance>(e, balance);
        (void)ecs_world_.add<FactionIntent>(e, FactionIntent{id.may_propose});
    }

    // 时钟从纪元前一年起算：ClockSystem 每年把年推进一格，第一年就是
    // 纪元年 8300，与台账的循环区间 [epoch, now) 一致。
    clock_ = ecs_world_.create();
    (void)ecs_world_.add<SimClock>(clock_,
                                   SimClock{config_.epoch - 1u, 0u});
}

void Simulation::run(FactionDiagnostics& diagnostics)
{
    // 执行顺序由每个 system 自己的 `order` 声明（设计 §3.13），下面这七行
    // 的注册顺序只作同 order 时的次级键。语义仍然不能改：ClockSystem 写
    // 潮位，另六个读它；TribulateSystem 要排在开采与评估之前——劫云抽干的
    // 那一年，被抽走的配额不该再被采；DepleteSystem 还要排在开采与越采
    // 之后——枯竭年是余额归零的那一年；ResolveSystem 排在最后——公有化与
    // 归属变更都要看当年的开采结果。
    //
    // 工作线程数给 1：本层规模下并行是零收益，而多线程会让
    // View::each_parallel 的行为依赖批内的位置。串行是刻意的选择。
    gamev2::ecs::Scheduler scheduler(1);
    ClockSystem clock(clock_);
    TribulateSystem tribulate(tolls_of_ledger(), vein_entities(),
                              clock_, events_, diagnostics);
    AssessSystem assess(config_.conversion);
    ExtractSystem extract(config_.conversion);
    OvertakeSystem overtake(world_.params().seed, config_.epoch, events_);
    DepleteSystem deplete(clock_, events_);
    ResolveSystem resolve(clock_, config_.epoch, events_);
    scheduler.add(clock);
    scheduler.add(tribulate);
    scheduler.add(assess);
    scheduler.add(extract);
    scheduler.add(overtake);
    scheduler.add(deplete);
    scheduler.add(resolve);
    scheduler.plan();

    gamev2::ecs::CommandBuffer cmd;
    for (std::uint32_t y = config_.epoch; y < config_.now; ++y)
    {
        scheduler.run(ecs_world_, cmd);
    }
}

std::vector<Toll> Simulation::tolls_of_ledger() const
{
    // 台账是劫损的唯一来源：候选池、排期与覆盖比例都在那边算完了。
    // 本层只负责按年施加——重算一遍就是两套模型各说各话。
    std::vector<Toll> tolls;
    tolls.reserve(ledger_.tribulation_events.size());
    for (const auto& event : ledger_.tribulation_events)
    {
        tolls.push_back(Toll{event.year, event.vein, event.amount});
    }
    return tolls;
}

std::vector<std::pair<gamev2::ecs::EntityId, std::uint32_t>>
Simulation::vein_entities() const
{
    // 脉实体与台账的脉一一对应（seed_entities 按 ledger.veins 建实体）。
    // 视图的遍历顺序不保证与脉编号一致，所以脉编号到实体的映射要自己
    // 记一份。
    std::vector<std::pair<gamev2::ecs::EntityId, std::uint32_t>> pairs;
    pairs.reserve(veins_.size());
    for (std::size_t i = 0; i < veins_.size(); ++i)
    {
        pairs.emplace_back(veins_[i], ledger_.veins[i].vein);
    }
    return pairs;
}

void Simulation::collect(EraReport& report, FactionDiagnostics& diagnostics)
{
    // seed_entities 保证时钟在场（simulate 先建实体再汇总）。
    const auto* clock = ecs_world_.get_if<SimClock>(clock_);
    assert(clock != nullptr);
    report.current_year = clock->year;
    report.current_level = clock->level;
    // 时钟从纪元前一年起算，跑完 N 年停在 epoch−1+N：反推年数。
    // 未推演任何一年时（时钟还在纪元前）按 0 计。
    report.years_simulated =
        (clock->year + 1u >= config_.epoch)
            ? clock->year + 1u - config_.epoch
            : 0u;
    report.events = events_;

    // 重勘的通过、驳回与空转次数：数事件表，不另设计数器——事件表就是
    // 唯一的事实来源。
    std::uint32_t proposed = 0u;
    for (const auto& event : events_)
    {
        if (event.kind == EraEventKind::redraft_passed)
        {
            ++report.redraft_passed;
            ++diagnostics.redraft_passed;
        }
        else if (event.kind == EraEventKind::redraft_rejected)
        {
            ++report.redraft_rejected;
            ++diagnostics.redraft_rejected;
        }
        else if (event.kind == EraEventKind::redraft_proposed)
        {
            ++proposed;
        }
    }
    // 空转：提了案却既没通过也没被否——清单是空的，没有东西可投
    // （设计 §3.11.5 的第二种僵持）。重勘只有这三种结局，所以空转的
    // 次数就是提案数减去通过与驳回数。
    diagnostics.redraft_empty = proposed - report.redraft_passed
                                - report.redraft_rejected;

    // 宗级汇总的累加器。归属会变（重勘转手、公有化交给无主），所以
    // 「当代持有」按 VeinOwner 数，「初始持有」按 VeinRight 的
    // initial_owner 数——两者的差就是格局变过多少。
    std::array<std::uint32_t, kFactionCount> vein_count{};
    std::array<std::uint32_t, kFactionCount> initial_vein_count{};
    std::array<std::uint32_t, kFactionCount> quota_total{};
    std::array<std::uint32_t, kFactionCount> balance_total{};
    std::array<std::uint32_t, kFactionCount> toll_total{};

    report.veins.reserve(veins_.size());
    for (const gamev2::ecs::EntityId e : veins_)
    {
        const auto* id = ecs_world_.get_if<VeinId>(e);
        const auto* profile = ecs_world_.get_if<VeinProfile>(e);
        const auto* right = ecs_world_.get_if<VeinRight>(e);
        const auto* stock = ecs_world_.get_if<VeinStock>(e);
        const auto* owner = ecs_world_.get_if<VeinOwner>(e);
        // 这几件都是 seed_entities 亲手挂上的：缺任何一件都是本层的
        // 内部错误，不是要兜住的运行时情形。
        assert(id != nullptr
               && profile != nullptr
               && right != nullptr
               && stock != nullptr
               && owner != nullptr);

        VeinReport vein;
        vein.vein = id->id;
        // 当代归属：ResolveSystem 公有化与转让之后的样子。填初始持有者
        // 会让「格局变过没有」这件事在报告里看不见。
        vein.owner = owner->owner;
        vein.initial_owner = right->initial_owner;
        vein.rank = profile->rank;
        vein.quota_total = right->quota_total;
        vein.extracted = stock->extracted;
        vein.legal_extracted = stock->legal_extracted;
        vein.balance = stock->stock;
        vein.tribulation_loss = stock->tribulation_loss;
        vein.depleted_year = stock->depleted_year;
        vein.registered = right->registered;
        vein.yin = profile->yin;
        report.veins.push_back(vein);

        // 持有脉数、配额与余额都按**当代**归属汇总：格局变了，账跟着
        // 变，这正是当代格局要与 8300 分开记的原因。
        const auto owner_index = static_cast<std::size_t>(vein.owner);
        vein_count[owner_index] += 1u;
        const auto initial_index =
            static_cast<std::size_t>(vein.initial_owner);
        initial_vein_count[initial_index] += 1u;
        quota_total[owner_index] += vein.quota_total;
        balance_total[owner_index] += vein.balance;
        toll_total[owner_index] += vein.tribulation_loss;
        report.total_extracted += vein.extracted;
        report.total_balance += vein.balance;

        // 格局变过没有：当代归属与初始归属不同，就是变过。
        if (vein.owner != vein.initial_owner)
        {
            ++report.owner_changes;
        }

        // 守恒：余额、开采与劫损之和必须等于配额。劫损是第三项——它不是
        // 开采，没有持有者，谁也不为它负责，但它确实从余额里走了。
        if (vein.balance + vein.extracted + vein.tribulation_loss
            != vein.quota_total)
        {
            ++diagnostics.conservation_broken;
        }
        // 公有化的一致性：抽干的脉不该还挂在原主人名下。
        if (vein.balance == 0u && vein.owner != Sect::unowned)
        {
            ++diagnostics.public_release_mismatch;
        }
    }

    // 洞天：名下还立着的洞天数。失效的不算——当代格局问的是「现在
    // 还有几个」，失效的那些在事件表里有名有姓（洞天失效）。
    std::array<std::uint32_t, kFactionCount> cave_heavens{};
    ecs_world_.view<const CaveHeavenState>().each(
        [&](const CaveHeavenState& state) {
            if (!state.alive)
            {
                return;
            }
            cave_heavens[static_cast<std::size_t>(state.owner)] += 1u;
        });

    report.factions.reserve(factions_.size());
    for (const gamev2::ecs::EntityId e : factions_)
    {
        const auto* id = ecs_world_.get_if<FactionId>(e);
        const auto* balance = ecs_world_.get_if<FactionBalance>(e);
        assert(id != nullptr && balance != nullptr);

        const auto index = static_cast<std::size_t>(id->sect);
        FactionReport faction;
        faction.sect = id->sect;
        faction.vein_count = vein_count[index];
        faction.initial_vein_count = initial_vein_count[index];
        faction.quota_total = quota_total[index];
        faction.balance = balance_total[index];
        faction.demand = balance->demand;
        faction.supply = balance->supply;
        faction.deficit = balance->deficit;
        faction.extracted = balance->extracted;
        faction.overmined = balance->overmined;
        faction.unregistered = balance->unregistered;
        faction.tribulation_loss = toll_total[index];
        faction.vote_weight = balance->vote_weight;
        faction.cave_heavens = cave_heavens[index];
        faction.unbounded_demand = balance->unbounded_demand;
        report.factions.push_back(faction);
    }
}

}  // namespace gamev2::faction

/// @file systems.cpp
/// @brief 派系演化层的五个逐年 system：时钟、评估、开采、越采、枯竭。
#include "systems.hpp"
#include "gamev2/ecs/view.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace gamev2::faction {
namespace {

/// 派系的个数：七宗加无主。
constexpr std::size_t kFactionCount = static_cast<std::size_t>(Sect::count);

/// 找时钟。实体世界里只有一个 SimClock 实体。
///
/// 三个 system 都要当年的潮位：抄三遍「找时钟」，改一次漏两处是常事。
const SimClock* find_clock(gamev2::ecs::World& world)
{
    const SimClock* clock = nullptr;
    world.view<const SimClock>().each(
        [&](const SimClock& c) { clock = &c; });
    return clock;
}

/// 按脉编号取脉的物理状态。找不到返回 nullptr。
///
/// 洞天挂在脉上，判它失效要读那条脉的枯竭年。视图的遍历顺序不保证
/// 与脉编号一致，所以只能按编号找。
///
/// 指针指向列存储里的元素，视图的临时对象销毁后仍然有效——列存储的
/// 生命周期跟着 World 走。但**不要**把它存到视图之外去用；本函数只在
/// 一次 `each` 之内用。
const VeinStock* find_vein_stock(gamev2::ecs::World& world,
                                 std::uint32_t vein_id)
{
    const VeinStock* found = nullptr;
    world.view<const VeinId, const VeinStock>().each(
        [&](const VeinId& id, const VeinStock& stock) {
            if (id.id == vein_id)
            {
                found = &stock;
            }
        });
    return found;
}

/// 派系表：宗门 → 该宗门的年度账。
///
/// 派系实体有八个，用脉的归属去找它们的账需要一个按宗门的索引。
/// 指针指向列存储里的元素：system 内没有结构变更，它们一直有效。
using FactionTable =
    std::array<FactionBalance*, static_cast<std::size_t>(Sect::count)>;

FactionTable build_faction_table(gamev2::ecs::World& world)
{
    FactionTable table{};
    world.view<const FactionId, FactionBalance>().each(
        [&](const FactionId& id, FactionBalance& balance) {
            table[static_cast<std::size_t>(id.sect)] = &balance;
        });
    return table;
}

/// 剩余配额 = 总量配额 − 累计开采，饱和到 0。
///
/// 越采会让累计开采超过配额，裸减法会回绕成一个巨大的「剩余」，
/// 合法开采就会接着从里面扣——守恒恒等式照样成立，账全错。
std::uint32_t quota_left_of(const VeinRight& right, const VeinStock& stock)
{
    return (stock.extracted < right.quota_total)
               ? right.quota_total - stock.extracted
               : 0u;
}

/// 一条脉当年的可采上限：有效流速、剩余配额、余额三者的最小值。
///
/// 评估与开采共用这一个口径：两处各写一份 min，迟早会有一处漏掉
/// 其中一个上限。
std::uint32_t capped_take_of(std::uint64_t eff, const VeinRight& right,
                             const VeinStock& stock)
{
    return static_cast<std::uint32_t>(
        std::min({eff, static_cast<std::uint64_t>(quota_left_of(right, stock)),
                  static_cast<std::uint64_t>(stock.stock)}));
}

/// 越采被发现：登记在册的宗派每年 1/8 的概率，一条脉至多报一次。
///
/// 随机按 (年, 派系, 脉) 无状态派生（设计 §3.14）：不依赖遍历顺序，
/// 调整循环次序不会换一个世界。
void discover_overmine(const SimClock& clock, std::uint64_t seed,
                       std::vector<EraEvent>& events, Sect sect,
                       std::uint32_t vein, std::uint32_t amount,
                       VeinStock& stock)
{
    if (stock.overmine_reported)
    {
        return;
    }
    std::uint64_t s = gamev2::world::stage_seed(
        seed, static_cast<std::uint32_t>(gamev2::world::Stream::faction));
    s = gamev2::world::stage_seed(s, clock.year);
    s = gamev2::world::stage_seed(s, static_cast<std::uint32_t>(sect));
    s = gamev2::world::stage_seed(s, vein);
    gamev2::world::Pcg32 rng(s);
    if (rng.below(8) != 0u)
    {
        return;
    }
    stock.overmine_reported = true;
    events.push_back(EraEvent{EraEventKind::overmine_discovered, clock.year,
                              vein, sect, Sect::unowned, amount});
}

/// 一个派系名下的一条脉。
///
/// 指针指向视图的列存储：本 system 内没有结构变更，它们在循环期间
/// 一直有效。Task 6/7 若在同一 system 里增删实体，这些指针会悬空，
/// 届时要改成按实体句柄回取。
struct Lane
{
    VeinStock* stock = nullptr;
    const VeinRight* right = nullptr;
    std::uint32_t vein = 0;
};

/// 一个派系当年的越采：无上限用度直接抽，有上限用度按缺口分摊。
///
/// `procedure_used`：正派越采的闸门（设计 §3.7）。正派必须先走完重勘
/// 程序——当年是重勘年而程序没能满足它的缺口，它才越线。魔修与散修
/// 不在盟约里，程序管不着它们。
void overtake_for_faction(gamev2::ecs::World& world, const SimClock& clock,
                          std::uint64_t seed, std::vector<EraEvent>& events,
                          Sect sect, FactionBalance& balance,
                          bool procedure_used)
{
    if (identity(sect).role == Role::sect && !procedure_used)
    {
        // 正派：程序已用尽才越线。
        return;
    }
    if (!balance.unbounded_demand && balance.deficit == 0u)
    {
        // 缺口为零的派系不行动（设计 §3.5）：这一刀没有主动扩张。
        return;
    }

    std::vector<Lane> lanes;
    world.view<const VeinId, const VeinOwner, const VeinRight,
               VeinStock>().each(
        [&](const VeinId& id, const VeinOwner& owner, const VeinRight& right,
            VeinStock& stock) {
            if (owner.owner != sect)
            {
                return;
            }
            if (stock.stock == 0u)
            {
                // 余额为零的脉分不到份额，也不该占分母：它按流速分到的那
                // 一份会被余额封顶成 0，把它算进分母只会稀释别人那份，
                // 缺口于是凭空少一块。
                return;
            }
            lanes.push_back(Lane{&stock, &right, id.id});
        });
    // 按脉编号排序：分摊的补齐次序以脉编号定序，结果因此与视图的遍历
    // 顺序无关。
    std::ranges::sort(lanes, {}, &Lane::vein);

    // 当年的有效流速取 ExtractSystem 算好的那个（VeinStock::eff）：
    // 衰减率只在年初算一次，本 system 重算会拿到开采之后的存量，
    // 分摊的分母就与缺口的分母不同源了（设计 §3.13）。
    std::uint64_t sum_eff = 0;
    for (const Lane& lane : lanes)
    {
        sum_eff += lane.stock->eff;
    }
    if (!balance.unbounded_demand && sum_eff == 0u)
    {
        // 分摊的分母为 0：谁也分不到份额。
        return;
    }

    // 缺口分摊：各脉分到的整数份额之和恰好等于缺口（设计 §3.7）。
    std::vector<std::uint32_t> shares;
    if (!balance.unbounded_demand)
    {
        std::vector<std::uint64_t> weights;
        weights.reserve(lanes.size());
        for (const Lane& lane : lanes)
        {
            weights.push_back(lane.stock->eff);
        }
        shares = apportion_deficit(balance.deficit, weights);
    }

    for (std::size_t i = 0; i < lanes.size(); ++i)
    {
        const Lane& lane = lanes[i];
        const std::uint64_t eff = lane.stock->eff;
        // 无上限用度不经缺口分摊：随时可以，采尽为止。
        const std::uint64_t share = balance.unbounded_demand
                                        ? eff * kOvertakeFactor
                                        : shares[i];
        // 越采的上限是**余额**，余额不能变负（计划差异第 2 条）。
        //
        // 余额是唯一的**物理**上限：井里还剩多少，就抽不穿多少。别的封顶
        // 都是记账口径，会比余额大——「剩余**合法**配额」就比余额大出正好
        // 是已越采的那一份（余额 ≡ 配额 − 合法 − 越采，剩余合法配额 ≡
        // 配额 − 合法）。拿它封顶，越采就会超过余额，`stock -= over` 做
        // 无符号减法回绕，余额从 0 跳到约 4.29e9，守恒恒等式随之崩掉。
        // 真正崩的是回绕，不是「累计开采超过配额」——累计开采仍然 ≤ 配额。
        //
        // 写「剩余配额」倒也不会崩，只是别写反：本层里
        // stock ≡ quota_total − extracted 恒等（建脉时两者相等，此后每次
        // 开采同减同增），「余额」与「剩余配额」是同一个数，写反并不会
        // 让越采消失。余额在这里再夹一次，抽不穿井底。
        const std::uint32_t over = static_cast<std::uint32_t>(
            std::min({share, eff * kOvertakeFactor,
                      static_cast<std::uint64_t>(lane.stock->stock)}));
        if (over == 0u)
        {
            continue;
        }
        lane.stock->stock -= over;
        lane.stock->extracted += over;
        lane.stock->taken += over;
        // 派系级的累计开采：合法开采由 ExtractSystem 记，越采由这里记。
        balance.extracted += over;
        balance.overmined += over;
        if (!lane.right->registered)
        {
            balance.unregistered += over;
        }
        // 登记在册的宗派越采会被仲裁发现；不登记的（九幽宫那条、
        // 散修的私采）报了也没人管（设计 §3.7）。
        if (lane.right->registered && sect != Sect::unowned)
        {
            discover_overmine(clock, seed, events, sect, lane.vein, over,
                              *lane.stock);
        }
    }
}

}  // namespace

ClockSystem::ClockSystem(gamev2::ecs::EntityId clock)
    : clock_(clock)
{
}

void ClockSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer&)
{
    auto* clock = world.get_if<SimClock>(clock_);
    if (clock == nullptr)
    {
        return;
    }
    ++clock->year;
    clock->level = gamev2::world::compute_spirit_tide(clock->year).level;
}

AssessSystem::AssessSystem(std::uint64_t conversion)
    : conversion_(conversion)
{
}

void AssessSystem::execute(gamev2::ecs::World& world,
                           gamev2::ecs::CommandBuffer&)
{
    const SimClock* clock = find_clock(world);
    if (clock == nullptr)
    {
        return;
    }

    // 供给与缺口是当年量，逐年重算；用度与表决权重是常量，不动。
    world.view<FactionBalance>().each(
        [](FactionBalance& balance) {
            balance.supply = 0u;
            balance.deficit = 0u;
        });
    const FactionTable table = build_faction_table(world);

    // 供给按 2^16 定点逐脉累加、整层取整（设计 §3.4）。口径与用度同源：
    // 纪元那年的供给与用度是同一个实数，取整后仍相等 → 缺口恰好是 0。
    std::array<std::uint64_t, kFactionCount> supply_fixed{};

    world.view<const VeinOwner, const VeinRight, const VeinStock,
               const VeinProfile>().each(
        [&](const VeinOwner& owner, const VeinRight& right,
            const VeinStock& stock, const VeinProfile& profile) {
            const auto index = static_cast<std::size_t>(owner.owner);
            if (table[index] == nullptr)
            {
                return;
            }
            // 三个上限（设计 §3.4）：有效流速是物理吞吐，剩余配额是制度
            // 上限，余额是物理存量。全都换算到 2^16 定点再比。
            //
            // 定标的 2^16 必须在**移位处**显式转成 64 位：左移 16 位若
            // 在 32 位里算，配额 ≥ 65536 时会回绕（封顶偏小，纪元那年冒
            // 出幻影缺口，零缺口不变式随之破）。默认图幅的最大配额是
            // 1232，够不着 65536——这是一颗潜伏的雷，不是现状。
            const std::uint64_t eff = eff_fixed(profile, right, stock,
                                                clock->level, conversion_);
            const std::uint32_t quota_left = quota_left_of(right, stock);
            supply_fixed[index] +=
                std::min({eff, static_cast<std::uint64_t>(quota_left) << 16,
                          static_cast<std::uint64_t>(stock.stock) << 16});
        });

    for (std::size_t i = 0; i < kFactionCount; ++i)
    {
        FactionBalance* balance = table[i];
        if (balance == nullptr)
        {
            continue;
        }
        balance->supply = round_fixed(supply_fixed[i]);
        if (balance->unbounded_demand)
        {
            // 无上限用度没有可比较的数，缺口恒为 0。
            continue;
        }
        balance->deficit = deficit_of(balance->demand, balance->supply);
    }
}

ExtractSystem::ExtractSystem(std::uint64_t conversion)
    : conversion_(conversion)
{
}

void ExtractSystem::execute(gamev2::ecs::World& world,
                            gamev2::ecs::CommandBuffer&)
{
    const SimClock* clock = find_clock(world);
    if (clock == nullptr)
    {
        return;
    }
    const FactionTable table = build_faction_table(world);
    world.view<const VeinOwner, const VeinRight, const VeinProfile,
               VeinStock, VeinRemainder>().each(
        [&](const VeinOwner& owner, const VeinRight& right,
            const VeinProfile& profile, VeinStock& stock,
            VeinRemainder& remainder) {
            const std::uint64_t eff = take_effective(
                profile, right, stock, clock->level, conversion_, remainder);
            // 当年的有效流速记在脉上：衰减率只在年初算一次，年内不变，
            // 越采复用这同一个数（设计 §3.13）。
            stock.eff = eff;
            const std::uint32_t quota_left = quota_left_of(right, stock);
            if (eff >= quota_left)
            {
                // 被配额卡住：未消费的小数没有意义，余数清零。
                remainder.value = 0u;
            }
            const std::uint32_t take = capped_take_of(eff, right, stock);
            stock.extracted += take;
            stock.legal_extracted += take;
            stock.stock -= take;
            stock.taken = take;
            // 开采记在归属派系的账上：派系级的累计开采要与脉级对得上。
            FactionBalance* balance =
                table[static_cast<std::size_t>(owner.owner)];
            if (balance != nullptr)
            {
                balance->extracted += take;
                if (!right.registered)
                {
                    // 未登记脉上的开采（合法 + 越采）单列一笔，口径与台账
                    // 的 `unregistered_extracted` 同名同义。
                    balance->unregistered += take;
                }
            }
        });
}

OvertakeSystem::OvertakeSystem(std::uint64_t seed, std::uint32_t epoch,
                               std::vector<EraEvent>& events)
    : seed_(seed), epoch_(epoch), events_(events)
{
}

void OvertakeSystem::execute(gamev2::ecs::World& world,
                             gamev2::ecs::CommandBuffer&)
{
    const SimClock* clock = find_clock(world);
    if (clock == nullptr)
    {
        return;
    }
    // 本年程序走没走过。判据与 ResolveSystem 用的是同一个函数：正派
    // 越采要看它，重勘也要看它，两处各写一份必然走偏。
    FactionIntent covenant;
    std::uint32_t covenant_deficit = 0u;
    world.view<const FactionId, const FactionBalance, const FactionIntent>()
        .each(
            [&](const FactionId& id, const FactionBalance& balance,
                const FactionIntent& intent) {
                if (!intent.may_propose)
                {
                    return;
                }
                covenant = intent;
                covenant_deficit = balance.deficit;
            });
    const bool procedure_used =
        redraft_this_year(clock->year, epoch_, covenant, covenant_deficit);

    // 一条脉只有一个持有者，按派系逐个处理不会重复扣同一条脉。
    world.view<const FactionId, FactionBalance>().each(
        [&](const FactionId& id, FactionBalance& balance) {
            overtake_for_faction(world, *clock, seed_, events_, id.sect,
                                 balance, procedure_used);
        });
}

DepleteSystem::DepleteSystem(gamev2::ecs::EntityId clock,
                             std::vector<EraEvent>& events)
    : clock_(clock), events_(events)
{
}

void DepleteSystem::execute(gamev2::ecs::World& world,
                            gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<SimClock>(clock_);
    if (clock == nullptr)
    {
        return;
    }
    world.view<VeinId, VeinStock>().each(
        [&](VeinId& id, VeinStock& stock) {
            if (stock.depleted_year != 0u || stock.stock != 0u)
            {
                return;
            }
            stock.depleted_year = clock->year;
            events_.push_back(
                EraEvent{EraEventKind::vein_depleted, clock->year, id.id,
                         Sect::unowned, Sect::unowned, 0u});
        });
    // 洞天：它挂在一条脉上，那条脉枯了，洞天就失效。
    world.view<CaveHeavenId, CaveHeavenState>().each(
        [&](CaveHeavenId& cave, CaveHeavenState& state) {
            if (!state.alive)
            {
                return;
            }
            const auto* vein = find_vein_stock(world, cave.vein);
            if (vein == nullptr || vein->depleted_year == 0u)
            {
                return;
            }
            state.alive = false;
            events_.push_back(EraEvent{EraEventKind::cave_heaven_lost,
                                       clock->year, cave.vein, state.owner,
                                       Sect::unowned, 0u});
        });
}

}  // namespace gamev2::faction

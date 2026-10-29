/// @file systems.cpp
/// @brief 众生层：年循环的七个 system。
#include "systems.hpp"
// 私有头与本文件同目录，不在也不该在 life 目标的包含路径里。
#include "generate.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/life/combat.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gamev2::life {

namespace {

/// 「没有这个人」在索引表里的记号。
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

/// 本年活人的快照。
///
/// 决策与行为都从这里读：两处各写一遍「取不齐八件就跳过」的判据，
/// 分叉会让两处对不上同一个人——谁被判死、谁被跳过必须是同一条线。
struct SoulSnapshot
{
    gamev2::ecs::EntityId e{};
    std::uint32_t index = 0;
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t progress = 0;
    std::uint32_t age = 0;
    std::uint32_t cell = 0;
    std::uint32_t seclude_until = 0;
    std::uint32_t starved = 0;
    std::uint64_t stone = 0;
    std::uint8_t body = 1;
    std::uint8_t greed = 3;
    std::uint8_t valor = 3;
    std::uint8_t order = 3;
    std::uint8_t mercy = 3;
    std::uint8_t vigor = 3;
    std::int32_t power = 0;
};

/// 本年活人快照。空 get_if = 死者留下的空位；寿尽者本年即死，
/// 不进快照（与 Aging 的销毁同一判据）。
[[nodiscard]] std::vector<SoulSnapshot> census(
    gamev2::ecs::World& world,
    const std::vector<gamev2::ecs::EntityId>& souls)
{
    std::vector<SoulSnapshot> out;
    out.reserve(souls.size());
    for (const auto e : souls)
    {
        const auto* id = world.get_if<SoulId>(e);
        const auto* cult = world.get_if<Cultivation>(e);
        const auto* mortal = world.get_if<Mortal>(e);
        const auto* apt = world.get_if<Aptitude>(e);
        const auto* temper = world.get_if<Temper>(e);
        const auto* purse = world.get_if<Purse>(e);
        const auto* office = world.get_if<Office>(e);
        const auto* site = world.get_if<Site>(e);
        if (id == nullptr || cult == nullptr || mortal == nullptr
            || apt == nullptr || temper == nullptr || purse == nullptr
            || office == nullptr || site == nullptr)
        {
            continue;
        }
        if (lifespan_reached(*mortal, *cult))
        {
            continue;
        }
        SoulSnapshot s;
        s.e = e;
        s.index = id->index;
        s.sect = id->sect;
        s.office = office->kind;
        s.realm = cult->realm;
        s.progress = cult->progress;
        s.age = mortal->age;
        s.cell = site->cell;
        s.seclude_until = cult->seclude_until;
        s.starved = office->starved;
        s.stone = purse->stone;
        s.body = apt->body;
        s.greed = temper->greed;
        s.valor = temper->valor;
        s.order = temper->order;
        s.mercy = temper->mercy;
        s.vigor = temper->vigor;
        s.power = combat_power(*cult, *apt, purse->stone);
        out.push_back(s);
    }
    return out;
}

/// 索引表：SoulId::index → 快照下标。身位序号稠密且稳定。
[[nodiscard]] std::vector<std::size_t> index_table(
    const std::vector<SoulSnapshot>& people)
{
    std::uint32_t max_index = 0;
    for (const auto& p : people)
    {
        max_index = std::max(max_index, p.index);
    }
    std::vector<std::size_t> table(max_index + 1u, kNoIndex);
    for (std::size_t i = 0; i < people.size(); ++i)
    {
        table[people[i].index] = i;
    }
    return table;
}

/// 性格轴。推移只动这几条，不做通用反射。
enum class Axis : std::uint8_t
{
    greed,
    valor,
    order,
    mercy,
    vigor,
};

/// 性格推移（差异 18）：距上次推移 ≥10 年才动一档、钳 [1,5]。
///
/// 「十年缓变」是防漂移闸：性格轴是岔路选择器，漂得太快会让判据
/// 变成噪声。
void nudge(Temper& temper, Axis axis, int dir, std::uint32_t year)
{
    if (temper.drift_year != 0u && year < temper.drift_year + 10u)
    {
        return;
    }
    std::uint8_t* value = nullptr;
    switch (axis)
    {
    case Axis::greed: value = &temper.greed; break;
    case Axis::valor: value = &temper.valor; break;
    case Axis::order: value = &temper.order; break;
    case Axis::mercy: value = &temper.mercy; break;
    case Axis::vigor: value = &temper.vigor; break;
    }
    if (value == nullptr)
    {
        return;
    }
    const int moved = std::clamp(static_cast<int>(*value) + dir, 1, 5);
    *value = static_cast<std::uint8_t>(moved);
    temper.drift_year = year;
}

/// 声望增减，钳在 [-5, 5]。
void bump_standing(std::int8_t& value, int delta)
{
    value = static_cast<std::int8_t>(
        std::clamp(static_cast<int>(value) + delta, -5, 5));
}

/// 记一条事件。
void push_event(std::vector<LifeEvent>& events, LifeEventKind kind,
                std::uint32_t year, std::uint32_t actor,
                std::uint32_t counterparty, history::Sect sect,
                std::uint32_t amount)
{
    events.push_back(
        LifeEvent{kind, year, actor, counterparty, sect, amount});
}

/// 横死清算：先清账再销毁。
///
/// 实体销毁会把私囊带出账外，守恒从此年年破——先烧进 burned_total
/// 再销毁。仇怨/恩义边随死清理，漏网的由年末 orphan_bonds 兜底。
/// 职位同时清空：死者不留职位，因此经济步不再给他发份额——份额若
/// 照发，烧毁后剩余的私囊会随实体一起消失，守恒从此年年破一次。
/// 事件 slain 由调用方在清算前先写入（清算只管账与销毁）。
void slay(gamev2::ecs::World& world, gamev2::ecs::CommandBuffer& cmd,
          gamev2::ecs::EntityId e, std::uint32_t index, BondGraph& bonds,
          EconomyLedger& ledger)
{
    if (auto* purse = world.get_if<Purse>(e); purse != nullptr)
    {
        ledger.burned_total += purse->stone;
        purse->stone = 0;
    }
    if (auto* office = world.get_if<Office>(e); office != nullptr)
    {
        office->kind = OfficeKind::none;
        office->years = 0;
    }
    bonds.erase_soul(index);
    cmd.destroy(e);
}

/// 找「去年那条害了人的事件」：仇怨边记在受害那年，报在次年——
/// 因果方向因此钉死（仇在先、报在后）。
[[nodiscard]] std::uint32_t harm_year_of(const std::vector<LifeEvent>& events,
                                         std::uint32_t aggressor,
                                         std::uint32_t year)
{
    for (auto it = events.rbegin(); it != events.rend(); ++it)
    {
        if (it->year + 1u != year || it->counterparty != aggressor)
        {
            continue;
        }
        if (it->kind != LifeEventKind::slain
            && it->kind != LifeEventKind::expulsion)
        {
            continue;
        }
        return it->year;
    }
    return year;
}

/// 从候选表里等概率取一个「不是自己」的序号。
///
/// 候选表含自己：抽 n-1 个下标，抽中的若恰是自己就改取最后一个——
/// 每个非自己元素恰好被命中一次，分布仍是均匀的。
[[nodiscard]] std::uint32_t pick_other(const std::vector<std::uint32_t>& pool,
                                       std::uint32_t self,
                                       gamev2::world::Pcg32& rng)
{
    if (pool.size() <= 1u)
    {
        return 0u;
    }
    auto k = rng.below(static_cast<std::uint32_t>(pool.size()) - 1u);
    if (pool[k] == self)
    {
        ++k;
    }
    return pool[k];
}

}  // namespace

ClockSystem::ClockSystem(gamev2::ecs::EntityId clock) : clock_(clock)
{
}

void ClockSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer&)
{
    auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // seed 亲手挂的：缺了是内部错误
    ++clock->year;
}

AgingSystem::AgingSystem(
    const std::vector<gamev2::ecs::EntityId>& souls, BondGraph& bonds)
    : souls_(souls), bonds_(bonds)
{
}

void AgingSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    // 遍历序号表而非 view.each：销毁需要 EntityId，而 each 只给组件
    // 引用。空 get_if = 死者留下的空位，跳过。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* mortal = world.get_if<Mortal>(e);
        const auto* cult = world.get_if<Cultivation>(e);
        if (mortal == nullptr || cult == nullptr)
        {
            continue;
        }
        ++mortal->age;
        // 寿元上限只查表不存表：kRealmLifespan 是设定 §5.1 的逐字
        // 落地、唯一权威（刀 1 的 Mortal 注释）。判定走共享谓词——
        // 与 Economy 的「死者账消」同一判据，分叉就对不上账。
        if (lifespan_reached(*mortal, *cult))
        {
            // 边随死清理：关系边的两端都是活人的身份，死一个就要把
            // 挂在它身上的边收走。漏收的由年末 orphan_bonds 报出。
            if (const auto* id = world.get_if<SoulId>(e))
            {
                bonds_.erase_soul(id->index);
            }
            cmd.destroy(e);
        }
    }
}

BirthSystem::BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                         std::vector<gamev2::ecs::EntityId>& souls,
                         std::uint32_t& next_index,
                         const EconomyLedger& ledger,
                         std::uint32_t cell_count)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      next_index_(next_index),
      ledger_(ledger),
      cell_count_(cell_count)
{
}

void BirthSystem::execute(gamev2::ecs::World& world,
                          gamev2::ecs::CommandBuffer& cmd)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);

    // 各宗在册数：序号表里活着且有宗籍的。空 get_if = 已死。
    //
    // 已寿尽、destroy 还排在年末 flush 的人也在序号表里：只查 SoulId 会把
    // 死者算进在册数，收徒人数因此在除数边界上多收一人。
    std::array<std::uint32_t, 7> alive{};
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        const auto* mortal = world.get_if<Mortal>(e);
        const auto* cult = world.get_if<Cultivation>(e);
        if (id == nullptr || mortal == nullptr || cult == nullptr
            || lifespan_reached(*mortal, *cult))
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(id->sect);
        if (sect < alive.size())
        {
            ++alive[sect];
        }
    }

    auto recruit = [&](history::Sect sect, std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            // 序号从 next_index_ 起单调：初始队列的 index 全部小于它，
            // 新老按号可分（差异与补齐 §6）。姓名/属性与 make_person
            // 同一套派生。
            const std::uint32_t index = next_index_++;
            const PersonInit p =
                make_person(seed_, index, sect, Realm::yinqi);
            const gamev2::ecs::EntityId e = world.create();
            souls_.push_back(e);
            (void)cmd.add<SoulId>(e,
                                  SoulId{index, p.name, p.sex, p.sect});
            (void)cmd.add<Cultivation>(e, Cultivation{p.realm, 0u, 0u});
            (void)cmd.add<Aptitude>(e, p.aptitude);
            (void)cmd.add<Temper>(e, p.temper);
            (void)cmd.add<Mortal>(
                e, Mortal{clock->year - kMinAge, kMinAge});
            // 新入门是外门：晋升是政事段的事。
            (void)cmd.add<Office>(
                e, Office{sect == history::Sect::unowned
                              ? OfficeKind::none
                          : OfficeKind::outer,
                          0u, 0u});
            (void)cmd.add<Purse>(e, Purse{});
            (void)cmd.add<Standing>(e, Standing{});
            (void)cmd.add<Motive>(e, Motive{});
            // 落格：site_init 掷运，与开局那一队共用同一条子流但序号
            // 不同——同 seed 重跑逐位相同（设计 §4.1）。
            const std::uint32_t cell =
                life_roll(seed_, Roll::site_init, clock->year, index)
                    .below(cell_count_);
            (void)cmd.add<Site>(e, Site{cell});
        }
    };

    // 宗门年收徒：max(1, 在册 / 除数)。标定值，锚点见常数注释。
    for (std::size_t s = 0; s < alive.size(); ++s)
    {
        // 断供则整宗停收：连 max(1,·) 的下限也停——下限是防绝嗣，
        // 断供年饿死徒弟更糟（差异 14）。
        if (ledger_.shorted[s])
        {
            continue;
        }
        const std::uint32_t n = alive[s] / kSectRecruitDivisor;
        recruit(static_cast<history::Sect>(s), n > 0u ? n : 1u);
    }

    // 散修每十年多一批（设定 §8.3）。年份取模而非计数器：重放即
    // 存档（设计 §4.4），口径不依赖「跑没跑过上一个十年」。
    if (clock->year % kFreeBatchYears == 0u)
    {
        recruit(history::Sect::unowned, kFreeBatch);
    }
}

CultivateSystem::CultivateSystem(
    std::uint64_t seed, gamev2::ecs::EntityId clock,
    const std::vector<gamev2::ecs::EntityId>& souls,
    const std::vector<std::uint32_t>& thick_cells, EconomyLedger& ledger,
    const gamev2::world::World& world)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      thick_cells_(thick_cells),
      ledger_(ledger),
      world_(world)
{
}

void CultivateSystem::execute(gamev2::ecs::World& world,
                              gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);
    const auto tide = gamev2::world::compute_spirit_tide(clock->year);
    for (const gamev2::ecs::EntityId e : souls_)
    {
        auto* cult = world.get_if<Cultivation>(e);
        auto* mortal = world.get_if<Mortal>(e);
        const auto* apt = world.get_if<Aptitude>(e);
        const auto* site = world.get_if<Site>(e);
        if (cult == nullptr || mortal == nullptr || apt == nullptr)
        {
            continue;  // 死者留下的空位
        }
        if (cult->realm == Realm::dacheng)
        {
            continue;  // 大乘不再进（kRealmYears 的哨兵 0）
        }
        // 寿尽者不突破：Aging 已按此刻的境界判他今年死，销毁在年末
        // flush。若这里还让他突破，境界上移、寿元上限跟着变长，Economy
        // 再判就成了活人——发份额、烧灵石，到年末实体销毁、私囊在账外
        // 消失，守恒恒等式从此每年破一次（终审 B1）。死人不会突破。
        if (lifespan_reached(*mortal, *cult))
        {
            continue;
        }
        ++cult->progress;
        const auto realm = static_cast<std::size_t>(cult->realm);
        if (cult->progress < kRealmYears[realm])
        {
            continue;
        }
        // 元婴百年闭关（设定 §5.1 逐字）：金丹圆满者先锁百年，锁满
        // 才付账掷运。锁是一次性的——seclude_until 非零即已开过。
        if (cult->realm == Realm::jindan)
        {
            if (cult->seclude_until == 0u)
            {
                cult->seclude_until =
                    clock->year + kRealmYears[static_cast<std::size_t>(
                                             Realm::yuanying)];
                continue;
            }
            if (cult->seclude_until > clock->year)
            {
                continue;
            }
        }
        // 金丹要厚域位置约束（设计 §2.4）：不在厚域就不尝试、不付账。
        // 厚域格表随潮位走（谷底期全天下没有厚域），故这里查的是
        // 当年的表而不是世界的静态分档。
        if (cult->realm == Realm::zhuji && site != nullptr)
        {
            const auto& cells = thick_cells_;
            const bool thick =
                std::ranges::binary_search(cells, site->cell);
            if (!thick)
            {
                continue;
            }
        }
        // 付账：门槛是「尝试时付、成败都烧」。付不起就不尝试。
        const auto next = realm + 1u;
        if (next >= kBreakthroughCost.size())
        {
            continue;
        }
        const std::uint64_t cost = kBreakthroughCost[next];
        auto* purse = world.get_if<Purse>(e);
        if (purse == nullptr || purse->stone < cost)
        {
            continue;
        }
        purse->stone -= cost;
        // 门槛是「尝试时付、成败都烧」：付掉的即刻进烧毁账，否则私囊
        // 少的那一块没有对应的去处，恒等式从此年年破一次。
        ledger_.burned_total += cost;

        const auto chance = breakthrough_chance(apt->comprehension, tide.level);

        // 掷运按 (seed, 流号, 年, 实体槽号) 无状态派生：调整遍历次序
        // 不得换一个世界（设计 §4.1）。既有派生链一字不动——动了它，
        // 刀 2 的探针读数全体漂移。
        const auto stream =
            static_cast<std::uint32_t>(world::Stream::life);
        world::Pcg32 rng(world::stage_seed(
            world::stage_seed(world::stage_seed(seed_, stream),
                              clock->year),
            e.index()));
        if (static_cast<std::int32_t>(rng.below(1000u))
            < static_cast<std::int32_t>(chance))
        {
            cult->realm = static_cast<Realm>(next);
            cult->progress = 0u;
        }
        // 失败：年资保留，下一年再试。
    }
}

DecideSystem::DecideSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                           const std::vector<gamev2::ecs::EntityId>& souls,
                           const BondGraph& bonds,
                           const std::vector<LifeEvent>& events,
                           const std::vector<std::uint32_t>& thick_cells,
                           const std::vector<std::uint32_t>& unowned_veins,
                           const gamev2::world::World& world)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      bonds_(bonds),
      events_(events),
      thick_cells_(thick_cells),
      unowned_veins_(unowned_veins),
      world_(world)
{
}

void DecideSystem::execute(gamev2::ecs::World& world,
                           gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);
    const std::uint32_t year = clock->year;

    const std::vector<SoulSnapshot> people = census(world, souls_);
    if (people.empty())
    {
        return;
    }

    // 宗主索引：每宗一个，取境界最高的那个（政事段保证至多一个）。
    std::array<std::size_t, 8> masters{};
    masters.fill(kNoIndex);
    for (std::size_t i = 0; i < people.size(); ++i)
    {
        if (people[i].office != OfficeKind::sect_master)
        {
            continue;
        }
        const auto s = static_cast<std::size_t>(people[i].sect);
        if (s >= masters.size())
        {
            continue;
        }
        if (masters[s] == kNoIndex
            || people[i].realm > people[masters[s]].realm)
        {
            masters[s] = i;
        }
    }

    // 社交候选表：同宗优先（差异 9）。
    std::array<std::vector<std::uint32_t>, 8> sect_pool;
    std::vector<std::uint32_t> all_pool;
    all_pool.reserve(people.size());
    for (const auto& p : people)
    {
        all_pool.push_back(p.index);
        const auto s = static_cast<std::size_t>(p.sect);
        if (s < sect_pool.size())
        {
            sect_pool[s].push_back(p.index);
        }
    }

    // 受害事件（去年的）。判据 3 要读的是「谁在去年遭了难」，而 Decide
    // 排在 Act 之前——同年写入的事件在决策时尚不存在，读本年永远是
    // 空集。
    struct Harm
    {
        std::uint32_t victim = 0;
        std::uint32_t aggressor = 0;
    };
    std::vector<Harm> harms;
    for (const auto& ev : events_)
    {
        if (ev.year + 1u != year)
        {
            continue;
        }
        if (ev.kind != LifeEventKind::slain
            && ev.kind != LifeEventKind::expulsion)
        {
            continue;
        }
        if (ev.actor == 0u || ev.counterparty == 0u)
        {
            continue;
        }
        harms.push_back(Harm{ev.actor, ev.counterparty});
    }

    const std::vector<std::size_t> by_index = index_table(people);
    const auto alive_index = [&](std::uint32_t index)
    {
        return index < by_index.size() && by_index[index] != kNoIndex;
    };

    for (const auto& s : people)
    {
        auto* motive = world.get_if<Motive>(s.e);
        if (motive == nullptr)
        {
            continue;
        }
        // 一把掷运一把流：判据 4 的社交机会与夺位机会共用同一个数
        // （同一实体同年的「机会」是同一个数），社交对象与厚域落点
        // 从同一把流里接着取。
        gamev2::world::Pcg32 rng =
            life_roll(seed_, Roll::decide, year, s.index);
        const std::uint32_t chance = rng.below(1000u);
        Motive m{};

        // 判据 0：元婴锁。Motive 无法覆盖——闭关锁死是设定 §2.4 的
        // 硬约束，寻衅的掷运在 Act 侧。
        if (s.seclude_until > year)
        {
            m.kind = MotiveKind::seclude;
            m.arg = 0u;
            *motive = m;
            continue;
        }

        // 判据 0.5：厚域寻址。筑基者年资将满而不在厚域，先去占着。
        const auto zhuji_years =
            kRealmYears[static_cast<std::size_t>(Realm::zhuji)];
        if (s.realm == Realm::zhuji
            && s.progress + kThickSeekLead >= zhuji_years
            && !thick_cells_.empty()
            && !std::ranges::binary_search(thick_cells_, s.cell))
        {
            m.kind = MotiveKind::seclude;
            m.arg = thick_cells_[rng.below(
                static_cast<std::uint32_t>(thick_cells_.size()))];
            *motive = m;
            continue;
        }

        // 夺位机会：长老、悖逆、境界不低于宗主一档。状态驱动无性格
        // 门槛，但「敢不敢反」读 Temper 的 order 轴（设计 §2.3）。
        //
        // 它排在判据 1/2 之前：夺位是状态驱动的**机会**，不是需求。
        // 计划把它列在判据 5 之前，可判据 2（缺口）会把几乎所有人
        // （灵石不够烧就是缺口）先截走，悖逆长老根本走不到夺位那一行
        // ——计划自己的验收（50‰/年 × 合格长老数）与此矛盾。
        if (s.office == OfficeKind::elder && s.order <= 2u
            && chance < kContestChancePermille)
        {
            const auto sect = static_cast<std::size_t>(s.sect);
            if (sect < masters.size() && masters[sect] != kNoIndex)
            {
                const auto& master = people[masters[sect]];
                if (static_cast<std::size_t>(s.realm) + 1u
                    >= static_cast<std::size_t>(master.realm))
                {
                    m.kind = MotiveKind::contest;
                    m.arg = master.index;
                    *motive = m;
                    continue;
                }
            }
        }

        // 判据 1：寿元危机。剩余寿元不够熬到突破、又付不起门槛——
        // 这时才需要另找出路（差异 5：付得起者什么都不用做）。
        const auto realm = static_cast<std::size_t>(s.realm);
        const std::uint32_t lifespan = kRealmLifespan[realm];
        const std::uint32_t remaining =
            lifespan > s.age ? lifespan - s.age : 0u;
        const std::uint64_t gate =
            realm + 1u < kBreakthroughCost.size()
                ? kBreakthroughCost[realm + 1u]
                : 0u;
        if (remaining < kRealmYears[realm] && s.stone < gate)
        {
            if (s.greed > 3u && !unowned_veins_.empty())
            {
                m.kind = MotiveKind::poach;
                m.arg = unowned_veins_[rng.below(
                    static_cast<std::uint32_t>(unowned_veins_.size()))];
            }
            else
            {
                m.kind = MotiveKind::operate;
                m.arg = 0u;
            }
            *motive = m;
            continue;
        }

        // 判据 2：缺口（断供或灵石不够烧）。悖逆者盗采、守序者经营，
        // 中值续走。
        const std::uint64_t burn = kRealmBurn[realm];
        if (s.starved > 0u || s.stone < burn)
        {
            if (s.order <= 2u && !unowned_veins_.empty())
            {
                m.kind = MotiveKind::poach;
                m.arg = unowned_veins_[rng.below(
                    static_cast<std::uint32_t>(unowned_veins_.size()))];
                *motive = m;
                continue;
            }
            if (s.order >= 4u)
            {
                m.kind = MotiveKind::operate;
                m.arg = 0u;
                *motive = m;
                continue;
            }
        }

        // 判据 3：亲缘/师徒有难。勇烈者寻仇，怯懦者记仇，中值续走。
        std::uint32_t aggressor = 0u;
        for (const auto& h : harms)
        {
            if (!alive_index(h.aggressor))
            {
                continue;
            }
            // 加害者本人不算仇家：师徒或亲缘关系也可能是「加害者与受害
            // 者之间」的（师父杀了徒弟），此时凶手若拿这条边去找仇，
            // 寻到的就是自己——事件表里会落一条 actor == counterparty
            // 的寻仇，一场自己打自己的架。
            if (h.aggressor == s.index)
            {
                continue;
            }
            if (bonds_.find(s.index, h.victim, BondKind::master) != nullptr
                || bonds_.find(s.index, h.victim, BondKind::kin) != nullptr)
            {
                aggressor = h.aggressor;
                break;
            }
        }
        if (aggressor != 0u)
        {
            if (s.valor >= 4u)
            {
                m.kind = MotiveKind::avenge;
                m.arg = aggressor;
                *motive = m;
                continue;
            }
            if (s.valor <= 2u)
            {
                m.kind = MotiveKind::grudge;
                m.arg = aggressor;
                *motive = m;
                continue;
            }
        }

        // 判据 4：声望收益机会。仁厚者结善缘、酷烈者立威，中值续走。
        if (chance < kSocialChancePermille && (s.mercy >= 4u || s.mercy <= 2u))
        {
            const auto sect = static_cast<std::size_t>(s.sect);
            const std::vector<std::uint32_t>& pool =
                (sect < sect_pool.size() && sect_pool[sect].size() > 1u)
                    ? sect_pool[sect]
                    : all_pool;
            m.kind = MotiveKind::social;
            m.arg = pick_other(pool, s.index, rng);
            *motive = m;
            continue;
        }

        // 判据 5：默认。慵懒者闭关原地、进取者历练、中值修炼。
        if (s.vigor <= 2u)
        {
            m.kind = MotiveKind::seclude;
            m.arg = 0u;
        }
        else if (s.vigor >= 4u)
        {
            m.kind = MotiveKind::roam;
            m.arg = 0u;
        }
        else
        {
            m.kind = MotiveKind::cultivate;
            m.arg = 0u;
        }
        *motive = m;
    }
}

ActSystem::ActSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                     const std::vector<gamev2::ecs::EntityId>& souls,
                     BondGraph& bonds, std::vector<LifeEvent>& events,
                     const std::vector<std::uint32_t>& thick_cells,
                     EconomyLedger& ledger, LifeDiagnostics& diagnostics,
                     const gamev2::world::World& world)
    : seed_(seed),
      clock_(clock),
      souls_(souls),
      bonds_(bonds),
      events_(events),
      thick_cells_(thick_cells),
      ledger_(ledger),
      diagnostics_(diagnostics),
      world_(world)
{
}

void ActSystem::execute(gamev2::ecs::World& world,
                        gamev2::ecs::CommandBuffer& cmd)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);
    const std::uint32_t year = clock->year;
    const std::uint32_t cells =
        world_.params().width * world_.params().height;

    const std::vector<SoulSnapshot> people = census(world, souls_);
    if (people.empty())
    {
        return;
    }
    const std::vector<std::size_t> by_index = index_table(people);
    // 本年被横死者。销毁排在年末 flush，实体还在——不另记一份，
    // 死者会在同一年里继续行动（死了还能盗采）。
    std::vector<char> dead(people.size(), 0);
    const auto find_person = [&](std::uint32_t index) -> std::size_t
    {
        if (index >= by_index.size() || by_index[index] == kNoIndex)
        {
            return kNoIndex;
        }
        const auto pos = by_index[index];
        return dead[pos] != 0 ? kNoIndex : pos;
    };

    for (std::size_t pi = 0; pi < people.size(); ++pi)
    {
        if (dead[pi] != 0)
        {
            continue;
        }
        const SoulSnapshot& s = people[pi];
        const auto* motive = world.get_if<Motive>(s.e);
        if (motive == nullptr)
        {
            continue;
        }
        switch (motive->kind)
        {
        case MotiveKind::cultivate:
            break;  // 修炼无额外效果（Cultivate 已 +1）

        case MotiveKind::seclude:
        {
            auto* site = world.get_if<Site>(s.e);
            if (site != nullptr && motive->arg != 0u
                && motive->arg != site->cell)
            {
                site->cell = motive->arg;
            }
            // 闭关被寻衅：锁死百年的人与仇家共处一地，旧怨找上门。
            if (s.seclude_until > year)
            {
                const auto enemies =
                    bonds_.neighbors(s.index, BondKind::feud);
                std::size_t foe = kNoIndex;
                for (const auto idx : enemies)
                {
                    foe = find_person(idx);
                    if (foe != kNoIndex)
                    {
                        break;
                    }
                }
                if (foe != kNoIndex)
                {
                    auto rng =
                        life_roll(seed_, Roll::seclude_hazard, year, s.index);
                    auto* cult = world.get_if<Cultivation>(s.e);
                    auto* apt = world.get_if<Aptitude>(s.e);
                    auto* purse = world.get_if<Purse>(s.e);
                    auto* f_cult = world.get_if<Cultivation>(people[foe].e);
                    auto* f_apt = world.get_if<Aptitude>(people[foe].e);
                    auto* f_purse = world.get_if<Purse>(people[foe].e);
                    if (cult != nullptr && apt != nullptr && purse != nullptr
                        && f_cult != nullptr && f_apt != nullptr
                        && f_purse != nullptr
                        && rng.below(1000u) < kSecludeHazardPermille)
                    {
                        const bool won =
                            duel(seed_, year, *cult, *apt, purse->stone,
                                 *f_cult, *f_apt, f_purse->stone, s.index,
                                 people[foe].index);
                        if (won)
                        {
                            push_event(events_, LifeEventKind::slain, year,
                                       people[foe].index, s.index,
                                       people[foe].sect, 0u);
                            slay(world, cmd, people[foe].e,
                                 people[foe].index, bonds_, ledger_);
                            dead[foe] = 1;
                        }
                        else
                        {
                            push_event(events_, LifeEventKind::slain, year,
                                       s.index, people[foe].index, s.sect, 0u);
                            slay(world, cmd, s.e, s.index, bonds_,
                                 ledger_);
                            dead[pi] = 1;
                        }
                    }
                }
            }
            break;
        }

        case MotiveKind::roam:
        {
            auto* cult = world.get_if<Cultivation>(s.e);
            auto* site = world.get_if<Site>(s.e);
            if (cult != nullptr)
            {
                ++cult->progress;  // 历练加成
            }
            // 落点与遇险同一把流：先取落点、再取遇险，顺序固定，
            // 因此无状态且确定（差异 6）。
            auto rng = life_roll(seed_, Roll::roam, year, s.index);
            if (site != nullptr && cells > 0u)
            {
                site->cell = rng.below(cells);
            }
            if (rng.below(1000u) < kRoamHazardPermille)
            {
                push_event(events_, LifeEventKind::slain, year, s.index, 0u,
                           s.sect, 0u);
                auto* purse = world.get_if<Purse>(s.e);
                if (purse != nullptr)
                {
                    slay(world, cmd, s.e, s.index, bonds_, ledger_);
                    dead[pi] = 1;
                }
            }
            break;
        }

        case MotiveKind::operate:
        {
            // 经营 = 任务赏钱，年入约六十两（与散修营生同额）。
            //
            // 散修也走这一支：他们的「营生」就是经营。经济步的散修
            // 营生对「当年经营过的人」让位——两份收入各发一次，散修
            // 的年入就会翻倍，「三十三年一枚下品」的设定锚当场破掉。
            auto* purse = world.get_if<Purse>(s.e);
            if (purse != nullptr)
            {
                purse->silver += kFreeCultivatorIncomeLiang;
            }
            break;
        }

        case MotiveKind::poach:
        {
            auto* purse = world.get_if<Purse>(s.e);
            auto* standing = world.get_if<Standing>(s.e);
            if (purse == nullptr)
            {
                break;
            }
            // 私铸入账：石头直接进私囊，同时在铸币侧同增——恒等式
            // 两侧一起动，守恒不动（差异 7）。
            purse->stone += kPoachYieldStone;
            ledger_.minted_total += kPoachYieldStone;
            ledger_.poached_total += kPoachYieldStone;
            if (standing != nullptr)
            {
                bump_standing(
                    standing->by_sect[static_cast<std::size_t>(
                        history::Sect::unowned)],
                    -1);
            }
            push_event(events_, LifeEventKind::poach, year, s.index, 0u,
                       s.sect, kPoachYieldStone);
            // 被擒：合成执法者（境界 +1、体魄 5、法器档 1）。
            auto rng = life_roll(seed_, Roll::poach_find, year, s.index);
            if (rng.below(1000u) < kPoachCaughtPermille)
            {
                auto* cult = world.get_if<Cultivation>(s.e);
                auto* apt = world.get_if<Aptitude>(s.e);
                if (cult != nullptr && apt != nullptr)
                {
                    Cultivation guard = *cult;
                    const auto realm =
                        static_cast<std::size_t>(guard.realm);
                    guard.realm = static_cast<Realm>(
                        realm + 1u < kRealmLifespan.size() ? realm + 1u
                                                            : realm);
                    Aptitude guard_apt{};
                    guard_apt.body = 5u;
                    const std::uint64_t guard_purse = kArtifactGradeStone[1];
                    // 对手键恒 0：合成执法者不是灵魂，键位让出。
                    const bool won =
                        duel(seed_, year, *cult, *apt, purse->stone, guard,
                             guard_apt, guard_purse, s.index, 0u);
                    if (!won)
                    {
                        push_event(events_, LifeEventKind::slain, year,
                                   s.index, 0u, s.sect, 0u);
                        slay(world, cmd, s.e, s.index, bonds_,
                             ledger_);
                        dead[pi] = 1;
                    }
                    else if (standing != nullptr)
                    {
                        ++standing->crimes;
                    }
                }
            }
            break;
        }

        case MotiveKind::social:
        {
            const auto target = find_person(motive->arg);
            if (target == kNoIndex)
            {
                break;
            }
            const SoulSnapshot& t = people[target];
            auto* my_standing = world.get_if<Standing>(s.e);
            auto* his_standing = world.get_if<Standing>(t.e);
            if (my_standing == nullptr || his_standing == nullptr)
            {
                break;
            }
            // 立威只压得住弱者：仁厚者结善缘，酷烈者遇上弱者才立威。
            const bool bully =
                (s.mercy <= 2u) && (s.power > t.power);
            if (bully)
            {
                bump_standing(
                    my_standing->by_sect[static_cast<std::size_t>(s.sect)],
                    1);
                bump_standing(
                    his_standing->by_sect[static_cast<std::size_t>(s.sect)],
                    -1);
                push_event(events_, LifeEventKind::social, year, s.index,
                           t.index, s.sect, 2u);
                auto rng =
                    life_roll(seed_, Roll::grudge, year, s.index);
                if (rng.below(1000u) < kGrudgePermille)
                {
                    bonds_.tie(s.index, t.index, BondKind::feud, year);
                }
            }
            else
            {
                bonds_.tie(s.index, t.index, BondKind::favor, year);
                bump_standing(
                    my_standing->by_sect[static_cast<std::size_t>(s.sect)],
                    1);
                bump_standing(
                    his_standing->by_sect[static_cast<std::size_t>(t.sect)],
                    1);
                push_event(events_, LifeEventKind::social, year, s.index,
                           t.index, s.sect, 1u);
            }
            break;
        }

        case MotiveKind::grudge:
        {
            const auto target = find_person(motive->arg);
            if (target == kNoIndex)
            {
                break;
            }
            // 仇记在受害那年：因果方向因此钉死（仇在先、报在后）。
            const std::uint32_t harm_year =
                harm_year_of(events_, motive->arg, year);
            bonds_.tie(s.index, motive->arg, BondKind::feud, harm_year);
            break;
        }

        case MotiveKind::avenge:
        {
            const auto target = find_person(motive->arg);
            if (target == kNoIndex)
            {
                break;
            }
            const SoulSnapshot& t = people[target];
            const std::uint32_t harm_year =
                harm_year_of(events_, motive->arg, year);
            bonds_.tie(s.index, t.index, BondKind::feud, harm_year);
            auto* cult = world.get_if<Cultivation>(s.e);
            auto* apt = world.get_if<Aptitude>(s.e);
            auto* purse = world.get_if<Purse>(s.e);
            auto* f_cult = world.get_if<Cultivation>(t.e);
            auto* f_apt = world.get_if<Aptitude>(t.e);
            auto* f_purse = world.get_if<Purse>(t.e);
            if (cult == nullptr || apt == nullptr || purse == nullptr
                || f_cult == nullptr || f_apt == nullptr || f_purse == nullptr)
            {
                break;
            }
            const bool won =
                duel(seed_, year, *cult, *apt, purse->stone, *f_cult, *f_apt,
                     f_purse->stone, s.index, t.index);
            if (won)
            {
                push_event(events_, LifeEventKind::avenge, year, s.index,
                           t.index, s.sect, 1u);
                push_event(events_, LifeEventKind::slain, year, t.index,
                           s.index, t.sect, 0u);
                slay(world, cmd, t.e, t.index, bonds_, ledger_);
                dead[target] = 1;
                auto* my_standing = world.get_if<Standing>(s.e);
                if (my_standing != nullptr)
                {
                    bump_standing(my_standing->by_sect[
                                      static_cast<std::size_t>(s.sect)],
                                  1);
                }
            }
            else
            {
                // 败也要落事件：LifeEventKind::avenge 的字段语义是
                // 「amount = 1 胜 0 败」，败方不落事件的话「0」这一档
                // 就是死字段，判据 3 也变得不可观测。
                push_event(events_, LifeEventKind::avenge, year, s.index,
                           t.index, s.sect, 0u);
                push_event(events_, LifeEventKind::slain, year, s.index,
                           t.index, s.sect, 0u);
                slay(world, cmd, s.e, s.index, bonds_, ledger_);
                dead[pi] = 1;
            }
            break;
        }

        case MotiveKind::contest:
        {
            const auto target = find_person(motive->arg);
            if (target == kNoIndex)
            {
                break;
            }
            const SoulSnapshot& m = people[target];
            auto* my_office = world.get_if<Office>(s.e);
            auto* his_office = world.get_if<Office>(m.e);
            auto* my_id = world.get_if<SoulId>(s.e);
            auto* his_id = world.get_if<SoulId>(m.e);
            auto* my_standing = world.get_if<Standing>(s.e);
            auto* his_standing = world.get_if<Standing>(m.e);
            auto* my_cult = world.get_if<Cultivation>(s.e);
            auto* his_cult = world.get_if<Cultivation>(m.e);
            auto* my_temper = world.get_if<Temper>(s.e);
            auto* his_temper = world.get_if<Temper>(m.e);
            if (my_office == nullptr || his_office == nullptr
                || my_id == nullptr || his_id == nullptr
                || my_standing == nullptr || his_standing == nullptr
                || my_cult == nullptr || his_cult == nullptr
                || my_temper == nullptr || his_temper == nullptr)
            {
                break;
            }
            // 复核：位子可能已经换过人。
            if (my_office->kind != OfficeKind::elder
                || his_office->kind != OfficeKind::sect_master)
            {
                break;
            }
            const bool won =
                contest_won(seed_, year, *my_id, *my_cult, *my_standing,
                            *his_id, *his_cult, *his_standing);
            if (won)
            {
                my_office->kind = OfficeKind::sect_master;
                my_office->years = 0;
                his_office->kind = OfficeKind::none;
                his_office->years = 0;
                his_id->sect = history::Sect::unowned;
                bonds_.tie(s.index, m.index, BondKind::feud, year);
                push_event(events_, LifeEventKind::expulsion, year, m.index,
                           s.index, m.sect, 0u);
                push_event(events_, LifeEventKind::contest, year, s.index,
                           m.index, s.sect, 1u);
                // 离开秩序的人更悖逆。
                nudge(*his_temper, Axis::order, -1, year);
            }
            else if (his_temper->mercy >= 3u)
            {
                my_office->kind = OfficeKind::none;
                my_office->years = 0;
                my_id->sect = history::Sect::unowned;
                bonds_.tie(s.index, m.index, BondKind::feud, year);
                push_event(events_, LifeEventKind::expulsion, year, s.index,
                           m.index, s.sect, 0u);
                push_event(events_, LifeEventKind::contest, year, s.index,
                           m.index, s.sect, 0u);
                nudge(*my_temper, Axis::order, -1, year);
            }
            else
            {
                push_event(events_, LifeEventKind::slain, year, s.index,
                           m.index, s.sect, 0u);
                push_event(events_, LifeEventKind::contest, year, s.index,
                           m.index, s.sect, 0u);
                auto* purse = world.get_if<Purse>(s.e);
                if (purse != nullptr)
                {
                    slay(world, cmd, s.e, s.index, bonds_, ledger_);
                    dead[pi] = 1;
                }
            }
            break;
        }

        case MotiveKind::none:
            break;
        }
    }

    // ── 政事段：状态驱动，无性格门槛（差异 11）──
    std::array<std::vector<std::size_t>, 8> roster;
    for (std::size_t i = 0; i < people.size(); ++i)
    {
        if (dead[i] != 0)
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(people[i].sect);
        if (sect < roster.size())
        {
            roster[sect].push_back(i);
        }
    }

    // 在位年数
    for (const auto& p : people)
    {
        auto* office = world.get_if<Office>(p.e);
        if (office != nullptr && office->kind != OfficeKind::none)
        {
            ++office->years;
        }
    }

    // 断供出走：制度性逃生门。底层先走——宗门养不起你，你自去。
    for (std::size_t sect = 0; sect < roster.size(); ++sect)
    {
        for (const auto i : roster[sect])
        {
            const SoulSnapshot& p = people[i];
            if (p.office != OfficeKind::outer)
            {
                continue;
            }
            if (p.starved < kStarveExitYears)
            {
                continue;
            }
            auto* id = world.get_if<SoulId>(p.e);
            auto* office = world.get_if<Office>(p.e);
            auto* temper = world.get_if<Temper>(p.e);
            if (id == nullptr || office == nullptr || temper == nullptr
                || temper->order > 2u)
            {
                continue;
            }
            std::size_t master = kNoIndex;
            for (const auto j : roster[sect])
            {
                if (people[j].office == OfficeKind::sect_master)
                {
                    master = j;
                    break;
                }
            }
            id->sect = history::Sect::unowned;
            office->kind = OfficeKind::none;
            office->years = 0;
            if (master != kNoIndex)
            {
                bonds_.tie(p.index, people[master].index, BondKind::feud,
                           year);
            }
            push_event(events_, LifeEventKind::departure, year, p.index,
                       master != kNoIndex ? people[master].index : 0u, p.sect,
                       0u);
            nudge(*temper, Axis::order, -1, year);
        }
    }

    // 晋升阶梯：外门→内门→长老（设计 §2.3：境界够了就晋升）。
    for (std::size_t sect = 0; sect < roster.size(); ++sect)
    {
        if (sect >= 7u)
        {
            continue;  // 散修圈不设职位阶梯
        }
        const auto n = static_cast<std::uint32_t>(roster[sect].size());
        if (n == 0u)
        {
            continue;
        }
        const std::uint32_t inner_quota = n * kInnerNum / kInnerDen;
        const std::uint32_t elder_quota =
            std::max(kMinElders, n / kElderDivisor);
        std::uint32_t inner = 0;
        std::uint32_t elder = 0;
        for (const auto i : roster[sect])
        {
            if (people[i].office == OfficeKind::inner)
            {
                ++inner;
            }
            else if (people[i].office == OfficeKind::elder)
            {
                ++elder;
            }
        }
        // 候选：境界最高者先上，同境界取年资最足者，再同则按序号
        // （序号是身位的稳定标识，逐位可复算）。
        //
        // 宗籍按**当下**的 SoulId 复核：出走在政事段的前半段已经
        // 把人改成了散修，席位表还是旧快照——照快照选人会把一个已经
        // 离宗的人提拔成长老，职位有宗门而人没有（office_without_sect）。
        const auto best_candidate = [&](OfficeKind from, Realm floor)
        {
            std::size_t best = kNoIndex;
            for (const auto i : roster[sect])
            {
                const SoulSnapshot& p = people[i];
                if (p.office != from
                    || static_cast<std::size_t>(p.realm)
                           < static_cast<std::size_t>(floor))
                {
                    continue;
                }
                const auto* id = world.get_if<SoulId>(p.e);
                if (id == nullptr
                    || static_cast<std::size_t>(id->sect) != sect)
                {
                    continue;
                }
                if (best == kNoIndex
                    || p.realm > people[best].realm
                    || (p.realm == people[best].realm
                        && p.progress > people[best].progress)
                    || (p.realm == people[best].realm
                        && p.progress == people[best].progress
                        && p.index < people[best].index))
                {
                    best = i;
                }
            }
            return best;
        };
        if (inner < inner_quota)
        {
            const auto cand = best_candidate(OfficeKind::outer, Realm::lianqi);
            if (cand != kNoIndex)
            {
                auto* office = world.get_if<Office>(people[cand].e);
                if (office != nullptr)
                {
                    office->kind = OfficeKind::inner;
                    office->years = 0;
                    push_event(events_, LifeEventKind::promotion, year,
                               people[cand].index, 0u, people[cand].sect,
                               static_cast<std::uint32_t>(OfficeKind::inner));
                }
                ++inner;
            }
        }
        if (elder < elder_quota)
        {
            const auto cand = best_candidate(OfficeKind::inner, Realm::zhuji);
            if (cand != kNoIndex)
            {
                auto* office = world.get_if<Office>(people[cand].e);
                if (office != nullptr)
                {
                    office->kind = OfficeKind::elder;
                    office->years = 0;
                    push_event(events_, LifeEventKind::promotion, year,
                               people[cand].index, 0u, people[cand].sect,
                               static_cast<std::uint32_t>(OfficeKind::elder));
                }
                ++elder;
            }
        }
    }

    // 宗主缺位 → 长老团推举。
    for (std::size_t sect = 0; sect < roster.size() && sect < 7u; ++sect)
    {
        bool has_master = false;
        for (const auto i : roster[sect])
        {
            if (people[i].office == OfficeKind::sect_master)
            {
                has_master = true;
                break;
            }
        }
        if (has_master)
        {
            continue;
        }
        // score = 境界×10 + 门内声望 + 资历（年龄/50）+ 掷运噪声 ±2。
        // 同分时亲缘边多的优先（设计 §2.3 读亲缘；死者边已随死清掉，
        // 亲缘只在活人之间当 tiebreak）。
        std::size_t best = kNoIndex;
        std::int32_t best_score = 0;
        std::size_t best_kin = 0;
        for (const auto i : roster[sect])
        {
            const SoulSnapshot& p = people[i];
            if (p.office != OfficeKind::elder
                && p.office != OfficeKind::inner
                && p.office != OfficeKind::outer)
            {
                continue;
            }
            const auto* id = world.get_if<SoulId>(p.e);
            if (id == nullptr
                || static_cast<std::size_t>(id->sect) != sect)
            {
                continue;  // 宗籍已变（出走）：不再有推举资格
            }
            auto rng = life_roll(seed_, Roll::succession, year, p.index);
            const std::int32_t noise =
                static_cast<std::int32_t>(rng.below(5u)) - 2;
            const auto* standing = world.get_if<Standing>(p.e);
            const std::int32_t rep =
                standing != nullptr
                    ? static_cast<std::int32_t>(
                          standing->by_sect[static_cast<std::size_t>(
                              p.sect)])
                    : 0;
            const std::int32_t score =
                static_cast<std::int32_t>(p.realm) * 10 + rep + 5
                + static_cast<std::int32_t>(p.age / 50u) + noise;
            const auto kin = bonds_.neighbors(p.index, BondKind::kin).size();
            if (best == kNoIndex || score > best_score
                || (score == best_score && kin > best_kin)
                || (score == best_score && kin == best_kin
                    && p.index < people[best].index))
            {
                best = i;
                best_score = score;
                best_kin = kin;
            }
        }
        if (best == kNoIndex)
        {
            continue;
        }
        auto* office = world.get_if<Office>(people[best].e);
        if (office != nullptr)
        {
            office->kind = OfficeKind::sect_master;
            office->years = 0;
            push_event(events_, LifeEventKind::succession, year,
                       people[best].index, 0u, people[best].sect, 0u);
        }
    }

    // 年末孤儿边诊断：关系边的两端必须都还活着。正常路径上死亡即清边，
    // 这里数的是漏网的那几条。
    {
        std::vector<char> alive_index(by_index.size(), 0);
        for (std::size_t i = 0; i < people.size(); ++i)
        {
            if (dead[i] == 0)
            {
                alive_index[people[i].index] = 1;
            }
        }
        for (auto it = bonds_.edges.begin(); it != bonds_.edges.end();)
        {
            if (alive_index[it->a] == 0 || alive_index[it->b] == 0)
            {
                ++diagnostics_.orphan_bonds;
                it = bonds_.edges.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

}  // namespace gamev2::life

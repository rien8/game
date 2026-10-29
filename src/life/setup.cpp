/// @file setup.cpp
/// @brief 众生层：从当代格局建实体、回读名册。
#include "gamev2/life/interface.hpp"
// 私有头与本文件同目录，不在也不该在 life 目标的包含路径里。
#include "generate.hpp"
#include "systems.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gamev2::life {

namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

}  // namespace

Life::Life(const gamev2::world::World& world,
           const gamev2::faction::EraReport& era,
           LifeDiagnostics& diagnostics)
    : world_(world),
      era_(era),
      diagnostics_(diagnostics)
{
}

void Life::seed()
{
    // 当代格局是众生的出身地：没有当代（台账为空）就没有可初始化的
    // 众生。这是退化不是错误，由诊断报出（设计 §7）。
    //
    // 空格局不建时钟，但年号仍要落位：名册是玩家读得到的对象，year()
    // 在没有时钟时返回的就是它，缺了它连「现在哪一年」都答不上。
    if (era_.veins.empty())
    {
        ++diagnostics_.no_contemporary_state;
        roster_.year = era_.now;
        return;
    }

    const std::vector<PersonInit> people =
        generate_population(world_.params().seed);
    const std::uint32_t cell_count =
        world_.params().width * world_.params().height;

    souls_.reserve(people.size());
    for (std::uint32_t i = 0; i < people.size(); ++i)
    {
        const PersonInit& p = people[i];
        const gamev2::ecs::EntityId e = ecs_world_.create();
        souls_.push_back(e);
        (void)ecs_world_.add<SoulId>(e, SoulId{i, p.name, p.sex, p.sect});
        (void)ecs_world_.add<Cultivation>(e, Cultivation{p.realm});
        (void)ecs_world_.add<Aptitude>(e, p.aptitude);
        (void)ecs_world_.add<Temper>(e, p.temper);
        (void)ecs_world_.add<Mortal>(e, Mortal{era_.now - p.age, p.age});
        (void)ecs_world_.add<Office>(e, Office{p.office});
        (void)ecs_world_.add<Purse>(e, Purse{});
        (void)ecs_world_.add<Standing>(e, Standing{});
        (void)ecs_world_.add<Motive>(e, Motive{});
        // 落格：site_init 掷运，与新生儿同一条子流但序号不同——同
        // seed 重跑逐位相同，且初始 970 人不会挤在同一个格。
        const std::uint32_t cell =
            life_roll(world_.params().seed, Roll::site_init, era_.now, i)
                .below(cell_count);
        (void)ecs_world_.add<Site>(e, Site{cell});
    }

    // 厚域格表：厚域格的线性格号（升序）。
    //
    // 世界的 domain_grade 是**潮位缩放之后**的分档：256²/seed13 开局
    // 正落在谷底潮位（level 33），全图没有一格够得上厚域。照那张静态
    // 表读，金丹门槛会永远锁死、金字塔冻结在筑基，招牌④的人口随潮
    // 升降也就无从谈起。厚域因此按当前潮位重算：浓度场本来就是潮位
    // 缩放出来的，读的时候必须带上当年的潮位。
    refresh_thick_cells(era_.now);

    // 无主脉表：盗采的目标（含枯脉残余）。空表则无人得起盗采心。
    for (const auto& vein : era_.veins)
    {
        if (vein.owner == history::Sect::unowned)
        {
            unowned_veins_.push_back(vein.vein);
        }
    }

    // 名册从实体回读，不从 PersonInit 抄：这样「组件真的挂上了」是
    // 被名册测试顺手钉住的事实（照 faction collect() 的先例）。
    roster_.year = era_.now;
    roster_.entries.reserve(souls_.size());
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = ecs_world_.get_if<SoulId>(e);
        const auto* cult = ecs_world_.get_if<Cultivation>(e);
        const auto* apt = ecs_world_.get_if<Aptitude>(e);
        const auto* temper = ecs_world_.get_if<Temper>(e);
        const auto* mortal = ecs_world_.get_if<Mortal>(e);
        const auto* office = ecs_world_.get_if<Office>(e);
        // 这六件都是本函数亲手挂上的：缺任何一件是内部错误，不是要
        // 兜住的运行时情形。
        assert(id != nullptr && cult != nullptr && apt != nullptr
               && temper != nullptr && mortal != nullptr
               && office != nullptr);

        RosterEntry entry;
        entry.soul = id->index;
        entry.name = id->name;
        entry.sex = id->sex;
        entry.sect = id->sect;
        entry.office = office->kind;
        entry.realm = cult->realm;
        entry.age = mortal->age;
        entry.temper = *temper;
        entry.aptitude = *apt;
        roster_.entries.push_back(entry);
    }

    // 初始关系网：宗主收长老为徒、长老带弟子、门内结亲缘、散修圈另有
    // 亲缘对。边由 bonds_init 子流派生——同 seed 逐位相同。
    {
        std::array<std::vector<std::size_t>, 8> sect_members;
        for (std::size_t i = 0; i < souls_.size(); ++i)
        {
            const auto* id = ecs_world_.get_if<SoulId>(souls_[i]);
            if (id == nullptr)
            {
                continue;
            }
            const auto sect = static_cast<std::size_t>(id->sect);
            if (sect < sect_members.size())
            {
                sect_members[sect].push_back(i);
            }
        }
        const auto index_of = [&](std::size_t i)
        {
            return ecs_world_.get_if<SoulId>(souls_[i])->index;
        };
        std::uint32_t key = 0;
        for (std::size_t s = 0; s < sect_members.size(); ++s)
        {
            const auto& members = sect_members[s];
            if (members.empty())
            {
                continue;
            }
            std::size_t master = kNoIndex;
            std::vector<std::size_t> elders;
            std::vector<std::size_t> disciples;
            for (const auto i : members)
            {
                const auto* office = ecs_world_.get_if<Office>(souls_[i]);
                if (office == nullptr)
                {
                    continue;
                }
                if (office->kind == OfficeKind::sect_master)
                {
                    master = i;
                }
                else if (office->kind == OfficeKind::elder)
                {
                    elders.push_back(i);
                }
                else if (office->kind == OfficeKind::inner
                         || office->kind == OfficeKind::outer)
                {
                    disciples.push_back(i);
                }
            }
            if (master != kNoIndex)
            {
                for (const auto e : elders)
                {
                    bonds_.tie(index_of(master), index_of(e),
                               BondKind::master, era_.now)
                        .strength = 3u;
                }
                // 每长老带在册/40 名内门外门弟子。
                const auto per = static_cast<std::uint32_t>(members.size())
                                 / 40u;
                for (const auto e : elders)
                {
                    for (std::uint32_t j = 0; j < per; ++j)
                    {
                        const auto pick =
                            life_roll(world_.params().seed,
                                      Roll::bonds_init, era_.now, key++)
                                .below(static_cast<std::uint32_t>(
                                    disciples.size()));
                        bonds_.tie(index_of(e),
                                   index_of(disciples[pick]),
                                   BondKind::master, era_.now)
                            .strength = 2u;
                    }
                }
            }
            // 亲缘对：在册/20。
            const auto pairs = static_cast<std::uint32_t>(members.size())
                               / 20u;
            for (std::uint32_t j = 0; j < pairs; ++j)
            {
                const auto a =
                    life_roll(world_.params().seed, Roll::bonds_init,
                              era_.now, key++)
                        .below(static_cast<std::uint32_t>(members.size()));
                const auto b =
                    life_roll(world_.params().seed, Roll::bonds_init,
                              era_.now, key++)
                        .below(static_cast<std::uint32_t>(members.size()));
                if (a == b)
                {
                    continue;
                }
                bonds_.tie(index_of(members[a]), index_of(members[b]),
                           BondKind::kin, era_.now)
                    .strength = 3u;
            }
        }
    }

    // 铸币表：当代活脉的年产能 × kVeinToStone（口径裁定 1：快照代替
    // 派系层逐年产出，刀 5 换 FactionOutcome）。枯脉不产——balance
    // 为 0 或已标枯年就是死了，再铸就是给死人发饷。
    for (const auto& vein : era_.veins)
    {
        if (vein.balance == 0u || vein.depleted_year != 0u)
        {
            continue;
        }
        const auto rank = static_cast<std::size_t>(vein.rank);
        const auto owner = static_cast<std::size_t>(vein.owner);
        economy_.mint_per_year[owner] += kRankAnnualYield[rank]
                                         * kVeinToStone;
    }

    // 时钟从当代起算：开局即 8914，run_years 每年推一格。
    clock_ = ecs_world_.create();
    (void)ecs_world_.add<LifeClock>(clock_, LifeClock{era_.now});
    // 新生序号从初始人口总数起：初始队列的 index 全部小于它。
    next_index_ = static_cast<std::uint32_t>(roster_.entries.size());
}

void Life::refresh_thick_cells(std::uint32_t year)
{
    thick_cells_.clear();
    const auto world_tide = world_.spirit_tide.level;
    const auto tide = gamev2::world::compute_spirit_tide(year).level;
    if (world_tide == 0u || tide == 0u)
    {
        return;
    }
    // 世界的浓度场是潮位缩放过的：domain_concentration = 势 × 潮位/255。
    // 换算回当年的浓度，够 kThickThreshold 的格就是当年的厚域。
    for (std::uint32_t i = 0; i < world_.domain_concentration.size(); ++i)
    {
        const auto effective = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(world_.domain_concentration.at(i))
            * tide / world_tide);
        if (effective >= gamev2::world::kThickThreshold)
        {
            thick_cells_.push_back(i);
        }
    }
}

std::uint32_t Life::year()
{
    // 空格局下 seed 不建时钟（没有众生可推演），名册里的年号就是唯一
    // 的时间来源。有时钟时以时钟为准：它才是逐年推进的那一份。
    auto* clock = ecs_world_.get_if<LifeClock>(clock_);
    return clock != nullptr ? clock->year : roster_.year;
}

void Life::run_years(std::uint32_t n)
{
    // 没有时钟就没有年循环可推进：空格局是退化态，不是让七个 system
    // 空转一遍再崩在解引用上。
    if (ecs_world_.get_if<LifeClock>(clock_) == nullptr)
    {
        return;
    }

    // 注册顺序即执行顺序（Scheduler::plan 按 order 稳定排序后贪心分层，
    // 冲突者落后一批）：年循环七步 Clock→Aging→Birth→Cultivate→
    // Decide→Act→Economy。七步两两冲突（读写集相交），因此各占一批、
    // 严格串行——串行是刻意的：本层规模下并行零收益，each_parallel
    // 的行为又依赖批内位置。
    const std::uint32_t cell_count =
        world_.params().width * world_.params().height;
    ecs::Scheduler scheduler(1);
    ClockSystem clock(clock_);
    AgingSystem aging(souls_, bonds_);
    BirthSystem birth(world_.params().seed, clock_, souls_, next_index_,
                      economy_, cell_count);
    CultivateSystem cultivate(world_.params().seed, clock_, souls_,
                               thick_cells_, economy_, world_);
    DecideSystem decide(world_.params().seed, clock_, souls_, bonds_, events_,
                        thick_cells_, unowned_veins_, world_);
    ActSystem act(world_.params().seed, clock_, souls_, bonds_, events_,
                  thick_cells_, economy_, diagnostics_, world_);
    EconomySystem economy(clock_, souls_, economy_, diagnostics_);
    scheduler.add(clock);
    scheduler.add(aging);
    scheduler.add(birth);
    scheduler.add(cultivate);
    scheduler.add(decide);
    scheduler.add(act);
    scheduler.add(economy);
    scheduler.plan();

    ecs::CommandBuffer cmd;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        // 厚域格表随潮位走：ClockSystem 把年推进一格，本年读到的就是
        // 新年号的潮位。表在年初刷新，system 读的是同一份。
        auto* clock = ecs_world_.get_if<LifeClock>(clock_);
        refresh_thick_cells(clock->year + 1u);
        scheduler.run(ecs_world_, cmd);
    }
}

}  // namespace gamev2::life

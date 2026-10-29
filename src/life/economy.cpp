/// @file economy.cpp
/// @brief 众生层：年循环的经济步——铸币、分配、营生、买入、烧毁、
/// 死者账消、守恒校验。
#include "systems.hpp"
#include "gamev2/life/constants.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace gamev2::life {

namespace {

/// 活人判据：组件齐全且寿元未尽。Aging 的 destroy 排在年末 flush，
/// 经济步在同一帧里仍看得见寿尽者——散修营生与买入都必须过这一关，
/// 否则死者会领银两、把公账灵石买走后立刻烧掉。
[[nodiscard]] bool is_alive(gamev2::ecs::World& world,
                            const gamev2::ecs::EntityId e)
{
    const auto* mortal = world.get_if<Mortal>(e);
    const auto* cult = world.get_if<Cultivation>(e);
    return mortal != nullptr && cult != nullptr
           && !lifespan_reached(*mortal, *cult);
}

/// 某宗某职级的活人。分配的两趟（数人与发份额）必须用同一套判据，
/// 否则 per-head 会除错人数——抽成共享谓词就是为了杜绝分叉。
[[nodiscard]] bool alive_member(gamev2::ecs::World& world,
                                const gamev2::ecs::EntityId e,
                                std::size_t sect, std::size_t cls)
{
    const auto* id = world.get_if<SoulId>(e);
    const auto* office = world.get_if<Office>(e);
    const auto* mortal = world.get_if<Mortal>(e);
    const auto* cult = world.get_if<Cultivation>(e);
    if (id == nullptr || office == nullptr || mortal == nullptr
        || cult == nullptr || world.get_if<Purse>(e) == nullptr)
    {
        return false;
    }
    return static_cast<std::size_t>(id->sect) == sect
           && static_cast<std::size_t>(office->kind) == cls
           && !lifespan_reached(*mortal, *cult);
}

}  // namespace

EconomySystem::EconomySystem(gamev2::ecs::EntityId clock,
                             const std::vector<gamev2::ecs::EntityId>& souls,
                             EconomyLedger& ledger,
                             LifeDiagnostics& diagnostics)
    : clock_(clock),
      souls_(souls),
      ledger_(ledger),
      diagnostics_(diagnostics)
{
}

void EconomySystem::execute(gamev2::ecs::World& world,
                            gamev2::ecs::CommandBuffer&)
{
    const auto* clock = world.get_if<LifeClock>(clock_);
    assert(clock != nullptr);  // seed 亲手挂的：缺了是内部错误
    (void)clock;

    // 铸币：快照表逐年入账（口径裁定 1，刀 5 换产出清单）。
    for (std::size_t s = 0; s < ledger_.vault.size(); ++s)
    {
        ledger_.vault[s] += ledger_.mint_per_year[s];
        ledger_.minted_total += ledger_.mint_per_year[s];
    }

    // 断供标记每年开头全清再置：shorted 说的是「上年有没有克扣」。
    for (auto& flag : ledger_.shorted)
    {
        flag = false;
    }

    // 分配（设计 §3.3）：Office 切蛋糕，支付次序 = 宗主→长老→内门→
    // 外门。金库不足时按类均分摊薄——外门先饿、内门次之、长老宗主
    // 不动。同类内均分（同级同酬）：整除不足额，余数留库；不引入
    // 「先入门先拿钱」的创建序偏差。被克扣者记断供年数（连续计数，
    // 足额清零）——断供既触发判据 2 的搞钱，也触发政事段的出走。
    for (std::size_t sect = 0; sect < ledger_.vault.size(); ++sect)
    {
        for (std::size_t cls = 0; cls < kOfficeShare.size(); ++cls)
        {
            const auto want =
                static_cast<std::uint64_t>(kOfficeShare[cls]);
            std::uint32_t members = 0;
            for (const gamev2::ecs::EntityId e : souls_)
            {
                if (alive_member(world, e, sect, cls))
                {
                    ++members;
                }
            }
            if (members == 0u)
            {
                continue;
            }
            const auto need = want * members;
            const bool shorted = ledger_.vault[sect] < need;
            const std::uint64_t per_head =
                shorted ? ledger_.vault[sect] / members : want;
            ledger_.vault[sect] -= per_head * members;
            if (shorted)
            {
                ledger_.shorted[sect] = true;
            }
            for (const gamev2::ecs::EntityId e : souls_)
            {
                if (!alive_member(world, e, sect, cls))
                {
                    continue;
                }
                auto* purse = world.get_if<Purse>(e);
                auto* office = world.get_if<Office>(e);
                if (purse == nullptr || office == nullptr)
                {
                    continue;
                }
                purse->stone += per_head;
                if (shorted)
                {
                    ++office->starved;
                }
                else
                {
                    office->starved = 0;
                }
            }
        }
    }

    // 散修营生（设计 §3.4，设定 §6.4 逐字）：年入约六十两入银两账。
    // 银两是凡人货币，不进灵石恒等式（口径裁定 8）。
    //
    // 当年「经营」过的人已经拿了任务赏钱，这里让位：两份各发一次，
    // 散修的年入就翻倍，「三十三年一枚下品」的设定锚当场破掉。出走
    // 的人因此也不会在离宗之年同时领到宗门赏钱与散修营生。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        const auto* motive = world.get_if<Motive>(e);
        if (id == nullptr || purse == nullptr || motive == nullptr
            || !is_alive(world, e))
        {
            continue;
        }
        if (id->sect == gamev2::history::Sect::unowned
            && motive->kind != MotiveKind::operate)
        {
            purse->silver += kFreeCultivatorIncomeLiang;
        }
    }

    // 攒够一枚就从本宗金库买进（固定换价，不是市场——设计 §3.1）。
    // 散修照旧走无主公账。这一步是划转不是铸币：金库与私囊在恒等式
    // 的两侧同增同减，守恒不动。买不进时银两原样留到明年，不许凭空
    // 铸币。宗门弟子能从自家金库买，是「守序者做任务营生攒钱续命」
    // 的经济落点（差异 15）。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        if (id == nullptr || purse == nullptr || !is_alive(world, e))
        {
            continue;
        }
        const auto sect = static_cast<std::size_t>(id->sect);
        const std::size_t vault_index =
            id->sect == gamev2::history::Sect::unowned ? 7u : sect;
        if (vault_index >= ledger_.vault.size())
        {
            continue;
        }
        while (purse->silver >= kLiangPerStone
               && ledger_.vault[vault_index] > 0u)
        {
            purse->silver -= kLiangPerStone;
            purse->stone += 1u;
            ledger_.vault[vault_index] -= 1u;
        }
    }

    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* cult = world.get_if<Cultivation>(e);
        const auto* mortal = world.get_if<Mortal>(e);
        auto* purse = world.get_if<Purse>(e);
        if (cult == nullptr || mortal == nullptr || purse == nullptr)
        {
            continue;  // 死者留下的空位
        }
        if (lifespan_reached(*mortal, *cult))
        {
            // 死者账消（口径裁定 13）：私囊随死烧毁入账。实体销毁会把
            // 私囊连同恒等式的一侧一起带走——先清账再等 Aging 的
            // destroy 落地，否则死亡当年守恒就破一块。刀 6 换成
            // 「按 Bonds 分给继承人」。
            ledger_.burned_total += purse->stone;
            purse->stone = 0;
            continue;
        }
        // 烧毁：设定 §6.1 的吸纳表逐字。囊中不足就烧多少算多少——
        // 不足额只是「今年没吃够」，不追缴不记账，恒等式因此恒成立。
        const auto realm = static_cast<std::size_t>(cult->realm);
        const std::uint64_t due = kRealmBurn[realm];
        const std::uint64_t take =
            purse->stone < due ? purse->stone : due;
        purse->stone -= take;
        ledger_.burned_total += take;
    }

    // 守恒校验（设计 §3.5）：逐年人工对账，破一次记一次。
    std::uint64_t accounts = ledger_.vault_total();
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* purse = world.get_if<Purse>(e);
        if (purse != nullptr)
        {
            accounts += purse->stone;
        }
    }
    if (accounts != ledger_.minted_total - ledger_.burned_total)
    {
        ++diagnostics_.stone_conservation_broken;
    }

    // 内部一致性（设计 §7）：有职位但无宗门归属。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        const auto* office = world.get_if<Office>(e);
        if (id == nullptr || office == nullptr)
        {
            continue;
        }
        if (office->kind != OfficeKind::none
            && id->sect == gamev2::history::Sect::unowned)
        {
            ++diagnostics_.office_without_sect;
        }
    }
}

}  // namespace gamev2::life

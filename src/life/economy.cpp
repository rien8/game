/// @file economy.cpp
/// @brief 众生层：年循环的经济步——铸币、分配、营生、买入、烧毁、
/// 死者账消、守恒校验。
#include "systems.hpp"
#include "gamev2/life/constants.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace gamev2::life {

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

    // 分配（设计 §3.3）：Office 切蛋糕，支付次序 = 宗主→长老→内门→
    // 外门。金库不足时按类均分摊薄——外门先饿、内门次之、长老宗主
    // 不动。同类内均分（同级同酬）：整除不足额，余数留库；不引入
    // 「先入门先拿钱」的创建序偏差。被克扣者不记欠账（口径裁定 5：
    // 欠账是刀 4 判据 2 的事，本刀不做半套）。
    for (std::size_t sect = 0; sect < ledger_.vault.size(); ++sect)
    {
        for (std::size_t cls = 0; cls < kOfficeShare.size(); ++cls)
        {
            const auto want =
                static_cast<std::uint64_t>(kOfficeShare[cls]);
            // 一数二发：两趟必须用同一套存活判据，否则 per-head 会
            // 除错人数。判据 = 组件齐全（SoulId/Mortal/Cultivation/
            // Purse 非空）+ 宗门职位对得上 + 寿元未尽。
            std::uint32_t members = 0;
            for (const gamev2::ecs::EntityId e : souls_)
            {
                const auto* id = world.get_if<SoulId>(e);
                const auto* office = world.get_if<Office>(e);
                const auto* mortal = world.get_if<Mortal>(e);
                const auto* cult = world.get_if<Cultivation>(e);
                if (id == nullptr || office == nullptr || mortal == nullptr
                    || cult == nullptr
                    || world.get_if<Purse>(e) == nullptr)
                {
                    continue;
                }
                if (static_cast<std::size_t>(id->sect) != sect
                    || static_cast<std::size_t>(office->kind) != cls
                    || lifespan_reached(*mortal, *cult))
                {
                    continue;
                }
                ++members;
            }
            if (members == 0u)
            {
                continue;
            }
            const auto need = want * members;
            const std::uint64_t per_head =
                ledger_.vault[sect] >= need
                    ? want
                    : ledger_.vault[sect] / members;
            ledger_.vault[sect] -= per_head * members;
            for (const gamev2::ecs::EntityId e : souls_)
            {
                const auto* id = world.get_if<SoulId>(e);
                const auto* office = world.get_if<Office>(e);
                const auto* mortal = world.get_if<Mortal>(e);
                const auto* cult = world.get_if<Cultivation>(e);
                auto* purse = world.get_if<Purse>(e);
                if (id == nullptr || office == nullptr || mortal == nullptr
                    || cult == nullptr || purse == nullptr)
                {
                    continue;
                }
                if (static_cast<std::size_t>(id->sect) != sect
                    || static_cast<std::size_t>(office->kind) != cls
                    || lifespan_reached(*mortal, *cult))
                {
                    continue;
                }
                purse->stone += per_head;
            }
        }
    }

    // 散修营生（设计 §3.4，设定 §6.4 逐字）：年入约六十两入银两账。
    // 银两是凡人货币，不进灵石恒等式（口径裁定 8）。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        if (id == nullptr || purse == nullptr)
        {
            continue;
        }
        if (id->sect == gamev2::history::Sect::unowned)
        {
            purse->silver += kFreeCultivatorIncomeLiang;
        }
    }

    // 攒够一枚就从无主金库买进（固定换价，不是市场——设计 §3.1）。
    // 公账为 0 买不进：银两原样留到明年，不许凭空铸币。参考世界
    // 无主脉全枯，买入分支因此只走到「不成交」侧；把灵石送进公账
    // 是刀 4 盗采的事。
    for (const gamev2::ecs::EntityId e : souls_)
    {
        const auto* id = world.get_if<SoulId>(e);
        auto* purse = world.get_if<Purse>(e);
        if (id == nullptr || purse == nullptr)
        {
            continue;
        }
        if (id->sect != gamev2::history::Sect::unowned)
        {
            continue;
        }
        while (purse->silver >= kLiangPerStone
               && ledger_.vault[7] > 0u)
        {
            purse->silver -= kLiangPerStone;
            purse->stone += 1u;
            ledger_.vault[7] -= 1u;
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

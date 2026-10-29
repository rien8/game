/// @file combat.hpp
/// @brief 众生层：战斗的一步判定。公共头而非私有头——刀 4 的战斗
///        只有这一处实现（盗采被擒、寻仇、闭关被寻衅共用），测试要
///        直调它（与 lifespan_reached 同例）。
#pragma once

#include "gamev2/life/constants.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace gamev2::life {

/// 战力。境界 10 点一体魄、3 点一法器档（差异 3）。
///
/// 法器不是实体，只是私囊折算出的一个档位——财力入战力是设定 §9.3
/// 的「法器」最薄的落地。
[[nodiscard]] constexpr std::int32_t combat_power(
    const Cultivation& cult, const Aptitude& apt,
    std::uint64_t purse) noexcept
{
    std::uint32_t grade = 0;
    for (std::size_t i = 0; i < kArtifactGradeStone.size(); ++i)
    {
        if (purse >= kArtifactGradeStone[i])
        {
            grade = static_cast<std::uint32_t>(i);
        }
    }
    return static_cast<std::int32_t>(cult.realm) * 10
           + static_cast<std::int32_t>(apt.body)
           + static_cast<std::int32_t>(grade) * 3;
}

/// 战斗掷运：键含双方序号（小者在先）。同一对人对同一年永远掷出同一
/// 个数——胜负因此可复算，不随遍历顺序漂移。
[[nodiscard]] inline gamev2::world::Pcg32 combat_roll(
    std::uint64_t seed, std::uint32_t year, std::uint32_t a,
    std::uint32_t b)
{
    const auto lo = a < b ? a : b;
    const auto hi = a < b ? b : a;
    const auto stream =
        static_cast<std::uint32_t>(gamev2::world::Stream::life);
    const std::uint64_t s1 = gamev2::world::stage_seed(seed, stream);
    const std::uint64_t s2 = gamev2::world::stage_seed(
        s1, static_cast<std::uint32_t>(Roll::combat));
    const std::uint64_t s3 = gamev2::world::stage_seed(s2, year);
    const std::uint64_t s4 = gamev2::world::stage_seed(s3, lo);
    const std::uint64_t s5 = gamev2::world::stage_seed(s4, hi);
    return gamev2::world::Pcg32(s5);
}

/// 一步战斗（设计 §2.2：境界差 × 体魄 × 档位 + 无状态随机）。
/// 返回 true = a 胜。越级手段（设定 §5.3）第一版不进判定。
[[nodiscard]] inline bool duel(std::uint64_t seed, std::uint32_t year,
                                const Cultivation& ca, const Aptitude& aa,
                                std::uint64_t purse_a, const Cultivation& cb,
                                const Aptitude& ab, std::uint64_t purse_b,
                                std::uint32_t index_a, std::uint32_t index_b)
{
    const std::int32_t pa = combat_power(ca, aa, purse_a);
    const std::int32_t pb = combat_power(cb, ab, purse_b);
    std::int32_t chance =
        500 + static_cast<std::int32_t>(kCombatSwingPermille) * (pa - pb);
    chance = std::clamp(chance,
                        static_cast<std::int32_t>(kCombatMinPermille),
                        static_cast<std::int32_t>(kCombatMaxPermille));
    return static_cast<std::int32_t>(
               combat_roll(seed, year, index_a, index_b).below(1000u))
           < chance;
}

/// 夺位成败（设计 §2.3：读境界差与门内声望，不走 duel）。
///
/// 胜率 = clamp(500 + 100×境界差 + 40×门内声望差, 50, 950)‰。
[[nodiscard]] inline bool contest_won(std::uint64_t seed, std::uint32_t year,
                                      const SoulId& challenger,
                                      const Cultivation& ca,
                                      const Standing& sa, const SoulId& master,
                                      const Cultivation& cm,
                                      const Standing& sm)
{
    std::int32_t chance = 500;
    chance += 100 * (static_cast<std::int32_t>(ca.realm)
                     - static_cast<std::int32_t>(cm.realm));
    const auto cs = static_cast<std::size_t>(challenger.sect);
    const auto ms = static_cast<std::size_t>(master.sect);
    chance += 40 * (static_cast<std::int32_t>(sa.by_sect[cs])
                    - static_cast<std::int32_t>(sm.by_sect[ms]));
    chance = std::clamp(chance,
                        static_cast<std::int32_t>(kCombatMinPermille),
                        static_cast<std::int32_t>(kCombatMaxPermille));

    const auto stream =
        static_cast<std::uint32_t>(gamev2::world::Stream::life);
    const std::uint64_t s1 = gamev2::world::stage_seed(seed, stream);
    const std::uint64_t s2 = gamev2::world::stage_seed(
        s1, static_cast<std::uint32_t>(Roll::contest));
    const std::uint64_t s3 = gamev2::world::stage_seed(s2, year);
    const std::uint64_t s4 = gamev2::world::stage_seed(s3, challenger.index);
    const std::uint64_t s5 = gamev2::world::stage_seed(s4, master.index);
    return static_cast<std::int32_t>(
               gamev2::world::Pcg32(s5).below(1000u))
           < chance;
}

}  // namespace gamev2::life

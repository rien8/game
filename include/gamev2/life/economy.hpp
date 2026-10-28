/// @file economy.hpp
/// @brief 众生层经济：换算系数、份额表、烧毁表与逐宗金库账。
#pragma once

#include <array>
#include <cstdint>

namespace gamev2::life {

/// 每脉每年的指数产能，按 world::VeinRank 序（none/barren/branch/
/// main/dragon）。
///
/// 标定值：锚点是龙脉:主脉:支脉 = 10:4:1 的量级差（设定没给产能表；
/// 换这组比值平移逐宗铸币的绝对量、不改贫富序——富宗仍是龙脉持有者）。
/// 枯脉（balance 为 0 或已标枯年）不产。
inline constexpr std::array<std::uint32_t, 5> kRankAnnualYield{
    {0, 2, 10, 40, 100}};

/// 指数产能 → 下品灵石。全局唯一换算（设计 §3.2：系数只写一份）。
///
/// 标定值：锚点是设定 §6.1.1 天下年产 4200 中品 = 420000 下品。
/// 256²/seed13 活脉 Σ产能 = 420（探针复核），420000 / 420 = 1000。
inline constexpr std::uint64_t kVeinToStone = 1000;

/// 逐职位年份额（下品/年），按 OfficeKind 序（宗主/长老/内门/外门）。
///
/// 制度性常量（设计 §3.3、§6：份额从底层往上克扣，高层不动）。锚点：
/// 宗主覆盖金丹级吸纳（33333）留余；长老是筑基级（1000）的三倍；
/// 内门是炼气级（100）的五倍；外门是引气级（37）的四倍糊口。换值
/// 只改断供出现的早晚，不改克扣次序。
inline constexpr std::array<std::uint32_t, 4> kOfficeShare{
    {35000, 3000, 500, 150}};

/// 逐境年烧毁（下品/年），按 Realm 序。
///
/// 设定 §6.1 品级表逐字折算：引气「下品约十日」→ 365/10 ≈ 37；
/// 炼气「中品约一年」→ 100；筑基「上品约十年」→ 10000/10 = 1000；
/// 金丹/元婴「极品约三十年」→ 1000000/30 ≈ 33333（与 §6.1.2 的元婴
/// 「三百三十余中品/年」互证）。化神及以上设定未给——×10 外推的
/// 标定值，刀 4 重估；测试跨度内结构性到不了（门槛之和 120 年 +
/// 掷运，100 年内无人化神）。
inline constexpr std::array<std::uint64_t, 9> kRealmBurn{
    {37, 100, 1000, 33333, 33333, 333330, 3333300, 33333000, 333330000}};

/// 固定换价（设定 §6.4 逐字）：一枚下品折两千两。
inline constexpr std::uint32_t kLiangPerStone = 2000;

/// 散修营生年入（设定 §6.4 逐字：猎兽采药押镖，年入约六十两）。
inline constexpr std::uint32_t kFreeCultivatorIncomeLiang = 60;

/// 逐宗金库账。刀 3 的金库不是组件（设计 §2.1 组件表无此项）——
/// 它是「宗门的账」不是「人的账」，与 Roster 同一先例：普通结构体
/// 由 Life 持有。下标按 history::Sect 序，第 8 位是无主（散修公账）。
struct EconomyLedger
{
    std::array<std::uint64_t, 8> vault{};
    /// 逐宗年铸币。当代格局快照定死（口径裁定 1）；刀 5 换成
    /// FactionOutcome 的产出清单后本字段退役。
    std::array<std::uint64_t, 8> mint_per_year{};
    std::uint64_t minted_total = 0;
    std::uint64_t burned_total = 0;

    /// Σ金库（含无主公账）。守恒校验与测试共用这一份求和。
    [[nodiscard]] constexpr std::uint64_t vault_total() const noexcept
    {
        std::uint64_t sum = 0;
        for (const auto v : vault)
        {
            sum += v;
        }
        return sum;
    }
};

}  // namespace gamev2::life

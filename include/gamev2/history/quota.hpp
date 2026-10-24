/// @file quota.hpp
/// @brief 台账与派系演化层共用的配额原语。
///
/// 这几个原语必须只有一份实现。标定系数、阴脉判定、有效流速这三样
/// 若各写一份，两层算出的数字会静默错位——错位在结果里看不出来，
/// 只在对照表里表现为「说不清的差」。
#pragma once

#include "gamev2/world/world.hpp"

#include <cstdint>

namespace gamev2::history {

/// 定点标度：年度开采量保留 2^16 分之一。
///
/// 年度开采量是个小数（流速 × 潮位 × 换算系数，量级 0.05..3），
/// 直接截断成整数会把绝大多数年份变成 0，分布上出现断崖。
inline constexpr std::uint64_t kQuotaFixedScale = 1ull << 16;

/// 有效流速的分母：255 × 2^16。
inline constexpr std::uint64_t kQuotaFixedDen = 255ull * kQuotaFixedScale;

/// 推演跨度的上限（年）。
///
/// 台账与派系演化层都是逐年推演，跨度就是循环次数：年份是个 uint32，
/// 传 UINT32_MAX 就能要出四十亿次循环，外加一份几百 GB 的余额轨迹。
/// 上限把这条岔路堵在分配之前——退化由诊断量报出，不是一段崩掉的
/// 白屏。
inline constexpr std::uint32_t kMaxSpanYears = 100000;

/// 潮位积分：epoch 到 now 之间 level 的总和（level 是 0..255 的字节）。
///
/// 换算系数用它做锚点：若无配额封顶，全世界的存量恰好在当代耗尽；
/// 配额封顶把枯竭摊在六百年里，当代仍有余额。
///
/// 调用方必须先查 `kMaxSpanYears`：这个原语自己不做跨度检查，跨度上限
/// 是调用方的责任——它是逐年循环，跨度就是循环次数。
[[nodiscard]] std::uint64_t tide_integral(std::uint32_t epoch,
                                           std::uint32_t now);

/// 标定系数 kNum。
///
/// 锚点：若无配额封顶，全世界的存量恰好在当代耗尽。它是**世界级**
/// 的量：两层必须用同一个值，否则两套推演不可比。
[[nodiscard]] std::uint64_t quota_conversion(std::uint64_t sum_total,
                                              std::uint64_t sum_rate,
                                              std::uint64_t integral);

/// 年度开采量的小数余数累加器。
///
/// 每条脉一个，跨年保留。抽干配额的那一年余数要清零：被配额卡住时
/// 未消费的小数没有意义。
class QuotaRemainder
{
public:
    /// 当年的有效流速（定点量的整数部分）。
    [[nodiscard]] std::uint64_t take(std::uint32_t rate, std::uint32_t level,
                                      std::uint64_t conversion);

    /// 清零余数。配额被卡住时调用。
    void reset() noexcept { remainder_ = 0; }

private:
    std::uint64_t remainder_ = 0;
};

/// 阴脉判定：这条脉占的格子里有没有阴地。
///
/// 设定 8.1 说寒江剑府握的是「一条阴脉（采出即衰）」，即地貌为阴地的
/// 那条。地貌是网格属性，所以要按 vein_id 网格反查这条脉的占格。
[[nodiscard]] bool is_yin_vein(const gamev2::world::World& world,
                               std::uint32_t vein_id);

}  // namespace gamev2::history

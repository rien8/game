/// @file economy.cpp
/// @brief 派系演化层的经济核心：流速、衰减、用度、供给、缺口。
#include "gamev2/faction/faction.hpp"
#include "gamev2/history/quota.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace gamev2::faction {
namespace {

/// 衰减把定点标度抬高一层：分母 = 255 × 2^16 × 2^16。
///
/// 两处必须用同一个分母（取模与整除）。差一层就会让有效流速整体
/// 差 65536 倍。
constexpr std::uint64_t kDecayDen =
    gamev2::history::kQuotaFixedDen * gamev2::history::kQuotaFixedScale;

/// 比例份额的定点值：deficit × weight ÷ total_weight，标度 2^16。
///
/// 溢出：缺口与权重都是「年开采量」量级（10^4 以内），乘积再乘 2^16
/// 落在 10^13 量级，离 uint64 的上限还差六个数量级。
std::uint64_t share_fixed(std::uint32_t deficit, std::uint64_t weight,
                          std::uint64_t total_weight)
{
    return static_cast<std::uint64_t>(deficit) * weight
           * gamev2::history::kQuotaFixedScale / total_weight;
}

}  // namespace

std::uint32_t decay_of(const VeinProfile& profile, const VeinRight& right,
                       const VeinStock& stock)
{
    if (!profile.yin)
    {
        // 非阴脉的衰减率恒为一：只有阴脉采出即衰（设定 §8.1）。
        return static_cast<std::uint32_t>(gamev2::history::kQuotaFixedScale);
    }
    if (right.quota_total == 0u)
    {
        return 0u;
    }
    // 采出即衰：产出随累计开采线性衰减，采完为止。
    //
    // 衰减率 = 剩余存量 / 总量配额。累计开采可能超过配额（越采），
    // 所以剩余量要夹在 0：衰减率不能为负。
    //
    // 注意分子是**剩余量**不是总量配额——写成总量会让「采了一半」
    // 的脉衰减率为满值，衰减就永远不会发生。
    const std::uint32_t left =
        (stock.extracted < right.quota_total)
            ? (right.quota_total - stock.extracted)
            : 0u;
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(left) * gamev2::history::kQuotaFixedScale)
        / right.quota_total);
}

std::uint64_t eff_fixed(const VeinProfile& profile, const VeinRight& right,
                        const VeinStock& stock, std::uint32_t level,
                        std::uint64_t conversion)
{
    const std::uint32_t decay = decay_of(profile, right, stock);
    // 乘法顺序要紧：rate × level 先做（≤ 1e6），再乘 conversion
    // （世界级常量，10^2 量级，实测 70~162），最后乘 decay（≤ 2^16）。
    // 整个乘积在 1e13 量级，离 uint64 的上限还差六个数量级，不溢出。
    //
    // 换算系数 F = conversion / (255 × 2^16)，衰减率 decay / 2^16，
    // 所以 eff_real = rate × level × conversion × decay / (255 × 2^32)；
    // 换成 2^16 定点就是下面这一式。
    return static_cast<std::uint64_t>(right.quota_rate) * level * conversion
           * decay / gamev2::history::kQuotaFixedDen;
}

std::uint64_t take_effective(const VeinProfile& profile,
                             const VeinRight& right, const VeinStock& stock,
                             std::uint32_t level, std::uint64_t conversion,
                             VeinRemainder& remainder)
{
    const std::uint32_t decay = decay_of(profile, right, stock);
    // 乘法顺序要紧：rate × level 先做（≤ 1e6），再乘 conversion
    // （世界级常量，10^2 量级，实测 70~162），最后乘 decay（≤ 2^16）。
    // 每一步的中间值都在 1e13 量级，不会溢出 uint64。
    const std::uint64_t num = static_cast<std::uint64_t>(right.quota_rate)
                              * level * conversion * decay;
    const std::uint64_t total = num + remainder.value;
    remainder.value = total % kDecayDen;
    return total / kDecayDen;
}

std::uint32_t deficit_of(std::uint32_t demand, std::uint32_t supply)
{
    return demand > supply ? demand - supply : 0u;
}

std::vector<std::uint32_t> apportion_deficit(
    std::uint32_t deficit, const std::vector<std::uint64_t>& weights)
{
    std::vector<std::uint32_t> units(weights.size(), 0u);
    std::uint64_t total = 0;
    for (const std::uint64_t weight : weights)
    {
        total += weight;
    }
    if (deficit == 0u || total == 0u)
    {
        return units;
    }

    // 各脉先取比例份额的下界。定点值的高位是下界、低位是小数部分：
    // 一次除法把两者都取出来，不必再算一遍。
    std::uint32_t left = deficit;
    std::vector<std::uint64_t> fraction(weights.size(), 0u);
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        const std::uint64_t fixed = share_fixed(deficit, weights[i], total);
        units[i] = static_cast<std::uint32_t>(
            fixed / gamev2::history::kQuotaFixedScale);
        fraction[i] = fixed % gamev2::history::kQuotaFixedScale;
        left -= units[i];
    }

    // 剩下的单位按小数部分从大到小补齐：各脉至多多拿一个单位，且总量
    // 恰好等于缺口。同余按下标定序（稳定排序），结果因此与遍历顺序无关。
    std::vector<std::size_t> order(weights.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&fraction](std::size_t a, std::size_t b) {
        return fraction[a] > fraction[b];
    });
    for (const std::size_t i : order)
    {
        if (left == 0u)
        {
            break;
        }
        ++units[i];
        --left;
    }
    return units;
}

std::uint32_t deficit_share_of(std::uint32_t deficit, std::uint64_t weight,
                                std::uint64_t total_weight)
{
    if (deficit == 0u || total_weight == 0u)
    {
        return 0u;
    }
    return round_fixed(share_fixed(deficit, weight, total_weight));
}

}  // namespace gamev2::faction

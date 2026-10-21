/// @file spirit_tables.hpp
/// @brief 灵气层的查表与共享常量。
///
/// 放头文件并用 inline constexpr，是为了让测试能引用同一份数据核对，
/// 而不是复制一份会与实现漂移的副本。调参项集中在这里，与既有
/// tables.hpp 的做法一致。
#pragma once

#include "gamev2/world/enums.hpp"
#include "gamev2/world/spirit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::world {

/// 参考图幅面积。灵脉数量按面积相对它缩放。
inline constexpr std::uint32_t kReferenceArea = 256u * 256u;

/// 天下龙脉、主脉、支脉的总数（设定 2.2）。数量是硬约束，不是调参项。
inline constexpr std::uint32_t kDragonVeinTotal = 1;
inline constexpr std::uint32_t kMainVeinTotal = 9;
inline constexpr std::uint32_t kBranchVeinTotal = 40;

/// 灵潮每轮的损耗比例。设定 2.6：「每轮损耗约一成」，于是九轮之后
/// 0.9^9 = 0.387，不足元潮时期的四成。
inline constexpr double kTideLossPerCycle = 0.9;

/// 谷底期保留的浓度比例。谷底期不是绝对零——设定说「灵脉半死」。
inline constexpr double kTroughFloor = 0.3;

/// 灵域分等的浓度阈值。调参项。
inline constexpr std::uint8_t kThinThreshold = 8;
inline constexpr std::uint8_t kMediumThreshold = 24;
inline constexpr std::uint8_t kThickThreshold = 60;
inline constexpr std::uint8_t kBlessedThreshold = 120;

/// 流量参考值。灵眼流量换算成浓度贡献时以它为基准。
/// 调参项：Task 10 用 demo 导出的 PPM 对着图调。
inline constexpr std::uint32_t kReferenceFlow = 2000;

/// 灵域影响半径的基准值。实际半径随流量开方增长。
///
/// 它是浓度场的衰减尺度，单位是绝对格数。定得太大时，天下几十只灵眼的
/// 尾巴会叠成一片背景高原，「福地」就从「强眼所在地」退化成「众眼尾巴
/// 的叠加区」。红线 8 要求稀缺性不为剧情让路，福地面积必须小。
inline constexpr double kDomainRadius = 16.0;

/// 浓度场盖章的最大半径。超过它的贡献按 0 计。
inline constexpr std::uint32_t kDomainMaxRadius = 64;

/// 地脉埋深的上下界，单位是格。
inline constexpr std::uint32_t kMinLeyDepth = 2;
inline constexpr std::uint32_t kMaxLeyDepth = 64;

/// 异质地脉出现的概率。设定 2.1：「少数地脉夹带异质」，稀有且难驾驭。
inline constexpr double kHeteroChance = 0.08;

/// 岩性到地脉属性的映射。顺序必须与 Stone 枚举逐项对齐。
///
/// 一对一映射：设定说属性「由流经的岩性决定」，没有给一岩多属的机制。
inline constexpr std::array<Element, 12> kStoneElement{{
    Element::earth,  // sandstone  厚重
    Element::water,  // shale     寒润
    Element::water,  // limestone 寒润
    Element::metal,  // granite   刚锐
    Element::fire,   // basalt    暴烈
    Element::metal,  // slate     刚锐
    Element::earth,  // gravel    厚重
    Element::earth,  // marble    厚重
    Element::wood,   // clay      绵长
    Element::water,  // ice       寒润
    Element::fire,   // igneous   暴烈
    Element::metal,  // igneous_intrusive 刚锐
}};

static_assert(kStoneElement.size()
              == static_cast<std::size_t>(Stone::igneous_intrusive) + 1);

/// 该岩性对应的地脉属性。
[[nodiscard]] constexpr Element element_of_stone(Stone stone) noexcept
{
    return kStoneElement[static_cast<std::size_t>(stone)];
}

/// 五行：金木水火土。
[[nodiscard]] constexpr bool is_fivefold(Element element) noexcept
{
    switch (element)
    {
    case Element::metal:
    case Element::wood:
    case Element::water:
    case Element::fire:
    case Element::earth:
        return true;
    default:
        return false;
    }
}

/// 异质：雷冰风光暗音毒。稀有且极难驾驭（设定 2.1）。
[[nodiscard]] constexpr bool is_hetero(Element element) noexcept
{
    switch (element)
    {
    case Element::thunder:
    case Element::ice:
    case Element::wind:
    case Element::light:
    case Element::dark:
    case Element::sound:
    case Element::poison:
        return true;
    default:
        return false;
    }
}

/// 该疤痕是否让地面成为死地。死地的三种成因：越采塌陷、渡劫失败、枯眼。
[[nodiscard]] constexpr bool is_dead_scar(Scar scar) noexcept
{
    return scar == Scar::sinkhole || scar == Scar::tribulation
           || scar == Scar::depleted_eye;
}

/// 浓度分等。
[[nodiscard]] constexpr DomainGrade domain_grade_of(
    std::uint8_t concentration) noexcept
{
    if (concentration >= kBlessedThreshold)
    {
        return DomainGrade::blessed;
    }
    if (concentration >= kThickThreshold)
    {
        return DomainGrade::thick;
    }
    if (concentration >= kMediumThreshold)
    {
        return DomainGrade::medium;
    }
    if (concentration >= kThinThreshold)
    {
        return DomainGrade::thin;
    }
    return DomainGrade::barren;
}

/// 有效浓度：地脉网络给出的浓度，扣掉历史疤痕造成的压制。
/// 派生量而非字段——它一旦成为 World 的成员就会有第二个写入者。
[[nodiscard]] constexpr std::uint8_t effective_concentration(
    std::uint8_t raw, std::uint8_t suppression) noexcept
{
    const int scaled = static_cast<int>(raw) * (255 - static_cast<int>(suppression))
                       / 255;
    return static_cast<std::uint8_t>(scaled);
}

/// 每轮的损耗比例，用整数幂精确表示。
///
/// 不用 std::pow：libm 的末位差异与 FMA 收缩都会让同一串数字在不同的
/// 编译选项下算出不同的字节，破坏「同 seed 同世界」。9^n / 10^n 可以
/// 整除到底，代价是分母 10^n 必须放得进 uint64。
///
/// 第 20 轮起封顶：分母 10^20 = 1e20 已经超过 UINT64_MAX（约 1.8e19），
/// 再乘就回绕。再往后每轮的损耗就停在 0.9^19 不再下降，等于「设定里的
/// 世界比这个模型活得久」，需要更长的轮次就得起用大整数。
[[nodiscard]] constexpr double tide_decay_ratio(std::uint32_t cycle) noexcept
{
    const std::uint32_t capped = cycle > 19u ? 19u : cycle;
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
    for (std::uint32_t i = 0; i < capped; ++i)
    {
        numerator *= 9u;
        denominator *= 10u;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

/// 比例转字节。四舍五入，不做截断——截断会让 0.5 永远差一格。
///
/// 不用 std::lround：它要到 C++26 才 constexpr 化，用了这条链就永远
/// 只能在运行时求值，签名上的 constexpr 成了空话。对本模型能产出的
/// 比例（0.9^n 与相位插值）而言，`x + 0.5` 再截断与 lround 的
/// 「远离零四舍五入」结果完全相同。
[[nodiscard]] constexpr std::uint8_t ratio_to_byte(double ratio) noexcept
{
    const double scaled = ratio * 255.0;
    const long rounded = scaled >= 0.0 ? static_cast<long>(scaled + 0.5) : 0L;
    return static_cast<std::uint8_t>(rounded > 255L ? 255L : rounded);
}

/// 由灵历年算出灵潮周期状态。
///
/// 周期是千年一轮：灵潮期 300 年、衰退期 500 年、谷底期 200 年。
/// 轮次索引从 0 起，0 即元潮（灵历元年）。
///
/// 合法年份是 1..UINT32_MAX。year 必须 >= 1：`(year - 1) / 1000` 在
/// year == 0 时下溢，会算出一个貌似合理的「第 4294967 轮」而不报错。
/// WorldParams::is_valid() 已经拦下这个值。
[[nodiscard]] constexpr SpiritTide compute_spirit_tide(
    std::uint32_t year) noexcept
{
    SpiritTide tide;
    tide.year = year;
    tide.cycle = (year - 1u) / 1000u;

    const std::uint32_t offset = year - tide.cycle * 1000u - 1u;
    if (offset < 300u)
    {
        tide.phase = TidePhase::tide;
        tide.year_in_phase = offset;
    }
    else if (offset < 800u)
    {
        tide.phase = TidePhase::decline;
        tide.year_in_phase = offset - 300u;
    }
    else
    {
        tide.phase = TidePhase::trough;
        tide.year_in_phase = offset - 800u;
    }
    tide.years_to_next_tide = 1000u - offset;

    const double peak = tide_decay_ratio(tide.cycle);
    double shape = 0.0;
    switch (tide.phase)
    {
    case TidePhase::tide:
        // +1 让阶段的最后一年正好取到 1.0：灵潮期只有 300 个相位年，
        // 不加一的话峰值永远差 1/300 达不到。
        shape = static_cast<double>(tide.year_in_phase + 1u) / 300.0;
        break;
    case TidePhase::decline:
        shape = 1.0 - static_cast<double>(tide.year_in_phase) / 500.0;
        break;
    case TidePhase::trough:
        shape = 0.0;
        break;
    }
    const double level =
        peak * (kTroughFloor + (1.0 - kTroughFloor) * shape);

    tide.peak = ratio_to_byte(peak);
    tide.level = ratio_to_byte(level);
    return tide;
}

/// 灵脉目标数量。面积相对参考图幅线性缩放，并以天下总数封顶。
struct VeinTarget
{
    std::uint32_t dragon;
    std::uint32_t main;
    std::uint32_t branch;
};

[[nodiscard]] constexpr VeinTarget vein_targets(std::uint32_t area) noexcept
{
    const auto scaled = [area](std::uint32_t total) noexcept {
        const std::uint64_t n =
            (static_cast<std::uint64_t>(total) * area + kReferenceArea / 2)
            / kReferenceArea;
        return static_cast<std::uint32_t>(
            n < total ? n : total);
    };
    return VeinTarget{kDragonVeinTotal, scaled(kMainVeinTotal),
                      scaled(kBranchVeinTotal)};
}

/// 灵脉切段阈值（字节刻度）。得分超过它的连续节点算一段。
/// 下调它会扩大合格集，但也会把原本分开的极大区间并起来，段数因此
/// 未必单调增加——切段时必须留段数最多的那组，不能只看最后一次。
inline constexpr std::uint8_t kVeinThreshold = 64;
inline constexpr std::uint8_t kVeinThresholdStep = 4;
inline constexpr std::uint8_t kVeinThresholdFloor = 4;

/// 枯眼那条脉的稳定年限。设定 2.2 的品阶标准是「储量 × 稳定年限」，
/// 被抽干的脉稳定年限骤降。
///
/// 它是**枯眼**的后果，不是越采的后果：两处「被采」各打一个品阶因子——
/// 越采削储量，枯眼压稳定年限。设定 2.3 说抽干后只剩枯眼、不可复生。
inline constexpr std::uint32_t kStabilityMined = 30;

/// 候选段被越采的概率。设定没给数，这是经验值。
inline constexpr double kOverminedChance = 0.15;

/// 枯眼占灵眼数的比例。设定 2.3 说「抽干后只剩枯眼，不可复生」，
/// 但当代有多少枯眼设定未规定，这是经验值。枯眼数 = round(灵眼数 × 它)，
/// 而灵眼数是固定的（有品阶的脉恒为 1+9+40 条），所以枯眼数也是固定的。
inline constexpr double kDepletedRatio = 0.15;

/// 算灵眼纯度时的污染统计半径，格。
inline constexpr std::uint32_t kPurityProbeRadius = 8;
/// 五行脉的基线纯度。设定 2.1：五行脉纯，异质脉稀有而难驾驭。
inline constexpr std::uint8_t kPurityBaseFivefold = 200;
/// 异质脉的基线纯度。难驾驭的代价就是纯度低。
inline constexpr std::uint8_t kPurityBaseHetero = 120;
/// 查不到地脉属性时的基线纯度。取中性值：既不冒充五行，也不冒充异质。
inline constexpr std::uint8_t kPurityBaseUnknown = 160;
/// 周边每格瘴域的纯度扣减。
///
/// 幅度按实测标定：污染块是毒属性地脉周围半径 6 的 13x13 方块，与灵眼
/// 半径 8 的 17x17 窗口相交最少 1 格、最多 169 格。幅度 60 时一格就扣掉
/// 基线 200 的三成、四格就扣穿到 0，1050 只眼里 88 只归零，扣减表退化成
/// 布尔量；幅度 8 让污染的多寡重新看得出来。
inline constexpr std::uint8_t kPurityMiasmaPenalty = 8;
/// 周边每格阴地的纯度扣减。取瘴域的一半（8 : 4），保持「大幅 : 中度」的
/// 比例；阴地块与灵眼窗口的相交格数同样从 1 到 169 不等，幅度 4 让阴地
/// 的扣减保持可测。
inline constexpr std::uint8_t kPurityYinPenalty = 4;

}  // namespace gamev2::world

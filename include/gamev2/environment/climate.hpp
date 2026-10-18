/// @file climate.hpp
/// @brief 气候场：EnvironmentSnapshot 是格级气候在某一季的取值，
///        CellIndex 是格的下标，ClimateField 持有逐格的基准值并按季节求值。
#pragma once

#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::environment {

/// 环境在个体建立时采样一次并锁定。个体因此具有确定的表型，可测、可存档。
/// 三项环境量的取值范围约定：temperature 是相对该物种适宜温度的偏移，
/// fertility 与 light 是 0..1 的充足度，默认值即「无环境影响」。
struct EnvironmentSnapshot
{
    float temperature = 0.0f;  ///< 相对该物种适宜温度的偏移
    float fertility = 1.0f;    ///< 0..1 食物充足度
    float light = 1.0f;
};

/// 编译期固定的格数。场要能定长进 ECS 组件，字段集合与尺寸都必须在编译期确定。
inline constexpr std::size_t kGridSide = 64;
inline constexpr std::size_t kCellCount = kGridSide * kGridSide;

/// 世界坐标空间即格坐标空间，[0, kGridSide)。个体在格内浮动，
/// 气候与资源是格级的。
///
/// value 是公开数据成员：\pre 只约束走 of() 的调用方，不是类型级不变量。
/// 聚合初始化能绕过它（如 CellIndex{5000}），随后 base() / at() 就会无检查
/// 地索引 cells_。排查越界时第一嫌疑是聚合初始化，不是 of()。
struct CellIndex
{
    std::uint16_t value = 0;

    /// 世界坐标 -> 所在格。截断即向下取整，因为坐标非负。
    ///
    /// \pre x, y ∈ [0, kGridSide)。越界输入不是可恢复的错误状态，
    /// 因此不返回错误，只在这里写明。
    [[nodiscard]] static CellIndex of(float x, float y) noexcept
    {
        const auto ix = static_cast<std::uint16_t>(x);
        const auto iy = static_cast<std::uint16_t>(y);
        return CellIndex{static_cast<std::uint16_t>(iy * kGridSide + ix)};
    }
};

/// 逐季不变的气候基准值。季节只在求值时给它们加偏移，不写回。
struct ClimateCell
{
    float base_temperature = 0.0f;
    float base_fertility = 1.0f;
    float base_light = 1.0f;
};

class ClimateField
{
public:
    ClimateField(std::uint64_t root_seed,
                 const EnvironmentConfig& config);

    /// 只改派生量，不累加：温度与光照在基准值上加一个由季节决定的正弦偏移。
    /// 因此任意一季的气候都能直接算出，不需要历史，也就没有「忘掉推进过
    /// 几季」这种可能出错的状态。
    void advance(std::uint32_t season_index) noexcept;

    [[nodiscard]] EnvironmentSnapshot at(CellIndex cell) const noexcept;
    [[nodiscard]] const ClimateCell& base(CellIndex cell) const noexcept;

    /// 调这一格的气候基准温度，夹到 [kMinTemperature, kMaxTemperature]。
    ///
    /// 改的是 `base_temperature`（构造后一直没被写过的那份基准），而
    /// `advance` 每季在上面叠正弦偏移 —— 所以一次笔刷的效果会持续到
    /// 地形之外的所有季，而不是被下一季的 `advance` 冲掉。
    void adjust_temperature(CellIndex cell, float delta) noexcept;

private:
    std::array<ClimateCell, kCellCount> cells_{};
    std::uint32_t season_ = 0;
    EnvironmentConfig config_{};
};

}  // namespace gamev2::environment

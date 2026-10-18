/// @file terrain.hpp
/// @brief 地形场：逐格海拔与通行性。与 ClimateField / ResourceField 同形状
///        的定长场，本身不含任何模拟逻辑。
///
/// 模块说明在 doc/2026-10-17-game-design.md §4.1。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::environment {

/// 地形完全由 root_seed 定出，不随季节变化：唯一的例外是玩家的干预，
/// 它在运行期改写通行性与（经 ClimateField）气候，但不重新生成高程。
class TerrainField
{
public:
    /// 值噪声 3 个八度铺 elevation，再按全图第 config.water_fraction
    /// 百分位切成水陆。
    ///
    /// \pre config.water_fraction ∈ [0, 1]（见 EnvironmentConfig 的说明）。
    static TerrainField generate(std::uint64_t root_seed,
                                 const EnvironmentConfig& config) noexcept;

    /// 不可通行的唯一来源是水。山脉不额外处理——这样「屏障」在语义上只有
    /// 一个，玩家也能一眼看懂屏障为什么有效。
    ///
    /// 与 ClimateField::base 同样，`\pre` 只约束走 CellIndex::of 的调用方，
    /// 不是类型级不变量：聚合初始化能绕过它（CellIndex{5000}）。
    [[nodiscard]] bool passable(CellIndex cell) const noexcept;

    /// 供渲染做水墨晕染与玩家画屏障时的合法性提示。本模块自己不用它，
    /// 因此它只被测试与将来的渲染层读到。
    ///
    /// 取值范围是 [0, 1 + gain + gain²] = [0, 1.75]，不是 [0, 1]：这是三
    /// 个八度各自落在 [0, 1]、振幅按 1 / gain / gain² 叠加的结果，没有做
    /// 归一化。渲染层要按 1.75 折算成 0..1。
    [[nodiscard]] float elevation(CellIndex cell) const noexcept;

    /// 把这一格变成不可通行。**只改 `passable_`，不动 `elevation_`** ——
    /// 高程是「这里本来多高」，玩家改的是「现在能不能站」。渲染层的水墨
    /// 晕染读 elevation，若 flood 顺手改了它，玩家一淹就把地势抹平了。
    void flood(CellIndex cell) noexcept;

    /// 把这一格变回可通行，**即使它的 elevation 低于海平面**。
    ///
    /// 因此「passable」与「elevation > sea_level」是两条独立的判据，
    /// 不允许在别处用后者反推前者。
    void drain(CellIndex cell) noexcept;

    /// 生成时定下的海平面。玩家的干预**不改变它** —— 它是地形生成的产物，
    /// 不是当前的水陆状态。
    [[nodiscard]] float sea_level() const noexcept
    {
        return sea_level_;
    }

private:
    std::array<float, kCellCount> elevation_{};
    std::array<bool, kCellCount> passable_{};
    float sea_level_ = 0.0f;
};

}  // namespace gamev2::environment

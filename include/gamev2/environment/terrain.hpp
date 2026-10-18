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

/// 生成一次，此后只读：地形完全由 root_seed 定出，不随季节变化，也不接受
/// 任何修改入口（玩家改写地形属阶段 4）。
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

private:
    std::array<float, kCellCount> elevation_{};
    std::array<bool, kCellCount> passable_{};
};

}  // namespace gamev2::environment

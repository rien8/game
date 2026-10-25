/// @file resource.hpp
/// @brief 资源场：每格的食物存量与它能支持多少个体。存量以「个体」为单位，
///        因此 stock / occupants 直接读作人均可得食物，量纲不需要转换。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gamev2::environment {

/// 定义在 sites.hpp。count_occupants 需要它的坐标，这里只前置声明，
/// 避免 resource.hpp 与 sites.hpp 互相 include。
struct Forager;

struct ResourceCell
{
    float stock = 0.0f;     ///< 当前存量，[0, capacity]
    float capacity = 0.0f;  ///< = capacity_base × 该格 base_fertility
};

class ResourceField
{
public:
    /// \pre config.regen_rate ∈ (0, 1]：指数回归的收敛系数，越界会让
    ///      stock 冲出 [0, capacity]。本模块没有配置期校验机制，
    ///      因此这里只写明，不在运行时拦。
    ResourceField(const ClimateField& climate,
                  const EnvironmentConfig& config);

    /// stock += regen_rate × (capacity - stock)。指数逼近而非线性：
    /// 线性在贴近 capacity 时仍以固定速率加，会在边界上反复穿越。
    void regrow() noexcept;

    /// 扣减，夹到 0。扣减超过存量时只会把该格吃空，不会透支成负数：
    /// 负存量会让下一轮的 want 与 got 同时为负，喂食满意度反而变正。
    /// \pre amount >= 0：需求由表型算出，恒非负。越界不是可恢复的错误
    ///      状态，因此不返回错误，只在这里写明。
    void draw(CellIndex cell, float amount) noexcept;

    [[nodiscard]] float stock(CellIndex cell) const noexcept;
    [[nodiscard]] float capacity(CellIndex cell) const noexcept;

    /// 本格当前有多少个体。季内没有移动，季初数一次即可；
    /// run_season 与 build_sites 共用同一份计数。
    [[nodiscard]] std::uint16_t occupants(CellIndex cell) const noexcept;

    /// 清零后按 foragers 的坐标重数一遍。
    void count_occupants(std::span<const Forager> foragers);

private:
    std::array<ResourceCell, kCellCount> cells_{};
    std::array<std::uint16_t, kCellCount> occupants_{};
    float regen_rate_ = 0.25f;
};

}  // namespace gamev2::environment

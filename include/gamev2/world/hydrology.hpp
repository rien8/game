/// @file hydrology.hpp
/// @brief 阶段 4 的内部量。刻意不放进 World：只有阶段 4 消费它。
#pragma once

#include "gamev2/world/enums.hpp"

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 洼地填平后的无洼 DEM。design 文档 11.6 记录了为何不把它存进 World。
///
/// parent 与 order 是 priority-flood 顺手产出的副产品，事后无法重建：
/// parent 是把本格推入堆的那一格，即通向出口的方向，而 order 是弹出次序，
/// 它给出的「子晚于父」是下游指针无环性论证的一半。
struct FilledDem
{
    std::vector<std::uint8_t> level;

    /// 推入本格的那一格，即通向出口的方向。种子格（边界与海洋）在最初入堆时
    /// 没有父，但若它恰好与某个先弹出的格 4-邻接，那一格照样会推它一次并被
    /// 记为父 —— 这没有行为后果（主规则在该父严格更低时本就选中它，相等时
    /// 回退也选中同一格），但论证里不能声称「种子没有父」。
    std::vector<std::uint32_t> parent;

    /// 弹出次序。父格必先于子格弹出，否则子格无从被推入。
    std::vector<std::uint32_t> order;
};

}  // namespace gamev2::world

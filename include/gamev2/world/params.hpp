/// @file params.hpp
/// @brief 世界生成的尺寸与常量，以及唯一的可预期失败原因。
#pragma once

#include <cstdint>
#include <limits>

namespace gamev2::world {

/// 一次生成所需的全部输入。
struct WorldParams
{
    std::uint32_t width = 256;
    std::uint32_t height = 256;
    /// z 层数：0 为地表，1..z_levels-1 为地下。
    std::uint32_t z_levels = 6;
    /// 每 z 层厚度，以格计。
    std::uint32_t z_cell_depth = 16;
    std::uint64_t seed = 0;
    /// 目标海洋面积占比，海平面由高程分位数反解，因此海陆比不随种子漂移。
    std::uint8_t ocean_percent = 60;

    /// 灵历年。世界的「年」以第一轮灵潮之始为元年。
    /// 当代为灵历 8914 年（谷底期第一百一十三年）。
    std::uint32_t spirit_year = 8914;

    /// 只拦下会让分配或下标计算失去意义的参数，其余字段一律放行。
    ///
    /// 总格深必须装得进 int：strata 把 depth 转成 int 后做 `depth - 1`，
    /// z_cell_depth = 0x80000000、z_levels = 1 时那一步是有符号溢出（UB），
    /// 而这两个值各自都「大于 0」。乘积在 uint64 里比较，避免先在 uint32
    /// 里回绕成小值。
    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        constexpr std::uint64_t kMaxDepth =
            static_cast<std::uint64_t>(std::numeric_limits<int>::max());
        return width > 0 && height > 0 && z_levels > 0
               && z_cell_depth > 0 && ocean_percent <= 100
               && spirit_year >= 1
               && static_cast<std::uint64_t>(z_levels) * z_cell_depth
                      <= kMaxDepth;
    }
};

/// 参数非法。分配失败走 bad_alloc，不归到这里。
///
/// 灵气层不引入新的失败模式：灵脉数量凑不齐是退化而不是错误（设计文档
/// §5），缺口由 vein_census() 报出。
enum class WorldError
{
    invalid_params,
};

}  // namespace gamev2::world

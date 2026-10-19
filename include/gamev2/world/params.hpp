/// @file params.hpp
/// @brief 世界生成的尺寸与常量，以及唯一的可预期失败原因。
#pragma once

#include <cstdint>

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

    /// 只拦下会让分配或下标计算失去意义的参数，其余字段一律放行。
    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return width > 0 && height > 0 && z_levels > 0
               && z_cell_depth > 0 && ocean_percent <= 100;
    }
};

/// 参数非法。分配失败走 bad_alloc，不归到这里。
enum class WorldError
{
    invalid_params,
};

}  // namespace gamev2::world

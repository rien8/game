/// @file rng.hpp
/// @brief 确定性随机源与无状态噪声。
///
/// 刻意不用 <random> 的分布：它们是实现相关的，跨标准库不稳定，会破坏
/// 「同 seed 同世界」这一前提。噪声同样无状态：按坐标与种子直接求值，
/// 不依赖遍历顺序，否则仅仅调整阶段内的循环次序就会换一个世界。
#pragma once

#include <cstdint>

namespace gamev2::world {

/// PCG-XSH-RR 32。取值一律经 below() 或 unit()，不直接用原始位模式。
class Pcg32
{
public:
    explicit Pcg32(std::uint64_t seed) noexcept;

    std::uint32_t next() noexcept;
    /// [0, n) 无偏。n 为 0 时返回 0，不做除零。
    [[nodiscard]] std::uint32_t below(std::uint32_t n) noexcept;
    /// [0, 1)。
    [[nodiscard]] double unit() noexcept;

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

/// 阶段之间的随机流互不串扰：各阶段用自己的派生 seed 自建 Pcg32。
[[nodiscard]] std::uint64_t stage_seed(std::uint64_t seed,
                                      std::uint32_t stage) noexcept;

/// 梯度噪声取格子点值，无状态。[0, 1]
[[nodiscard]] double value_noise(std::uint64_t seed,
                                 double x, double y) noexcept;
[[nodiscard]] double value_noise3(std::uint64_t seed, double x, double y,
                                  double z) noexcept;

/// 分形叠加，归一化到 [-1, 1]
[[nodiscard]] double fbm(std::uint64_t seed, double x, double y, int octaves,
                        double lacunarity, double gain) noexcept;
[[nodiscard]] double fbm3(std::uint64_t seed, double x, double y, double z,
                         int octaves, double lacunarity, double gain) noexcept;

}  // namespace gamev2::world

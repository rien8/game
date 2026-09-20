#pragma once

#include <concepts>
#include <cstdint>

namespace game::pcg {

// 可设置种子的类型。
template <typename T>
concept Seedable = requires(T& t, std::uint64_t seed) {
    t.seed(seed);
};

// 二维噪声场：可采样 [0,1] 值。
template <typename T>
concept NoiseField2D = requires(const T& t, float x, float y) {
    { t.sample(x, y) } -> std::convertible_to<float>;
};

// 生成器：给定 seed 确定性产出 Output。
template <typename T, typename Output>
concept Generator = requires(const T& t, std::uint64_t seed) {
    { t.generate(seed) } -> std::convertible_to<Output>;
};

}  // namespace game::pcg

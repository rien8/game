#pragma once

#include <concepts>
#include <cstdint>

namespace game::pcg {

// splitmix64 风格：从主 seed + salt 确定性派生子 seed。
constexpr auto derive_seed(std::uint64_t seed, std::uint32_t salt) noexcept -> std::uint64_t {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ull + (static_cast<std::uint64_t>(salt) << 32);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// 模板化重载：接受任意整数类型 seed，返回同类型。
template <std::integral T>
    requires(!std::same_as<T, std::uint64_t>)
constexpr auto derive_seed(T seed, std::uint32_t salt) noexcept -> T {
    return static_cast<T>(derive_seed(static_cast<std::uint64_t>(seed), salt));
}

}  // namespace game::pcg

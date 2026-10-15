#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace gamev2::genetics {

/// splitmix64 finalizer。用它把根种子与用途标签混合成独立子流。
[[nodiscard]] constexpr std::uint64_t
mix_seed(std::uint64_t seed, std::uint64_t purpose) noexcept
{
    std::uint64_t z = seed + (purpose + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 每种用途一条独立子流。共用一条流会导致「某处多抽一次随机数，
/// 后续所有结果整体漂移」的脆弱性——一旦出现，无法定位是哪一处改动引起的。
enum class RandomPurpose : std::uint64_t
{
    Mating = 0,      ///< 亲代抽样
    Survival = 1,    ///< 存活重抽
    Crossover = 2,   ///< 交叉点
    Jitter = 3,      ///< jitter 步进
    AlleleSwitch = 4 ///< 等位基因替换
};

inline constexpr std::size_t kPurposeCount = 5;

// 两者手工同步过一次就会漂移；漂了会让 stream() 越界访问 engines_。
static_assert(kPurposeCount
              == static_cast<std::size_t>(RandomPurpose::AlleleSwitch) + 1);

/// 同一种子两次运行必须逐位相同，这是可测的硬要求。
///
/// 均匀分布不能用 std::*_distribution：其实现未由标准规定，不同标准库
/// 给出不同结果，会直接破坏上面这条要求。std::mt19937_64 的算法本身由
/// 标准规定，可以安全使用。
class RandomStreams
{
public:
    RandomStreams(std::uint64_t root_seed, std::uint64_t season_index)
    {
        for (std::size_t i = 0; i < kPurposeCount; ++i)
        {
            const std::uint64_t s =
                mix_seed(mix_seed(root_seed, season_index),
                         static_cast<std::uint64_t>(i));
            engines_[i].seed(s);
        }
    }

    [[nodiscard]] std::mt19937_64&
    stream(RandomPurpose purpose) noexcept
    {
        return engines_[static_cast<std::size_t>(purpose)];
    }

private:
    std::array<std::mt19937_64, kPurposeCount> engines_{};
};

/// 取高 24 位映射到 [0, 1)，24 位正好落在 float 尾数里，不引入舍入偏差。
[[nodiscard]] inline float uniform_unit(std::mt19937_64& rng) noexcept
{
    const auto bits = static_cast<std::uint32_t>(rng() >> 40);
    return static_cast<float>(bits) * 0x1.0p-24f;
}

/// Lemire 拒绝采样：对任意 bound 无偏。
///
/// 浮点乘法取整的做法在 bound 不是 2 的幂时会系统性地偏袒小索引，
/// 累积若干代后表现为等位基因频率的假性漂移。
///
/// \pre 调用方必须保证 `n > 0`：bound 为 0 时下面的取模会除零。调用点的
/// 长度都是编译期已知的非零常量，热路径不做运行时检查是有意的。
[[nodiscard]] inline std::size_t
uniform_index(std::mt19937_64& rng, std::size_t n) noexcept
{
    const std::uint64_t bound = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = (~bound + 1u) % bound;
    for (;;)
    {
        const std::uint64_t x = rng();
        if (x >= threshold)
        {
            return static_cast<std::size_t>(x % bound);
        }
    }
}

}  // namespace gamev2::genetics

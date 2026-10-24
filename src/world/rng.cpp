#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace gamev2::world {
namespace {

constexpr double kInv53 = 1.0 / 9007199254740992.0;
constexpr double kInv24 = 1.0 / 16777216.0;

/// splitmix64：把一个 seed 摊成高质量的 64 位。
std::uint64_t splitmix64(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 坐标混入种子。乘法散列而非异或链：相邻格子的哈希必须看起来无关。
std::uint64_t lattice_hash(std::uint64_t seed, std::int64_t x,
                           std::int64_t y, std::int64_t z) noexcept
{
    std::uint64_t h = seed;
    h ^= static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<std::uint64_t>(z) * 0x165667B19E3779F9ull;
    return splitmix64(h);
}

double lattice(std::uint64_t seed, std::int64_t x, std::int64_t y,
               std::int64_t z) noexcept
{
    return static_cast<double>(lattice_hash(seed, x, y, z) >> 11) * kInv53;
}

/// quintic 插值：一阶与二阶导数在格点处都为 0，避免出现格状痕迹。
double smooth(double t) noexcept
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

std::int64_t floor_to(std::int64_t& base, double v) noexcept
{
    base = static_cast<std::int64_t>(std::floor(v));
    return base;
}

}  // namespace

Pcg32::Pcg32(std::uint64_t seed) noexcept
    : state_(0), inc_((splitmix64(seed) << 1) | 1)
{
    next();
    state_ += seed;
    next();
}

std::uint32_t Pcg32::next() noexcept
{
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    const auto xorshifted =
        static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return std::rotr(xorshifted, rot);
}

std::uint32_t Pcg32::below(std::uint32_t n) noexcept
{
    if (n == 0)
    {
        return 0;
    }
    // 拒绝采样消除取模偏差：2^32 不整除 n 时，低位若干个值出现概率更高。
    const std::uint32_t threshold = (~n + 1u) % n;
    for (;;)
    {
        const std::uint32_t r = next();
        if (r >= threshold)
        {
            return r % n;
        }
    }
}

double Pcg32::unit() noexcept
{
    return static_cast<double>(next() >> 8) * kInv24;
}

std::uint64_t stage_seed(std::uint64_t seed, std::uint32_t stage) noexcept
{
    return splitmix64(seed ^ (static_cast<std::uint64_t>(stage)
                              * 0x9E3779B97F4A7C15ull));
}

double value_noise(std::uint64_t seed, double x, double y) noexcept
{
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    floor_to(x0, x);
    floor_to(y0, y);
    const double fx = smooth(x - static_cast<double>(x0));
    const double fy = smooth(y - static_cast<double>(y0));

    const double v00 = lattice(seed, x0, y0, 0);
    const double v10 = lattice(seed, x0 + 1, y0, 0);
    const double v01 = lattice(seed, x0, y0 + 1, 0);
    const double v11 = lattice(seed, x0 + 1, y0 + 1, 0);

    const double a = v00 + (v10 - v00) * fx;
    const double b = v01 + (v11 - v01) * fx;
    return a + (b - a) * fy;
}

double value_noise3(std::uint64_t seed, double x, double y, double z) noexcept
{
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t z0 = 0;
    floor_to(x0, x);
    floor_to(y0, y);
    floor_to(z0, z);
    const double fx = smooth(x - static_cast<double>(x0));
    const double fy = smooth(y - static_cast<double>(y0));
    const double fz = smooth(z - static_cast<double>(z0));

    double acc = 0.0;
    for (std::int64_t dz = 0; dz < 2; ++dz)
    {
        const double wz = dz == 0 ? 1.0 - fz : fz;
        for (std::int64_t dy = 0; dy < 2; ++dy)
        {
            const double wy = dy == 0 ? 1.0 - fy : fy;
            for (std::int64_t dx = 0; dx < 2; ++dx)
            {
                const double wx = dx == 0 ? 1.0 - fx : fx;
                acc += wx * wy * wz
                       * lattice(seed, x0 + dx, y0 + dy, z0 + dz);
            }
        }
    }
    return acc;
}

double fbm(std::uint64_t seed, double x, double y, int octaves,
           double lacunarity, double gain) noexcept
{
    double sum = 0.0;
    double amp = 1.0;
    double norm = 0.0;
    double freq = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * (value_noise(seed, x * freq, y * freq) * 2.0 - 1.0);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

double fbm3(std::uint64_t seed, double x, double y, double z, int octaves,
            double lacunarity, double gain) noexcept
{
    double sum = 0.0;
    double amp = 1.0;
    double norm = 0.0;
    double freq = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp
               * (value_noise3(seed, x * freq, y * freq, z * freq) * 2.0 - 1.0);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

}  // namespace gamev2::world

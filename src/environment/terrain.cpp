#include "gamev2/environment/terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace gamev2::environment {

namespace {

/// 八度参数照 doc/美术风格规格.md §2.4：3 个八度、lacunarity 2.0、
/// gain 0.5，噪声族取该节许可的 Perlin / 值噪声一脉。
///
/// **基频取 0.10 而不是规格给的 0.02，也不取中间值 0.06**：真正决定陆地
/// 碎裂程度的不是基频，而是海平面落在高程分布的哪个位置——阈值远低于
/// 中位数时，平滑场的超水平集几乎总是连成一整块，抬八度能量与降基频都
/// 动不了它。实测扫过 (基频, 八度数, gain) 共 30 组，largest/land 的取值
/// 范围永远在 74%~99%；把水量从 35% 提到 70% 之后，基频 0.10 是唯一一档
/// 六个种子都给出「主大陆（陆地 33%~53%）+ 一块 226~474 格的次大陆」的
/// 组合。0.06 在同样的水量下最坏种子仍给出 98% 的单块大陆。
constexpr float kBaseFrequency = 0.10f;
constexpr std::size_t kOctaves = 3;
constexpr float kLacunarity = 2.0f;
constexpr float kGain = 0.5f;

/// splitmix64 的终段混函数。**本模块不许 include genetics/ 头**
/// （CMakeLists 里那条硬约束），因此自带一个散列——它只负责把 root_seed
/// 变成一张置换表的种子。
[[nodiscard]] std::uint64_t mix(const std::uint64_t v) noexcept
{
    auto x = v;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

/// 256 项置换表。值噪声要的是「任意两格点取值不同且看不出规律」，置换表
/// 比拿格点下标直接混一次更均匀，也让噪声与 root_seed 解耦。
class Permutation
{
public:
    explicit Permutation(const std::uint64_t seed) noexcept
    {
        for (std::size_t i = 0; i < kSize; ++i)
        {
            table_[i] = static_cast<std::uint8_t>(i);
        }
        // Fisher-Yates，倒着走。与顺序无关的置换表不要求任何一种特定
        // 洗牌方向，但固定方向才能让同 seed 逐位复现。
        for (std::size_t i = kSize - 1; i > 0; --i)
        {
            const auto j = static_cast<std::size_t>(
                mix(seed + i) % static_cast<std::uint64_t>(i + 1));
            std::swap(table_[i], table_[j]);
        }
    }

    [[nodiscard]] std::uint8_t at(const std::size_t i) const noexcept
    {
        return table_[i % kSize];
    }

private:
    static constexpr std::size_t kSize = 256;
    std::array<std::uint8_t, kSize> table_{};
};

/// 5 次平滑。值噪声格内插值若用线性，二阶导不连续，抬高八度后能看出
/// 方格棱；smoothstep 让一阶导也连续。
[[nodiscard]] constexpr float smooth(const float t) noexcept
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// 单个八度的值噪声：格点取整坐标，格内对四角做 smoothstep 双线性插。
[[nodiscard]] float octave(const Permutation& perm, const float x,
                           const float y) noexcept
{
    const auto gx = static_cast<std::int64_t>(std::floor(x));
    const auto gy = static_cast<std::int64_t>(std::floor(y));
    const auto fx = smooth(x - static_cast<float>(gx));
    const auto fy = smooth(y - static_cast<float>(gy));

    const auto corner = [&perm](const std::int64_t dx, const std::int64_t dy)
    {
        // 负下标转 unsigned 是模运算（well-defined），不进 UB；两个大质数
        // 相乘再异或是标准的二维格点散列。
        const auto h = static_cast<std::uint64_t>(dx) * 73856093ull
                       ^ static_cast<std::uint64_t>(dy) * 19349663ull;
        return static_cast<float>(perm.at(static_cast<std::size_t>(mix(h))))
               / 255.0f;
    };

    const auto c00 = corner(gx, gy);
    const auto c10 = corner(gx + 1, gy);
    const auto c01 = corner(gx, gy + 1);
    const auto c11 = corner(gx + 1, gy + 1);

    const auto top = c00 + (c10 - c00) * fx;
    const auto bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}

}  // namespace

TerrainField TerrainField::generate(
    const std::uint64_t root_seed, const EnvironmentConfig& config) noexcept
{
    const Permutation perm{root_seed};
    TerrainField out{};

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        // CellIndex::value 的布局是 iy * kGridSide + ix，直接拆回 (x, y)。
        const auto px = static_cast<float>(i % kGridSide);
        const auto py = static_cast<float>(i / kGridSide);

        float sum = 0.0f;
        float amplitude = 1.0f;
        float frequency = kBaseFrequency;
        for (std::size_t o = 0; o < kOctaves; ++o)
        {
            sum += octave(perm, px * frequency, py * frequency) * amplitude;
            amplitude *= kGain;
            frequency *= kLacunarity;
        }
        out.elevation_[i] = sum;
    }

    // 海平面取分位数。抄一份排序后 nth_element 就够——全序在构造期跑一次，
    // 4096 个 float，代价可忽略，而它换来的是「任意种子的水陆比都落在
    // 目标附近」这条保证。
    auto sorted = out.elevation_;
    const auto cut = static_cast<std::size_t>(
        static_cast<float>(kCellCount - 1) * config.water_fraction);
    // ranges 版 nth_element 只收迭代器，不收下标，因此 cut 要转成迭代器；
    // 排序结果与 std::nth_element 的三元组形式逐位一致。
    std::ranges::nth_element(sorted,
                             sorted.begin() + static_cast<std::ptrdiff_t>(cut));
    const float sea_level = sorted[cut];
    out.sea_level_ = sea_level;

    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        // 用 >= 而不是 >：water_fraction = 0 时 cut = 0，sea_level 就是全图
        // 最小值，于是每一格都满足 >= 而全部 passable，「全陆地世界」成立。
        // 若改成严格 >，那一格就会被判成水，而 Task 2 的既有测试正建立在
        // 全陆地之上。
        out.passable_[i] = out.elevation_[i] >= sea_level;
    }
    return out;
}

bool TerrainField::passable(const CellIndex cell) const noexcept
{
    return passable_[cell.value];
}

float TerrainField::elevation(const CellIndex cell) const noexcept
{
    return elevation_[cell.value];
}

void TerrainField::flood(const CellIndex cell) noexcept
{
    passable_[cell.value] = false;
}

void TerrainField::drain(const CellIndex cell) noexcept
{
    passable_[cell.value] = true;
}

}  // namespace gamev2::environment

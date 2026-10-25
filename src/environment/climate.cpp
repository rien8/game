// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/climate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace gamev2::environment {

namespace {

/// splitmix64：把种子与格下标混成一个相关性不明显的 64 位值。
/// 不复用 genetics 的 mix_seed：设计文档明令 src/environment/ 不得 include
/// 任何 genetics/ 头文件，这是硬规则而非成本权衡，后来的任务不能因为
/// 「不划算」就擅自复用。两者的形态本就不同——这里只加一次黄金常数，
/// mix_seed 加的是 (purpose+1) 倍黄金常数。
[[nodiscard]] std::uint64_t mix(std::uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/// 把哈希映射到 [lo, hi)。取高 24 位：float 的尾数只有 24 位，
/// 多取低位只是把它丢掉。
[[nodiscard]] float uniform(std::uint64_t bits, float lo, float hi) noexcept
{
    const auto unit = static_cast<float>(bits >> 40)
                      / static_cast<float>(1ULL << 24);
    return lo + unit * (hi - lo);
}

}  // namespace

ClimateField::ClimateField(const std::uint64_t root_seed,
                           const EnvironmentConfig& config)
    : config_{config}
{
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto salt = static_cast<std::uint64_t>(i)
                          * 0x9e3779b97f4a7c15ULL;
        const auto base = mix(root_seed ^ salt);
        cells_[i].base_temperature = uniform(mix(base + 1), -8.0f, 8.0f);
        cells_[i].base_fertility = uniform(mix(base + 2), 0.25f, 1.0f);
        cells_[i].base_light = uniform(mix(base + 3), 0.6f, 1.0f);
    }
}

void ClimateField::advance(const std::uint32_t season_index) noexcept
{
    season_ = season_index;
}

EnvironmentSnapshot ClimateField::at(const CellIndex cell) const noexcept
{
    const auto& b = cells_[cell.value];

    const float period = std::max(1.0f, config_.temperature_period);
    const float phase = 2.0f * std::numbers::pi_v<float>
                        * static_cast<float>(season_) / period;
    const float swing = std::sin(phase);

    EnvironmentSnapshot s{};
    s.temperature =
        b.base_temperature + config_.temperature_amplitude * swing;
    // fertility 逐季不变：食物承载力是生境属性，气候才是季节属性。
    s.fertility = b.base_fertility;
    // 光照与温度同相：季节是同一个太阳在走，把两者拆成独立相位会让
    // 「夏天暗」这种组合自然出现，而它没有物理来源。
    s.light = b.base_light
              * (1.0f - config_.light_amplitude
                 + config_.light_amplitude * swing);
    return s;
}

const ClimateCell& ClimateField::base(const CellIndex cell) const noexcept
{
    return cells_[cell.value];
}

}  // namespace gamev2::environment

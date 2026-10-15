#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::genetics {

enum class TraitKind : std::uint8_t
{
    BodySize,
    Speed,
    Strength,
    Metabolism,
    Lifespan,
    SenseRange,
    Fecundity,
    Resilience,
    kTraitCount
};

inline constexpr std::size_t kTraitCount =
    static_cast<std::size_t>(TraitKind::kTraitCount);

[[nodiscard]] constexpr std::size_t trait_index(TraitKind t) noexcept
{
    return static_cast<std::size_t>(t);
}

/// 固定结构目录：字段集合编译期固定，才能定长进 ECS。
struct Phenotype
{
    std::array<float, kTraitCount> traits{};

    std::uint8_t body_segments = 1;
    std::uint8_t limb_pairs = 0;
    std::uint8_t eyes = 0;
    std::uint8_t flags = 0;  ///< bit 0 壳 / 1 翼 / 2 鳍 / 3 腺体
};

}  // namespace gamev2::genetics

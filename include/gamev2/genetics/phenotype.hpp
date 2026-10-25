/// @file phenotype.hpp
/// @brief 表型的值类型定义：性状轴 TraitKind 的固定目录与定长结构
///        Phenotype。
///
/// 性状用一组连续浮点轴表达，结构特征用离散字段表达，二者分开存放而非塞进
/// 同一张表，是为了让表达层能对两者施加完全不同的规则：前者可被 clamp 与环境
/// 修正，后者只能被等位基因替换。字段集合编译期固定，才能定长进 ECS。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gamev2::genetics {

/// 性状轴的固定目录。枚举顺序即下标，kTraitCount 是哨兵而非真实性状。
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

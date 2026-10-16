/// @file species.hpp
/// @brief 物种标识与登记表：SpeciesId 句柄、描述「该物种启用哪些位点」的
///        SpeciesMask，以及签发并保存掩码的 SpeciesRegistry。
///
/// 物种之间共享同一张位点蓝图，差异只落在激活掩码上，因此新建物种是廉价操作。
/// 本模块把物种视为外部给定的静态标签，不涉及物种形成——那是下一代 spec 的事。
#pragma once

#include "gamev2/genetics/genome.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace gamev2::genetics {

/// 物种句柄。value 为 0 是保留的非法值，登记表签发的 id 从 1 起。
struct SpeciesId
{
    std::uint32_t value = 0;

    constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }

    friend constexpr bool operator==(SpeciesId, SpeciesId) noexcept = default;
};

/// 物种之间共享同一张位点蓝图，差异只体现在「启用哪些位点」。
struct SpeciesMask
{
    std::bitset<kLocusCount> active{};
};

/// 物种登记的失败原因。没有任何激活位点的物种无法产生表型，注册期即拒绝，
/// 而不是等到表达层才静默产出全零表型。
enum class SpeciesError
{
    /// 没有激活任何位点的物种无法产生表型，也无法参与表达层
    EmptyMask,
    TooManySpecies,
};

/// 物种在本轮是外部给定的静态标签。形成新物种属于下一代 spec；
/// 这里只保证位点掩码的接口在那里。
class SpeciesRegistry
{
public:
    [[nodiscard]] std::expected<SpeciesId, SpeciesError>
    register_species(SpeciesMask mask);

    /// \pre `id` 是本登记表签发过的 SpeciesId（`value` 落在
    /// `[1, species_count()]`）。下标 0 与越界 id 都来自调用方——settle 由
    /// SeasonInput 里调用方给的 `ParentPool::species` 调它；越界时返回一张
    /// 全空掩码而不是越界读，见实现里的说明。
    [[nodiscard]] const SpeciesMask& mask(SpeciesId id) const;

    [[nodiscard]] std::size_t species_count() const noexcept
    {
        return masks_.size();
    }

private:
    /// 下标 0 是保留的非法值，因此 id 从 1 起，mask 数组下标用 value - 1。
    std::vector<SpeciesMask> masks_;
};

}  // namespace gamev2::genetics

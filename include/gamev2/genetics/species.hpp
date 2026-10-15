#pragma once

#include "gamev2/genetics/genome.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace gamev2::genetics {

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

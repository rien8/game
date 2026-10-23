/// @file quota.cpp
/// @brief 台账与派系演化层共用的配额原语的实现。
#include "gamev2/history/quota.hpp"
#include "gamev2/world/spirit_tables.hpp"

namespace gamev2::history {

std::uint64_t tide_integral(std::uint32_t epoch, std::uint32_t now)
{
    std::uint64_t sum = 0;
    for (std::uint32_t year = epoch; year < now; ++year)
    {
        sum += gamev2::world::compute_spirit_tide(year).level;
    }
    return sum;
}

std::uint64_t quota_conversion(std::uint64_t sum_total,
                               std::uint64_t sum_rate,
                               std::uint64_t integral)
{
    // 这道守卫是给没有前置判断的调用方兜底的。台账侧调用前已判过
    // integral == 0 / sum_rate == 0 并早退，所以从台账这一侧看它是
    // 走不到的；派系演化层没有那道前置判断，去掉它那边就会除零。
    if (integral == 0u || sum_rate == 0u)
    {
        return 0u;
    }
    return sum_total * 255ull * kQuotaFixedScale / (integral * sum_rate);
}

std::uint64_t QuotaRemainder::take(std::uint32_t rate, std::uint32_t level,
                                    std::uint64_t conversion)
{
    const std::uint64_t num =
        static_cast<std::uint64_t>(rate) * level * conversion;
    const std::uint64_t total = num + remainder_;
    remainder_ = total % kQuotaFixedDen;
    return total / kQuotaFixedDen;
}

bool is_yin_vein(const gamev2::world::World& world, std::uint32_t vein_id)
{
    for (std::uint32_t cell = 0; cell < world.vein_id.size(); ++cell)
    {
        if (world.vein_id.at(cell) != vein_id)
        {
            continue;
        }
        if (world.land_type.at(cell) == gamev2::world::LandType::yin)
        {
            return true;
        }
    }
    return false;
}

}  // namespace gamev2::history

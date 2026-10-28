// 许可证注释：模块说明在 doc/2026-10-16-environment-design.md
#include "gamev2/environment/resource.hpp"

#include "gamev2/environment/sites.hpp"

#include <algorithm>

namespace gamev2::environment {

ResourceField::ResourceField(const ClimateField& climate,
                             const EnvironmentConfig& config)
    : regen_rate_{config.regen_rate}
{
    for (std::size_t i = 0; i < kCellCount; ++i)
    {
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        cells_[i].capacity =
            config.capacity_base * climate.base(index).base_fertility;
        cells_[i].stock = cells_[i].capacity;
    }
}

void ResourceField::regrow() noexcept
{
    for (auto& cell : cells_)
    {
        cell.stock += regen_rate_ * (cell.capacity - cell.stock);
    }
}

void ResourceField::draw(const CellIndex cell, const float amount) noexcept
{
    cells_[cell.value].stock =
        std::max(0.0f, cells_[cell.value].stock - amount);
}

float ResourceField::stock(const CellIndex cell) const noexcept
{
    return cells_[cell.value].stock;
}

float ResourceField::capacity(const CellIndex cell) const noexcept
{
    return cells_[cell.value].capacity;
}

std::uint32_t ResourceField::occupants(const CellIndex cell) const noexcept
{
    return occupants_[cell.value];
}

void ResourceField::count_occupants(const std::span<const Forager> foragers)
{
    occupants_.fill(0);
    for (const auto& f : foragers)
    {
        ++occupants_[CellIndex::of(f.x, f.y).value];
    }
}

}  // namespace gamev2::environment

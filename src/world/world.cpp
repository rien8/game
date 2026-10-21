#include "gamev2/world/world.hpp"

namespace gamev2::world {
namespace {

/// 三维量就是每 z 层一份同构网格，用同一个工厂避免四段重复的循环。
template<typename T>
std::vector<Grid<T>> make_layers(const WorldParams& params, T fill)
{
    std::vector<Grid<T>> layers;
    layers.reserve(params.z_levels);
    for (std::uint32_t z = 0; z < params.z_levels; ++z)
    {
        layers.emplace_back(params.width, params.height, fill);
    }
    return layers;
}

}  // namespace

World::World(WorldParams params)
    : params_(params),
      elevation(params.width, params.height, 0),
      rainfall(params.width, params.height, 0),
      temperature(params.width, params.height, 128),
      biome(params.width, params.height, Biome::ocean),
      downstream(params.width, params.height, kNoDownstream),
      flow(params.width, params.height, 0),
      river_width(params.width, params.height, 0),
      lake_depth(params.width, params.height, 0),
      drainage(params.width, params.height, 100),
      soil_depth(params.width, params.height, 0),
      aquifer_top(params.width, params.height, 0),
      ley_strength(params.width, params.height, 0),
      ley_depth(params.width, params.height, 0),
      ley_element(params.width, params.height, Element::none),
      vein_rank(params.width, params.height, VeinRank::none),
      vein_id(params.width, params.height, kNoVein),
      eye_flow(params.width, params.height, 0),
      domain_concentration(params.width, params.height, 0),
      domain_grade(params.width, params.height, DomainGrade::barren),
      scar_kind(params.width, params.height, Scar::none),
      spirit_suppression(params.width, params.height, 0),
      land_type(params.width, params.height, LandType::mundane),
      eye_purity(params.width, params.height, 0),
      cave_heaven(params.width, params.height, false),
      tribulation_site(params.width, params.height, false),
      stone(make_layers(params, Stone::granite)),
      deposit(make_layers(params, Deposit::none)),
      cavern(make_layers(params, false)),
      aquifer(make_layers(params, false))
{
}

std::uint32_t World::depth() const noexcept
{
    return params_.z_levels * params_.z_cell_depth;
}

std::uint32_t World::cell_depth(std::uint32_t z,
                                std::uint32_t d) const noexcept
{
    return z * params_.z_cell_depth + d;
}

bool World::is_ocean(std::uint32_t x, std::uint32_t y) const noexcept
{
    return elevation(x, y) < sea_level_;
}

}  // namespace gamev2::world

#include "gamev2/world/generate.hpp"

namespace gamev2::world {

std::expected<World, WorldError> generate_world(const WorldParams& params)
{
    if (!params.is_valid())
    {
        return std::unexpected(WorldError::invalid_params);
    }

    World world(params);
    make_elevation(world);
    make_rainfall(world);
    make_temperature(world);
    make_hydrology(world);
    make_biomes(world);
    make_soils(world);
    make_strata(world);
    make_caverns(world);
    make_deposits(world);
    finalize_aquifers(world);
    make_spirit_tide(world);
    make_ley_network(world);
    make_spirit_veins(world);
    make_spirit_domains(world);
    make_historical_scars(world);
    make_land_types(world);
    make_cave_heavens(world);
    make_tribulation_sites(world);
    return world;
}

}  // namespace gamev2::world

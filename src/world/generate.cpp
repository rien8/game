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
    return world;
}

}  // namespace gamev2::world

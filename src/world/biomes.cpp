#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"

namespace gamev2::world {

void make_biomes(World& world)
{
    const WorldParams& p = world.params();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.lake_depth.at(i) > 0)
            {
                world.biome.at(i) = Biome::lake;
                continue;
            }
            world.biome.at(i) =
                select_biome(elev_class_of(world.elevation.at(i),
                                           world.sea_level()),
                             world.drainage.at(i), world.rainfall.at(i),
                             world.temperature.at(i));
        }
    }
}

}  // namespace gamev2::world

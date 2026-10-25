#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

namespace {

using gamev2::world::make_elevation;
using gamev2::world::make_rainfall;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 96;
    p.height = 96;
    p.seed = seed;
    return p;
}

TEST(rainfall_is_wettest_at_the_coast_and_dries_inland)
{
    World w(params(5));
    make_elevation(w);
    make_rainfall(w);

    double ocean_sum = 0.0;
    double land_sum = 0.0;
    std::uint32_t ocean_n = 0;
    std::uint32_t land_n = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const double v = w.rainfall(x, y);
            if (w.is_ocean(x, y))
            {
                ocean_sum += v;
                ++ocean_n;
            }
            else
            {
                land_sum += v;
                ++land_n;
            }
        }
    }
    CHECK(ocean_n > 100);
    CHECK(land_n > 100);
    CHECK(ocean_sum / ocean_n > land_sum / land_n);
}

TEST(rainfall_is_deterministic)
{
    World a(params(5));
    World b(params(5));
    make_elevation(a);
    make_elevation(b);
    make_rainfall(a);
    make_rainfall(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.rainfall.size(); ++i)
    {
        same = same && a.rainfall.at(i) == b.rainfall.at(i);
    }
    CHECK(same);
}

}  // namespace

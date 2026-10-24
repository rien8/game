#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>

namespace {

using gamev2::world::make_elevation;
using gamev2::world::make_rainfall;
using gamev2::world::make_temperature;
using gamev2::world::World;
using gamev2::world::WorldParams;

WorldParams params()
{
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.seed = 11;
    return p;
}

TEST(temperature_falls_towards_the_poles)
{
    World w(params());
    make_elevation(w);
    make_rainfall(w);
    make_temperature(w);

    // 比较赤道行与北行，而非南北两极行：纬向基线关于 y = height/2 严格
    // 对称，两极行的基线逐字相等，那样的断言不含任何纬度信号，只测噪声。
    const std::uint32_t north = w.params().height - 1;
    const std::uint32_t equator = w.params().height / 2;
    double equator_sum = 0.0;
    double north_sum = 0.0;
    for (std::uint32_t x = 0; x < w.params().width; ++x)
    {
        equator_sum += w.temperature(x, equator);
        north_sum += w.temperature(x, north);
    }
    CHECK(north_sum < equator_sum);
}

TEST(temperature_anticorrelates_with_height_above_sea)
{
    World w(params());
    make_elevation(w);
    make_rainfall(w);
    make_temperature(w);

    // 用协方差而不是分组均值：分组在样本偏斜时会不稳定，而协方差只看
    // 整体趋势，正是高度递减率该被验证的东西。
    const double n = static_cast<double>(w.elevation.size());
    double sx = 0.0;
    double sy = 0.0;
    double sxy = 0.0;
    for (std::uint32_t i = 0; i < w.elevation.size(); ++i)
    {
        const double above = std::max(
            0.0, static_cast<double>(w.elevation.at(i)) - w.sea_level());
        const double t = w.temperature.at(i);
        sx += above;
        sy += t;
        sxy += above * t;
    }
    CHECK(sxy / n - (sx / n) * (sy / n) < 0.0);
}

TEST(temperature_is_deterministic)
{
    World a(params());
    World b(params());
    make_elevation(a);
    make_elevation(b);
    make_rainfall(a);
    make_rainfall(b);
    make_temperature(a);
    make_temperature(b);

    bool same = true;
    for (std::uint32_t i = 0; i < a.temperature.size(); ++i)
    {
        same = same && a.temperature.at(i) == b.temperature.at(i);
    }
    CHECK(same);
}

}  // namespace

#include "gamev2/world/rng.hpp"
#include "test_framework.hpp"

#include <array>

namespace {

using gamev2::world::fbm;
using gamev2::world::fbm3;
using gamev2::world::Pcg32;
using gamev2::world::stage_seed;
using gamev2::world::value_noise;
using gamev2::world::value_noise3;

TEST(pcg32_is_reproducible_and_bounded)
{
    Pcg32 a(12345);
    Pcg32 b(12345);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK_EQ(a.next(), b.next());
    }

    Pcg32 c(12345);
    bool below_in_range = true;
    for (int i = 0; i < 1000; ++i)
    {
        below_in_range = below_in_range && c.below(7) < 7;
    }
    CHECK(below_in_range);
    CHECK_EQ(Pcg32(1).below(0), 0u);

    Pcg32 d(1);
    bool unit_in_range = true;
    for (int i = 0; i < 1000; ++i)
    {
        const double u = d.unit();
        unit_in_range = unit_in_range && u >= 0.0 && u < 1.0;
    }
    CHECK(unit_in_range);
}

TEST(pcg32_below_covers_every_bucket)
{
    std::array<bool, 5> seen{};
    Pcg32 rng(2024);
    for (int i = 0; i < 500; ++i)
    {
        seen[rng.below(5)] = true;
    }
    for (const bool hit : seen)
    {
        CHECK(hit);
    }
}

TEST(stage_seeds_are_reproducible_and_independent)
{
    CHECK_EQ(stage_seed(9, 0), stage_seed(9, 0));
    CHECK(stage_seed(9, 0) != stage_seed(9, 1));
    CHECK(stage_seed(9, 1) != stage_seed(8, 1));
}

TEST(value_noise_is_stateless_and_bounded)
{
    // 无状态是硬约束：求值顺序不影响结果，所以反复取值必须一致。
    CHECK_EQ(value_noise(7, 3.5, -2.25), value_noise(7, 3.5, -2.25));
    CHECK(value_noise(7, 3.5, -2.25) != value_noise(8, 3.5, -2.25));

    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = value_noise(7, i * 0.37, i * -0.11);
        in_range = in_range && v >= 0.0 && v <= 1.0;
    }
    CHECK(in_range);
}

TEST(value_noise3_is_bounded)
{
    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = value_noise3(7, i * 0.37, i * -0.11, i * 0.53);
        in_range = in_range && v >= 0.0 && v <= 1.0;
    }
    CHECK(in_range);
}

TEST(fbm_stays_within_unit_range)
{
    bool in_range = true;
    for (int i = 0; i < 500; ++i)
    {
        const double v = fbm(3, i * 0.21, i * 0.17, 4, 2.0, 0.5);
        const double w = fbm3(3, i * 0.21, i * 0.17, i * 0.13, 3, 2.0, 0.5);
        in_range = in_range && v >= -1.0 && v <= 1.0;
        in_range = in_range && w >= -1.0 && w <= 1.0;
    }
    CHECK(in_range);
}

}  // namespace

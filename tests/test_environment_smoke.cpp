#include "gamev2/environment/climate.hpp"

#include "test_framework.hpp"

using gamev2::environment::EnvironmentSnapshot;

TEST(environment_snapshot_defaults_mean_no_effect)
{
    const EnvironmentSnapshot s{};
    CHECK_EQ(s.temperature, 0.0f);
    CHECK_EQ(s.fertility, 1.0f);
    CHECK_EQ(s.light, 1.0f);
}

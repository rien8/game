#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <cstdint>

using gamev2::environment::EnvironmentConfig;
using gamev2::environment::EnvironmentSnapshot;

TEST(environment_snapshot_defaults_mean_no_effect)
{
    const EnvironmentSnapshot s{};
    CHECK_EQ(s.temperature, 0.0f);
    CHECK_EQ(s.fertility, 1.0f);
    CHECK_EQ(s.light, 1.0f);
}

TEST(the_seed_founder_pool_defaults_to_a_single_world_wide_lineage)
{
    // 播种住在 sim 的匿名 namespace 里（刻意不进测试目标 —— 见 CMakeLists 的
    // 排除注释），因此这里只能钉住配置项本身。真正的守卫是 Task 6 的 θ 实测与
    // `water_actually_splits_the_population` —— 它们会在播种被改回「每体独立
    // 随机」时变红。
    const EnvironmentConfig config{};
    CHECK_EQ(config.seed_founder_pool, std::uint32_t{32});
    CHECK(config.seed_mutation_rate > 0.0f);
    CHECK(config.seed_mutation_rate < 0.01f);
}

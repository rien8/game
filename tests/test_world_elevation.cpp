#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <array>

namespace {

using gamev2::world::generate_world;
using gamev2::world::make_elevation;
using gamev2::world::World;
using gamev2::world::WorldError;
using gamev2::world::WorldParams;

WorldParams small_params()
{
    WorldParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 4242;
    return p;
}

TEST(elevation_stays_in_range_and_hits_target_ocean_share)
{
    World w(small_params());
    make_elevation(w);

    bool in_range = true;
    std::uint32_t ocean = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            in_range = in_range && w.elevation(x, y) <= 100;
            ocean += w.is_ocean(x, y) ? 1u : 0u;
        }
    }
    CHECK(in_range);

    const double ratio = static_cast<double>(ocean) / w.elevation.size();
    CHECK(ratio > 0.55);
    CHECK(ratio < 0.65);
}

TEST(elevation_ocean_share_tracks_ocean_percent_without_seed_drift)
{
    // 参数表同时覆盖「换种子」与「换 ocean_percent」两个维度：前者是
    // params.hpp 对反解海平面作出的承诺（海陆比不随种子漂移），后者证明
    // 该参数确实接进了反解，而不是被某个固定常量吃掉。
    struct Case
    {
        std::uint64_t seed;
        std::uint8_t ocean_percent;
    };
    constexpr Case cases[] = {
        {4242, 60}, {777, 60}, {0, 60},      {99999, 60},
        {4242, 30}, {777, 30}, {99999, 30},   {4242, 80},
    };

    for (const Case& c : cases)
    {
        WorldParams p = small_params();
        p.seed = c.seed;
        p.ocean_percent = c.ocean_percent;

        World w(p);
        make_elevation(w);

        std::uint32_t ocean = 0;
        for (std::uint32_t y = 0; y < w.params().height; ++y)
        {
            for (std::uint32_t x = 0; x < w.params().width; ++x)
            {
                ocean += w.is_ocean(x, y) ? 1u : 0u;
            }
        }

        const double target = c.ocean_percent / 100.0;
        const double ratio = static_cast<double>(ocean) / w.elevation.size();

        // 上界取名义值本身而非经验带：反解出的海平面是累计计数首次达到
        // cut 的那一档，而 is_ocean 又是严格小于该档，所以海洋格数必然少于
        // cut，比值只能低于名义值。把 is_ocean 放宽成 <= 会把整桶格子拉进
        // 海洋，正好越过这道上界。
        CHECK(ratio < target);
        // 下界欠量至多为切中那一桶的占据量：0..100 的量化把 64×64 切成
        // 约 100 个桶，单桶不超过格数的 3%，故留 3pp 余量。
        CHECK(ratio > target - 0.03);
    }
}

TEST(sea_level_is_the_lowest_elevation_reaching_the_ocean_quota)
{
    // 海陆比抓不到把 seen >= cut 写成 > 的变体：在前缀恰好落在 cut 的种子上，
    // 该变体仍停在同一档的下一档，海陆比正好等于 cut/N——那是正确实现能给出
    // 的最好结果，仍落在正确实现的取值范围内。两种实现只有在「前缀恰好落在
    // cut」时才产生分歧，所以要分辨只能直接钉住反解的定义本身：海平面是累计
    // 计数首次达到 cut 的最小档位。seed 9 是扫描中第一个触发该平局的种子。
    WorldParams p = small_params();
    p.seed = 9;

    World w(p);
    make_elevation(w);

    std::array<std::uint32_t, 256> histogram{};
    for (std::uint32_t i = 0; i < w.elevation.size(); ++i)
    {
        ++histogram[w.elevation.at(i)];
    }
    const std::uint32_t cut = w.elevation.size() * p.ocean_percent / 100;

    const std::uint8_t sea = w.sea_level();
    std::uint32_t below = 0;
    for (std::uint32_t v = 0; v < sea; ++v)
    {
        below += histogram[v];
    }
    CHECK(below < cut);
    CHECK(below + histogram[sea] >= cut);
}

TEST(elevation_is_deterministic_and_seed_sensitive)
{
    const WorldParams p = small_params();

    World a(p);
    World b(p);
    make_elevation(a);
    make_elevation(b);

    WorldParams other = p;
    other.seed = 777;
    World c(other);
    make_elevation(c);

    bool ab_same = true;
    bool c_differs = false;
    for (std::uint32_t i = 0; i < a.elevation.size(); ++i)
    {
        ab_same = ab_same && a.elevation.at(i) == b.elevation.at(i);
        c_differs = c_differs || a.elevation.at(i) != c.elevation.at(i);
    }
    CHECK(ab_same);
    CHECK(c_differs);
    CHECK_EQ(a.sea_level(), b.sea_level());
}

TEST(generate_world_rejects_degenerate_params)
{
    WorldParams p;
    p.width = 0;

    auto rejected = generate_world(p);
    CHECK(!rejected.has_value());
    if (!rejected.has_value())
    {
        CHECK(rejected.error() == WorldError::invalid_params);
    }

    CHECK(generate_world(WorldParams{}).has_value());
}

}  // namespace

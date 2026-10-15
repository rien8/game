#include "gamev2/genetics/random.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <set>

using namespace gamev2::genetics;

TEST(random_uniform_unit_stays_in_range)
{
    auto rng = std::mt19937_64{42};
    for (int i = 0; i < 10000; ++i)
    {
        const float u = uniform_unit(rng);
        CHECK(u >= 0.0f);
        CHECK(u < 1.0f);
    }
}

TEST(random_uniform_index_covers_full_range)
{
    auto rng = std::mt19937_64{7};
    std::set<std::size_t> seen;
    for (int i = 0; i < 5000; ++i)
    {
        seen.insert(uniform_index(rng, 5));
    }
    CHECK_EQ(seen.size(), std::size_t{5});
}

TEST(random_uniform_index_is_unbiased_enough)
{
    // 拒绝采样应给出大致均匀的分布，允许 8% 相对偏差作为容差。
    //
    // 但要清楚这条断言的真实边界：uniform_unit 只取 24 位有效位，
    // 对 bound 3–7 的相对偏差实测 < 1e-5%，远低于本容差。换任何 bound
    // 都无法让它证伪「浮点偏差」这一类系统性偏差——它的实际价值是
    // 「没有索引不可达」与「分布没有肉眼可见的偏斜」。
    auto rng = std::mt19937_64{1234};
    std::array<int, 5> counts{};
    constexpr int kDraws = 40000;
    for (int i = 0; i < kDraws; ++i)
    {
        ++counts[uniform_index(rng, 5)];
    }
    const double expected = static_cast<double>(kDraws) / 5.0;
    for (const auto c : counts)
    {
        const double ratio = static_cast<double>(c) / expected;
        CHECK(ratio > 0.92);
        CHECK(ratio < 1.08);
    }
}

TEST(uniform_index_stays_below_a_huge_bound)
{
    // 这条用例锁的是**取值范围**，不是拒绝分支本身。真实调用点的 bound 是
    // 5 / kMaxCrossovers+1 / kLocusCount，threshold = 2^64 % bound 分别是
    // 1 / 7 / 16，拒绝概率都在 2^-64 量级，一次都走不到；只有
    // bound ≈ 2^63 时拒绝率才约 50%，而项目里没有这种调用点。
    //
    // 保留这个巨大 bound 仍有价值：模数与真实调用点差了几个数量级，能顺带
    // 抓住「下标算错后被夹取」这类变异。此前这里还复制了一遍 uniform_index
    // 的接受条件去数 rejections > 0 —— 那个 > 0 在任何实现下都成立（复制品
    // 必然走自己的计数），证明不了真函数有拒绝分支，已删。
    constexpr std::size_t kBound = (std::size_t{1} << 63) + 1;
    auto rng = std::mt19937_64{2024};

    std::size_t lowest = kBound;
    std::size_t highest = 0;
    for (int i = 0; i < 200000; ++i)
    {
        const std::size_t v = uniform_index(rng, kBound);
        CHECK(v < kBound);
        lowest = std::min(lowest, v);
        highest = std::max(highest, v);
    }
    // 取值要真正铺开，而不是全挤在阈值附近。
    CHECK(lowest < highest);
}

TEST(random_streams_are_independent_across_purposes)
{
    RandomStreams a{1, 0};
    RandomStreams b{1, 0};
    // 同一条用途在两次构造后必须给出完全相同的序列。
    for (int i = 0; i < 100; ++i)
    {
        CHECK(uniform_unit(a.stream(RandomPurpose::Crossover))
              == uniform_unit(b.stream(RandomPurpose::Crossover)));
    }

    RandomStreams c{2, 0};
    RandomStreams d{1, 0};
    CHECK(uniform_unit(c.stream(RandomPurpose::Crossover))
          != uniform_unit(d.stream(RandomPurpose::Crossover)));
}

TEST(random_streams_differ_across_seasons)
{
    RandomStreams a{1, 0};
    RandomStreams b{1, 1};
    CHECK(uniform_unit(a.stream(RandomPurpose::Mating))
          != uniform_unit(b.stream(RandomPurpose::Mating)));
}

TEST(random_streams_isolate_purposes)
{
    // 在一条用途上多抽一次，不得影响另一条用途的序列。
    RandomStreams a{9, 0};
    RandomStreams b{9, 0};
    for (int i = 0; i < 50; ++i)
    {
        (void)uniform_unit(a.stream(RandomPurpose::Jitter));
    }
    for (int i = 0; i < 50; ++i)
    {
        CHECK(uniform_unit(a.stream(RandomPurpose::Mating))
              == uniform_unit(b.stream(RandomPurpose::Mating)));
    }
}

TEST(mix_seed_is_stable)
{
    CHECK(mix_seed(0, 0) != mix_seed(0, 1));
    CHECK(mix_seed(0, 0) != mix_seed(1, 0));
    CHECK(mix_seed(12345, 7) != mix_seed(12345, 8));
}

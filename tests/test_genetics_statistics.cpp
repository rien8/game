#include "gamev2/genetics/statistics.hpp"

#include "test_framework.hpp"

#include <cstddef>
#include <vector>

using namespace gamev2::genetics;

namespace {

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

Genome make_genome(AlleleId a, AlleleId b)
{
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i].allele = a;
        g.b[i].allele = b;
    }
    return g;
}

}  // namespace

TEST(statistics_on_empty_population_is_zero)
{
    const std::vector<Genome> none;
    const auto s = summarize(none, all_loci());
    CHECK_EQ(s.census, std::uint32_t{0});
    CHECK(s.loci.empty());

    // 非空种群 + 空掩码是另一条退化路径：没有激活位点时杂合度的平均值
    // 分母为 0，缺额求和也为空。前者漏掉会得到 NaN，后者漏掉会得到 +inf。
    const std::vector<Genome> one{make_genome(0, 1)};
    const auto m = summarize(one, SpeciesMask{});
    CHECK(m.loci.empty());
    CHECK(m.heterozygosity == 0.0f);
    CHECK(m.effective_population == 0.0f);
}

TEST(statistics_counts_alleles)
{
    // 3 个 Aa + 1 个 aa，共 8 条等位基因：A = 3，a = 5。
    // p(A) = 3/8 = 0.375，p(a) = 5/8 = 0.625
    // H_obs = 3/4 = 0.75，H_exp = 2 × 0.375 × 0.625 = 0.46875
    const std::vector<Genome> pop{make_genome(0, 1), make_genome(0, 1),
                                  make_genome(0, 1), make_genome(1, 1)};
    const auto s = summarize(pop, all_loci());
    CHECK_EQ(s.census, std::uint32_t{4});
    const auto& l = s.loci[0];
    CHECK(l.frequency[0] == 0.375f);
    CHECK(l.frequency[1] == 0.625f);
    CHECK(l.h_observed == 0.75f);
    CHECK(l.h_expected == 0.46875f);

    // 明细只覆盖掩码点名的位点，不是 300 个全量输出。
    SpeciesMask single{};
    single.active.set(5);
    CHECK_EQ(summarize(pop, single).loci.size(), std::size_t{1});
}

TEST(statistics_reports_negative_f_for_heterozygote_excess)
{
    // 同一个群体：H_obs 0.75 高于 H_exp 0.46875，
    // F = 1 - 0.75 / 0.46875 = -0.6。负值表示杂合过剩，不是近交。
    const std::vector<Genome> pop{make_genome(0, 1), make_genome(0, 1),
                                  make_genome(0, 1), make_genome(1, 1)};
    const auto s = summarize(pop, all_loci());
    CHECK(s.f_coefficient < 0.0f);

    // 上面的群体每个位点都长一个样，看不出权重。换成两种位点：
    // 位点 0-3 是 3:1 纯合，H_exp = 0.375、H_obs = 0，故 F = 1、权重 0.375；
    // 位点 4-8 全员杂合，H_exp = 0.5、H_obs = 1，故 F = -1、权重 0.5；
    // 其余 291 个位点单态，H_exp = 0，按阈值跳过。
    // 加权 F = (4 × 0.375 - 5 × 0.5) / (4 × 0.375 + 5 × 0.5)
    //         = -1 / 4 = -0.25。
    // 丢掉权重改成等权平均，同一组数据会得 (4 - 5) / 9 ≈ -0.111。
    Genome common = make_genome(0, 0);
    Genome rare = common;
    for (std::size_t i = 0; i < 4; ++i)
    {
        rare.a[i].allele = 1;
        rare.b[i].allele = 1;
    }
    std::vector<Genome> mixed{common, common, common, rare};
    for (auto& genome : mixed)
    {
        for (std::size_t i = 4; i < 9; ++i)
        {
            genome.a[i].allele = 0;
            genome.b[i].allele = 1;
        }
    }
    CHECK(summarize(mixed, all_loci()).f_coefficient == -0.25f);
}

TEST(statistics_falls_back_to_zero_for_monomorphic_population)
{
    // 全体同型合子且基因型相同：每位点 p 均为 1 或 0，H_exp = 0，全部位点
    // 低于阈值。此时 F 的加权分母为 0，必须退回 0 而不是 NaN 或除零。
    // （若只是「人人都是纯合子」但基因型有两种，如 3 个 AA + 1 个 aa，
    //   p = 0.75 / 0.25，H_exp = 0.375，F 照常算得出 1，并不进这条分支。）
    const std::vector<Genome> pop{make_genome(0, 0), make_genome(0, 0),
                                  make_genome(0, 0), make_genome(0, 0)};
    const auto s = summarize(pop, all_loci());
    CHECK(s.f_coefficient == 0.0f);
}

TEST(statistics_clamps_out_of_range_allele_indices)
{
    // Genome 是公开的 ECS 组件，任何 World::add<Genome>() 都能塞进 uint8_t
    // 宽度的越界 AlleleId，而 counts 只有 kMaxAlleles 个元素。std::array 不在
    // _ITERATOR_DEBUG_LEVEL 的管辖范围内，debug STL 的边界检查抓不到它。
    Genome wild = make_genome(0, 1);
    wild.a[0].allele = 200;
    wild.b[0].allele = 201;
    const std::vector<Genome> pop{make_genome(0, 1), wild};

    SpeciesMask single{};
    single.active.set(0);
    const auto s = summarize(pop, single);

    CHECK_EQ(s.census, std::uint32_t{2});
    CHECK_EQ(s.loci.size(), std::size_t{1});
    // 越界的两个值都夹进第 4 个槽位，与另一个个体的两条等位基因共 4 条里
    // 的 2 条。跳过计数则会让 sum(counts) = 2 而非 2n。
    CHECK(s.loci[0].frequency[3] == 0.5f);
    CHECK(s.loci[0].frequency[0] == 0.25f);
    CHECK(s.loci[0].frequency[1] == 0.25f);
    const float sum = s.loci[0].frequency[0] + s.loci[0].frequency[1]
                      + s.loci[0].frequency[2] + s.loci[0].frequency[3];
    CHECK(sum == 1.0f);
    // 两个越界值夹取后同为槽位 3，被判成纯合子；另一个个体是杂合子。
    CHECK(s.loci[0].h_observed == 0.5f);
}

TEST(statistics_counts_monomorphic_loci_at_full_loss_in_ne)
{
    // 全体同型合子：每位点 H_exp = 0，故 h_loss_sum = 300，Ne = 1 / (2 × 300)。
    const std::vector<Genome> pop{make_genome(0, 0), make_genome(0, 0),
                                  make_genome(0, 0), make_genome(0, 0)};
    const auto s = summarize(pop, all_loci());
    CHECK(s.effective_population == 1.0f / 600.0f);

    // 单态位点并没有被跳过，只是它对 h_loss 的贡献恒为 1；真正参与
    // 缺额计算的是多态位点的 1 - H_exp。混一个多态位点进去就能把两者
    // 分开：位点 0-3 全员杂合，H_exp = 0.5，缺额 0.5；其余 296 个位点
    // 单态，缺额 1。h_loss_sum = 4 × 0.5 + 296 = 298，Ne = 1 / 596。
    // 若缺额误用 H_obs（此处为 1），h_loss_sum 会变成 296，Ne = 1 / 592。
    std::vector<Genome> partial(4, make_genome(0, 0));
    for (auto& genome : partial)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            genome.a[i].allele = 0;
            genome.b[i].allele = 1;
        }
    }
    CHECK(summarize(partial, all_loci()).effective_population
          == 1.0f / 596.0f);
}

TEST(statistics_heterozygosity_is_mean_over_loci)
{
    const std::vector<Genome> pop{make_genome(0, 1)};
    const auto s = summarize(pop, all_loci());
    // 全 Aa：每位点 H_obs = 1
    CHECK(s.heterozygosity == 1.0f);
}

#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/population.hpp"
#include "gamev2/genetics/random.hpp"

#include "test_framework.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <print>
#include <vector>

using namespace gamev2::genetics;
using gamev2::environment::EnvironmentSnapshot;

namespace {

struct Fixture
{
    LocusTable table;
    GeneticsConfig config;
    SpeciesRegistry registry;
    SpeciesId species{};
    std::vector<Genome> genomes;
    std::vector<Phenotype> phenotypes;
    std::vector<EnvironmentSnapshot> environments;
    std::vector<float> threats;
    std::vector<std::uint32_t> species_ids;
};

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 与确定性用例同一套基因型规则，独立复制而非共享——共享 helper 会让两个文件
/// 在其中一个被改动时连带失效。
void fill_genome(std::size_t i, Genome& g)
{
    constexpr std::uint32_t kQuarter = 1u << 22;
    for (std::size_t l = 0; l < kLocusCount; ++l)
    {
        const auto pick =
            static_cast<std::uint32_t>(mix_seed(i + 1, l + 1) >> 40);
        if (pick < kQuarter)
        {
            g.a[l].allele = 0;
            g.b[l].allele = 0;
        }
        else if (pick < 3 * kQuarter)
        {
            g.a[l].allele = 1;
            g.b[l].allele = 0;
        }
        else
        {
            g.a[l].allele = 1;
            g.b[l].allele = 1;
        }
    }
}

/// trait_min / trait_max 显式给出合法区间：trait_max[Lifespan] 为 0 时
/// outlived(p, 0) 恒真，全员判死、亲代池恒空、settle 立刻返回且不报错。
/// 那条路径下本用例会「飞快」并轻轻松松落在预算内，数字因此毫无意义。
Fixture make_fixture(std::size_t n)
{
    Fixture f;
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        f.table.loci[i].kind = LocusKind::Trait;
        f.table.loci[i].allele_count = 2;
        f.table.loci[i].scale = 1.0f;
        f.table.loci[i].switch_rate = 0.0f;
        f.table.loci[i].alleles[1].dom_rank = 1;
    }
    f.config.trait_base.fill(1.0f);
    // SenseRange 取 2 让 energy_survival 的 intake = 2 与
    // burn = Metabolism * BodySize * (1 + Speed) = 1 * 1 * 2 相等，
    // survival_rate 恰好为 1，10000 个个体一个都不死。
    f.config.trait_base[trait_index(TraitKind::SenseRange)] = 2.0f;
    f.config.trait_min.fill(0.0f);
    f.config.trait_max.fill(10.0f);
    f.config.jitter_step = 0;
    f.species = *f.registry.register_species(all_loci());

    f.genomes.resize(n);
    f.phenotypes.resize(n);
    f.environments.resize(n);
    f.threats.assign(n, 0.0f);
    f.species_ids.assign(n, f.species.value);
    for (std::size_t i = 0; i < n; ++i)
    {
        fill_genome(i, f.genomes[i]);
        f.phenotypes[i] = phenotype_of(f.genomes[i], f.table,
                                       f.registry.mask(f.species),
                                       f.environments[i], f.config);
    }
    return f;
}

}  // namespace

TEST(performance_one_wan_individuals_per_season)
{
    constexpr std::size_t kN = 10000;
    const auto f = make_fixture(kN);

    // 配置期条件成立是下面每一条断言的前提，先把它钉住：trait 区间退化时
    // fixture 是非法的，而 settle 不会为它报错。
    CHECK(validate(f.table, f.config).has_value());

    std::vector<Parent> parents;
    parents.reserve(kN);
    for (std::size_t i = 0; i < kN; ++i)
    {
        parents.push_back(Parent{i, &f.genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, f.species}};

    // Debug 构建（-O0）下不放宽：这是回归护栏，用来捕捉意外的复杂度爆炸，
    // 不是对 Release 性能的承诺。超预算说明算法退化，不是机器慢。
    //
    // 本机实测中位数 47.1 ms（7 次：44.98 / 46.11 / 46.31 / 47.08 / 50.40 /
    // 50.53 / 51.16 ms），预算 1000 ms 只够抓「退化 > 21 倍 / 个体」这一档：
    // 10 倍退化是 470 ms，照样绿。旧的 3000 ms 有 64 倍余量，涨到 1.5 s
    //（32 倍）仍然会绿，那等于只抓爆炸。
    //
    // 两处限定。其一，计时区内只有 1 个 site，第 2-4 步的每 site 繁殖路径只跑
    // 了一遍：这条护栏锁的是 settle 在单物种单 site 下为 O(N)，O(N × sites)
    // 那一维没有被计时。其二，valgrind、重度插桩、模拟器或某些 ARM 机器
    //（20-50 倍减速很常见）会误触红；将来一旦在插桩下跑套件，这是第一处假
    // 阳性。
    constexpr auto kBudget = std::chrono::milliseconds(1000);

    RandomStreams rng{5150, 0};
    const auto start = std::chrono::steady_clock::now();
    // want_statistics 必须显式打开：第 5 步默认关闭，而下面 census == 10000
    // 这条断言量的是那一步确实扫完了全部基因组。
    const auto out = settle(
        SeasonInput{f.genomes, f.phenotypes, f.environments, f.threats,
                    f.species_ids, sites, true},
        f.table, f.registry, f.config, rng);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    std::println("settle({}) = {}", kN, elapsed);

    // 单个 site 只产一个子代，因此 offspring.size() 恒为 1——它证明不了
    // 10000 个个体被处理过。能证明的是下面三条：存活筛选（第 1 步）按
    // genomes 的长度赋值并逐个判定，种群普查（第 5 步）按 species_ids
    // 扫完 10000 个基因组，census 因此是 10000 而非 0 或某个更小的数。
    CHECK_EQ(out.offspring.size(), std::size_t{1});
    CHECK_EQ(out.survived.size(), kN);
    std::size_t living = 0;
    for (const auto s : out.survived)
    {
        living += s;
    }
    CHECK_EQ(living, kN);
    CHECK_EQ(out.stats.size(), std::size_t{1});
    CHECK_EQ(out.stats[0].census, static_cast<std::uint32_t>(kN));

    CHECK(elapsed < kBudget);
}

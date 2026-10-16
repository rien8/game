#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/population.hpp"
#include "gamev2/genetics/random.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
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

/// 个体 i 在位点 l 上的基因型由 mix_seed 打散后定死，四类的比例恰为
/// AA : Aa : aa = 1 : 2 : 1，因此全群 p = 0.5、H_obs = H_exp = 0.5、F = 0。
///
/// 必须逐（个体, 位点）独立取样。若让同一个个体的 300 个位点共享同一个基因型
/// （例如按 i % 4 定全基因组），那么任意 4 个体组成的小池都会是「1 个 AA 加
/// 2 个 Aa 加 1 个 aa」的固定名额：池内等位基因频率零涨落，子代 F 恒为 0，
/// 近交信号会被 fixture 本身抹掉，而统计量看上去仍然「有值」。
///
/// 取高位 24 位与 uniform_unit 同口径，再四等分。
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

/// 300 个等价的二等位 Trait 位点：加性值与 switch_rate 全为 0，
/// 于是子代基因组完全由重组与抽样决定——这里测的是漂变与近交，
/// 测不到突变，也测不到选择。
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
    // survival_rate 恰好为 1：无人淘汰，每季唯一的变化来源就是抽样。
    // 留 1 的话存活率只有 0.5，种群逐季腰斩，两个被比较的池规模就不再相等。
    f.config.trait_base[trait_index(TraitKind::SenseRange)] = 2.0f;
    // 区间显式给出且满足 validate() 的 trait_min < trait_max。
    // trait_max[Lifespan] 尤其不能留 0：outlived(p, 0) 会恒真，全员判死、
    // 亲代池恒空、整季零子代，而 settle 不报任何错。
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
        // 表型交给 phenotype_of 算，与 settle 给子代算表型走同一条路径。
        // 手写一份 traits 数组等于让两代的取值来源分叉，日后改基因型时
        // 很容易只改到其中一边。
        f.phenotypes[i] = phenotype_of(f.genomes[i], f.table,
                                       f.registry.mask(f.species),
                                       f.environments[i], f.config);
    }
    return f;
}

/// ParentPool::parents 是 span，指向的 Parent vector 必须活过 settle 的调用。
/// 因此 blocks 由调用方持有，不能在建站函数里就地建了就返回——那会在函数
/// 返回时把 vector 析构掉，把 span 变成悬垂指针。
struct BlockedSites
{
    std::vector<std::vector<Parent>> blocks;
    std::vector<ParentPool> pools;
};

/// 把 genomes 切成 bucket_size 长的连续块，每块重复 bucket_size 次作为
/// site。两种块长下每个块内个体数不同，基因型则是各自独立取的随机样本。
///
/// 重复是必需的：settle 每个 site 只产一个子代，不重复的话每季子代数会
/// 塌缩到块数，种群两三代内就消失，漂变无从积累。
BlockedSites make_sites(const std::vector<Genome>& genomes, SpeciesId species,
                        std::size_t bucket_size)
{
    BlockedSites out;
    for (std::size_t start = 0; start < genomes.size(); start += bucket_size)
    {
        const std::size_t end = std::min(start + bucket_size, genomes.size());
        std::vector<Parent> block;
        block.reserve(end - start);
        for (std::size_t i = start; i < end; ++i)
        {
            block.push_back(Parent{i, &genomes[i], 1.0f});
        }
        out.blocks.push_back(std::move(block));
        for (std::size_t repeat = 0; repeat < bucket_size; ++repeat)
        {
            out.pools.push_back(ParentPool{out.blocks.back(), species});
        }
    }
    return out;
}

/// 逐季推进若干代，返回各季子代按顺序拼成的轨迹。种群规模逐季守恒，
/// 轨迹长度因此恒为 seasons * 起始规模——这条长度就是「确实跑了这么多代」
/// 的凭证，循环只在某季一个子代都产不出时提前结束。
std::vector<Genome> run_seasons(const Fixture& f, std::size_t bucket_size,
                                std::uint64_t seed, std::size_t seasons)
{
    std::vector<Genome> current = f.genomes;
    std::vector<Phenotype> pheno = f.phenotypes;
    std::vector<EnvironmentSnapshot> envs = f.environments;
    std::vector<float> threats = f.threats;
    std::vector<std::uint32_t> ids = f.species_ids;
    std::vector<Genome> trace;

    for (std::size_t season = 0; season < seasons; ++season)
    {
        const auto sites = make_sites(current, f.species, bucket_size);
        RandomStreams rng{seed, static_cast<std::uint64_t>(season)};
        const auto out = settle(SeasonInput{current, pheno, envs, threats, ids,
                                            sites.pools},
                                f.table, f.registry, f.config, rng);
        if (out.offspring.empty())
        {
            break;
        }
        trace.insert(trace.end(), out.offspring.begin(), out.offspring.end());
        current = out.offspring;
        pheno = out.offspring_phenotypes;
        envs = out.offspring_environments;
        // 与 current 同步收缩。settle 只按 genomes 的下标读剩下四个数组，
        // 留长不会越界，但同步让测试意图显式，也不会在 settle 改用别的长度
        // 做索引时静默读到上一季的数据。
        threats.assign(current.size(), 0.0f);
        ids.assign(current.size(), f.species.value);
    }
    return trace;
}

/// 确定性两条用例的代数：season_trace 按它跑满，断言按它核对轨迹长度。
constexpr std::size_t kDeterminismSeasons = 8;

/// 确定性两条用例共用的跑法：池长 8、跑满 kDeterminismSeasons 代，只有根种子
/// 是自变量。抽成 helper 是为了让「同种子」与「不同种子」的对照只差这一个
/// 可见变量，而不是各自抄一遍调用参数。
std::vector<Genome> season_trace(const Fixture& f, std::uint64_t seed)
{
    return run_seasons(f, 8, seed, kDeterminismSeasons);
}

}  // namespace

TEST(determinism_same_seed_gives_identical_seasons)
{
    const auto f = make_fixture(64);

    const auto a = season_trace(f, 1234);
    const auto b = season_trace(f, 1234);

    // 两条轨迹都必须跑满 8 代。长度不符时上面已记过失败，而下面的比较
    // 仍要在等长时逐位进行，所以只在这里判长度。
    CHECK_EQ(a.size(), kDeterminismSeasons * f.genomes.size());
    CHECK_EQ(b.size(), a.size());
    CHECK(a == b);
}

TEST(determinism_different_seeds_diverge)
{
    const auto f = make_fixture(64);

    const auto a = season_trace(f, 1234);
    const auto c = season_trace(f, 5678);

    // 换种子必须换结果，且必须是两条等长轨迹之间的差异——否则「某一季一个
    // 子代都没产出来」也能满足 a != c，那不是种子在起作用，是种群提前崩了。
    CHECK_EQ(a.size(), kDeterminismSeasons * f.genomes.size());
    CHECK_EQ(c.size(), a.size());
    CHECK(!(a == c));
}

TEST(inbreeding_small_pools_have_higher_f)
{
    const auto f = make_fixture(400);
    const auto mask = f.registry.mask(f.species);

    // 起点：全群 p = 0.5、H_obs = H_exp，前代的 F 因而应当接近 0（实测 -0.003）。
    // 没有这条，下面比较的就不是「池变小」而是「起点本来就偏」。
    const auto parents = summarize(f.genomes, mask);
    CHECK(std::fabs(parents.f_coefficient) < 0.05f);

    const auto measure = [&](std::size_t bucket_size)
    {
        const auto sites = make_sites(f.genomes, f.species, bucket_size);
        RandomStreams rng{99, 0};
        const auto out = settle(
            SeasonInput{f.genomes, f.phenotypes, f.environments, f.threats,
                        f.species_ids, sites.pools},
            f.table, f.registry, f.config, rng);
        // settle 每个 site 只产一个子代，而 site 数 = 400 / bucket_size *
        // bucket_size = 400，恒等于亲代规模。少了这条：settle 静默丢掉一半
        // site 时子代降到 200，F 只从 0.125 挪到 0.1225，下面三条界照样全过，
        // 而测的已经是另一个规模的群体。
        CHECK_EQ(out.offspring.size(), f.genomes.size());
        // 必须统计子代：settle 的 stats 描述的是本季亲代（第 5 步按
        // species_ids 过滤的是 input.genomes），两种池长下那份摘要完全相同，
        // 读它只会让 small == large。
        return summarize(out.offspring, mask).f_coefficient;
    };

    const float small = measure(4);
    const float large = measure(100);

    // 超额纯合来自自交而非 Wahlund：H_obs 与 H_exp 测在同一份合并后的子代上，
    // 没有「块内 vs 合并」的对照。实测把两个亲代强制成不同个体后 F(4) 从
    // 0.132 掉到 -0.001，而此时跨块等位基因频率方差仍是 1 / (8N) ≠ 0。
    //
    // 机制：make_offspring 带放回独立抽 i、j，P(i == j) = 1 / N。该分支上
    // 两条配子来自同一个体的两条单倍型，杂合概率只有 P(杂合) / 2 = 1 / 4，
    // 而 i != j 分支给 2p(1 - p) = 1 / 2。于是 E[H_obs] = 1/2 - 1/(4N)、
    // H_exp = 1/2，F = 1 / (2N)。
    //
    // M = 400 不够大，H_exp 自身被压低 1 / (2M)，故 F ≈ 1/(2N) - 1/(2M)：
    // N = 4 给 0.12375，N = 100 给 0.00375。
    // 24 个根种子实测：F(4) 均值 0.121、极差 [0.096, 0.145]，
    // F(100) 均值 0.001、极差 [-0.013, 0.011]。两条界都落在极差之外。
    // 种子固定后数值是常量，这些余量挡的是行为回归而不是抽样涨落。
    std::println("F(4) = {}, F(100) = {}", small, large);
    CHECK(small > large);
    CHECK(small > 0.08f);
    CHECK(large < 0.03f);
}

TEST(drift_small_pools_lose_heterozygosity_faster)
{
    constexpr std::size_t kSeasons = 12;
    const auto f = make_fixture(400);
    const auto mask = f.registry.mask(f.species);

    const auto run = [&](std::size_t bucket_size)
    {
        const auto trace = run_seasons(f, bucket_size, 777, kSeasons);
        // 长度即凭证：12 代 × 400 个体都真的跑过了，末代才是轨迹的最后一段。
        CHECK_EQ(trace.size(), kSeasons * f.genomes.size());
        return summarize(std::span{trace}.last(f.genomes.size()), mask)
            .heterozygosity;
    };

    const float small = run(4);
    const float large = run(100);

    // H_t = H_0 * (1 - 1 / (2N))^t，H_0 = 0.5：
    // 4 个亲代给 0.5 * 0.875^12 = 0.10，100 个给 0.5 * 0.995^12 = 0.47。
    // 换 12 个根种子实测：H(4) 均值 0.099、极差 [0.089, 0.105]，
    // H(100) 均值 0.470、极差 [0.464, 0.478]。
    std::println("H(4) = {}, H(100) = {}", small, large);
    CHECK(small < large);
    // 两条界各自判的是末代杂合度的绝对水平，不只是「小池低于大池」这一个相对
    // 关系，后者几乎不设防：把重组换成同一亲代的两条单倍型后，H(4) 掉到
    // 0.01 量级而 H(100) 仍有 0.34 量级，相对高低照样成立，让它变红的是
    // large > 0.40 这条绝对下限。
    //
    // 24 个根种子（1..24，不含本用例实际用的 777）实测基线 H(100) 极差
    // [0.464, 0.478]，与上面 12 种子那条同；777 的 0.46738 落在区间中部，
    // 是被断言的那一个取值点，不在扫描样本里。
    // 0.40 在该下沿之下约 14%：大池损失不到一成多样性的改动这套断言看不见，
    // 而重组整个关掉会落到 [0.321, 0.358]。两侧都留了余量——低于 0.358 会漏
    // 掉上述变异体，高于 0.464 就贴上基线下沿。
    CHECK(small < 0.25f);
    CHECK(large > 0.40f);
}

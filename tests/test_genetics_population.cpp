#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/population.hpp"

#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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

/// 全体位点都是无加性、无突变的 Trait 位点，target 默认 0 即 BodySize。
LocusTable neutral_table()
{
    LocusTable t{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        t.loci[i].kind = LocusKind::Trait;
        t.loci[i].allele_count = 2;
        t.loci[i].scale = 1.0f;
        t.loci[i].switch_rate = 0.0f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

/// 三项生理约束在全默认配置下都取 1，survival_rate 恰好为 1。
Phenotype robust_phenotype()
{
    Phenotype p{};
    p.traits[trait_index(TraitKind::BodySize)] = 1.0f;
    p.traits[trait_index(TraitKind::Speed)] = 4.0f;
    p.traits[trait_index(TraitKind::Metabolism)] = 0.2f;
    p.traits[trait_index(TraitKind::Lifespan)] = 100.0f;
    p.traits[trait_index(TraitKind::SenseRange)] = 4.0f;
    p.traits[trait_index(TraitKind::Fecundity)] = 4.0f;
    p.traits[trait_index(TraitKind::Resilience)] = 5.0f;
    return p;
}

/// Speed 归零后逃脱余量为 0，survival_rate 恰好为 0，与种子无关。
Phenotype unescapable_phenotype()
{
    Phenotype p = robust_phenotype();
    p.traits[trait_index(TraitKind::Speed)] = 0.0f;
    return p;
}

/// 活得久但不下崽：Metabolism 归零让 energy_survival 走 burn <= 0 的满分支，
/// survival_rate 恒为 1、本季必定存活；Fecundity 归零则让 fitness() 算出的
/// 季节适应力恰为 0。生态上就是「不死但绝后」。
Phenotype sterile_phenotype()
{
    Phenotype p = robust_phenotype();
    p.traits[trait_index(TraitKind::Metabolism)] = 0.0f;
    p.traits[trait_index(TraitKind::Fecundity)] = 0.0f;
    return p;
}

/// 体能尚可但逃不快：Speed 取 0.5 时逃脱余量恰好 0.5，survival_rate 落到
/// 0.5，于是它一半靠抽签活下来、一半当场淘汰。关键在于适应力与抽签结果
/// 相互独立——抽输了的那一半照样带着 fitness() 给的权重，权重通道不能拿来
/// 代替存活判定。
Phenotype coin_flip_phenotype()
{
    Phenotype p = robust_phenotype();
    p.traits[trait_index(TraitKind::Speed)] = 0.5f;
    return p;
}

/// SenseRange 压到 1 后 intake 恰为 1，energy_survival 仍取满值 1，但
/// fitness() 只认 Fecundity，于是季节适应力落到 robust 个体的四分之一。
Phenotype frugal_phenotype()
{
    Phenotype p = robust_phenotype();
    p.traits[trait_index(TraitKind::SenseRange)] = 1.0f;
    p.traits[trait_index(TraitKind::Fecundity)] = 1.0f;
    return p;
}

/// 两条单倍型取同一个等位基因的基因组。同值的双单倍型让 make_gamete 无论
/// 交叉点落在哪儿都产出同一条配子，子代基因组因而能直接读回亲代类型。
Genome uniform_genome(AlleleId allele)
{
    Genome g{};
    for (auto& call : g.a)
    {
        call.allele = allele;
    }
    for (auto& call : g.b)
    {
        call.allele = allele;
    }
    return g;
}

/// 全默认 GeneticsConfig 的 trait_min / trait_max 都是 0，express() 的
/// clamp 会把所有性状清零。子类里放开区间，表型才带得出信号。
GeneticsConfig open_config()
{
    GeneticsConfig config{};
    config.trait_min.fill(-1000.0f);
    config.trait_max.fill(1000.0f);
    return config;
}

/// 每项性状各由一个位点承担，且 loci 的下标与 TraitKind 的下标一一对应——
/// 下面的 robust_genome / sterile_genome 靠这条对应关系按下标翻位点。
///
/// trait_base 全为 0，因此 allele 1 的加性值就是该性状的绝对取值；两个单倍型
/// 同型时 trait_value 恒为 additive，与显性系数无关。其余 292 个位点加性为
/// 0，不参与求和。
LocusTable viable_table()
{
    LocusTable t = neutral_table();
    static constexpr TraitKind kAxes[kTraitCount] = {
        TraitKind::BodySize,   TraitKind::Speed,     TraitKind::Strength,
        TraitKind::Metabolism, TraitKind::Lifespan,  TraitKind::SenseRange,
        TraitKind::Fecundity,  TraitKind::Resilience};
    static constexpr float kValues[kTraitCount] = {
        1.0f, 4.0f, 2.0f, 0.2f, 100.0f, 4.0f, 4.0f, 5.0f};

    for (std::size_t i = 0; i < kTraitCount; ++i)
    {
        t.loci[i].target = static_cast<std::uint8_t>(trait_index(kAxes[i]));
        t.loci[i].alleles[1].additive = kValues[i];
    }
    return t;
}

/// 与 viable_table 配套的健壮基因组：全位点 allele 1，各性状等于表里的加性
/// 值，三项生理约束全满因而 survival_rate 恰为 1。
Genome robust_genome()
{
    return uniform_genome(1);
}

/// 同一张表下的绝育基因组：Metabolism 与 Fecundity 两个位点退回 allele 0。
/// Metabolism 归零让 burn <= 0 走满值分支、必定存活，Fecundity 归零则把季节
/// 适应力压到 0。生态上就是 robust_phenotype 版的「不死但绝后」。
Genome sterile_genome()
{
    Genome g = uniform_genome(1);
    for (const auto axis : {trait_index(TraitKind::Metabolism),
                            trait_index(TraitKind::Fecundity)})
    {
        g.a[axis].allele = 0;
        g.b[axis].allele = 0;
    }
    return g;
}

/// 子代表型完全由这张 trait_base 决定的配置：neutral_table 的全部位点都指向
/// BodySize 且加性为 0，express 因此原样返回 trait_base，子代与亲代逐项同值。
///
/// 两处刻意取值：Metabolism 归零让能量收支的分母一并归零而取满值 1，
/// Speed 取 1 让逃脱余量取满，于是 survival_rate 恒为 1、存活抽签必然全中，
/// 断言不再依赖种子。Fecundity 取 kSteadyFecundity 使每季的规模倍率是确定的
/// 1 + kSteadyFecundity。
GeneticsConfig steady_config(float fecundity)
{
    GeneticsConfig config = open_config();
    config.trait_base.fill(1.0f);
    config.trait_base[trait_index(TraitKind::Metabolism)] = 0.0f;
    config.trait_base[trait_index(TraitKind::Fecundity)] = fecundity;
    return config;
}

/// 每季多产的比例。32 季后规模是初值的 1.05^32 ≈ 4.76 倍。
constexpr float kSteadyFecundity = 0.05f;
constexpr std::uint64_t kSeasons = 32;

/// 把一季的结果接成下一季的输入：存活者在前，子代在后。与调用方提交换代的
/// 顺序一致，因此 settle 的 survived 语义一旦反过来，这条驱动立刻失真。
void advance(std::vector<Genome>& genomes, std::vector<Phenotype>& phenotypes,
             std::vector<EnvironmentSnapshot>& environments,
             std::vector<float>& threats, std::vector<std::uint32_t>& ids,
             const SeasonOutput& out)
{
    std::vector<Genome> next_g;
    std::vector<Phenotype> next_p;
    std::vector<EnvironmentSnapshot> next_e;
    std::vector<float> next_t;
    std::vector<std::uint32_t> next_i;
    next_g.reserve(genomes.size() + out.offspring.size());
    for (std::size_t k = 0; k < out.survived.size(); ++k)
    {
        if (out.survived[k] == 0)
        {
            continue;
        }
        next_g.push_back(genomes[k]);
        next_p.push_back(phenotypes[k]);
        next_e.push_back(environments[k]);
        next_t.push_back(threats[k]);
        next_i.push_back(ids[k]);
    }
    next_g.insert(next_g.end(), out.offspring.begin(), out.offspring.end());
    next_p.insert(next_p.end(), out.offspring_phenotypes.begin(),
                  out.offspring_phenotypes.end());
    next_e.insert(next_e.end(), out.offspring_environments.begin(),
                  out.offspring_environments.end());
    next_t.resize(next_g.size(), 0.0f);
    next_i.resize(next_g.size(), 1);
    genomes = std::move(next_g);
    phenotypes = std::move(next_p);
    environments = std::move(next_e);
    threats = std::move(next_t);
    ids = std::move(next_i);
}

}  // namespace

TEST(settle_keeps_offspring_in_staging_until_returned)
{
    const std::vector<Genome> genomes(4, Genome{});
    const std::vector<Phenotype> phenotypes(4, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(4, EnvironmentSnapshot{});
    const std::vector<float> threats(4, 0.0f);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    // 单亲池完不成有性生殖，这个位置本季不产子代，但仍占一个 site。
    const std::vector<Parent> lonely{Parent{0, &genomes[0], 1.0f}};
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}},
                                        ParentPool{lonely, SpeciesId{1}}};
    const std::vector<std::uint32_t> species_ids(4, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{31, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.offspring.size(), std::size_t{1});
    CHECK_EQ(out.offspring_phenotypes.size(), std::size_t{1});
    CHECK_EQ(out.offspring_environments.size(), std::size_t{1});
    CHECK_EQ(out.survived.size(), std::size_t{4});
    // 统计覆盖全部已注册物种：第二物种本季一个个体都没有，仍要拿到
    // 一条 census = 0 的摘要，而不是被悄悄省掉。
    CHECK_EQ(out.stats.size(), std::size_t{2});
    if (out.stats.size() != 2)
    {
        // 长度不符时上面已经记过失败；继续下标会越界，把整轮测试带崩。
        return;
    }
    CHECK_EQ(out.stats[0].census, std::uint32_t{4});
    CHECK_EQ(out.stats[1].census, std::uint32_t{0});
}

TEST(settle_survival_filter_does_not_depend_on_breeding_sites)
{
    // 无待繁殖位置时 offspring 必须为空，但存活筛选照常跑完：它是本季
    // 存活的唯一依据，与本季是否繁殖无关。两半合起来挡掉「sites 为空就
    // 全灭」与「survived 压根没赋值」这两个错误实现。
    const std::vector<Genome> genomes(6, Genome{});
    std::vector<Phenotype> phenotypes(6, unescapable_phenotype());
    const std::vector<EnvironmentSnapshot> envs(6, EnvironmentSnapshot{});
    const std::vector<float> threats(6, 0.0f);
    const std::vector<std::uint32_t> species_ids(6, 1);
    const std::vector<ParentPool> sites{};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto doomed_rng = RandomStreams{31, 0};
    const auto doomed = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, doomed_rng);

    CHECK(doomed.offspring.empty());
    CHECK(doomed.offspring_phenotypes.empty());
    CHECK(doomed.offspring_environments.empty());
    CHECK_EQ(doomed.survived.size(), std::size_t{6});
    for (const auto s : doomed.survived)
    {
        CHECK_EQ(s, std::uint8_t{0});
    }

    phenotypes.assign(6, robust_phenotype());
    auto live_rng = RandomStreams{31, 0};
    const auto living = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, live_rng);

    CHECK(living.offspring.empty());
    CHECK_EQ(living.stats.size(), std::size_t{1});
    CHECK_EQ(living.survived.size(), std::size_t{6});
    for (const auto s : living.survived)
    {
        CHECK_EQ(s, std::uint8_t{1});
    }

    // 寿命是硬判定而非概率约束：Lifespan 归零后即便三项生理约束全满、
    // survival_rate 恰为 1，个体仍然判死。
    for (auto& p : phenotypes)
    {
        p.traits[trait_index(TraitKind::Lifespan)] = 0.0f;
    }
    auto aged_rng = RandomStreams{31, 0};
    const auto aged = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, aged_rng);

    CHECK(aged.offspring.empty());
    CHECK_EQ(aged.survived.size(), std::size_t{6});
    for (const auto s : aged.survived)
    {
        CHECK_EQ(s, std::uint8_t{0});
    }
}

TEST(settle_stages_phenotype_from_selected_genome_and_site_environment)
{
    // 只有 loci[0] 带加性，且 allele 1 完全显性（additive 1、dominance 1），
    // 因此 BodySize 直接读出两条单倍型是否都抽到了大个亲代：AA 与 Aa 同为
    // 1.0，aa 为 0.0。其余 299 个位点加性为 0，不参与求和。
    auto table = neutral_table();
    table.loci[0].alleles[0].additive = 0.0f;
    table.loci[0].alleles[1].additive = 1.0f;
    table.loci[0].dominance = 1.0f;

    Genome big{};
    for (auto& c : big.a)
    {
        c.allele = 1;
    }
    for (auto& c : big.b)
    {
        c.allele = 1;
    }
    // 0 号是小个、1 号是大个，而绝育的是 0 号。下标顺序本身就是断言的一部分：
    // 归一化基准必须先扫全体求 max_fecundity。若实现改成边遍历边累积 running
    // max，处理 0 号时基准仍是 0，fitness() 会走 max_fecundity <= 0 的退化分支
    // 原样返回 survival = 1.0，绝育个体因此拿到满权重参与抽样，CDF 变成
    // [1, 1] 而不是 [0, 1]。
    const std::vector<Genome> genomes{Genome{}, big};

    // 先验权重仍然故意填反（0 号填 1.0）：「误用池里先验权重而不是
    // season_fitness」的实现会稳定抽中 0 号，BodySize 断言随之失败。
    std::vector<Parent> parents{Parent{0, &genomes[0], 1.0f},
                                Parent{1, &genomes[1], 0.0f}};
    const EnvironmentSnapshot site_env{4.0f, 1.0f, 1.0f};
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, site_env}};

    const std::vector<Phenotype> phenotypes{sterile_phenotype(),
                                            robust_phenotype()};
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    const std::vector<float> threats(2, 0.0f);
    const std::vector<std::uint32_t> species_ids(2, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    // 正确实现下 CDF 是 [0, 1]，两次抽样都必落在大个亲代上。running max 的
    // 变异让 0 号拿到 1.0 权重，于是每条单倍型各有 1/2 落到小个上、每季有
    // 3/4 概率生出 BodySize = 0 的子代——48 个季节全部蒙混过关的概率约 1e-6。
    // 换种子同时覆盖交叉点与突变抽签：任一季漏掉权重、漏掉环境或漏掉表型
    // 计算都会在这条循环里被逮到。
    for (std::uint64_t season = 0; season < 48; ++season)
    {
        auto rng = RandomStreams{53, season};
        const auto out = settle(
            SeasonInput{genomes, phenotypes, envs, threats, species_ids,
                        sites, true},
            table, registry, open_config(), rng);

        CHECK_EQ(out.offspring.size(), std::size_t{1});
        CHECK_EQ(out.offspring_phenotypes.size(), std::size_t{1});
        CHECK_EQ(out.offspring_environments.size(), std::size_t{1});
        if (out.offspring_phenotypes.size() != 1
            || out.offspring_environments.size() != 1)
        {
            // 长度不符时上面已记过失败；继续下标会越界，把整轮测试带崩。
            continue;
        }
        const auto& child = out.offspring_phenotypes[0];
        CHECK_EQ(child.traits[trait_index(TraitKind::BodySize)], 1.0f);
        // |temperature| * temperature_metabolic_load = 4.0 * 0.1
        CHECK_EQ(child.traits[trait_index(TraitKind::Metabolism)], 0.4f);
        CHECK_EQ(out.offspring_environments[0].temperature, 4.0f);
    }
}

TEST(settle_is_deterministic_for_a_fixed_seed)
{
    const std::vector<Genome> genomes(8, Genome{});
    const std::vector<Phenotype> phenotypes(8, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(8, EnvironmentSnapshot{});
    const std::vector<float> threats(8, 0.0f);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};
    const std::vector<std::uint32_t> species_ids(8, 1);

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    const auto run = [&]
    {
        auto rng = RandomStreams{41, 3};
        return settle(
            SeasonInput{genomes, phenotypes, envs, threats, species_ids,
                        sites, true},
            neutral_table(), registry, open_config(), rng);
    };

    const auto a = run();
    const auto b = run();

    CHECK_EQ(a.offspring.size(), std::size_t{1});
    CHECK_EQ(a.offspring.size(), b.offspring.size());
    CHECK_EQ(a.offspring_phenotypes.size(), b.offspring_phenotypes.size());
    CHECK_EQ(a.stats.size(), b.stats.size());
    if (a.offspring_phenotypes.empty() || a.stats.empty())
    {
        // 上面已记过失败；继续下标会越界，把整轮测试带崩。
        return;
    }
    for (std::size_t i = 0; i < a.offspring.size(); ++i)
    {
        CHECK(a.offspring[i] == b.offspring[i]);
    }
    CHECK_EQ(a.offspring_phenotypes[0].traits,
             b.offspring_phenotypes[0].traits);
    CHECK(a.survived == b.survived);
    CHECK_EQ(a.stats[0].census, b.stats[0].census);
    CHECK_EQ(a.stats[0].heterozygosity, b.stats[0].heterozygosity);
}

TEST(settle_drops_parents_that_did_not_survive)
{
    // 0、1 号（Fecundity 1 → 适应力 0.25）本季必定存活，2、3 号
    // （Fecundity 4、survival_rate 0.5 → 适应力 0.5）靠抽签生死各半。
    // 适应力刻意让死的那两个更高：若 settle 不过滤就重建父池，每次抽样命中
    // 死亡个体的概率高达 2/3，断言因此几乎不可能蒙混过关。
    const std::vector<Genome> genomes{uniform_genome(0), uniform_genome(0),
                                      uniform_genome(1), uniform_genome(1)};
    const std::vector<Phenotype> phenotypes{frugal_phenotype(),
                                            frugal_phenotype(),
                                            coin_flip_phenotype(),
                                            coin_flip_phenotype()};
    const std::vector<EnvironmentSnapshot> envs(4, EnvironmentSnapshot{});
    const std::vector<float> threats(4, 0.0f);
    const std::vector<std::uint32_t> species_ids(4, 1);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    // 子代基因组只可能来自本季活着的亲代，且每条单倍型都整条取自同一个亲代，
    // 因此它读回来的等位基因必定属于某个存活者。2、3 号抽签全灭的季节里，
    // 1 号等位基因无人幸存，断言才有区分力；两者都活下来的季节则恒真。
    std::size_t seasons_with_a_gap = 0;
    for (std::uint64_t season = 0; season < 48; ++season)
    {
        auto rng = RandomStreams{97, season};
        const auto out = settle(
            SeasonInput{genomes, phenotypes, envs, threats, species_ids,
                        sites, true},
            neutral_table(), registry, GeneticsConfig{}, rng);

        CHECK_EQ(out.survived.size(), genomes.size());
        CHECK_EQ(out.offspring.size(), std::size_t{1});
        if (out.survived.size() != genomes.size() || out.offspring.empty())
        {
            // 长度不符时上面已记过失败；继续下标会越界，把整轮测试带崩。
            continue;
        }
        // 下标留 kMaxAlleles 的余量：变异实现产出越界等位基因时读的是
        // 有效内存，而不是把整轮测试带崩。
        std::array<bool, kMaxAlleles> inherited{};
        for (std::size_t i = 0; i < genomes.size(); ++i)
        {
            if (out.survived[i] != 0)
            {
                inherited[genomes[i].a[0].allele] = true;
            }
        }
        CHECK(inherited[out.offspring[0].a[0].allele]);
        CHECK(inherited[out.offspring[0].b[0].allele]);
        if (!inherited[1])
        {
            ++seasons_with_a_gap;
        }
    }
    // 全程一次都没出现「1 号等位基因无人幸存」的季节，上面两条 CHECK 就恒真，
    // 整个循环什么也没测。
    CHECK(seasons_with_a_gap > 0);
}

TEST(settle_breeds_from_genomes_through_phenotype_of)
{
    // 端到端链路：基因组 → phenotype_of → settle。表型一律从基因组算出来，
    // 不手工构造——前面的用例都直接喂 Phenotype，这条真实链路此前零覆盖。
    const LocusTable table = viable_table();
    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());
    const SpeciesMask mask = registry.mask(SpeciesId{1});
    const GeneticsConfig config = open_config();

    const std::vector<Genome> genomes{sterile_genome(), robust_genome()};
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    std::vector<Phenotype> phenotypes;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        phenotypes.push_back(
            phenotype_of(genomes[i], table, mask, envs[i], config));
    }

    // 先验断言 fixture 本身：trait_base 全 0，allele 1 的加性值就是性状值。
    // trait_base 刻意留默认的 0 而不是显式写一遍——写出来与默认值同值，只会
    // 让读者以为加性值之外还有一层偏置。
    CHECK_EQ(phenotypes[0].traits[trait_index(TraitKind::Lifespan)], 100.0f);
    CHECK_EQ(phenotypes[1].traits[trait_index(TraitKind::Lifespan)], 100.0f);
    CHECK_EQ(phenotypes[0].traits[trait_index(TraitKind::Fecundity)], 0.0f);
    CHECK_EQ(phenotypes[1].traits[trait_index(TraitKind::Fecundity)], 4.0f);

    std::vector<Parent> parents{Parent{0, &genomes[0], 0.0f},
                                Parent{1, &genomes[1], 0.0f}};
    const EnvironmentSnapshot site_env{2.0f, 1.0f, 1.0f};
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, site_env}};
    const std::vector<float> threats(2, 0.0f);
    const std::vector<std::uint32_t> species_ids(2, 1);

    auto rng = RandomStreams{67, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        table, registry, config, rng);

    CHECK_EQ(out.survived.size(), genomes.size());
    CHECK_EQ(out.offspring.size(), std::size_t{1});
    CHECK_EQ(out.offspring_phenotypes.size(), std::size_t{1});
    CHECK_EQ(out.offspring_environments.size(), std::size_t{1});
    if (out.offspring.size() != 1 || out.offspring_phenotypes.size() != 1
        || out.offspring_environments.size() != 1)
    {
        // 长度不符时上面已记过失败；继续下标会越界，把整轮测试带崩。
        return;
    }
    for (const auto s : out.survived)
    {
        CHECK_EQ(s, std::uint8_t{1});
    }

    // 0 号绝育、季节适应力为 0，CDF 只有 1 号一处非零，两次抽样都落在 1 号；
    // 表里 switch_rate 全 0 故无突变，1 号又是全位点同型，子代基因组必与它
    // 逐位相同。
    CHECK(out.offspring[0] == genomes[1]);

    // 子代表型由它的基因组加位置环境推出：基因型给出加性值，g(E) 再叠加。
    const auto& child = out.offspring_phenotypes[0];
    CHECK_EQ(child.traits[trait_index(TraitKind::BodySize)], 1.0f);
    CHECK_EQ(child.traits[trait_index(TraitKind::Lifespan)], 100.0f);
    CHECK_EQ(child.traits[trait_index(TraitKind::Fecundity)], 4.0f);
    // |2.0| * temperature_metabolic_load = 2.0 * 0.1，叠在加性值 0.2 上
    CHECK_EQ(child.traits[trait_index(TraitKind::Metabolism)], 0.4f);
    CHECK_EQ(out.offspring_environments[0].temperature, 2.0f);
}

TEST(settle_yields_no_offspring_when_default_config_zeroes_every_trait)
{
    // 这条用例钉的是一个退化状态，不是理想行为。GeneticsConfig{} 的
    // trait_min / trait_max 全是 0，express() 的 clamp 把每个性状都夹成 0：
    // Lifespan 因此为 0，outlived(p, 0) 恒真，全员判死，季节适应力恒 0，
    // 亲代池恒空——整季零子代且不报任何错。库不去猜一个「看起来能活」的
    // 默认界：调用方没配好 trait_max 是调用方的事，测试只需保证这条路径
    // 的结果是被知道的。
    const LocusTable table = viable_table();
    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());
    const SpeciesMask mask = registry.mask(SpeciesId{1});

    // 基因组与上面那条用例完全相同，唯一的差别是配置——把退化归因锁死在
    // 配置上，而不是基因组或表型表。
    const std::vector<Genome> genomes{robust_genome(), robust_genome()};
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    std::vector<Phenotype> phenotypes;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        phenotypes.push_back(
            phenotype_of(genomes[i], table, mask, envs[i], GeneticsConfig{}));
    }
    for (const auto& p : phenotypes)
    {
        for (std::size_t t = 0; t < kTraitCount; ++t)
        {
            CHECK_EQ(p.traits[t], 0.0f);
        }
    }

    std::vector<Parent> parents{Parent{0, &genomes[0], 1.0f},
                                Parent{1, &genomes[1], 1.0f}};
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};
    const std::vector<float> threats(2, 0.0f);
    const std::vector<std::uint32_t> species_ids(2, 1);

    auto rng = RandomStreams{11, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        table, registry, GeneticsConfig{}, rng);

    // 两个亲代都拿得到满分先验权重，但寿命这道硬判定先把他们全判死。
    CHECK_EQ(out.survived.size(), genomes.size());
    for (const auto s : out.survived)
    {
        CHECK_EQ(s, std::uint8_t{0});
    }
    CHECK(out.offspring.empty());
    CHECK(out.offspring_phenotypes.empty());
    CHECK(out.offspring_environments.empty());
}

TEST(settle_drops_parent_entries_whose_index_is_out_of_range)
{
    // 越界下标没有「小越界」这回事：调用方把上一季的父池原样传进来的话，
    // index 可能来自一个曾经更大、如今已失效的种群。因此这里同时喂两种量级
    // ——2 号是真实调用方会犯的小越界，kOutOfRange 必然落在用户地址空间之
    // 外。前者撞上 debug STL 自带的边界检查，后者即便在不做检查的实现里
    // 也只能是非规范地址异常：没有守卫，settle 就不是在丢弃越界条目，而是
    // 在读一段它无权访问的内存。
    constexpr std::size_t kOutOfRange = 0x7FFF'FFFF'FFFFull;

    const std::vector<Genome> genomes(2, Genome{});
    const std::vector<Phenotype> phenotypes(2, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    const std::vector<float> threats(2, 0.0f);
    const std::vector<std::uint32_t> species_ids(2, 1);

    // 0、1 号在 genomes 内，两个越界条目都该被丢弃，只留下两个有效条目。
    std::vector<Parent> parents{Parent{0, &genomes[0], 1.0f},
                                Parent{1, &genomes[1], 1.0f},
                                Parent{2, &genomes[0], 1.0f},
                                Parent{kOutOfRange, &genomes[0], 1.0f}};
    const std::vector<ParentPool> sites{ParentPool{parents, SpeciesId{1}}};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{23, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, rng);

    // 存活筛选不受越界条目影响，仍按 genomes 的长度赋值；剩下的两个有效
    // 条目足够完成有性生殖，本季照常产子代。
    CHECK_EQ(out.survived.size(), genomes.size());
    for (const auto s : out.survived)
    {
        CHECK_EQ(s, std::uint8_t{1});
    }
    CHECK_EQ(out.offspring.size(), std::size_t{1});
}

TEST(settle_leaves_stats_empty_for_an_empty_population)
{
    // 空种群提前返回、stats 留空，是与「统计覆盖全部已注册物种」并存的有意
    // 取舍，优先级是空种群在前。后者的目的是不让面板在某个物种归零时漏掉
    // 条目，而空种群连一个个体都没有，零摘要只会把「本季普查过」与「本季
    // 无人」混成同一件事。
    const std::vector<Genome> genomes{};
    const std::vector<Phenotype> phenotypes{};
    const std::vector<EnvironmentSnapshot> envs{};
    const std::vector<float> threats{};
    const std::vector<std::uint32_t> species_ids{};
    const std::vector<ParentPool> sites{};

    // 注册两个物种：非空种群下 stats 必有两条，空种群下一条都不该有。
    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{5, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        viable_table(), registry, GeneticsConfig{}, rng);

    CHECK(out.survived.empty());
    CHECK(out.offspring.empty());
    CHECK(out.offspring_phenotypes.empty());
    CHECK(out.offspring_environments.empty());
    CHECK(out.stats.empty());
}

TEST(settle_breeds_exactly_as_many_offspring_as_the_pool_asks_for)
{
    // 六个健壮亲代（survival_rate 恒为 1，Fecundity 4，季节适应力因此全为 1，
    // 抽样在六者上等概率），池要求 4 个子代。「每个 site 至多一个子代」的
    // 老实现只出 1 个，这条断言直接判死。
    //
    // 基因组一半是全 0 号、一半是全 1 号：减数分裂对同型双单倍型只会原样
    // 吐出整条单倍型，子代因而是非黑即白的四种组合之一。四个子代若来自
    // 四个独立配对（全同的概率 4/4^4 = 1/64），下面那条「不是同一个结果
    // 重复四遍」的断言才有意义。
    const std::vector<Genome> genomes{uniform_genome(0), uniform_genome(0),
                                      uniform_genome(0), uniform_genome(1),
                                      uniform_genome(1), uniform_genome(1)};
    const std::vector<Phenotype> phenotypes(6, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(6, EnvironmentSnapshot{});
    const std::vector<float> threats(6, 0.0f);
    const std::vector<std::uint32_t> species_ids(6, 1);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, std::nullopt, 4}};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{17, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.offspring.size(), std::size_t{4});
    // 三条 staging 数组必须与 genome 一一对应：只放大其中一条会让换代后的
    // 表型或环境错位，而下游只按它们的下标取。
    CHECK_EQ(out.offspring_phenotypes.size(), std::size_t{4});
    CHECK_EQ(out.offspring_environments.size(), std::size_t{4});

    // 子代彼此独立：把一次配对的结果重复四遍的实现会让下面这条恒真。
    bool all_same = true;
    for (std::size_t i = 1; i < out.offspring.size(); ++i)
    {
        if (!(out.offspring[i] == out.offspring[0]))
        {
            all_same = false;
        }
    }
    CHECK(!all_same);
}

TEST(settle_treats_a_zero_progeny_as_one)
{
    // 0 既不是灭绝也不是繁殖，settle 按 1 处理。这条钉的是「0 照旧走老行为」
    // 那个降级分支，而不是让它静默产出 0 个子代。
    const std::vector<Genome> genomes(4, Genome{});
    const std::vector<Phenotype> phenotypes(4, robust_phenotype());
    const std::vector<EnvironmentSnapshot> envs(4, EnvironmentSnapshot{});
    const std::vector<float> threats(4, 0.0f);
    const std::vector<std::uint32_t> species_ids(4, 1);

    std::vector<Parent> parents;
    for (std::size_t i = 0; i < genomes.size(); ++i)
    {
        parents.push_back(Parent{i, &genomes[i], 1.0f});
    }
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, std::nullopt, 0}};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{19, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.offspring.size(), std::size_t{1});
}

TEST(settle_population_holds_a_steady_size_over_thirty_two_seasons)
{
    // 回归 C1：每个物种池每季只产 1 个子代时 N' = s·N + 1，子代数与种群规模
    // 无关，s < 1 的物种必然单调衰减到 1，而 1 个个体完不成有性生殖，
    // 种群从此永久灭绝。本例把 survival_rate 钉成 1（见 steady_config），
    // 于是规模倍率是确定的 1 + Fecundity = 1.05，32 季约 4.76 倍。
    const LocusTable table = neutral_table();
    const GeneticsConfig config = steady_config(kSteadyFecundity);
    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    constexpr std::size_t kStart = 100;
    std::vector<Genome> genomes(kStart, Genome{});
    std::vector<Phenotype> phenotypes(kStart, Phenotype{});
    for (auto& p : phenotypes)
    {
        p.traits = config.trait_base;
    }
    std::vector<EnvironmentSnapshot> envs(kStart, EnvironmentSnapshot{});
    std::vector<float> threats(kStart, 0.0f);
    std::vector<std::uint32_t> ids(kStart, 1);

    for (std::uint64_t season = 0; season < kSeasons; ++season)
    {
        std::vector<Parent> parents;
        parents.reserve(genomes.size());
        for (std::size_t i = 0; i < genomes.size(); ++i)
        {
            parents.push_back(Parent{i, &genomes[i], 1.0f});
        }
        // 与 BreedingSystem 同一套公式：子代 = 本池个体数 × 平均繁殖力。
        const auto raw = std::lround(static_cast<double>(genomes.size())
                                     * kSteadyFecundity);
        const std::vector<ParentPool> sites{ParentPool{
            parents, SpeciesId{1}, std::nullopt,
            static_cast<std::uint32_t>(std::max(0L, raw))}};

        auto rng = RandomStreams{3, season};
        const auto out = settle(
            SeasonInput{genomes, phenotypes, envs, threats, ids, sites, true},
            table, registry, config, rng);

        // 子代表型由 trait_base 决定，父代表型也是 trait_base：全员同型，
        // 存活判定与种子无关。这条断言是后面那条规模断言的前提。
        CHECK_EQ(out.survived.size(), genomes.size());
        CHECK_EQ(out.offspring.size(),
                 static_cast<std::size_t>(std::max(0L, raw)));
        advance(genomes, phenotypes, envs, threats, ids, out);
    }

    // 老行为（progeny 恒 1）在这 32 季里只能攒到 kStart + 32 = 132，
    // 远够不到 3 倍；子代数失控（漏掉上限、或把存活数当个体数再乘一次）
    // 则会是 2^k 量级。这两条一起把「稳定」钉在同一个数量级上。
    CHECK(genomes.size() > 3 * kStart);
    CHECK(genomes.size() < 8 * kStart);
}

TEST(settle_skips_species_statistics_when_the_caller_does_not_ask)
{
    // 同一份输入、同一颗种子，只切开关：关掉时 stats 留空，而存活标记、
    // 适应力与子代逐位相同——统计是纯旁路，摘掉它不该动到任何别的输出。
    const LocusTable table = viable_table();
    const GeneticsConfig config = open_config();
    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());
    const SpeciesMask mask = registry.mask(SpeciesId{1});

    const std::vector<Genome> genomes{sterile_genome(), robust_genome()};
    const std::vector<Phenotype> phenotypes{sterile_phenotype(),
                                            robust_phenotype()};
    const std::vector<EnvironmentSnapshot> envs(2, EnvironmentSnapshot{});
    const std::vector<float> threats(2, 0.0f);
    const std::vector<std::uint32_t> species_ids(2, 1);

    std::vector<Parent> parents{Parent{0, &genomes[0], 0.0f},
                                Parent{1, &genomes[1], 0.0f}};
    const std::vector<ParentPool> sites{
        ParentPool{parents, SpeciesId{1}, std::nullopt, 2}};

    auto off_rng = RandomStreams{29, 0};
    const auto off = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    false},
        table, registry, config, off_rng);
    auto on_rng = RandomStreams{29, 0};
    const auto on = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    true},
        table, registry, config, on_rng);

    CHECK(off.stats.empty());
    CHECK_EQ(on.stats.size(), std::size_t{1});
    CHECK(off.survived == on.survived);
    CHECK(off.season_fitness == on.season_fitness);
    CHECK_EQ(off.offspring.size(), on.offspring.size());
    for (std::size_t i = 0; i < off.offspring.size(); ++i)
    {
        CHECK(off.offspring[i] == on.offspring[i]);
    }
}

TEST(settle_reports_the_survival_rate_of_every_individual)
{
    // survival_rates 是 survival_rate(…) 的原样回传，与抽签结果无关：它回答
    // 「这个季节有多难」，survived 回答「这一个个体活下来了」。Vitality 的
    // survival_rate 字段正是靠它才不再是恒为 1.0 的死字段。
    // 1 号绝育但代谢归零，生存率仍是满值；2 号 Lifespan 归零，被寿命这道硬
    // 判定挡在存活筛选之外，生存率保持 0（与「抽输了」是不同的成因）。
    const std::vector<Genome> genomes(4, Genome{});
    std::vector<Phenotype> phenotypes{robust_phenotype(), sterile_phenotype(),
                                      robust_phenotype(),
                                      coin_flip_phenotype()};
    phenotypes[2].traits[trait_index(TraitKind::Lifespan)] = 0.0f;
    const std::vector<EnvironmentSnapshot> envs(4, EnvironmentSnapshot{});
    const std::vector<float> threats(4, 0.0f);
    const std::vector<std::uint32_t> species_ids(4, 1);
    const std::vector<ParentPool> sites{};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{37, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    false},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.survival_rates.size(), genomes.size());
    CHECK_EQ(out.survival_rates[0], 1.0f);
    CHECK_EQ(out.survival_rates[1], 1.0f);
    CHECK_EQ(out.survival_rates[2], 0.0f);
    CHECK_EQ(out.survival_rates[3], 0.5f);
}

TEST(settle_reports_the_season_fitness_of_every_individual)
{
    // season_fitness 是 fitness(survival_rate, …) 的输出，与抽签结果无关。
    // 0 号绝育但必定存活，适应力因此是 0 而不是「死了所以没有值」——两条
    // 通道必须分得开，否则 BreedingSystem 会把绝育个体的适应力写成死因。
    // 2 号 Lifespan 归零，被寿命这道硬判定挡在存活筛选之外，适应力保持 0。
    const std::vector<Genome> genomes(3, Genome{});
    std::vector<Phenotype> phenotypes{sterile_phenotype(), robust_phenotype(),
                                      robust_phenotype()};
    phenotypes[2].traits[trait_index(TraitKind::Lifespan)] = 0.0f;
    const std::vector<EnvironmentSnapshot> envs(3, EnvironmentSnapshot{});
    const std::vector<float> threats(3, 0.0f);
    const std::vector<std::uint32_t> species_ids(3, 1);
    const std::vector<ParentPool> sites{};

    SpeciesRegistry registry;
    (void)registry.register_species(all_loci());

    auto rng = RandomStreams{37, 0};
    const auto out = settle(
        SeasonInput{genomes, phenotypes, envs, threats, species_ids, sites,
                    false},
        neutral_table(), registry, GeneticsConfig{}, rng);

    CHECK_EQ(out.season_fitness.size(), genomes.size());
    CHECK_EQ(out.survived[0], std::uint8_t{1});
    CHECK_EQ(out.season_fitness[0], 0.0f);
    CHECK_EQ(out.season_fitness[1], 1.0f);
    CHECK_EQ(out.survived[2], std::uint8_t{0});
    CHECK_EQ(out.season_fitness[2], 0.0f);
}

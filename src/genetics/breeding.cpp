// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/breeding.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <random>
#include <vector>

namespace gamev2::genetics {

Haplotype make_gamete(const Haplotype& x, const Haplotype& y,
                      std::size_t crossover_count, std::mt19937_64& rng)
{
    const std::size_t count = std::min(crossover_count, kMaxCrossovers);

    std::bitset<kLocusCount> cuts{};
    for (std::size_t i = 0; i < count; ++i)
    {
        cuts.set(uniform_index(rng, kLocusCount));
    }

    bool take_x = uniform_unit(rng) < 0.5f;
    Haplotype out{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        out[i] = take_x ? x[i] : y[i];
        if (cuts.test(i))
        {
            take_x = !take_x;
        }
    }
    return out;
}

namespace {

/// \pre `allele_count >= 2`：为 1 时 span 为 0，uniform_index(rng, 0) 会除零。
/// 该条件由 validate() 在配置期保证，热路径不做运行时检查是有意的。
AlleleId pick_other_allele(AlleleId current, std::uint8_t allele_count,
                           std::mt19937_64& rng) noexcept
{
    // 在其余 allele_count - 1 个等位基因中均匀选一个。
    const auto span = static_cast<std::size_t>(allele_count) - 1;
    const std::size_t offset = uniform_index(rng, span);
    return static_cast<AlleleId>(
        offset < current ? offset : offset + 1);
}

}  // namespace

void mutate(Haplotype& haplotype, const LocusTable& table,
            const GeneticsConfig& config, RandomStreams& streams)
{
    auto& rng = streams.stream(RandomPurpose::Jitter);
    auto& allele_rng = streams.stream(RandomPurpose::AlleleSwitch);

    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        const LocusDef& locus = table.loci[i];
        if (uniform_unit(rng) >= locus.switch_rate)
        {
            continue;
        }

        if (locus.kind == LocusKind::Structural)
        {
            haplotype[i].allele =
                pick_other_allele(haplotype[i].allele, locus.allele_count,
                                  allele_rng);
            continue;
        }

        if (uniform_unit(allele_rng) < config.allele_switch_share)
        {
            haplotype[i].allele =
                pick_other_allele(haplotype[i].allele, locus.allele_count,
                                  allele_rng);
            continue;
        }

        const auto step = static_cast<std::int8_t>(
            uniform_index(rng, static_cast<std::size_t>(
                2 * static_cast<int>(config.jitter_step) + 1))
            - static_cast<int>(config.jitter_step));

        int jitter = static_cast<int>(haplotype[i].jitter)
                     + static_cast<int>(step);
        // 必须有界：否则位点会沿某个方向无限漂移，等于给了它一条不消耗
        // 突变代价的适应趋势，加性微效的统计性质随之失效。
        jitter = std::clamp(jitter, -static_cast<int>(config.jitter_step),
                            static_cast<int>(config.jitter_step));
        haplotype[i].jitter = static_cast<std::int8_t>(jitter);
    }
}

namespace {

/// 在 CDF 上二分查找，把 fitness 权重转成一次抽样。
///
/// CDF 而非别名表：亲代池只有几十个量级，构建成本可忽略。
///
/// \pre `cdf` 非空且 `cdf.back()` 是全部权重之和；负的 fitness 已由调用方截为 0。
/// \pre `cdf.size() == pool.parents.size() > 0`。
[[nodiscard]] std::size_t sample_by_fitness(const ParentPool& pool,
                                            const std::vector<float>& cdf,
                                            std::mt19937_64& rng) noexcept
{
    const float total = cdf.back();
    // 判据是「不是可用的正有限值」而非 `total <= 0`：NaN 的所有比较都为假，
    // inf 也一样，两者都会绕过加权路径落到下面的夹取上，把整季钉死在一个亲代
    // 身上且不报错。std::max(NaN, 0.0f) 返回 NaN，NaN 因此能一路传到这里。
    if (!std::isfinite(total) || total <= 0.0f)
    {
        // CDF 此时不是一条严格递增的线：每次 upper_bound 都返回 end() 并被
        // 夹到最后一个亲代，整季只由 pool.back() 繁殖。退化为等概率。
        return uniform_index(rng, pool.parents.size());
    }
    const float target = uniform_unit(rng) * total;
    const auto it = std::ranges::upper_bound(cdf, target);
    const auto idx = static_cast<std::size_t>(it - cdf.begin());
    // 夹取不是可有可无的：total <= FLT_MIN 时，uniform_unit 的最大值会让 target
    // 被舍入成恰好 total（FLT_MIN 自身是整个触发区间里唯一的平局，落在半个 ULP
    // 上，按就近取偶归到 total），upper_bound 返回 end()，idx 就会越界读
    // pool.parents。
    return std::min(idx, pool.parents.size() - 1);
}

}  // namespace

std::expected<Genome, BreedingError>
make_offspring(const ParentPool& pool, const LocusTable& table,
               const GeneticsConfig& config, RandomStreams& streams)
{
    if (pool.parents.empty())
    {
        return std::unexpected(BreedingError::EmptyPool);
    }
    if (pool.parents.size() < 2)
    {
        return std::unexpected(BreedingError::PoolTooSmall);
    }

    std::vector<float> cdf;
    cdf.reserve(pool.parents.size());
    float running = 0.0f;
    for (const auto& p : pool.parents)
    {
        // survival_rate 是三个 clamp 因子之积，恒落在 [0, 1]，fitness() 因此
        // 不可能返回负数——这条截断实际只防手工构造的父池。但它不能省：不截
        // 的话 CDF 非单调，upper_bound「区间已排序」的前置条件随之失效。
        running += std::max(p.fitness, 0.0f);
        cdf.push_back(running);
    }

    auto& mating = streams.stream(RandomPurpose::Mating);
    const std::size_t i = sample_by_fitness(pool, cdf, mating);
    const std::size_t j = sample_by_fitness(pool, cdf, mating);

    const Genome& p0 = *pool.parents[i].genome;
    const Genome& p1 = *pool.parents[j].genome;

    auto& crossover = streams.stream(RandomPurpose::Crossover);
    const std::size_t cuts = uniform_index(crossover, kMaxCrossovers + 1);

    Genome child{};
    child.a = make_gamete(p0.a, p0.b, cuts, crossover);
    child.b = make_gamete(p1.a, p1.b, cuts, crossover);

    mutate(child.a, table, config, streams);
    mutate(child.b, table, config, streams);
    return child;
}

}  // namespace gamev2::genetics

/// @file breeding.hpp
/// @brief 繁殖：make_gamete 与 mutate 实现减数分裂和突变这两个遗传算子，
///        make_offspring 把它们串起来抽亲代并产出一个子代基因组。
///
/// 亲代池 ParentPool 由地图 / 行为 spec 按物种与空间分桶完毕后传入，本模块
/// 只借用其中的基因组指针、不关心它的来源，也不接管所有权。抽中的两个亲代
/// 可能是同一个个体，此时子代为自交——近交衰退在个体层面的入口就在这里。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/random.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <random>
#include <span>

namespace gamev2::genetics {

inline constexpr std::size_t kMaxCrossovers = 8;

/// 减数分裂产生一个配子：单倍型 x 与 y 在随机交叉点处交替取段。
///
/// 交叉点用位图而非排序数组记录——交叉点只作为「来源在此翻转」的集合有意义，
/// 顺序无关，位图省掉一次 O(k log k) 排序。
///
/// 交叉点数固定均值而非服从泊松分布：它对群体均值的影响远小于位点数，
/// 换来的是实现与调参的简单。
[[nodiscard]] Haplotype make_gamete(const Haplotype& x, const Haplotype& y,
                                    std::size_t crossover_count,
                                    std::mt19937_64& rng);

/// 对单条单倍型施加一次突变。
///
/// switch_rate 是该位点每代发生突变的概率。突变发生时，Trait 位点以
/// config.allele_switch_share 的概率替换等位基因、否则 jitter 微调，
/// Structural 位点一律均匀替换，jitter 对它无意义。
void mutate(Haplotype& haplotype, const LocusTable& table,
            const GeneticsConfig& config, RandomStreams& streams);

/// 亲代索引与相对适应力。父池由地图 / 行为 spec 按物种与空间分桶完毕后传入，
/// 本模块不关心它从哪来。index 只被 settle 读，本模块的抽样路径不碰它。
struct Parent
{
    /// 在调用方 SeasonInput::genomes 中的下标。settle 靠它把父池条目
    /// 映射回本季的存活标记与适应力。
    std::size_t index = 0;
    const Genome* genome = nullptr;
    /// 调用方给出的先验权重。settle 读得到它却不使用：重建父池时本字段被
    /// 本季适应力整体替换，先验值不与适应力相乘。存活标记则在建池时应用。
    float fitness = 0.0f;
};

/// 一个待繁殖位置及其亲代池。父池由地图 / 行为 spec 按物种与空间分桶完毕后
/// 传入，本模块只消费它，不判断它的来源。
struct ParentPool
{
    std::span<const Parent> parents;
    SpeciesId species{};
    /// 待繁殖位置的环境快照，由环境 spec 采样后传入。表型层的 g(E) 读它。
    std::optional<environment::EnvironmentSnapshot> environment{};
    /// 本季从该池产生的子代数量。生态位/空间上「一个 site 一个子代」是
    /// 自然读法,但物种级池需要按繁殖力批量产出,否则种群与物种数成反比
    /// 衰减。
    /// 按 1 处理,保持旧行为。
    std::uint32_t progeny = 1;
};

/// 繁殖期的失败原因。池内不足两个个体时无法完成有性生殖——该状态在真实
/// 的地理隔离末期可达，如何处置（放弃繁殖或允许自交）由调用方决定。
enum class BreedingError
{
    EmptyPool,
    /// 池内只有一个个体时无法完成有性生殖。这一状态在真实的
    /// 地理隔离末期是可达的，调用方必须决定是放弃繁殖还是允许自交。
    PoolTooSmall,
};

/// 抽取亲代、重组、突变，返回一个子代基因组。
///
/// 亲代按 fitness 加权，有放回抽取。同一个亲代可能被选中两次，
/// 此时子代为自交——这是近交衰退在个体层面的入口。
///
/// \pre `pool.parents` 中每个 `genome` 非空，且指向的对象活得比本次调用久；
/// 本模块只借用指针，不接管所有权。
/// \pre 池内亲代同物种。本模块不读 `pool.species`，也不校验——混物种池会静默
/// 产出子代，按物种分桶是调用方的责任（设计文档 §6.1）。
/// \pre `table` 已通过 validate()：突变的等位基因替换要求 `allele_count >= 2`。
[[nodiscard]] std::expected<Genome, BreedingError>
make_offspring(const ParentPool& pool, const LocusTable& table,
               const GeneticsConfig& config, RandomStreams& streams);

}  // namespace gamev2::genetics

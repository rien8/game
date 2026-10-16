/// @file locus.hpp
/// @brief 遗传蓝图的静态数据：位点类型与目录、单倍体的等位基因定义表
///        LocusTable、可调平衡系数 GeneticsConfig，以及加载期一次性校验
///        validate()。
///
/// 所有物种共享同一张蓝图，物种差异只体现为等位基因分布与激活掩码。蓝图是
/// 定长数组而非动态容器，才能作为普通值进 ECS 组件；相应地，配置错误必须在
/// 启动期由 validate 拦下，热路径不做任何检查。
#pragma once

#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace gamev2::genetics {

/// 位点的两种形态。数量性状位点贡献到一个连续性状轴，结构位点解析为离散
/// 形态状态；两者共用同一张表，只是消费方式不同。
enum class LocusKind : std::uint8_t
{
    Trait,       ///< 数量性状位点，贡献到一个连续性状轴
    Structural,  ///< 结构位点，解析为离散形态状态
};

/// 固定结构目录的槽位。槽位集合编译期固定，才能定长进 ECS。
enum class StructuralSlot : std::uint8_t
{
    BodySegments,
    LimbPairs,
    Eyes,
    Flags,  ///< 壳 / 翼 / 鳍 / 腺体的位域
    kSlotCount
};

/// 单个等位基因的取值定义。显性是设计数据给出的等级而非「谁是 A」，
/// 一个 A 对一个 a 的真实语义由此表达。
struct AlleleDef
{
    float additive = 0.0f;    ///< 加性取值 v_i
    std::uint8_t state = 0;   ///< 结构态编码，Trait 位点为 0
    std::uint8_t dom_rank = 0;///< 显性等级，越大越显
};

/// 单个位点的完整定义：它的形态、等位基因个数、投射目标、权重、显性系数与
/// 突变率，以及至多 kMaxAlleles 个等位基因的定义。
struct LocusDef
{
    LocusKind kind = LocusKind::Trait;
    std::uint8_t allele_count = 0;
    /// 目标编号，随 kind 而变：Trait 位点是 TraitKind 轴下标，Structural 位点
    /// 是 StructuralSlot 下标。两类位点共用一个字段，是为了让表达层对每个位点
    /// 都走同一条「算出取值、投到 target」的路径。
    std::uint8_t target = 0;
    float scale = 0.0f;       ///< 该位点在该轴上的权重
    float dominance = 0.0f;   ///< 显性系数 h，须 >= 0
    /// 每代发生突变的概率。不是「替换等位基因的概率」——后者会让突变率恒为 1。
    float switch_rate = 0.0f;
    std::array<AlleleDef, kMaxAlleles> alleles{};
};

/// 表达层与突变中需要调平衡的标量，随配置加载一次。
struct GeneticsConfig
{
    std::array<float, kTraitCount> trait_base{};
    std::array<float, kTraitCount> trait_min{};
    std::array<float, kTraitCount> trait_max{};
    std::int8_t jitter_step = 1;
    /// 1 单位 jitter 对应的 additive 偏移。int8_t 与 float 之间必须写明
    /// 比例尺，否则同一 jitter 值在不同位点上表示量级完全不同的效应。
    float jitter_unit = 0.01f;
    float allele_switch_share = 0.05f;
    /// 以下三项是 g(E) 的系数：环境偏移映射到性状轴的强度。
    float temperature_metabolic_load = 0.1f;
    float fertility_fecundity_gain = 1.0f;
    float light_sense_gain = 1.0f;
    float thermal_base_width = 1.0f;
    float thermal_resilience_gain = 1.0f;
    float escape_responsiveness = 1.0f;
    std::uint8_t max_overdominant_loci = 8;
};

/// 所有物种共享同一张蓝图。物种差异体现为等位基因分布与激活掩码。
struct LocusTable
{
    std::array<LocusDef, kLocusCount> loci{};
};

/// 蓝图配置的校验失败原因。除区间退化的 BadTraitRange 外，多数错误同样只在
/// 启动期暴露一次，因此枚举逐项给注释以便定位。
enum class LocusConfigError
{
    ///< allele_count 不在 [2, kMaxAlleles]
    AlleleCountOutOfRange,
    DuplicateRank,
    SlotOutOfRange,   ///< Structural 位点的 target 越界
    TraitOutOfRange,  ///< Trait 位点的 target 越界
    NegativeDominance,
    BadSwitchRate,
    TooManyOverdominant,
    /// 某个性状的区间退化：trait_min[t] >= trait_max[t]。相等时该性状被 clamp
    /// 成常量，上下界倒置则让 clamp 的区间自相矛盾。Lifespan 尤其致命——
    /// 它恒为 0 会让 outlived(p, 0) 恒真，全员判死、亲代池恒空、整季零子代
    /// 且不报错。
    BadTraitRange,
};

/// 只在加载配置时调用一次。热路径零校验：配置期不成立的条件是配置错误，
/// 应当在启动时失败，而不是在第 3 季第 4000 个个体身上失败。
[[nodiscard]] std::expected<void, LocusConfigError>
validate(const LocusTable& table, const GeneticsConfig& config);

}  // namespace gamev2::genetics

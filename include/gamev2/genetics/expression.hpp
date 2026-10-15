#pragma once

#include "gamev2/genetics/environment.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstdint>

namespace gamev2::genetics {

/// 基因型 (x, y) 在 Trait 位点上的取值。
///
/// 显性由等位基因的 dom_rank 推导而非由「谁是 A」决定——这正是「一个 A 对
/// 一个 a」的真实语义：显性是设计数据指定的等级关系。h 恒取非负，负值等价于
/// 把两个 dom_rank 对调，不构成额外自由度。
[[nodiscard]] float trait_value(const LocusDef& locus, AlleleId x,
                                 std::int8_t jx, AlleleId y, std::int8_t jy,
                                 const GeneticsConfig& config) noexcept;

/// 基因型 (x, y) 解析出的结构态编码。
///
/// 配置校验要求 Structural 位点内 dom_rank 两两不等，因此共显分支实际不会
/// 触发；写它只为让规则完备，不留未定义路径。
[[nodiscard]] std::uint8_t structural_state(const LocusDef& locus,
                                            AlleleId x, AlleleId y) noexcept;

/// f(G)：基因型到表型的映射，忽略环境。
///
/// 环境修正在此之后单独施加（见 apply_environment）：基因效应的夹取必须
/// 先于环境修正，否则一个极端基因型会被环境增量救回，掩盖掉本应淘汰的表型。
///
/// \pre `table` 已通过 validate()：Trait 位点的 `target` 因此
/// `< kTraitCount`，Structural 位点的 `< kSlotCount`——write_slot 的兜底
/// 断言中 alleles 的下标都靠这条兜底。
///
/// \pre `genome` 的每个 `AlleleCall::allele` 落在所属位点的
/// `[0, allele_count)` 内。validate() 只约束表侧（`allele_count <= kMaxAlleles`），
/// 而 AlleleId 是 uint8_t，外部直接构造的 Genome 可以带出越界下标；本模块
/// 自身经重组 / 突变产出的基因组天然满足。
[[nodiscard]] Phenotype express(const Genome& genome,
                                const LocusTable& table,
                                const SpeciesMask& mask,
                                const GeneticsConfig& config);

/// g(E)：环境只修正性状轴，不触碰结构字段。
void apply_environment(Phenotype& phenotype,
                       const EnvironmentSnapshot& environment,
                       const GeneticsConfig& config);

/// P = f(G) ⊕ g(E)
[[nodiscard]] Phenotype phenotype_of(const Genome& genome,
                                     const LocusTable& table,
                                     const SpeciesMask& mask,
                                     const EnvironmentSnapshot& environment,
                                     const GeneticsConfig& config);

}  // namespace gamev2::genetics

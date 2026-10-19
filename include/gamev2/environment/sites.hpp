/// @file sites.hpp
/// @brief 季内摄食与承载力预算：run_season 跑完一季的 K 步，产出每个体的
///        实测 feeding；build_sites 按格与物种算出本季的子代预算。
///
/// 环境层不读 Phenotype、不认 SpeciesId、不依赖 ECS —— Forager::demand 由
/// 调用方从表型算出，BreedingSite::species_key 是裸整数。这是 genetics 依赖
/// 方向不被反转的关键，也是这些断言能在没有 World 的前提下写出的原因。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gamev2::environment {

/// 一个摄食者。demand 由表型算出（SenseRange × Fecundity）。
struct Forager
{
    float x = 0.0f;
    float y = 0.0f;
    /// \pre demand >= 0：需求由表型算出，恒非负。越界不是可恢复的错误状态，
    ///      因此不返回错误。代码里已由 run_season 的 `demand <= 0.0f`
    ///      这条判据兜住，负值会走进「什么都不需要」那条分支而非饿死。
    float demand = 0.0f;
};

/// 跑完 steps_per_season 步：先再生，再逐格按存量均分摄食。
/// 返回与 foragers 同序的累计 feeding（0..1，1 = 从未受限）。
///
/// \pre `field.count_occupants(foragers)` 已用**同一批** `foragers` 调用：
///      换一批会让 occupants 与实际在格个体数不符，而 share = stock / n
///      会取到 inf 并把 feeding 变成 NaN（std::clamp 拦不住 NaN）。
/// \pre config.steps_per_season >= 1：返回值要除以它做归一化，0 会得到
///      `0.0f / 0.0f`，而 std::clamp 拦不住 NaN，下游抽签会一路带下去。
[[nodiscard]] std::vector<float>
run_season(ResourceField& field, std::span<const Forager> foragers,
           const EnvironmentConfig& config);

struct BreedingSite
{
    CellIndex cell;
    /// 外部物种标签，值域与 genetics::SpeciesId::value 一致。
    /// 环境层不认识 SpeciesId，翻译发生在 ecs/adapt。
    std::uint32_t species_key = 0;
    std::uint32_t progeny = 0;
    EnvironmentSnapshot environment;
};

/// 对每个有占用的格、每个出现在该格的物种算一次子代预算。
/// 算出 0 的格不发站点——settle 把 progeny == 0 按 1 处理。
///
/// 输出按 (cell, species_key) 排序后成组，因此与输入实体顺序无关。
///
/// \pre `foragers` 与 `species_keys` 等长同序；
/// `field.count_occupants(foragers)` 已用**同一批** `foragers` 调用：
///      换一批会让某格 occupants 为 0 而这里仍有它的个体，
///      `headroom * here / all` 于是取到 inf，而 `std::lround(inf)` 是 UB。
[[nodiscard]] std::vector<BreedingSite>
build_sites(const ResourceField& field, const ClimateField& climate,
            std::span<const Forager> foragers,
            std::span<const std::uint32_t> species_keys,
            const EnvironmentConfig& config);

}  // namespace gamev2::environment

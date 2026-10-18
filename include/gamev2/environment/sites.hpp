/// @file sites.hpp
/// @brief 季内摄食与承载力预算：SeasonFeeder 分步跑完一季的 K 步，产出每
///        个体的实测 feeding；run_season 是「一步到位」的同一件事；
///        build_sites 按格与物种算出本季的子代预算。
///
/// 环境层不读 Phenotype、不认 SpeciesId、不依赖 ECS —— Forager::demand 由
/// 调用方从表型算出，BreedingSite::species_key 是裸整数。这是 genetics 依赖
/// 方向不被反转的关键，也是这些断言能在没有 World 的前提下写出的原因。
#pragma once

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"

#include <cstddef>
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
    ///      因此不返回错误。代码里已由 SeasonFeeder::step 的 `demand <= 0.0f`
    ///      这条判据兜住，负值会走进「什么都不需要」那条分支而非饿死。
    float demand = 0.0f;
};

/// 季内摄食的分步器。把 run_season 的 K 步拆开，使渲染层能在步与步之间
/// 暂停、观察存量被吃穿的过程。
///
/// 持状态而非每步重建缓冲：steps_per_season 默认 8，每步重新分配会让一季
/// 的摄食成为世界的主要分配源。
class SeasonFeeder
{
public:
    /// \pre config.steps_per_season >= 1：finish 要除以它做归一化，0 会
    ///      得到 0.0f / 0.0f，而 std::clamp 拦不住 NaN，下游抽签一路带下去。
    explicit SeasonFeeder(EnvironmentConfig config);

    /// 开一季：按新的 forager 数量重置累加器。
    void reset(std::size_t forager_count);

    /// 跑一步：先 field.regrow()，再按扣减前的存量算人均份，最后按格统一
    /// 扣减，并把该步的满意度累加进内部缓冲。
    ///
    /// \pre `field.count_occupants(foragers)` 已用**同一批** `foragers`
    ///      调用过：换一批会让 occupants 与实际在格个体数不符，
    ///      share = stock / n 会取到 inf 并把结果变成 NaN。
    /// \pre `foragers.size()` 等于 reset 给定的数量。
    void step(ResourceField& field, std::span<const Forager> foragers);

    /// 本季结束：累加值除以 steps_per_season 并夹到 [0, 1]。返回与 reset
    /// 时给定数量同序的实测 feeding。
    [[nodiscard]] std::vector<float> finish() const;

private:
    EnvironmentConfig config_;
    std::vector<float> satisfaction_;
    std::vector<float> got_;
    std::vector<float> want_of_;
    /// 唯一跨季存活的成员：reset 不清它，正确性全靠 step() 开头那句无条件
    /// fill。reset 若被改成跳过定容的早退（如 `if (already_sized_) return;`），
    /// 或有代码在 step() 之外读它，都会静默读到上季残留，且没有任何编译期
    /// 或运行期信号。
    std::vector<float> per_cell_;
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

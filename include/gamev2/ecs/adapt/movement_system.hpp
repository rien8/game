/// @file movement_system.hpp
/// @brief 行为系统：每季把个体按其 Speed 性状朝一个随机方向推进一步。
///
/// 它存在的理由是 build_sites 的一个结构性后果：出生预算取
/// min(eco_ceiling, parent_ceiling)，而 parent_ceiling =
/// lround(here × offspring_scale)——没有亲代就没有站点，于是一个空掉的格
/// 永远不会再被填上。个体不动，地图上的可用面积就只减不增，世界必然单调
/// 收缩。见 doc/2026-10-16-world-design.md §8。
#pragma once

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/terrain.hpp"

#include <cstdint>
#include <tuple>
#include <utility>

namespace gamev2::ecs::adapt {

/// 随机游走 + 资源存量梯度。每季每个体先掷硬币：命中梯度就在 4 个可通行
/// 邻格里挑**比本格更富**的那个；没有比本格更富的邻格时（已在局部峰顶，
/// 或四面环水）退回随机方向。两者随后走同一条推进路径——整数格步长、
/// 逐格判通行、撞墙沿墙滑。
///
/// 「比本格更富」而不是「4 邻里存量最高的」：后者在个体站上局部峰顶时仍会
/// 把它推向更贫的邻格，类头下面承诺的那条负反馈因此落空。
///
/// 步长改为整数是被沿墙滑行逼出来的：浮点步长没法逐格判碰撞。
///
/// 梯度追的是**存量**而不是容量。存量会被吃穿，因此**设计上**带负反馈——
/// 挤进富格 → 吃光 → 梯度翻转 → 离开；容量静态、永远给一个干净的梯度，却
/// 没有反向作用力，所有个体会一起爬向同一批格并留在那里。
///
/// **那条负反馈目前是未验证假设，不是既成机制。** 现有测试守不住它：
/// `a_gradient_bearing_...` 的夹具不跑 EnvironmentSystem，资源场静态，因此
/// 「追存量」与「追容量」在那个夹具里是同一件事，负反馈从未被触发。引用它
/// 设计阶段 3 的 hysteresis 之前，先把它当假设验一遍。
///
/// 另注：设计文档 §2.1 曾断言梯度「指向空而肥的格子，是填坑而不是拥挤」，
/// 已被 §2.2 推翻 —— 梯度不填空，它**重新分配**，富格被填上而中等格被腾空。
/// 那句原文不要再引用。
///
/// 只在 execute 里就地改写 Position，不做任何结构变更，因此不需要
/// mutates_structure。
class MovementSystem final : public System
{
public:
    /// 排在 EnvironmentSystem 之前——摄食要按当季坐标分格，与 BreedingSystem
    /// 的先后同理。二者的冲突都由访问集自动成立：本 system 写 Position，
    /// 对方读 Position。**不要只靠这一行表达先后**——System::order 只在两个
    /// system 本来就会冲突时才等于批次先后，而这两条冲突是真的。
    static constexpr int order = -1;

    /// 只读 CreaturePhenotype 是为了取 Speed 定步长。
    using ReadSet = std::tuple<const CreaturePhenotype>;
    using WriteSet = std::tuple<Position>;

    /// \pre resources 与 terrain 的生命周期必须覆盖本对象——它们随
    ///       EnvironmentWorld 构造一次即固定，World 持有后者。
    MovementSystem(std::uint64_t root_seed, std::uint32_t season_index,
                   const gamev2::environment::ResourceField& resources,
                   const gamev2::environment::TerrainField& terrain,
                   const float gradient_weight)
        : root_seed_{root_seed},
          season_index_{season_index},
          resources_{resources},
          terrain_{terrain},
          gradient_weight_{gradient_weight}
    {
    }

    /// 改写本 system 消费的季号。每季开始前调用一次。
    ///
    /// \pre 调用必须发生在 execute 之前：execute 在开头把 root_seed_ 与
    ///       season_index_ 混成随机流，之后改它不会有效果。
    void advance_season(std::uint32_t season_index) noexcept
    {
        season_index_ = season_index;
    }

    void execute(World& world, CommandBuffer& cmd) override;

private:
    /// 这个坐标能不能站人。越界与水下都算不能。
    [[nodiscard]] bool standable(const float x, const float y) const noexcept;

    /// 4 个可通行邻格里**比本格更富**者的方向。返回 {0, 0} 表示无处可去
    /// （四面环水）或没有邻格比本格富，调用方据此退回随机游走。
    ///
    /// 邻格之间同存量时取遍历序里的第一个（+x, −x, +y, −y）：固定遍历序让
    /// 平手有确定答案，否则同种子的两次运行可能各挑一个不同的平手方向。
    [[nodiscard]] std::pair<float, float>
    richest_neighbour(const float x, const float y) const noexcept;

    /// 离 (x, y) 最近的那个可通行格的中心。整张图没有陆地时原样返回。
    ///
    /// 四邻不够用：被水围死又撞着图边界的那一格，逐格判通行加沿墙滑行
    /// 永远出不去——设计文档 §7.2 契约 3 的钳回按「最近的可行走格」定，
    /// 不是按「邻格可达」。
    [[nodiscard]] std::pair<float, float>
    nearest_standable(const float x, const float y) const noexcept;

    /// 朝 (dx, dy) 推进 stride 格，逐格判通行；撞墙先试只走 x 再试只走 y，
    /// 两边都不通就停在最后一个能站的格。
    void advance(Position& position, const float dx, const float dy,
                 const int stride) const noexcept;

    std::uint64_t root_seed_;
    std::uint32_t season_index_;
    const gamev2::environment::ResourceField& resources_;
    const gamev2::environment::TerrainField& terrain_;
    float gradient_weight_ = 0.0f;
};

}  // namespace gamev2::ecs::adapt

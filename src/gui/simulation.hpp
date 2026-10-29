/// @file simulation.hpp
/// @brief 演示程序的世界实例：装配 Ecology，管住玩家的那一个个体。
///
/// 不依赖 Qt：界面读它的快照与只读引用，不反过来改它的内部。
#pragma once

#include "gamev2/demo/scenario.hpp"
#include "gamev2/ecology/ecology.hpp"
#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace gamev2::gui {

/// 一个个体的只读快照。选择列表与状态面板读它，不碰 ECS。
struct CreatureView
{
    gamev2::ecs::EntityId id{};
    std::uint32_t age = 0;
    float fitness = 0.0f;
    float survival_rate = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float body_size = 0.0f;
    float speed = 0.0f;
    float lifespan = 0.0f;
    float resilience = 0.0f;
};

class Simulation
{
public:
    /// 装配一局。配置非法时抛 std::runtime_error——配置来自固定常量，
    /// 真出问题就是程序错了，不该让它悄悄跑起来。
    Simulation(std::uint64_t seed, gamev2::demo::Preset preset);

    /// 换种子与预设重开一局。
    ///
    /// 世界实例就地重建，外部持有的 Simulation 引用仍然有效——界面不必
    /// 重建自己。
    void reset(std::uint64_t seed, gamev2::demo::Preset preset);

    /// 推进一季。玩家的本季移动意图在结算前落到 Position 上。
    void step();

    /// 已推进的季数。
    [[nodiscard]] std::uint32_t season() const noexcept { return season_; }

    /// 本季存活总数。
    [[nodiscard]] std::size_t population() const noexcept { return population_; }

    /// 杂合度、近交系数：唯一物种的统计。
    [[nodiscard]] float heterozygosity() const noexcept
    {
        return heterozygosity_;
    }
    [[nodiscard]] float inbreeding() const noexcept { return inbreeding_; }

    /// 玩家接管的个体。空句柄表示无人接管。
    [[nodiscard]] gamev2::ecs::EntityId player() const noexcept
    {
        return player_;
    }

    /// 玩家个体是否还活着。
    [[nodiscard]] bool player_alive() const noexcept { return player_alive_; }

    /// 玩家的状态快照。玩家死亡时返回空快照。
    [[nodiscard]] CreatureView player_view();

    /// 接管一个个体。
    void take_control(gamev2::ecs::EntityId e);

    /// 交还控制权：被接管的个体重新参与随机游走。
    void release_control() noexcept;

    /// 玩家的移动意图。方向键每按一次记一次方向；季末按玩家自己的 Speed
    /// 性态折成步长落到 Position 上——与其它个体同一套步长公式，被接管不会
    /// 换来额外机动。
    void nudge(int dx, int dy) noexcept;

    /// 候选个体：活过至少一季的个体，按存活率从高到低取若干个。
    ///
    /// 存活率高的排前面——开局那几季环境对个体不利，存活率可能只有 0.1，
    /// 接管它等于每季赌命，玩家会被死亡弹窗反复打断。
    ///
    /// 刚出生的个体挂着默认构造的 Vitality，适应力恒为 1.0——把它们排在前面
    /// 会让列表看起来全是「满分个体」，因此只取活过一季的；真的一季都没活过
    /// 时才拿它们垫底。
    [[nodiscard]] std::vector<CreatureView> candidates(std::size_t limit);

    /// 生态场。视图读它画底色。
    [[nodiscard]] const gamev2::ecs::adapt::EnvironmentWorld&
    environment() const noexcept
    {
        return ecology_->environment();
    }

    [[nodiscard]] gamev2::ecs::World& entities() noexcept
    {
        return ecology_->entities();
    }

    [[nodiscard]] gamev2::demo::Preset preset() const noexcept
    {
        return preset_;
    }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

private:
    /// 把玩家的移动意图落到 Position 上。
    void apply_intent();

    /// 取一个实体的快照。组件缺失时返回空快照。
    [[nodiscard]] CreatureView view_of(gamev2::ecs::EntityId e);

    std::uint64_t seed_ = 0;
    gamev2::demo::Preset preset_ = gamev2::demo::Preset::Evolve;

    /// 必须在 ecology_ 之前声明：Ecology 持有 scenario_.table 的引用，
    /// 而 reset 会就地改写 scenario_ —— 引用的地址不变，内容才换得掉。
    gamev2::demo::Scenario scenario_{};

    /// 就地重建：Ecology 不可移动也不可拷贝，reset 只能靠 emplace。
    std::optional<gamev2::ecology::Ecology> ecology_;

    std::uint32_t season_ = 0;
    std::size_t population_ = 0;
    float heterozygosity_ = 0.0f;
    float inbreeding_ = 0.0f;

    gamev2::ecs::EntityId player_{};
    bool player_alive_ = false;
    int pending_dx_ = 0;
    int pending_dy_ = 0;
};

}  // namespace gamev2::gui

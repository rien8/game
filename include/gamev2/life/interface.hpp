/// @file interface.hpp
/// @brief 众生层的公共入口：初始化众生、导出名册。
#pragma once

#include "gamev2/ecs/world.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gamev2::life {

/// 名册条目：玩家选身位时看到的一行（设计 §5.1）。
struct RosterEntry
{
    std::uint32_t soul = 0;
    std::string name;
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t age = 0;
    Temper temper;
    Aptitude aptitude;
};

/// 开局名册。
struct Roster
{
    std::uint32_t year = 0;
    std::vector<RosterEntry> entries;
};

/// 诊断（设计 §7）。其余字段随各自的刀加入：本刀只消费
/// 「有没有当代」这一条。
struct LifeDiagnostics
{
    /// 当代早于纪元：没有当代格局，就没有众生可初始化。
    std::uint32_t no_contemporary_state = 0;
};

/// 众生层的容器。刀 1 只做种子与名册；年循环在刀 2。
class Life
{
public:
    Life(const gamev2::world::World& world,
         const gamev2::faction::EraReport& era,
         LifeDiagnostics& diagnostics);

    /// 从当代格局初始化众生，写实体与名册。重复调用是调用方的错，
    /// 不做幂等兜底。
    void seed();

    [[nodiscard]] const Roster& roster() const noexcept
    {
        return roster_;
    }
    [[nodiscard]] gamev2::ecs::World& ecs_world() noexcept
    {
        return ecs_world_;
    }
    [[nodiscard]] std::uint32_t year() const noexcept { return year_; }

private:
    gamev2::ecs::World ecs_world_;
    const gamev2::world::World& world_;
    const gamev2::faction::EraReport& era_;
    LifeDiagnostics& diagnostics_;
    std::uint32_t year_ = 0;
    Roster roster_;
    std::vector<gamev2::ecs::EntityId> souls_;
};

/// 导出人可读名册文本。返回 false 表示文件写不动。
[[nodiscard]] bool export_roster(const Roster& roster,
                                 const std::filesystem::path& path);

}  // namespace gamev2::life

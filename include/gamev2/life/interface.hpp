/// @file interface.hpp
/// @brief 众生层的公共入口：初始化众生、导出名册。
#pragma once

#include "gamev2/ecs/world.hpp"
#include "gamev2/faction/era.hpp"
#include "gamev2/life/economy.hpp"
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
    Sex sex = Sex::male;
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
    /// 灵石恒等式对账不通过的年份（设计 §7）。
    std::uint32_t stone_conservation_broken = 0;
    /// 有职位但无宗门归属（内部一致性，设计 §7）。
    std::uint32_t office_without_sect = 0;
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

    /// 逐宗金库账。非 const：读的是可变推演状态，与 ecs_world() 同理由。
    [[nodiscard]] gamev2::life::EconomyLedger& economy()
    {
        return economy_;
    }
    [[nodiscard]] const gamev2::life::EconomyLedger& economy() const
    {
        return economy_;
    }

    /// 推进 n 年：设计 §1.2 年循环的五步（Clock→Aging→Birth→
    /// Cultivate→Economy）。玩家窗口与派系交接随后续刀加入。
    ///
    /// 当代格局为空时 seed 不建时钟，本函数直接返回、不推进任何年号。
    void run_years(std::uint32_t n);

    /// 当前年（seed 后等于开局年，run_years 后逐年推进）。非 const：
    /// 读的是可变的推演状态，与 ecs_world() 同理由。空格局下同样返回
    /// 开局年（名册里的年号），不依赖时钟实体是否存在。
    [[nodiscard]] std::uint32_t year();

private:
    gamev2::ecs::World ecs_world_;
    const gamev2::world::World& world_;
    const gamev2::faction::EraReport& era_;
    LifeDiagnostics& diagnostics_;
    gamev2::ecs::EntityId clock_{};
    /// 新生实体的下一个序号。从初始人口总数起单调递增，让「初始队列
    /// vs 新生队列」可按 index 区分。
    std::uint32_t next_index_ = 0;
    Roster roster_;
    std::vector<gamev2::ecs::EntityId> souls_;
    EconomyLedger economy_;
};

/// 导出人可读名册文本。返回 false 表示文件写不动。
[[nodiscard]] bool export_roster(const Roster& roster,
                                 const std::filesystem::path& path);

}  // namespace gamev2::life

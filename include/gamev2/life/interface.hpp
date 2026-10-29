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

/// 众生层事件。事件表是「谁在何年对谁做了什么」的唯一权威——刀 6 的
/// 传承接管与刀 7 的编年都从它取材，字段不得事后改义。
enum class LifeEventKind : std::uint8_t
{
    succession,  ///< 宗主换代（推举）
    promotion,   ///< 晋升（外→内、补长老）
    departure,   ///< 断供出走
    expulsion,   ///< 被逐（夺位败/夺位后的旧宗主）
    poach,       ///< 盗采（amount = 得石）
    contest,     ///< 夺位（amount = 1 胜 0 败）
    avenge,      ///< 寻仇（amount = 1 胜 0 败）
    social,      ///< 社交（amount = 1 结善缘 2 立威）
    slain,       ///< 横死（历练/被擒/寻衅/寻仇败）
};

/// 一条事件。actor/counterparty 以 SoulId::index 标识，0 = 无。
struct LifeEvent
{
    LifeEventKind kind = LifeEventKind::succession;
    std::uint32_t year = 0;
    std::uint32_t actor = 0;
    std::uint32_t counterparty = 0;
    history::Sect sect = history::Sect::unowned;
    std::uint32_t amount = 0;
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
    /// 关系边一端已死未清理（设计 §7）。年末兜底扫描报出。
    std::uint32_t orphan_bonds = 0;
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

    /// 事件表。刀 4 起逐年累积。
    [[nodiscard]] const std::vector<LifeEvent>& events() const noexcept
    {
        return events_;
    }
    /// 关系边集。与 EconomyLedger 同一先例：不是组件，是 Life 持有的
    /// 普通结构体。
    [[nodiscard]] const BondGraph& bonds() const noexcept
    {
        return bonds_;
    }
    /// 厚域格表（线性格号，升序）。开局从 world.domain_grade 扫出，
    /// 是「找厚域闭关」与「金丹厚域门槛」的共同权威。
    [[nodiscard]] const std::vector<std::uint32_t>& thick_cells()
        const noexcept
    {
        return thick_cells_;
    }

    /// 推进 n 年：设计 §1.2 年循环的七步（Clock→Aging→Birth→
    /// Cultivate→Decide→Act→Economy）。玩家窗口与派系交接随后续刀
    /// 加入。
    ///
    /// 当代格局为空时 seed 不建时钟，本函数直接返回、不推进任何年号。
    void run_years(std::uint32_t n);

    /// 当前年（seed 后等于开局年，run_years 后逐年推进）。非 const：
    /// 读的是可变的推演状态，与 ecs_world() 同理由。空格局下同样返回
    /// 开局年（名册里的年号），不依赖时钟实体是否存在。
    [[nodiscard]] std::uint32_t year();

private:
    /// 按当年的潮位重算厚域格表。世界的浓度场是潮位缩放出来的，
    /// 厚域因此不是一张静态图——谷底期全天下都没有厚域。
    void refresh_thick_cells(std::uint32_t year);

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
    BondGraph bonds_;
    std::vector<LifeEvent> events_;
    std::vector<std::uint32_t> thick_cells_;
    /// 无主脉号（含枯脉）。盗采的目标表——空表则无人得起盗采心。
    std::vector<std::uint32_t> unowned_veins_;
};

/// 导出人可读名册文本。返回 false 表示文件写不动。
[[nodiscard]] bool export_roster(const Roster& roster,
                                 const std::filesystem::path& path);

}  // namespace gamev2::life

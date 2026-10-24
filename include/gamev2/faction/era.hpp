/// @file era.hpp
/// @brief 派系演化层的产物：当代格局、事件表与文本导出。
#pragma once

#include "gamev2/faction/faction.hpp"
#include "gamev2/history/ledger.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gamev2::faction {

/// 事件类型。
///
/// 只有这几种：多了就说明有行为没有被想清楚，它会变成事件表里的
/// 一行谁也读不懂的记录。
///
/// `count` 是哨兵，跟 `history::Sect::count` 一个用法：种类数由它定，
/// 按种类开的长度和表都用它定长。少了它，「多了一种事件」就只是一句
/// 注释，代码拦不住——计数表会静默越界。
enum class EraEventKind : std::uint8_t
{
    redraft_proposed,    ///< 有人提出重勘
    redraft_passed,      ///< 表决通过
    redraft_rejected,    ///< 表决被否
    vein_transferred,    ///< 脉换了持有者
    vein_depleted,       ///< 脉枯竭
    vein_publicized,    ///< 枯竭的脉公有化
    overmine_discovered, ///< 越采被发现
    cave_heaven_lost,   ///< 洞天失效
    count,              ///< 哨兵：种类数，不是事件类型
};

/// 一条事件。
struct EraEvent
{
    EraEventKind kind = EraEventKind::redraft_proposed;
    std::uint32_t year = 0;
    /// 涉及的脉（提案与表决不用脉）。
    std::uint32_t vein = 0;
    /// 谁做的。
    Sect actor = Sect::unowned;
    /// 涉及谁（受损方）。越采没有确定的受害方，这一项恒为无主。
    Sect counterparty = Sect::unowned;
    /// 数额（开采量、转移的脉数等）。
    std::uint32_t amount = 0;
};

/// 一脉的当代状态。
struct VeinReport
{
    std::uint32_t vein = 0;
    Sect owner = Sect::unowned;
    /// 8300 年的持有者。与 owner 不同就是格局变过。
    Sect initial_owner = Sect::unowned;
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;
    std::uint32_t quota_total = 0;
    std::uint32_t extracted = 0;
    /// 累计合法开采（不含越采）。与台账的对照只比这一项。
    std::uint32_t legal_extracted = 0;
    std::uint32_t balance = 0;
    std::uint32_t depleted_year = 0;
    bool registered = true;
    bool yin = false;
};

/// 一宗的当代格局。
struct FactionReport
{
    Sect sect = Sect::unowned;
    std::uint32_t vein_count = 0;
    std::uint32_t initial_vein_count = 0;
    std::uint32_t quota_total = 0;
    std::uint32_t balance = 0;
    std::uint32_t demand = 0;
    std::uint32_t supply = 0;
    std::uint32_t deficit = 0;
    std::uint32_t extracted = 0;
    std::uint32_t overmined = 0;
    std::uint32_t unregistered = 0;
    std::uint32_t vote_weight = 0;
    std::uint32_t cave_heavens = 0;
    /// 用度无上限（魔修、散修）。
    ///
    /// 模型自己的权威位：`FactionBalance::unbounded_demand`。导出层不再
    /// 按角色重推一遍——同一口径有两个来源，改了一处忘了另一处，导出就会
    /// 给一个缺口是真数的派系印上「不适用」。
    bool unbounded_demand = false;
};

/// 当代格局。
struct EraReport
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    /// 实际推演的年份数。
    std::uint32_t years_simulated = 0;
    /// 最后一个被推演的年份。
    std::uint32_t current_year = 0;
    /// 最后一年的潮位。
    std::uint32_t current_level = 0;
    std::vector<FactionReport> factions;
    std::vector<VeinReport> veins;
    std::vector<EraEvent> events;
    /// 当代与 8300 的持有格局不同的脉数。
    std::uint32_t owner_changes = 0;
    std::uint32_t redraft_passed = 0;
    std::uint32_t redraft_rejected = 0;
    std::uint64_t total_extracted = 0;
    std::uint64_t total_balance = 0;
};

/// 从 world 与台账推演 8300 → 当代，产出当代格局。
///
/// 退化不是错误：由 diagnostics 报出，由调用方决定要不要打印。
[[nodiscard]] EraReport simulate(const gamev2::world::World& world,
                                 const gamev2::history::Ledger& ledger,
                                 FactionDiagnostics& diagnostics);

/// 导出人可读格局文本。返回 false 表示文件写不动。
///
/// 诊断量一并写出：退化不是错误，但格局是给人读的文档，读者必须能
/// 在里面看见每一处对不上的地方。
[[nodiscard]] bool export_era(const EraReport& report,
                              const FactionDiagnostics& diagnostics,
                              const std::filesystem::path& path);

}  // namespace gamev2::faction

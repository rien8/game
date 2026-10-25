/// @file systems.hpp
/// @brief 派系演化层的私有头：推演上下文与推演器。
///
/// 不放进公共头：这些类型是实现细节，调用方只需要 faction.hpp 与
/// era.hpp。测试也不许包含本头——测试要读的东西必须出现在 EraReport 上。
#pragma once

#include "gamev2/faction/era.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gamev2::faction {

/// 一次劫损。台账记的：哪一年、哪条脉、抽走多少。
///
/// 金额是台账的**记账值**，本层原样重放——劫云抽走多少是既成事实，
/// 不是本层要重算的量。
struct Toll
{
    std::uint32_t year = 0;
    std::uint32_t vein = 0;
    std::uint32_t amount = 0;
};

/// 推演上下文。
///
/// 标定系数在这里固化一次：两层推演必须用同一个值，否则对照表失效。
struct SimConfig
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    /// 标定系数（台账的同一个值）。
    std::uint64_t conversion = 0;
};

/// 推演器：持有实体世界、时钟与事件表。
class Simulation
{
public:
    Simulation(const gamev2::world::World& world,
               const gamev2::history::Ledger& ledger,
               SimConfig config,
               std::vector<EraEvent>& events);

    /// 建实体：脉、派系、洞天、时钟。
    void seed_entities();

    /// 推演 [epoch, now) 的每一年。
    void run(FactionDiagnostics& diagnostics);

    /// 台账记的劫损，按年施加。
    [[nodiscard]] std::vector<Toll> tolls_of_ledger() const;

    /// 脉实体与脉编号的对应。
    [[nodiscard]] std::vector<std::pair<gamev2::ecs::EntityId, std::uint32_t>>
    vein_entities() const;

    /// 把实体世界的终态读成报告。
    void collect(EraReport& report, FactionDiagnostics& diagnostics);

private:
    /// 输入。引用：生命周期由调用方保证（推演器不拥有世界）。
    const gamev2::world::World& world_;
    const gamev2::history::Ledger& ledger_;
    /// 实体世界。名字与上面那个 `world_` 故意分开：一个是只读的
    /// 地图，一个是本层自己的实体世界，两个都是「世界」，混成一个名字
    /// 迟早会写错其中一个。
    gamev2::ecs::World ecs_world_;
    gamev2::ecs::EntityId clock_;
    SimConfig config_;
    /// 事件表。引用：生命周期由调用方保证（推演器不拥有它）。
    std::vector<EraEvent>& events_;
    /// 脉实体的顺序。视图的遍历顺序不保证与台账一致，
    /// 所以要自己记一份，才能按脉编号取回。
    std::vector<gamev2::ecs::EntityId> veins_;
    std::vector<gamev2::ecs::EntityId> factions_;
};

/// 本年要不要重勘。
///
/// 例行年由历法定：脉律每二百年一勘（设定 §6.5）。提前年要执掌盟约者
/// 当年有缺口，且距上次提请已满 kEarlyRedraftInterval——提案与表决是
/// 大事，不能年年开。
///
/// 越采与重勘都要这个判据：越采要知道「程序今年走过了没有」，重勘要
/// 知道「今年开不开」。两处各写一份，迟早走偏。
[[nodiscard]] bool redraft_this_year(std::uint32_t year, std::uint32_t epoch,
                                     const FactionIntent& covenant,
                                     std::uint32_t covenant_deficit);

/// 时钟。每年把年推进一格，并写入当年的潮位。
///
/// 它排在批首：当年的潮位是所有经济计算的输入，读者必须落在
/// 写者之后——而调度器靠读写集保证这一点（ClockSystem 写 SimClock，
/// 别的 system 读它，所以它们自动落到后面一批）。
class ClockSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 1 步。七个 system 的 order 一律写死：
    /// 调度器按 order 定序，注册顺序只是同 order 时的次级键——靠注册序
    /// 兑现先后，等于把语义押在「注册代码不动」上。
    ///
    /// order 只在两个 system 本来就会冲突时才兑现批次先后。这七对两两
    /// 冲突（时钟被所有人读、脉状态与派系账被多人写），因此 order 说的
    /// 就是批次先后。
    static constexpr int order = 1;

    /// 读写集。`const SimClock` 出现在别的 system 的读集里，是为了让
    /// 调度器把「写钟的」与「读钟的」分到不同批次——同批次是并行跑的。
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<SimClock>;

    explicit ClockSystem(gamev2::ecs::EntityId clock);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_;
};

/// 劫损。天地抽走的一部分，按台账记的账重放一遍。
///
/// 它排在时钟之后、评估之前：劫云在年内抽干，抽取要看到被抽干之后的
/// 余额——台账的年序是「越采 → 劫损 → 正常开采」，本层的越采在开采之后，
/// 但劫损必须赶在开采之前，否则被抽走的配额还会被人采走。
class TribulateSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 2 步。
    static constexpr int order = 2;

    /// `const SimClock` 必须在读集里：调度器只靠读写集判冲突，本 system
    /// 要读当年的年份，不声明成读，它就会与写钟的 ClockSystem 落进同一批
    /// 并行跑。
    using ReadSet = std::tuple<const SimClock, const VeinStock>;
    using WriteSet = std::tuple<VeinStock>;

    TribulateSystem(
        std::vector<Toll> tolls,
        std::vector<std::pair<gamev2::ecs::EntityId, std::uint32_t>> veins,
        gamev2::ecs::EntityId clock,
        std::vector<EraEvent>& events,
        FactionDiagnostics& diagnostics);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    /// 按年分桶。年份是稀疏的：有些年没有劫。逐年扫全表是六百多次
    /// 无谓比较，分桶后每一年一次查找。
    std::unordered_map<std::uint32_t, std::vector<Toll>> by_year_;
    /// 脉编号 → 实体。视图的遍历顺序不保证与脉编号一致，只能按编号找。
    std::unordered_map<std::uint32_t, gamev2::ecs::EntityId> entity_of_;
    gamev2::ecs::EntityId clock_;
    std::vector<EraEvent>& events_;
    FactionDiagnostics& diagnostics_;
};

/// 评估。对每个派系，扫描它名下的脉，算当年的供给与缺口。
///
/// 排在开采之前：缺口按年初状态算，越采再按这个缺口分摊。
class AssessSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 3 步。
    static constexpr int order = 3;

    /// `const SimClock` 必须在读集里：调度器只靠读写集判冲突，
    /// 潮位不声明成读，本 system 就会与写钟的 ClockSystem 落进
    /// 同一批并行跑，读到旧值或撕裂值。
    using ReadSet = std::tuple<const SimClock, const VeinOwner,
                               const VeinRight, const VeinStock,
                               const VeinProfile, const FactionId>;
    using WriteSet = std::tuple<FactionBalance>;

    explicit AssessSystem(std::uint64_t conversion);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t conversion_;
};

/// 合法开采。对每条脉，取有效流速、剩余配额、余额三者的最小值开采。
class ExtractSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 4 步。
    static constexpr int order = 4;

    /// 同 AssessSystem：`const SimClock` 声明成读，调度器才不会把
    /// 本 system 与写钟的 ClockSystem 分进同一批。
    ///
    /// `const VeinOwner` 不是白列的：合法开采要记在归属派系的账上
    /// （`FactionBalance::extracted`），归属从它读。
    ///
    /// `const FactionId` 同样不是白列的：派系账按宗门编号索引，
    /// 索引表由 `build_faction_table` 按 `FactionId` 建出来。
    using ReadSet = std::tuple<const SimClock, const VeinOwner,
                               const VeinRight, const VeinProfile,
                               const FactionId>;
    /// `FactionBalance` 在写集里：本 system 写归属派系的累计开采，
    /// OvertakeSystem 写其中的越采部分。两处都是单调累加，写集冲突
    /// 保证它们分属不同批次，不会互相覆盖。
    using WriteSet = std::tuple<VeinStock, VeinRemainder, FactionBalance>;

    explicit ExtractSystem(std::uint64_t conversion);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t conversion_;
};

/// 越采。缺口（或无上限用度）驱动的超额抽取，外加越采被发现的事件。
///
/// 排在开采之后：越采在当年合法开采的基础上再抽，余额已经扣过一轮。
class OvertakeSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 5 步。
    static constexpr int order = 5;

    /// 读集里带 `const VeinId`（简报清单之外的追加）：发现事件要写
    /// 脉编号，随机也按脉编号无状态派生。
    ///
    /// 读集里没有 `const VeinProfile`：有效流速由 ExtractSystem 算好
    /// 记在 `VeinStock::eff`，本 system 复用它，不重算（设计 §3.13：
    /// 衰减率只在年初算一次）。
    ///
    /// `const FactionIntent` 是正派越采的闸门：本 system 要知道「程序
    /// 今年走过了没有」（设计 §3.7），那要读执掌盟约者上次提请的年份。
    using ReadSet = std::tuple<const SimClock, const VeinId, const VeinOwner,
                               const VeinRight, const FactionId,
                               const FactionIntent>;
    using WriteSet = std::tuple<VeinStock, FactionBalance>;

    OvertakeSystem(std::uint64_t seed, std::uint32_t epoch,
                   std::vector<EraEvent>& events);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    /// 世界种子：越采发现按 (年, 派系, 脉) 从它无状态派生。
    std::uint64_t seed_;
    /// 纪元。重勘的例行年由它算。
    std::uint32_t epoch_;
    std::vector<EraEvent>& events_;
};

/// 枯竭与洞天失效。
///
/// 它排在开采与越采之后：枯竭年是余额归零的那一年，判定必须在
/// 开采之后。
class DepleteSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 6 步。
    static constexpr int order = 6;

    /// `const SimClock` 必须在读集里：枯竭年要用当年的年份，调度器只靠
    /// 读写集判冲突，不声明成读，本 system 就会与写钟的 ClockSystem
    /// 落进同一批并行跑。
    ///
    /// `const CaveHeavenId` 是洞天那一趟遍历真读的东西：洞天挂在哪条
    /// 脉上全靠它。`VeinStock` 与 `CaveHeavenState` 已在写集里——写集
    /// 会被并进读集，不必再列一遍。
    using ReadSet = std::tuple<const SimClock, const VeinId,
                                const CaveHeavenId>;
    using WriteSet = std::tuple<VeinStock, CaveHeavenState>;

    DepleteSystem(gamev2::ecs::EntityId clock,
                  std::vector<EraEvent>& events);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_;
    std::vector<EraEvent>& events_;
};

/// 重勘：公有化、清单生成、表决与归属变更。
///
/// 它排在最后：公有化与转让都要看当年的开采结果，而开采与越采都在它
/// 前面。`VeinOwner` 只有它写（设计 §3.13）。
class ResolveSystem final : public gamev2::ecs::System
{
public:
    /// 年内次序，设计 §3.13 的第 7 步。
    static constexpr int order = 7;

    /// 读集是它真读的东西：时钟、脉的编号与配额、脉的归属与物理状态、
    /// 派系的身份与账、提案记录。
    ///
    /// 归属、物理状态与提案记录同时出现在写集里：它们既读又写。写集
    /// 会被调度器并进读集，不必再列一遍——但列全更稳妥，也防住将来
    /// 调度器改了合并规则时的静默失序。
    using ReadSet = std::tuple<const SimClock, const VeinId,
                               const VeinOwner, const VeinStock,
                               const VeinRight, const FactionId,
                               const FactionBalance, const FactionIntent>;
    using WriteSet = std::tuple<VeinOwner, VeinStock, FactionIntent>;

    ResolveSystem(gamev2::ecs::EntityId clock, std::uint32_t epoch,
                  std::vector<EraEvent>& events);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_;
    /// 纪元。例行重勘的年份由它算。
    std::uint32_t epoch_;
    std::vector<EraEvent>& events_;
};

}  // namespace gamev2::faction

/// @file systems.hpp
/// @brief 众生层的私有头：年循环的七个 system。
///
/// 不放进公共头：这些类型是实现细节。调用方只需要 Life 与
/// interface.hpp；测试要读的东西必须从 ecs_world() 的视图上读到。
#pragma once

#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/system.hpp"
#include "gamev2/ecs/world.hpp"
#include "gamev2/life/constants.hpp"
#include "gamev2/life/economy.hpp"
#include "gamev2/life/interface.hpp"
#include "gamev2/life/soul.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <tuple>
#include <vector>

namespace gamev2::life {

/// 时钟。每年把年推进一格（设计 §1.2 步 1）。
class ClockSystem final : public gamev2::ecs::System
{
public:
    using ReadSet = std::tuple<>;
    using WriteSet = std::tuple<LifeClock>;

    explicit ClockSystem(gamev2::ecs::EntityId clock);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_;
};

/// 老化。年老一岁，寿尽者死（设计 §1.2 步 2、§2.4 寿元表）。
class AgingSystem final : public gamev2::ecs::System
{
public:
    /// `const LifeClock` 进读集不是数据依赖，是调度次序声明：不声明
    /// 它，Aging 会与写钟的 ClockSystem 落进同一批（照 faction
    /// AssessSystem 的先例）。
    ///
    /// 读 Cultivation：寿元上限由境界定（kRealmLifespan），组件里
    /// 刻意不存——两份上限会在突破那一刻静默分岔。
    using ReadSet = std::tuple<const LifeClock, const Mortal,
                               const Cultivation>;
    using WriteSet = std::tuple<Mortal>;

    AgingSystem(const std::vector<gamev2::ecs::EntityId>& souls,
                BondGraph& bonds);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    const std::vector<gamev2::ecs::EntityId>& souls_;
    BondGraph& bonds_;
};

/// 补员。宗门年收徒 + 散修十年一批（设计 §1.2 步 3、差异与补齐 §2）。
class BirthSystem final : public gamev2::ecs::System
{
public:
    /// const Mortal / const Cultivation 进读集不是数据依赖，是调度声明：
    /// 补员向 souls_ 追加新生，而 Aging/Cultivate 遍历同一向量——
    /// souls_ 不是组件，Scheduler 看不见这个冲突；借这两个组件让
    /// plan() 把补员拆进单独一批，否则同批并行时 push_back 与
    /// 遍历互踩（迭代器失效，调试构建直接 abort）。
    /// Purse 进写集不是装饰：新生儿经 cmd.add 落私囊，而 EconomySystem
    /// 也写私囊——Scheduler 看不见 cmd 的延迟写入。不声明这一个冲突，
    /// Economy 会与 Birth 落进同一批：Birth 追加 souls_、Economy 遍历
    /// souls_，迭代器失效在调试构建直接 abort（照刀 2 借读写集定批
    /// 的先例）。
    ///
    /// Site/Standing/Motive 同理：这三个组件也是新生儿经 cmd.add 落的，
    /// 而 Act 写 Site/Standing、Decide 写 Motive。声明了它们，Birth
    /// 才必然排在 Act 与 Decide 之前——否则新生实体带着半套组件被
    /// 决策与行为读到。
    using ReadSet = std::tuple<const LifeClock, const SoulId,
                               const Mortal, const Cultivation>;
    using WriteSet = std::tuple<Purse, Site, Standing, Motive>;

    BirthSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                std::vector<gamev2::ecs::EntityId>& souls,
                std::uint32_t& next_index,
                const EconomyLedger& ledger,
                std::uint32_t cell_count);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    std::vector<gamev2::ecs::EntityId>& souls_;
    std::uint32_t& next_index_;
    const EconomyLedger& ledger_;
    /// 世界总格数：落格掷运的取值域。
    std::uint32_t cell_count_ = 0;
};

/// 修炼。年资 +1，够门槛掷悟性，中则突破（设计 §1.2 步 4、§2.4）。
class CultivateSystem final : public gamev2::ecs::System
{
public:
    /// const Mortal 进读集是数据依赖也是调度声明：Aging 写 Mortal、
    /// 本 system 读它来判「寿尽者不突破」（终审 B1：同年寿尽又突破
    /// 会让 Economy 把他当活人结算，私囊随实体销毁在账外消失）。
    /// 声明后与 Aging 的写集冲突，落进 Aging 之后的批——次序成立。
    ///
    /// const Site 进读集是数据依赖：金丹的厚域门槛要读站位查
    /// 当年的厚域格表。
    using ReadSet = std::tuple<const LifeClock, const Cultivation,
                               const Aptitude, const Mortal, const Site>;
    using WriteSet = std::tuple<Cultivation>;

    CultivateSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                    const std::vector<gamev2::ecs::EntityId>& souls,
                    const std::vector<std::uint32_t>& thick_cells,
                    EconomyLedger& ledger,
                    const gamev2::world::World& world);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
    const std::vector<std::uint32_t>& thick_cells_;
    EconomyLedger& ledger_;
    const gamev2::world::World& world_;
};

/// 决策（设计 §1.2 步 5、§2.2 判据级联）。年年覆写 Motive——
/// 覆写即「年后清空」，玩家接管（刀 6）插在本 system 之前。
class DecideSystem final : public gamev2::ecs::System
{
public:
    /// const Cultivation/Purse 进读集是数据依赖也是调度声明：Purse 与
    /// Economy 的写集冲突（本 system 先注册、先执行）；Cultivation 与
    /// Cultivate 的写集冲突（落在其后）。Motive 在写集——与 Act 的
    /// 读集冲突，Act 因此必在其后。
    using ReadSet = std::tuple<const LifeClock, const SoulId,
                               const Mortal, const Cultivation,
                               const Aptitude, const Temper,
                               const Purse, const Office, const Site>;
    using WriteSet = std::tuple<Motive>;

    DecideSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
                 const std::vector<gamev2::ecs::EntityId>& souls,
                 const BondGraph& bonds,
                 const std::vector<LifeEvent>& events,
                 const std::vector<std::uint32_t>& thick_cells,
                 const std::vector<std::uint32_t>& unowned_veins,
                 const gamev2::world::World& world);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
    const BondGraph& bonds_;
    const std::vector<LifeEvent>& events_;
    const std::vector<std::uint32_t>& thick_cells_;
    const std::vector<std::uint32_t>& unowned_veins_;
    const gamev2::world::World& world_;
};

/// 行为（设计 §1.2 步 6）。执行 Motive：修炼/闭关/历练/经营/社交/
/// 盗采/夺位/寻仇，并在年末走政事段（晋升、换代、出走）。
class ActSystem final : public gamev2::ecs::System
{
public:
    /// 写集宽是刻意的：SoulId 的写者只有本 system（出走/被逐，设计
    /// §4.3 批准的迁移写入者）；Purse 与 Economy 冲突（Act 先注册）；
    /// Cultivation 与 Cultivate 冲突（历练加成在其后落账）。
    /// const Motive 进读集：与 Decide 的写集冲突，Act 必在其后。
    using ReadSet = std::tuple<const LifeClock, const Motive, const Mortal,
                               const SoulId, const Cultivation,
                               const Aptitude, const Temper, const Purse,
                               const Office, const Site>;
    using WriteSet = std::tuple<SoulId, Cultivation, Purse, Office,
                                Standing, Site, Temper>;

    ActSystem(std::uint64_t seed, gamev2::ecs::EntityId clock,
              const std::vector<gamev2::ecs::EntityId>& souls,
              BondGraph& bonds, std::vector<LifeEvent>& events,
              const std::vector<std::uint32_t>& thick_cells,
              EconomyLedger& ledger, LifeDiagnostics& diagnostics,
              const gamev2::world::World& world);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    std::uint64_t seed_;
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
    BondGraph& bonds_;
    std::vector<LifeEvent>& events_;
    const std::vector<std::uint32_t>& thick_cells_;
    EconomyLedger& ledger_;
    LifeDiagnostics& diagnostics_;
    const gamev2::world::World& world_;
};

/// 经济。铸币入金库、Office 份额分配、散修营生、烧毁、死者账消、
/// 守恒校验（设计 §1.2 步 7）。
class EconomySystem final : public gamev2::ecs::System
{
public:
    /// const Cultivation 进读集是调度声明：与 Birth 分批的是 Purse
    /// 写冲突本身、不靠这一项（理由见 BirthSystem::WriteSet）；它真正
    /// 隔开的是 Cultivate——Economy 逐年读 Cultivation 判寿元，声明后
    /// 与 Cultivate 的写集冲突、落独立批且排在其后，年循环次序
    /// Clock→Aging→Birth→Cultivate→Decide→Act→Economy 才成立。
    ///
    /// Office 进写集：断供计数（Office::starved）的唯一写入者在此。
    ///
    /// const Motive 进读集是数据依赖：散修营生与经营赏钱不叠加，靠
    /// 「当年有没有经营过」判定。声明后与 Decide 的写集冲突，落在
    /// Decide 之后——次序本已成立，此处只是把它写进访问集。
    using ReadSet = std::tuple<const LifeClock, const SoulId, const Mortal,
                               const Cultivation, const Motive>;
    using WriteSet = std::tuple<Purse, Office>;

    EconomySystem(gamev2::ecs::EntityId clock,
                  const std::vector<gamev2::ecs::EntityId>& souls,
                  EconomyLedger& ledger, LifeDiagnostics& diagnostics);

    void execute(gamev2::ecs::World& world,
                 gamev2::ecs::CommandBuffer& cmd) override;

private:
    gamev2::ecs::EntityId clock_{};
    const std::vector<gamev2::ecs::EntityId>& souls_;
    EconomyLedger& ledger_;
    LifeDiagnostics& diagnostics_;
};

}  // namespace gamev2::life

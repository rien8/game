/// @file faction.hpp
/// @brief 派系演化层：活的状态与设定身份。
///
/// 本层只读 worldgen 的终态与台账的 8300 快照，不回写它们的任何字段。
/// 它的全部状态住在自己的 ecs::World 里：六百年的推演是一个有状态的
/// 过程，不是一次性函数。
#pragma once

#include "gamev2/history/ledger.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>

namespace gamev2::faction {

using gamev2::history::Sect;

/// 越采能采到流速的几倍。
///
/// 越采是强行抽取：能采到比流速更多的量，代价是把这口井抽废。
/// 它是标定值：只影响枯竭的早晚，不影响格局的结构。
inline constexpr std::uint32_t kOvertakeFactor = 2;

/// 脉律重勘的间隔（年）。设定 §6.5：脉律每二百年重勘一次。
inline constexpr std::uint32_t kRedraftPeriod = 200;

/// 两次重勘的最小间隔（年）。
///
/// 提案与表决是大事，不能年年开。50 年是「一代人的功夫」这个量级的
/// 程序性间隔，不是经济参数。
inline constexpr std::uint32_t kEarlyRedraftInterval = 50;

/// 过半数门槛。写成整数对是为了免掉除法。
inline constexpr std::uint32_t kVoteThresholdNum = 1;
inline constexpr std::uint32_t kVoteThresholdDen = 2;

/// 派系的角色。决定用度规则与开采规则。
enum class Role : std::uint8_t
{
    /// 正派宗门：用度 = 8300 年实际可采量，采到配额上限就停。
    sect,
    /// 魔修：用度无上限，采尽为止（设定 §8.2「资源当取尽」）。
    demonic,
    /// 散修与无主：用度无上限，无约束开采。
    free_cultivator,
    /// 无地（天机阁）：用度为零，不开采，不参与争夺。
    landless,
};

/// 派系的立场。决定它参不参与争夺，以及能不能被夺。
enum class Stance : std::uint8_t
{
    /// 参与争夺：可以提案，也可以被夺。
    contesting,
    /// 中立：不提案，且不可被夺。
    neutral,
    /// 孤立：不提案，但可以被夺。
    isolationist,
};

/// 派系的设定身份。不可变，不进组件。
///
/// 默认值对齐身份表里 unowned 那一行：默认构造出来的身份不能与设定
/// 矛盾（默认构造出「无主的正派宗门」是自相矛盾的）。
struct FactionIdentity
{
    Sect sect = Sect::unowned;
    Role role = Role::free_cultivator;
    Stance stance = Stance::neutral;
    /// 执掌盟约者有提案权。
    bool may_propose = false;
};

/// 派系的设定身份表：七宗 + 无主。
///
/// 这张表是**设定给定的常量**，不是调参项。角色与立场是定性的，
/// 数值差异一律来自状态——没有一个派系拥有自己的「贪婪度」参数。
[[nodiscard]] const FactionIdentity& identity(Sect sect);

/// 派系的身份。
struct FactionId
{
    Sect sect = Sect::unowned;
};

/// 派系的年度账。
struct FactionBalance
{
    /// 用度：门人每年要采出的量。制度量，固定不变。
    ///
    /// 无上限用度的派系（魔修、散修）这一项恒为 0，改由
    /// `unbounded_demand` 表达：用度没有上限，就没有可比较的数。
    std::uint32_t demand = 0;
    /// 用度无上限。
    bool unbounded_demand = false;
    /// 当年合法开采到的量。
    std::uint32_t supply = 0;
    /// 缺口 = demand − supply（饱和，不足为 0）。
    /// 用度无上限的派系（魔修、散修）这一项恒为 0。
    std::uint32_t deficit = 0;
    /// 累计开采（含越采）。
    std::uint32_t extracted = 0;
    /// 累计越采。
    std::uint32_t overmined = 0;
    /// 未登记脉上的累计开采（合法 + 越采）。
    ///
    /// 口径与台账的 `Ledger::unregistered_extracted` **同名同义**：同一件事
    /// 必须同名同义——这份报告的用途是让人裁决哪个格局算数，一个同名
    /// 不同义的列会让人拿它跟台账对，对不上就判定模型坏了。
    ///
    /// 「未登记」说的是**脉律的册子**，不是所有权：它数的是落在不登记
    /// 在册的脉上的开采，按**开采当时的持有者**记在那家的账上。所以散
    /// 修在**登记在册**的脉上的私采不计入这一列——它记在「无主」这个
    /// 派系的 `extracted` 名下；而散修若在未登记的脉上开采，那一笔照
    /// 样计在这里（数的是脉，不是人）。
    std::uint32_t unregistered = 0;
    /// 表决权重：8300 年名下脉的总量配额之和。
    std::uint32_t vote_weight = 0;
};

/// 派系的提案权。
struct FactionIntent
{
    /// 执掌盟约者（太玄宗）可以提前发起脉律重勘。
    bool may_propose = false;
    /// 已经提出重勘的年份，0 表示还没提。
    std::uint32_t proposed_year = 0;
};

/// 脉的身份。生成后不变。
struct VeinId
{
    std::uint32_t id = 0;
};

/// 脉的固有属性。生成后不变。
struct VeinProfile
{
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;
    gamev2::world::Element element = gamev2::world::Element::none;
    /// 阴脉：采出即衰（设定 §8.1）。
    bool yin = false;
    /// 挂在这条脉上的洞天。
    std::uint32_t cave_heaven = gamev2::world::kNoCaveHeaven;
};

/// 脉的制度性权利。《脉律》核定的结果，本层不改。
struct VeinRight
{
    /// 8300 年核定的总量配额。
    std::uint32_t quota_total = 0;
    /// 8300 年核定的安全流速。
    std::uint32_t quota_rate = 0;
    /// 该脉的配额是否登记在册（九幽宫那条不登记）。
    bool registered = true;
    /// 8300 年的持有者。建实体时写下、此后不变：与 VeinOwner::owner
    /// （当代归属，转让后会变）分开，报告才能说出「这条脉换没换过主人」。
    Sect initial_owner = Sect::unowned;
};

/// 脉的物理状态。唯一的「真账」。
struct VeinStock
{
    /// 当代余额。
    std::uint32_t stock = 0;
    /// 累计开采（含越采）。
    std::uint32_t extracted = 0;
    /// 累计合法开采（不含越采）。与台账的对照只能比这一项。
    std::uint32_t legal_extracted = 0;
    /// 当年开采量：ExtractSystem 赋值，OvertakeSystem 累加。
    ///
    /// 重勘的试算要拿「这条脉今年被抽了多少」与「转手之后会被抽多少」
    /// 比，所以当年开采量必须留在数据里：事后重算算不出原值，
    /// 余额已经扣过了。
    std::uint32_t taken = 0;
    /// 当年的有效流速。ExtractSystem 写，OvertakeSystem 读。
    ///
    /// 衰减率只在年初算一次，年内不变（设计 §3.13）：越采复用这同一个
    /// 数，自己重算会拿到开采之后的存量，分摊的分母就与缺口的分母
    /// 不同源了。
    std::uint64_t eff = 0;
    /// 枯竭年，0 表示未枯竭。
    std::uint32_t depleted_year = 0;
    /// 公有化的年份，0 表示未公有化。
    ///
    /// 它是**幂等凭据**，不是一张账：ResolveSystem 靠它判断「这条脉公有化
    /// 过了没有」，否则每年会重复记一笔公有化事件。公有化的年份本身在
    /// 事件表里，报告不读这个字段。
    std::uint32_t public_year = 0;
    /// 这条脉的越采是否已经被发现过。一条脉至多报一次。
    bool overmine_reported = false;
};

/// 脉的归属。只有 ResolveSystem 写它。
struct VeinOwner
{
    Sect owner = Sect::unowned;
};

/// 年度开采量的小数余数。跨年保留，所以它是状态。
///
/// 挂成独立组件而不是塞进 VeinStock：余数是「算术的余数」，
/// 不是「账上的量」，混在一起会让守恒恒等式的代码读起来含混。
struct VeinRemainder
{
    std::uint64_t value = 0;
};

/// 阴脉的衰减率（定点，一 = 2^16 分之一）。
///
/// 只有阴脉采出即衰（设定 §8.1），其余脉恒为一。衰减率的分子是
/// 剩余存量而不是总量配额：写成总量会让「采了一半」的脉衰减率
/// 仍为满值，衰减永远不会发生。
[[nodiscard]] std::uint32_t decay_of(const VeinProfile& profile,
                                     const VeinRight& right,
                                     const VeinStock& stock);

/// 当年的有效流速（定点量的整数部分），小数余数结转进 remainder。
///
/// 余数的标度是 255 × 2^32（比台账多一层衰减因子），与台账的
/// QuotaRemainder 不能混用；整数结果必须与台账的 take() 相同，
/// 那是等价性测试要守住的。
[[nodiscard]] std::uint64_t take_effective(const VeinProfile& profile,
                                           const VeinRight& right,
                                           const VeinStock& stock,
                                           std::uint32_t level,
                                           std::uint64_t conversion,
                                           VeinRemainder& remainder);

/// 有效流速的 2^16 定点值。
///
/// 不含余数结转——余数是**开采量**的机制（把每年的小数零头带到下一年），
/// 不是流速本身。需求与供给要的是「这条脉今年能给出多少」，按实数算。
///
/// 逐脉取整再相加是错的：8300 年每脉的有效流速只有零点几个单位，逐脉
/// 取整会把每宗两到四条脉的小数全扔掉，用度系统性低估两到四倍，某些
/// 派系甚至归零——**用度归零的派系永远不会有缺口，也就不会行动**。
[[nodiscard]] std::uint64_t eff_fixed(const VeinProfile& profile,
                                      const VeinRight& right,
                                      const VeinStock& stock,
                                      std::uint32_t level,
                                      std::uint64_t conversion);

/// 把 2^16 定点值四舍五入成整数。
///
/// 取整而不是截断：截断会让某些派系的用度归零，而用度归零的派系永远
/// 不会有缺口、也就永不行动（设计 §3.10 承诺的「寒江的结构性赤字从状态
/// 里长出来」就落空了）。取整让它留在 1，赤字照常从潮位与阴脉衰减里
/// 长出来。
///
/// 两处取整（用度与供给）必须用同一个规则，否则纪元那年的供给与用度
/// 会差一格，缺口的零值不变式就破了。
[[nodiscard]] constexpr std::uint32_t round_fixed(std::uint64_t fixed) noexcept
{
    return static_cast<std::uint32_t>((fixed + (1u << 15)) / (1u << 16));
}

/// 缺口：用度超出供给的部分，不足为 0。
[[nodiscard]] std::uint32_t deficit_of(std::uint32_t demand,
                                       std::uint32_t supply);

/// 洞天。
struct CaveHeavenId
{
    std::uint32_t cell = 0;
    std::uint32_t vein = 0;
};

struct CaveHeavenState
{
    Sect owner = Sect::unowned;
    bool alive = true;
};

/// 时钟。
struct SimClock
{
    std::uint32_t year = 0;
    std::uint32_t level = 0;
};

/// 推演的诊断量。退化不是错误，由这里报出。
struct FactionDiagnostics
{
    /// 台账不可用（当代早于纪元、台账为空）。
    std::uint32_t no_baseline = 0;
    /// 换算系数整除成 0：推演不出任何开采量。
    std::uint32_t conversion_collapsed = 0;
    /// 跨度超限：按「没有推演」处理。
    std::uint32_t span_too_large = 0;
    /// 提案被驳回的次数。
    std::uint32_t redraft_rejected = 0;
    /// 提案空转的次数：清单为空，没有东西可投。
    std::uint32_t redraft_empty = 0;
    /// 提案通过的次数。
    std::uint32_t redraft_passed = 0;
    /// 公有化后仍挂在原持有者名下的脉数（内部一致性错误）。
    std::uint32_t public_release_mismatch = 0;
    /// 守恒对账不通过的脉数。
    std::uint32_t conservation_broken = 0;
};

}  // namespace gamev2::faction

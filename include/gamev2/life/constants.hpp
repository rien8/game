/// @file constants.hpp
/// @brief 众生层的世界常数。设定给定的逐字采用，标定值注明锚点。
#pragma once

#include "gamev2/life/soul.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gamev2::life {

/// 各宗与散修的开局人口，按 history::Sect 的序（含 unowned 末位）。
///
/// 设定 §8 的量级：七宗各百余、散修数百、魔修数十。标定值——换这组
/// 数只平移绝对规模，不改金字塔结构（设计 §6）。
inline constexpr std::array<std::uint32_t, 8> kPopulation{{
    140,  // 太玄宗
    120,  // 丹霞谷
     90,  // 寒江剑府
    100,  // 万兽山
     90,  // 青囊谷
     60,  // 天机阁
     70,  // 九幽宫
    300,  // 无主（散修）
}};

/// 九境寿元（设定 §5.1，逐字）。大乘「近乎无穷」：给一个远大于任何
/// 玩法跨度的数，让「死于寿元」在大乘身上不发生。
inline constexpr std::array<std::uint32_t, 9> kRealmLifespan{{
    120, 150, 300, 600, 1200, 2400, 5000, 10000, 1000000,
}};

/// 寿元已尽。Aging 据此销毁、Cultivate 据此不再让他突破、Economy
/// 据此跳过分配——同一谓词三处用：分叉会让「谁被跳过」与「谁被销毁」
/// 判在不同人身上，账就对不上（终审 B1：当年寿尽又突破的人会被
/// Economy 当活人结算，私囊随实体销毁在账外消失）。
[[nodiscard]] constexpr bool lifespan_reached(
    const Mortal& mortal, const Cultivation& cult) noexcept
{
    return mortal.age
           >= kRealmLifespan[static_cast<std::size_t>(cult.realm)];
}

/// 最小入册年龄：低于此岁不入名册（弟子是少年，不是婴儿）。
inline constexpr std::uint32_t kMinAge = 16;

/// 长老数 = max(kMinElders, N / kElderDivisor)。制度性常量（设计 §6）。
inline constexpr std::uint32_t kMinElders = 3;
inline constexpr std::uint32_t kElderDivisor = 20;

/// 内门占比 = kInnerNum / kInnerDen，其余是外门。
inline constexpr std::uint32_t kInnerNum = 2;
inline constexpr std::uint32_t kInnerDen = 5;

/// 宗门年收徒 = max(1, 在册数 / kSectRecruitDivisor)。
///
/// 标定值（刀 3 重标，2026-10-27；刀 4 断供停收后重钉）。探针实测
/// （128²/seed13，跑 200 年）：除数 50 是正反馈——100 年 4254 人、
/// 200 年 26002 人，末 50 年漂移 154%；100 降到 100 年 1979 人但
/// 漂移仍 42%；200 与 400/800 读数逐位相同（100 年 1923 人、
/// 200 年 2850 人、末 50 年漂移 19.4%）。取 200：再大无效——
/// `max(1,·)` 的下限已绑定（每宗恒收一徒），除数从此不再是旋钮。
/// 刀 2 记的 3554 是旧探针读数（与当前代码不符），已作废。
///
/// 「稳态（漂移 < 10%）」这一锚未达成。残余漂移来自金字塔上移——
/// 突破把寿元短的换成寿元长的，平均寿命随人口一起涨。真正的稳态
/// 要靠收徒受供给约束（断供则不收徒）与断供出走，两者都是刀 4 的
/// 行为面：刀 4 落地后本注释的读数须按新探针重钉。
inline constexpr std::uint32_t kSectRecruitDivisor = 200;

/// 散修每十年多一批（设定 §8.3 逐字）。每批人数是标定值（刀 4 重标）：
/// 锚点是与七宗年收徒总量同量级，让散修圈不被宗门吸空。刀 3 重标
/// 除数后这条锚仍成立：除数 200 下各宗在册 200 年内未达 400 人，
/// 七宗全程每宗恒收一徒（7 人/年），散修批 3 人/年，仍属同量级；
/// 未被选用的除数 50 则是开局 10 人/年、随人口升档迅速爆炸。
/// 但参照点已随除数移动——散修批的绝对量归刀 4 与收徒规则一起定。
inline constexpr std::uint32_t kFreeBatchYears = 10;
inline constexpr std::uint32_t kFreeBatch = 30;

/// 人口金字塔（万分比，逐境求和 = 10000）。
///
/// 七宗：引气 45%、炼气 38%、筑基 12%、金丹 4%、元婴 1%。
/// 反推自设定 §6.1 的消耗金字塔——谷底期的供给撑不起高境，
/// 元婴一人一年吃掉天下年产八分之一。
inline constexpr std::array<std::uint32_t, 9> kSectPyramid{{
    4500, 3800, 1200, 400, 100, 0, 0, 0, 0,
}};

/// 散修：更扁（设定 §6.4 的三十三年一枚下品），筑基以上只有 10%。
inline constexpr std::array<std::uint32_t, 9> kFreePyramid{{
    5500, 3500, 900, 100, 0, 0, 0, 0, 0,
}};

/// 境内年资门槛（年）：攒够这些年资才开始掷突破。
///
/// 元婴的一百年闭关逐字来自设定 §5.1；其余是标定值（刀 4 重标——
/// 资源门槛并入后整套要重估）。锚点：百年窗口内众生主要停在
/// 筑基—金丹；化神及以上在 100 年内结构性不可能（四境门槛之和
/// 120 > 100）。大乘的 0 是哨兵：大乘不再进。
inline constexpr std::array<std::uint32_t, 9> kRealmYears{{
    20, 20, 30, 50, 100, 200, 300, 500, 0,
}};

/// 突破资源门槛（下品），按「进入该境」的序号索引（差异 13）。
///
/// 设定 §5.1 的门槛列逐字换算：进炼气 1 枚下品；进筑基 2500
/// （结基灵物 20~30 中品取中值，1 中品 = 100 下品）；进金丹 33333
/// （「结丹材料」设定没给数，取金丹一年烧毁同额）；进元婴 333333
/// （「高阶灵脉」取十年烧毁额）；化神以上 ×10 外推——测试跨度内
/// 结构性不可达。下标 0（进引气）是 0：出生即引气，不付费。
inline constexpr std::array<std::uint64_t, 9> kBreakthroughCost{{
    0, 1, 2500, 33333, 333333, 3333330, 33333300, 333333000, 3333330000,
}};

/// 突破成功率（千分制）：base + (悟性 − 5) × per，钳制在 [min, max]。
///
/// 标定值（刀 4 重标，2026-10-29）。三个锚点逐条对上：中位悟性
/// （5 档）30%、最聪颖（9 档）500‰ 恰好顶格、最愚钝（1 档）算出
/// 100‰ 被钳到 20%。旧读数「2%/10%/18%」（base 100 / per 20）
/// 钳制区间从不生效，是刀 2 的口径，已作废。
inline constexpr std::uint32_t kBreakthroughBasePermille = 300;
inline constexpr std::uint32_t kBreakthroughPerAptitudePermille = 50;
inline constexpr std::uint32_t kBreakthroughMinPermille = 200;
inline constexpr std::uint32_t kBreakthroughMaxPermille = 500;

/// 潮位因子的下限（千分制）。潮位因子 = 潮位字节 × 1000 / 255，
/// 再与本下限取大——谷底期突破近于停摆，这是招牌④的人口杠杆。
inline constexpr std::uint32_t kTideBreakthroughFloorPermille = 50;

/// 突破成功率：悟性定基准、潮位定折损（千分制）。
///
/// 潮位因子把「灵潮多浓」翻译成「多大概率突破」：满潮（字节 255）
/// 因子 1000、基准原样；谷底因子触到下限 50，基准只剩 5%。
[[nodiscard]] constexpr std::uint32_t breakthrough_chance(
    std::uint8_t aptitude, std::uint8_t tide_level) noexcept
{
    std::int32_t base = static_cast<std::int32_t>(kBreakthroughBasePermille);
    base += (static_cast<std::int32_t>(aptitude) - 5)
            * static_cast<std::int32_t>(kBreakthroughPerAptitudePermille);
    base = std::clamp(base, static_cast<std::int32_t>(kBreakthroughMinPermille),
                      static_cast<std::int32_t>(kBreakthroughMaxPermille));
    const std::uint32_t factor =
        std::max(kTideBreakthroughFloorPermille,
                 static_cast<std::uint32_t>(tide_level) * 1000u / 255u);
    return static_cast<std::uint32_t>(
        static_cast<std::int64_t>(base) * factor / 1000);
}

/// 筑基寻厚域的提前量（年）。筑基者年资快满时提前去厚域候着
/// （判据 0.5）——厚域格的争用因此发生在突破之前而不是当年。
inline constexpr std::uint32_t kThickSeekLead = 10;

/// 盗采年产量（下品/年）。盗采的是无主脉（含枯脉残余），不入宗门
/// 金库、直接进私囊，同时在 EconomyLedger 记私铸（差异 7）。
inline constexpr std::uint32_t kPoachYieldStone = 800;

/// 盗采被擒率（千分制/年）。派系层的发现判定是刀 5 的接口，本刀的
/// 被擒率是标定值。
inline constexpr std::uint32_t kPoachCaughtPermille = 30;

/// 历练遇险率（千分制/年）。中者横死——荒野不是没有代价的练功场。
inline constexpr std::uint32_t kRoamHazardPermille = 5;

/// 闭关被寻衅率（千分制/年）。只对有仇家的闭关者有效。
inline constexpr std::uint32_t kSecludeHazardPermille = 10;

/// 社交机会率（千分制/年）。
inline constexpr std::uint32_t kSocialChancePermille = 200;

/// 立威结怨率（千分制）。立威压人时对方是否记恨。
inline constexpr std::uint32_t kGrudgePermille = 300;

/// 夺位机会率（千分制/年）。只对符合资格的 elder 生效。
inline constexpr std::uint32_t kContestChancePermille = 50;

/// 断供出走门限（连续断供年数）。制度性逃生门：宗门养不起你，
/// 你自己走。
inline constexpr std::uint32_t kStarveExitYears = 2;

/// 战斗一步判定的战力摆幅（千分制/点战力差）。
inline constexpr std::uint32_t kCombatSwingPermille = 60;
inline constexpr std::uint32_t kCombatMinPermille = 50;
inline constexpr std::uint32_t kCombatMaxPermille = 950;

/// 法器档位门槛（下品）。法器不是实体，只是战力的一个档位（设计
/// §9.3）：档 = 不超过私囊的最高档（0..3）。
inline constexpr std::array<std::uint64_t, 4> kArtifactGradeStone{{
    0, 100, 10000, 100000,
}};

/// 姓名表。设定没给姓名表——这是内容常数，不是数值旋钮；同名不冲突，
/// 身位的唯一标识是 SoulId::index。窄字面量就是 UTF-8 字节（本仓库
/// 源文件即 UTF-8），拼进 std::string 没有编码链问题。
inline constexpr std::array<std::string_view, 16> kSurnames{{
    "陈", "林", "沈", "陆", "苏", "叶", "顾", "谢",
    "裴", "宋", "白", "孟", "温", "韩", "江", "燕",
}};

inline constexpr std::array<std::string_view, 32> kGivenNames{{
    "云", "松", "竹", "兰", "石", "溪", "岩", "岚",
    "霜", "雪", "风", "雷", "山", "泽", "川", "渊",
    "霖", "尘", "霄", "鹤", "鸣", "珂", "瑜", "琰",
    "璟", "珩", "玄", "素", "微", "澜", "怀", "默",
}};

/// 子流登记表。同一子流号只能被一种掷运用途占用，新增用途必须
/// 同改这张表（照 world::kStreamIds 的纪律）。
///
/// 派生于 world::Stream::life 之下再分一层：与 Cultivate 既有掷运
/// （无 roll 层）分属不同的键空间——动了它，刀 2 的探针读数全体漂移。
enum class Roll : std::uint32_t
{
    decide = 0,          ///< 判据 4 社交机会、夺位机会、社交对象
    site_init = 1,       ///< 开局/新生落格
    poach_find = 2,      ///< 盗采被擒
    combat = 3,          ///< 一步战斗
    roam = 4,            ///< 历练遇险与落点
    succession = 5,      ///< 推举噪声
    contest = 6,         ///< 夺位成败
    seclude_hazard = 7,  ///< 闭关被寻衅
    bonds_init = 8,      ///< 初始师徒/亲缘布网
    /// 立威是否结怨（刀 4 新增的第 10 条子流）。
    ///
    /// 计划给的表里没有它，而它不能借用 Roll::decide：decide 的第一个
    /// 数是社交机会率（200‰），立威结怨率是 300‰——借同一条流的同一个
    /// 数，凡立威者必结怨，概率就退化成了恒真。新增一条子流是登记表
    /// 纪律允许的（照 kStreamIds 的做法：新增必须同改登记表）。
    grudge = 9,
};

/// 已占用的子流号。清单与枚举必须同步维护。
inline constexpr std::array<std::uint32_t, 10> kRollIds{
    static_cast<std::uint32_t>(Roll::decide),
    static_cast<std::uint32_t>(Roll::site_init),
    static_cast<std::uint32_t>(Roll::poach_find),
    static_cast<std::uint32_t>(Roll::combat),
    static_cast<std::uint32_t>(Roll::roam),
    static_cast<std::uint32_t>(Roll::succession),
    static_cast<std::uint32_t>(Roll::contest),
    static_cast<std::uint32_t>(Roll::seclude_hazard),
    static_cast<std::uint32_t>(Roll::bonds_init),
    static_cast<std::uint32_t>(Roll::grudge),
};

/// 子流号两两不同。重复即意味着两种用途共用一条随机流。
consteval bool roll_ids_are_distinct() noexcept
{
    for (std::size_t i = 0; i < kRollIds.size(); ++i)
    {
        for (std::size_t j = i + 1; j < kRollIds.size(); ++j)
        {
            if (kRollIds[i] == kRollIds[j])
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(roll_ids_are_distinct());

/// 无状态派生：stage_seed 链 (seed, Stream::life, roll, year, soul_index)。
///
/// 键用 SoulId::index 而非 EntityId 槽号：槽位会被池回收，身份键
/// 才保证「同 seed 重跑逐位相同」。
///
/// 落在这里而不是私有头 systems.hpp：combat.hpp 是公共头，不能依赖
/// 私有头——两者共用同一条派生链，定义只写一份。
[[nodiscard]] inline gamev2::world::Pcg32 life_roll(std::uint64_t seed,
                                                     Roll roll,
                                                     std::uint32_t year,
                                                     std::uint32_t soul_index)
{
    const auto stream =
        static_cast<std::uint32_t>(gamev2::world::Stream::life);
    const std::uint64_t s1 = gamev2::world::stage_seed(seed, stream);
    const std::uint64_t s2 =
        gamev2::world::stage_seed(s1, static_cast<std::uint32_t>(roll));
    const std::uint64_t s3 = gamev2::world::stage_seed(s2, year);
    const std::uint64_t s4 = gamev2::world::stage_seed(s3, soul_index);
    return gamev2::world::Pcg32(s4);
}

}  // namespace gamev2::life

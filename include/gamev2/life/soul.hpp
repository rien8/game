/// @file soul.hpp
/// @brief 众生层：个体的组件与身份枚举。
#pragma once

#include "gamev2/history/ledger.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gamev2::life {

/// 九境（设定 §5.1）。名字用拼音，与 history::Sect 的命名一致。
enum class Realm : std::uint8_t
{
    yinqi,     ///< 引气，寿 120
    lianqi,    ///< 炼气，寿 150
    zhuji,     ///< 筑基，寿 300
    jindan,    ///< 金丹，寿 600
    yuanying,  ///< 元婴，寿 1200
    huashen,   ///< 化神，寿 2400
    lianxu,    ///< 炼虚，寿 5000
    heti,      ///< 合体，寿 10000
    dacheng,   ///< 大乘，近乎无穷（给一个远超任何玩法跨度的数）
    count,     ///< 哨兵：境数，不是境界
};

/// 宗门职位（设计 §2.3）。散修与未任职者是 none。
enum class OfficeKind : std::uint8_t
{
    sect_master, ///< 宗主
    elder,       ///< 长老
    inner,       ///< 内门弟子
    outer,       ///< 外门弟子
    none,        ///< 散修或未任职
    count,       ///< 哨兵：职位数，不是职位
};

/// 性别（设计 §2.1 的 SoulId 字段之一）。出生即定，此后只读。
enum class Sex : std::uint8_t
{
    male,
    female,
};

/// 身份。建实体时写一次，此后只读（写入者表见实施计划 Global
/// Constraints；后续刀的迁移事件是经批准的例外）。
struct SoulId
{
    std::uint32_t index = 0;  ///< 创建次序：身位的稳定标识
    std::string name;
    Sex sex = Sex::male;
    history::Sect sect = history::Sect::unowned;
};

/// 境界与境内年资。
struct Cultivation
{
    Realm realm = Realm::yinqi;
    /// 境内年资（刀 1 注释预告的进度轴）。CultivateSystem 逐年写。
    std::uint32_t progress = 0;
    /// 百年闭关锁：year < 它则本年锁死，不掷突破（设定 §5.1 的元婴
    /// 百年闭关）。CultivateSystem 写，DecideSystem 读（判据 0）。
    std::uint32_t seclude_until = 0;
};

/// 能力四轴（1..9 档）。天生的，出生后不变。
struct Aptitude
{
    std::uint8_t comprehension = 1;  ///< 悟性
    std::uint8_t sense = 1;          ///< 神识
    std::uint8_t body = 1;           ///< 体魄
    std::uint8_t fortitude = 1;      ///< 韧性
};

/// 性格五轴（1..5 档，3 为中值）。
///
/// 档位来自种子、行为来自规则（设计 §2.1 的数值纪律）：轴只做岔路
/// 选择器，不做连续效用参数。设计文本里的「悖逆轴高」就是 order 轴低。
struct Temper
{
    std::uint8_t greed = 3;  ///< 1 节制 … 5 贪婪
    std::uint8_t valor = 3;  ///< 1 怯懦 … 5 勇烈
    std::uint8_t order = 3;  ///< 1 悖逆 … 5 守序
    std::uint8_t mercy = 3;  ///< 1 酷烈 … 5 仁厚
    std::uint8_t vigor = 3;  ///< 1 慵懒 … 5 进取
    /// 上次大事推移的年份（0 = 从未）。十年缓变纪律的锚（差异 18）。
    std::uint32_t drift_year = 0;
};

/// 年岁。寿元上限不存——它由境界决定（kRealmLifespan），存两份会
/// 在突破的那一刻静默分岔。
struct Mortal
{
    std::uint32_t birth_year = 0;
    std::uint32_t age = 0;
};

/// 职位。在位年数与断供计数随刀 4 加入。
struct Office
{
    OfficeKind kind = OfficeKind::none;
    std::uint32_t years = 0;    ///< 在位年数（政事段写）
    std::uint32_t starved = 0;  ///< 连续断供年数（EconomySystem 写）
};

/// 声望。七宗各一维 + 散修圈一维，外加罪名计数（设计 §2.1）。
struct Standing
{
    /// 按 history::Sect 序；第 8 位（unowned）是散修圈。取值钳在
    /// [-5, 5]。
    std::int8_t by_sect[8] = {};
    std::uint8_t crimes = 0;  ///< 罪名计数
};

/// 站位。格号是 row-major 线性格号，与 world 的 Grid 寻址一致。
struct Site
{
    std::uint32_t cell = 0;
};

/// 当年意图（设计 §2.1）。决策 system 写、行为 system 读；玩家的
/// 接管点即此。
enum class MotiveKind : std::uint8_t
{
    none,
    cultivate,  ///< 修炼（默认）
    seclude,    ///< 闭关
    roam,       ///< 历练
    operate,    ///< 经营（营生）
    poach,      ///< 盗采
    social,     ///< 社交
    grudge,     ///< 记仇
    avenge,     ///< 寻仇
    contest,    ///< 夺位
};

/// 一年的意图。arg 的含义随 kind 变，语义写在各分支上。
struct Motive
{
    MotiveKind kind = MotiveKind::none;
    std::uint32_t arg = 0;  ///< 格号 / 无主脉号 / 对象 SoulId::index
};

/// 关系边的种类。边不是组件（差异 2）：它是「两个人之间的账」。
enum class BondKind : std::uint8_t
{
    master,   ///< 师徒
    kin,      ///< 亲缘
    partner,  ///< 道侣（本刀只建结构不产生边，刀 6 用）
    favor,    ///< 恩义
    feud,     ///< 仇怨
};

/// 一条关系边。两端以 SoulId::index 标识，恒取较小者在 a。
struct BondEdge
{
    BondKind kind = BondKind::favor;
    std::uint32_t a = 0;        ///< SoulId::index
    std::uint32_t b = 0;        ///< SoulId::index
    std::uint8_t strength = 1;  ///< 1..5
    std::uint32_t year = 0;     ///< 结缘年
};

/// 关系边集。不是组件（差异 2）：与 EconomyLedger 同一先例——金库
/// 也不是组件。由 Life 持有。
struct BondGraph
{
    std::vector<BondEdge> edges;

    /// 加边或加劲：已有同 kind 的边则强度 +1（钳 5），否则新建。
    /// 已有边的结缘年不改——那是「缘起之年」，不是最近一次来往之年。
    BondEdge& tie(std::uint32_t a, std::uint32_t b, BondKind kind,
                  std::uint32_t year)
    {
        const auto lo = a < b ? a : b;
        const auto hi = a < b ? b : a;
        for (auto& e : edges)
        {
            if (e.kind == kind && e.a == lo && e.b == hi)
            {
                if (e.strength < 5u)
                {
                    ++e.strength;
                }
                return e;
            }
        }
        edges.push_back(BondEdge{kind, lo, hi, 1u, year});
        return edges.back();
    }

    /// 找边；无则 nullptr。
    [[nodiscard]] const BondEdge* find(std::uint32_t a, std::uint32_t b,
                                       BondKind kind) const
    {
        const auto lo = a < b ? a : b;
        const auto hi = a < b ? b : a;
        for (const auto& e : edges)
        {
            if (e.kind == kind && e.a == lo && e.b == hi)
            {
                return &e;
            }
        }
        return nullptr;
    }

    /// 死者清边：抹掉任一端为 index 的全部边，返回抹除数。
    std::size_t erase_soul(std::uint32_t index)
    {
        const auto before = edges.size();
        std::erase_if(edges, [index](const BondEdge& e)
                                { return e.a == index || e.b == index; });
        return before - edges.size();
    }

    /// 与 index 有 kind 边的活人序号集（不含 index 自身）。
    [[nodiscard]] std::vector<std::uint32_t> neighbors(
        std::uint32_t index, BondKind kind) const
    {
        std::vector<std::uint32_t> out;
        for (const auto& e : edges)
        {
            if (e.kind != kind)
            {
                continue;
            }
            if (e.a == index && e.b != index)
            {
                out.push_back(e.b);
            }
            else if (e.b == index && e.a != index)
            {
                out.push_back(e.a);
            }
        }
        return out;
    }
};

/// 灵石账（设计 §2.1）。stone 按品级折算成统一记账单位（下品），
/// silver 是凡人货币——两本账不混：守恒恒等式只数 stone。
struct Purse
{
    std::uint64_t stone = 0;   ///< 下品折算
    std::uint32_t silver = 0;  ///< 两（设定 §6.4：一枚下品折两千两）
};

/// 年时钟。与 faction::SimClock 无关（那个还带潮位），故不同名。
struct LifeClock
{
    std::uint32_t year = 0;
};

}  // namespace gamev2::life

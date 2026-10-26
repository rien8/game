/// @file soul.hpp
/// @brief 众生层：个体的组件与身份枚举。
#pragma once

#include "gamev2/history/ledger.hpp"

#include <cstdint>
#include <string>

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

/// 身份。建实体时写一次，此后只读（写入者表见实施计划 Global
/// Constraints；后续刀的迁移事件是经批准的例外）。
struct SoulId
{
    std::uint32_t index = 0;  ///< 创建次序：身位的稳定标识
    std::string name;
    history::Sect sect = history::Sect::unowned;
};

/// 境界与境内年资。
struct Cultivation
{
    Realm realm = Realm::yinqi;
    /// 境内年资（刀 1 注释预告的进度轴）。CultivateSystem 逐年写。
    std::uint32_t progress = 0;
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
};

/// 年岁。寿元上限不存——它由境界决定（kRealmLifespan），存两份会
/// 在突破的那一刻静默分岔。
struct Mortal
{
    std::uint32_t birth_year = 0;
    std::uint32_t age = 0;
};

/// 职位。在位年数与上位途径随刀 4 / 刀 6 加入。
struct Office
{
    OfficeKind kind = OfficeKind::none;
};

/// 年时钟。与 faction::SimClock 无关（那个还带潮位），故不同名。
struct LifeClock
{
    std::uint32_t year = 0;
};

}  // namespace gamev2::life

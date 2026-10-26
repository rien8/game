/// @file constants.hpp
/// @brief 众生层的世界常数。设定给定的逐字采用，标定值注明锚点。
#pragma once

#include "gamev2/life/soul.hpp"

#include <array>
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

/// 最小入册年龄：低于此岁不入名册（弟子是少年，不是婴儿）。
inline constexpr std::uint32_t kMinAge = 16;

/// 长老数 = max(kMinElders, N / kElderDivisor)。制度性常量（设计 §6）。
inline constexpr std::uint32_t kMinElders = 3;
inline constexpr std::uint32_t kElderDivisor = 20;

/// 内门占比 = kInnerNum / kInnerDen，其余是外门。
inline constexpr std::uint32_t kInnerNum = 2;
inline constexpr std::uint32_t kInnerDen = 5;

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

}  // namespace gamev2::life

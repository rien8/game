/// @file generate.hpp
/// @brief 私有头：人口生成的中间形态。被 setup.cpp 与 systems.cpp 包含。
#pragma once

#include "gamev2/life/constants.hpp"
#include "gamev2/life/soul.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gamev2::life {

/// 一个尚未进 ECS 的人。
struct PersonInit
{
    std::string name;
    Sex sex = Sex::male;
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t age = 0;
    Temper temper;
    Aptitude aptitude;
};

/// 一个人的全部随机字段。无状态派生：只依赖 (seed, 流号, 序号)。
///
/// 收徒与散修批（systems.cpp）走同一条姓名/属性派生——两套实现会
/// 静默分岔，名册与众生长得像两个世界的人。
[[nodiscard]] PersonInit make_person(std::uint64_t seed,
                                     std::uint32_t index,
                                     history::Sect sect, Realm realm);

/// 按 (seed, Stream::life, 序号) 无状态生成全体众生。
///
/// 返回序 = 创建序 = 名册序。职位在生成后按宗门就地指派。
[[nodiscard]] std::vector<PersonInit> generate_population(
    std::uint64_t seed);

}  // namespace gamev2::life

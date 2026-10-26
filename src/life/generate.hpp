/// @file generate.hpp
/// @brief 私有头：人口生成的中间形态。只被 setup.cpp 包含。
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
    history::Sect sect = history::Sect::unowned;
    OfficeKind office = OfficeKind::none;
    Realm realm = Realm::yinqi;
    std::uint32_t age = 0;
    Temper temper;
    Aptitude aptitude;
};

/// 按 (seed, Stream::life, 序号) 无状态生成全体众生。
///
/// 返回序 = 创建序 = 名册序。职位在生成后按宗门就地指派。
[[nodiscard]] std::vector<PersonInit> generate_population(
    std::uint64_t seed);

}  // namespace gamev2::life

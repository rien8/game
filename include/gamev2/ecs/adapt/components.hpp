/// @file components.hpp
/// @brief 环境与遗传两个 system 共享的组件与输入结构。
///
/// 单独立一个头是因为它们互相要用：EnvironmentSystem 的 ReadSet 需要
/// genetics_systems.hpp 里的 SpeciesTag / CreaturePhenotype，因此
/// environment_systems.hpp 已经 include 了它；BreedingSystem 又要用
/// BreedingTarget 与 Position，反向 include 就是头文件环。
#pragma once

#include "gamev2/environment/climate.hpp"

#include "gamev2/genetics/species.hpp"

#include <cstdint>

namespace gamev2::ecs::adapt {

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

/// 本季实际摄入满足度。EnvironmentSystem 写，BreedingSystem 读。
///
/// 必须走组件而不是跨 system 传下标数组：两个 system 各自 for_each_entity
/// 一遍，遍历顺序是各自的实现细节，拿同一个下标去对齐它们是隐患。
struct Feeding
{
    float satisfaction = 1.0f;
};

/// 一个待繁殖位置的输入。EnvironmentSystem 填，BreedingSystem 读。
struct BreedingTarget
{
    gamev2::genetics::SpeciesId species;
    gamev2::environment::CellIndex cell;
    std::uint32_t progeny = 0;
    gamev2::environment::EnvironmentSnapshot environment;
};

}  // namespace gamev2::ecs::adapt

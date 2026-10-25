#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>

namespace {

using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::WorldParams;

/// 走整条流水线的世界参数。灵潮期末才有福地：谷底期没有福地，洞天随之
/// 不存在，用例就抓不到接线顺序。
WorldParams tide_end(std::uint64_t seed)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 2;
    p.seed = seed;
    p.spirit_year = 9300;
    return p;
}

/// 两个世界都既有洞天、又有死疤与浊疤（实测：909 有 1 座洞天，777 有
/// 9 座；死疤 6000 格上下，浊疤 1000 格上下）。
constexpr std::uint64_t kSeeds[] = {909, 777};

TEST(the_pipeline_lands_cave_heavens_only_on_blessed_ground)
{
    // 接线顺序的护栏：make_land_types 必须排在 make_cave_heavens 之前。
    // 反了的话洞天开在还没定型的地貌上，落点与数量一起错。
    //
    // 为什么单独立一个文件：tests/test_world_heavens.cpp 里的 build_world
    // 自己手工编排阶段，它把顺序排对了，于是 generate_world() 里顺序写
    // 错也抓不到——而顺序错了世界照样生成，只是内容全错。
    for (const std::uint64_t seed : kSeeds)
    {
        const auto w = generate_world(tide_end(seed));
        CHECK(w.has_value());
        if (!w)
        {
            continue;
        }
        CHECK(!w->cave_heavens.empty());
        for (const CaveHeaven& cave : w->cave_heavens)
        {
            CHECK(w->land_type.at(cave.cell) == LandType::blessed);
            // 福地只能来自规则 3（分等为福地）：死地与浊域优先于福地，
            // 所以「地貌是福地」蕴含「分等是福地」。
            CHECK(w->domain_grade.at(cave.cell) == DomainGrade::blessed);
        }
    }
}

TEST(the_pipeline_decides_the_land_types_after_the_scars)
{
    // 同样的护栏：make_historical_scars 必须排在 make_land_types 之前。
    // 反了的话地貌算出来时疤还没落下，死地与浊域就永远不存在。
    for (const std::uint64_t seed : kSeeds)
    {
        const auto w = generate_world(tide_end(seed));
        CHECK(w.has_value());
        if (!w)
        {
            continue;
        }
        std::uint32_t dead_cells = 0;
        std::uint32_t turbid_cells = 0;
        for (std::uint32_t i = 0; i < w->scar_kind.size(); ++i)
        {
            const Scar scar = w->scar_kind.at(i);
            if (scar == Scar::sinkhole || scar == Scar::tribulation
                || scar == Scar::depleted_eye)
            {
                ++dead_cells;
                CHECK(w->land_type.at(i) == LandType::dead);
            }
            else if (scar == Scar::turbid)
            {
                ++turbid_cells;
                CHECK(w->land_type.at(i) == LandType::turbid);
            }
        }
        // 数量必须非零：只断言蕴含关系的话，一个没有浊疤的世界照样通过，
        // 而「疤没落下」正是这条用例要抓的失效。
        CHECK(dead_cells > 0u);
        CHECK(turbid_cells > 0u);
    }
}

}  // namespace

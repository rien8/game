#include "gamev2/world/generate.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

using gamev2::world::Biome;
using gamev2::world::Deposit;
using gamev2::world::Grid;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;

using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::LeyChannel;
using gamev2::world::LeyNode;
using gamev2::world::Scar;
using gamev2::world::ScarSite;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritTide;
using gamev2::world::SpiritVein;
using gamev2::world::TidePhase;
using gamev2::world::TribulationSite;
using gamev2::world::VeinRank;
/// 逐格比较两张网格。返回首个不一致的字段名；一致时返回 sofar。
/// 尺寸也必须比：格数相同但宽高不同（80x48 与 48x80）不是同一张图。
///
/// sofar 非空就直接沿用它：调用方按阶段顺序把本函数串起来用，必须保住
/// 最早出现的差异，否则报出来的是最后一个跑偏的阶段，失去定位价值。
template<typename T>
std::string_view first_diff(std::string_view label, const Grid<T>& a,
                            const Grid<T>& b, std::string_view sofar)
{
    if (!sofar.empty())
    {
        return sofar;
    }
    if (a.width() != b.width() || a.height() != b.height())
    {
        return label;
    }
    for (std::uint32_t i = 0; i < a.size(); ++i)
    {
        if (!(a.at(i) == b.at(i)))
        {
            return label;
        }
    }
    return sofar;
}

/// 逐层比较。层数不同或某层有差异都算不一致。
/// sofar 的处理与逐格版本一致：更早的差异优先。
template<typename T>
std::string_view first_diff(std::string_view label,
                            const std::vector<Grid<T>>& a,
                            const std::vector<Grid<T>>& b,
                            std::string_view sofar)
{
    if (!sofar.empty())
    {
        return sofar;
    }
    if (a.size() != b.size())
    {
        return label;
    }
    for (std::size_t z = 0; z < a.size(); ++z)
    {
        if (const auto d = first_diff(label, a[z], b[z], {}); !d.empty())
        {
            return d;
        }
    }
    return sofar;
}

/// 灵气层的非网格数据也要逐字段比。结构体没有 operator==，
/// 因此这里逐字段写——漏一个字段就会漏掉一类不确定性。
bool same_node(const LeyNode& a, const LeyNode& b)
{
    return a.cell == b.cell && a.depth == b.depth && a.flow == b.flow
           && a.next == b.next && a.merges == b.merges
           && a.element == b.element;
}

bool same_channel(const LeyChannel& a, const LeyChannel& b)
{
    return a.nodes == b.nodes;
}

bool same_vein(const SpiritVein& a, const SpiritVein& b)
{
    return a.id == b.id && a.rank == b.rank && a.channel == b.channel
           && a.first_node == b.first_node && a.last_node == b.last_node
           && a.element == b.element && a.reserve == b.reserve
           && a.stability == b.stability && a.eye == b.eye
           && a.overmined == b.overmined;
}

bool same_eye(const SpiritEye& a, const SpiritEye& b)
{
    return a.vein == b.vein && a.node == b.node && a.cell == b.cell
           && a.flow == b.flow && a.depleted == b.depleted;
}

bool same_scar(const ScarSite& a, const ScarSite& b)
{
    return a.kind == b.kind && a.cell == b.cell && a.radius == b.radius
           && a.target == b.target;
}

bool same_heaven(const CaveHeaven& a, const CaveHeaven& b)
{
    return a.cell == b.cell && a.capacity == b.capacity
           && a.upkeep == b.upkeep;
}

bool same_tribulation_site(const TribulationSite& a, const TribulationSite& b)
{
    return a.cell == b.cell && a.radius == b.radius && a.vein == b.vein;
}

bool same_tide(const SpiritTide& a, const SpiritTide& b)
{
    return a.year == b.year && a.cycle == b.cycle && a.phase == b.phase
           && a.year_in_phase == b.year_in_phase
           && a.years_to_next_tide == b.years_to_next_tide
           && a.level == b.level && a.peak == b.peak;
}

/// 逐项比较两个向量。比较器由调用方给——灵气层的结构体没有
/// operator==，逐字段比较也更容易看出漏了哪个字段。
template<typename T, typename Eq>
std::string_view first_diff(std::string_view label,
                            const std::vector<T>& a,
                            const std::vector<T>& b, Eq eq,
                            std::string_view sofar)
{
    if (!sofar.empty())
    {
        return sofar;
    }
    if (a.size() != b.size())
    {
        return label;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!eq(a[i], b[i]))
        {
            return label;
        }
    }
    return sofar;
}

/// 按阶段顺序逐字段比较，返回首个不一致的字段名。
///
/// 字段的排列顺序就是阶段的先后顺序，因此报出来的名字直接指出
/// 最先跑偏的阶段——这正是本任务存在的理由。
std::string_view world_diff(const World& a, const World& b)
{
    std::string_view d;
    d = first_diff("elevation", a.elevation, b.elevation, d);
    d = first_diff("rainfall", a.rainfall, b.rainfall, d);
    d = first_diff("temperature", a.temperature, b.temperature, d);
    d = first_diff("downstream", a.downstream, b.downstream, d);
    d = first_diff("flow", a.flow, b.flow, d);
    d = first_diff("river_width", a.river_width, b.river_width, d);
    d = first_diff("lake_depth", a.lake_depth, b.lake_depth, d);
    d = first_diff("drainage", a.drainage, b.drainage, d);
    d = first_diff("biome", a.biome, b.biome, d);
    d = first_diff("soil_depth", a.soil_depth, b.soil_depth, d);
    d = first_diff("stone", a.stone, b.stone, d);
    d = first_diff("aquifer_top", a.aquifer_top, b.aquifer_top, d);
    d = first_diff("cavern", a.cavern, b.cavern, d);
    d = first_diff("deposit", a.deposit, b.deposit, d);
    d = first_diff("aquifer", a.aquifer, b.aquifer, d);
    // 灵气层排在既有字段之后：字段的排列顺序就是阶段的先后顺序，
    // 报出来的名字因此直接指出最先跑偏的阶段。
    d = first_diff("ley_strength", a.ley_strength, b.ley_strength, d);
    d = first_diff("ley_depth", a.ley_depth, b.ley_depth, d);
    d = first_diff("ley_element", a.ley_element, b.ley_element, d);
    d = first_diff("vein_rank", a.vein_rank, b.vein_rank, d);
    d = first_diff("vein_id", a.vein_id, b.vein_id, d);
    d = first_diff("eye_flow", a.eye_flow, b.eye_flow, d);
    d = first_diff("domain_concentration", a.domain_concentration,
                   b.domain_concentration, d);
    d = first_diff("domain_grade", a.domain_grade, b.domain_grade, d);
    d = first_diff("scar_kind", a.scar_kind, b.scar_kind, d);
    d = first_diff("spirit_suppression", a.spirit_suppression,
                   b.spirit_suppression, d);
    d = first_diff("land_type", a.land_type, b.land_type, d);
    d = first_diff("eye_purity", a.eye_purity, b.eye_purity, d);
    d = first_diff("cave_heaven", a.cave_heaven, b.cave_heaven, d);
    d = first_diff("tribulation_site", a.tribulation_site, b.tribulation_site,
                   d);
    d = first_diff("ley_channels", a.ley_channels, b.ley_channels,
                   same_channel, d);
    d = first_diff("ley_nodes", a.ley_nodes, b.ley_nodes, same_node, d);
    d = first_diff("veins", a.veins, b.veins, same_vein, d);
    d = first_diff("eyes", a.eyes, b.eyes, same_eye, d);
    d = first_diff("scars", a.scars, b.scars, same_scar, d);
    d = first_diff("cave_heavens", a.cave_heavens, b.cave_heavens,
                   same_heaven, d);
    d = first_diff("tribulation_sites", a.tribulation_sites,
                   b.tribulation_sites, same_tribulation_site, d);
    if (d.empty() && !same_tide(a.spirit_tide, b.spirit_tide))
    {
        return "spirit_tide";
    }

    // sea_level 是高程直方图的分位数，所以「海平面不同」必然蕴含「高程不同」。
    // 它排在最后，报出来的才是**原因**而不是派生出来的结果；放在最前会
    // 把最早跑偏的阶段遮住。
    //
    // 诚实标注：同参数比较时这一支**不可达**——海平面是 (elevation,
    // ocean_percent) 的函数，elevation 相同即海平面相同，而 elevation 先被
    // 比较且不等时 d 已非空。只有两个世界的 ocean_percent 不同时它才可能
    // 触发。留着是为了 sea_level_ 日后变成独立可写字段时不必重写比较函数。
    if (d.empty() && a.sea_level() != b.sea_level())
    {
        return "sea_level";
    }
    return d;
}

/// 打印并断言两个世界一致。字段名要自己打印：CHECK_EQ 只印表达式源码。
void check_same(const World& a, const World& b)
{
    const auto d = world_diff(a, b);
    if (!d.empty())
    {
        std::println("  首个不一致字段: {}", d);
    }
    CHECK(d.empty());
}

/// 断言两个世界恰好在 expected 这个字段上不同，且 world_diff 报出的就是它。
/// 自证用例专用：它故意制造差异，因此不能用 check_same。
void check_differs(const World& a, const World& b, std::string_view expected)
{
    const auto d = world_diff(a, b);
    if (d != expected)
    {
        std::println("  期望差异字段 {}, 实际报出 {}", expected, d);
    }
    CHECK(d == expected);
}

WorldParams params_of(std::uint32_t width, std::uint32_t height,
                      std::uint32_t z_levels, std::uint64_t seed)
{
    WorldParams p;
    p.width = width;
    p.height = height;
    p.z_levels = z_levels;
    p.seed = seed;
    return p;
}

TEST(full_pipeline_is_identical_for_the_same_seed)
{
    const WorldParams p = params_of(64, 64, 5, 2026);

    const auto a = generate_world(p);
    const auto b = generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    // 世界不是空的：否则「两次都是全零」也会被判成一致。
    CHECK(a->elevation.size() > 0);
    check_same(*a, *b);
}

TEST(determinism_holds_for_several_seeds)
{
    constexpr std::uint64_t kSeeds[] = {0, 1, 2026, 0xDEADBEEFull, 424242};
    for (const std::uint64_t seed : kSeeds)
    {
        const WorldParams p = params_of(48, 32, 4, seed);
        const auto a = generate_world(p);
        const auto b = generate_world(p);
        CHECK(a.has_value());
        CHECK(b.has_value());
        if (a && b)
        {
            check_same(*a, *b);
        }
    }
}

TEST(determinism_holds_for_several_geometries)
{
    // 尺寸、层数、宽高比都换一遍：z_levels=1 走「没有地下」的分支，
    // 80x48 走行主序下非方形的索引路径，17x13 走非 2 的幂。
    constexpr std::uint32_t kWidths[] = {64, 80, 32, 17};
    constexpr std::uint32_t kHeights[] = {64, 48, 32, 13};
    constexpr std::uint32_t kLevels[] = {5, 6, 1, 3};
    for (std::size_t k = 0; k < std::size(kWidths); ++k)
    {
        const WorldParams p = params_of(kWidths[k], kHeights[k], kLevels[k],
                                        700 + k);
        const auto a = generate_world(p);
        const auto b = generate_world(p);
        CHECK(a.has_value());
        CHECK(b.has_value());
        if (a && b)
        {
            check_same(*a, *b);
        }
    }
}

TEST(the_degenerate_one_cell_world_is_deterministic)
{
    const WorldParams p = params_of(1, 1, 2, 5);

    const auto a = generate_world(p);
    const auto b = generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (a && b)
    {
        check_same(*a, *b);
    }
}

TEST(a_world_is_unaffected_by_an_intervening_generation)
{
    // A → B → A。中间那次生成若在某个阶段留下了状态（函数内 static、
    // 全局可变变量），A1 与 A3 就会不同。背靠背的两次生成抓不住这种缺陷。
    const WorldParams pa = params_of(32, 32, 3, 11);
    const WorldParams pb = params_of(48, 24, 4, 22);

    const auto first = generate_world(pa);
    const auto middle = generate_world(pb);
    const auto again = generate_world(pa);

    CHECK(first.has_value());
    CHECK(middle.has_value());
    CHECK(again.has_value());
    if (first && middle && again)
    {
        check_same(*first, *again);
    }
}

TEST(different_seeds_produce_different_worlds)
{
    constexpr std::uint64_t kBaseSeeds[] = {0, 7, 1000, 65535};
    for (const std::uint64_t base : kBaseSeeds)
    {
        const WorldParams pa = params_of(48, 48, 4, base);
        const WorldParams pb = params_of(48, 48, 4, base + 1);

        const auto a = generate_world(pa);
        const auto b = generate_world(pb);
        CHECK(a.has_value());
        CHECK(b.has_value());
        if (!a || !b)
        {
            continue;
        }
        const auto d = world_diff(*a, *b);
        if (d.empty())
        {
            std::println("  seed {} 与 {} 产出了完全相同的世界", base,
                         base + 1);
        }
        CHECK(!d.empty());
        // 差异必须落在源字段上，而不是派生的 sea_level 上：报出 sea_level
        // 意味着「先查海平面」的退化顺序又回来了，那样会把最早跑偏的阶段
        // 遮住——正是这个用例要防的失效。
        CHECK(d == "elevation");
    }
}

TEST(the_field_comparison_catches_every_writable_field)
{
    // 这条用例自证比较函数不是空断言：逐个字段把一份副本改坏，
    // world_diff 必须报出那个字段的名字。少比较一个字段，这里就会红。
    //
    // 扰动位置取**末格**与**最深层**而不是首格：只扰动首格的话，「只比较
    // 首格」这种循环边界写错的比较实现照样能让本用例通过——末格上的真实
    // 差异会被它漏掉，而那种实现配合落在非首格上的不确定性，整套用例仍然
    // 全绿（实测过：只比首格 + 不确定性注入在 elevation[5] → 7/0 全绿）。
    // 灵气层需要一张足够大的图才有内容：16x16 的陆地凑不出山骨源点，
    // 地脉、灵脉、灵眼全会是空的。
    //
    // 还要取**灵潮期末**：谷底期（当代灵历 8914）没有福地，洞天是空的。
    // 那条 `if (!w.cave_heavens.empty())` 守卫会把扰动整段跳过，于是
    // 两个世界一模一样，断言「必须不相等」就成了假的。256x256 + 灵历
    // 9300（潮位 99）能产出洞天（种子 909 实测 1 座），扰动才真的落在
    // 非空向量上。
    WorldParams p = params_of(256, 256, 2, 909);
    p.spirit_year = 9300;

    const auto base = generate_world(p);
    CHECK(base.has_value());
    if (!base)
    {
        return;
    }

    // 全部地表网格与每层地下网格的尺寸相同，同一个下标对它们都成立。
    const std::uint32_t last = base->elevation.size() - 1;
    const std::uint32_t deep = p.z_levels - 1;

    {
        World w = *base;
        w.elevation.at(last) =
            static_cast<std::uint8_t>(w.elevation.at(last) ^ 0xFFu);
        check_differs(*base, w, "elevation");
    }
    {
        World w = *base;
        w.rainfall.at(last) =
            static_cast<std::uint8_t>(w.rainfall.at(last) ^ 0xFFu);
        check_differs(*base, w, "rainfall");
    }
    {
        World w = *base;
        w.temperature.at(last) =
            static_cast<std::uint8_t>(w.temperature.at(last) ^ 0xFFu);
        check_differs(*base, w, "temperature");
    }
    {
        World w = *base;
        w.downstream.at(last) = w.downstream.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "downstream");
    }
    {
        World w = *base;
        w.flow.at(last) = w.flow.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "flow");
    }
    {
        World w = *base;
        w.river_width.at(last) =
            static_cast<std::uint8_t>(w.river_width.at(last) ^ 0xFFu);
        check_differs(*base, w, "river_width");
    }
    {
        World w = *base;
        w.lake_depth.at(last) =
            static_cast<std::uint8_t>(w.lake_depth.at(last) ^ 0xFFu);
        check_differs(*base, w, "lake_depth");
    }
    {
        World w = *base;
        w.drainage.at(last) =
            static_cast<std::uint8_t>(w.drainage.at(last) ^ 0xFFu);
        check_differs(*base, w, "drainage");
    }
    {
        World w = *base;
        w.biome.at(last) = w.biome.at(last) == Biome::ocean ? Biome::lake
                                                             : Biome::ocean;
        check_differs(*base, w, "biome");
    }
    {
        World w = *base;
        w.soil_depth.at(last) =
            static_cast<std::uint8_t>(w.soil_depth.at(last) ^ 0xFFu);
        check_differs(*base, w, "soil_depth");
    }
    {
        World w = *base;
        w.aquifer_top.at(last) =
            static_cast<std::uint8_t>(w.aquifer_top.at(last) ^ 0xFFu);
        check_differs(*base, w, "aquifer_top");
    }
    {
        World w = *base;
        w.stone[deep].at(last) = w.stone[deep].at(last) == Stone::granite
                                     ? Stone::basalt
                                     : Stone::granite;
        check_differs(*base, w, "stone");
    }
    {
        World w = *base;
        w.cavern[deep].at(last) = !w.cavern[deep].at(last);
        check_differs(*base, w, "cavern");
    }
    {
        World w = *base;
        w.deposit[deep].at(last) = w.deposit[deep].at(last) == Deposit::none
                                       ? Deposit::iron
                                       : Deposit::none;
        check_differs(*base, w, "deposit");
    }
    {
        World w = *base;
        w.aquifer[deep].at(last) = !w.aquifer[deep].at(last);
        check_differs(*base, w, "aquifer");
    }
    // 灵气层的网格：逐个扰动。
    {
        World w = *base;
        w.ley_strength.at(last) =
            static_cast<std::uint8_t>(w.ley_strength.at(last) ^ 0xFFu);
        check_differs(*base, w, "ley_strength");
    }
    {
        World w = *base;
        w.ley_depth.at(last) =
            static_cast<std::uint8_t>(w.ley_depth.at(last) ^ 0xFFu);
        check_differs(*base, w, "ley_depth");
    }
    {
        World w = *base;
        w.ley_element.at(last) = w.ley_element.at(last) == Element::metal
                                    ? Element::fire
                                    : Element::metal;
        check_differs(*base, w, "ley_element");
    }
    {
        World w = *base;
        w.vein_rank.at(last) = w.vein_rank.at(last) == VeinRank::none
                                   ? VeinRank::branch
                                   : VeinRank::none;
        check_differs(*base, w, "vein_rank");
    }
    {
        World w = *base;
        w.vein_id.at(last) = w.vein_id.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "vein_id");
    }
    {
        World w = *base;
        w.eye_flow.at(last) = w.eye_flow.at(last) ^ 0xFFFFFFFFu;
        check_differs(*base, w, "eye_flow");
    }
    {
        World w = *base;
        w.domain_concentration.at(last) =
            static_cast<std::uint8_t>(w.domain_concentration.at(last) ^ 0xFFu);
        check_differs(*base, w, "domain_concentration");
    }
    {
        World w = *base;
        w.domain_grade.at(last) =
            w.domain_grade.at(last) == DomainGrade::barren
                ? DomainGrade::thick
                : DomainGrade::barren;
        check_differs(*base, w, "domain_grade");
    }
    {
        World w = *base;
        w.scar_kind.at(last) = w.scar_kind.at(last) == Scar::none
                                   ? Scar::sinkhole
                                   : Scar::none;
        check_differs(*base, w, "scar_kind");
    }
    {
        World w = *base;
        w.spirit_suppression.at(last) =
            static_cast<std::uint8_t>(w.spirit_suppression.at(last) ^ 0xFFu);
        check_differs(*base, w, "spirit_suppression");
    }
    {
        World w = *base;
        w.land_type.at(last) = w.land_type.at(last) == LandType::mundane
                                   ? LandType::dead
                                   : LandType::mundane;
        check_differs(*base, w, "land_type");
    }
    {
        World w = *base;
        w.eye_purity.at(last) =
            static_cast<std::uint8_t>(w.eye_purity.at(last) ^ 0xFFu);
        check_differs(*base, w, "eye_purity");
    }
    {
        World w = *base;
        w.cave_heaven.at(last) = !w.cave_heaven.at(last);
        check_differs(*base, w, "cave_heaven");
    }
    {
        World w = *base;
        w.tribulation_site.at(last) = !w.tribulation_site.at(last);
        check_differs(*base, w, "tribulation_site");
    }
    // 灵气层的非网格数据：逐个扰动。
    {
        World w = *base;
        w.spirit_tide.level =
            static_cast<std::uint8_t>(w.spirit_tide.level ^ 0xFFu);
        check_differs(*base, w, "spirit_tide");
    }
    {
        World w = *base;
        if (!w.ley_channels.empty())
        {
            w.ley_channels[0].nodes.push_back(12345u);
        }
        check_differs(*base, w, "ley_channels");
    }
    {
        World w = *base;
        if (!w.ley_nodes.empty())
        {
            w.ley_nodes[0].depth ^= 1u;
        }
        check_differs(*base, w, "ley_nodes");
    }
    {
        World w = *base;
        if (!w.veins.empty())
        {
            w.veins[0].reserve ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "veins");
    }
    {
        World w = *base;
        if (!w.eyes.empty())
        {
            w.eyes[0].flow ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "eyes");
    }
    {
        World w = *base;
        if (!w.scars.empty())
        {
            w.scars[0].radius ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "scars");
    }
    {
        World w = *base;
        if (!w.cave_heavens.empty())
        {
            w.cave_heavens[0].upkeep ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "cave_heavens");
    }
    {
        World w = *base;
        if (!w.tribulation_sites.empty())
        {
            w.tribulation_sites[0].radius ^= 0xFFFFFFFFu;
        }
        check_differs(*base, w, "tribulation_sites");
    }}

TEST(the_comparison_notices_grids_of_a_different_shape)
{
    // first_diff 的两支尺寸检查（宽高不等、层数不等）此前从不被任何用例
    // 走到，等于死代码：删掉它们，整套用例依然全绿。这里用两张「格数相同、
    // 逐格内容相同、形状不同」的网格把它们逼出来——只有尺寸检查能把它们
    // 判成不同的图。
    const Grid<std::uint8_t> wide(4, 1, 7);
    const Grid<std::uint8_t> tall(1, 4, 7);
    CHECK(first_diff("elevation", wide, tall, {}) == "elevation");

    // 层数少的一方放前面：删掉层数检查后，循环只按较短的一侧迭代，得到
    // 「没有差异」这个干净结论，而不是越界访问较长的那一侧。
    const std::vector<Grid<std::uint8_t>> one_layer{wide};
    const std::vector<Grid<std::uint8_t>> two_layers{wide, tall};
    CHECK(first_diff("stone", one_layer, two_layers, {}) == "stone");
}

TEST(the_spirit_tide_is_a_real_parameter)
{
    // 同一个 seed，地脉网络必须完全相同——它是地质的，不随潮位变；
    // 但灵域分等必须变——设定 2.6 的「灵潮期：泉涌、福地扩张」与
    // 「谷底期：灵脉半死」就是这个区别。这条用例证明灵潮周期不是
    // 写在文档里的背景，而是一个真的参数。
    WorldParams trough = params_of(96, 96, 2, 606);
    trough.spirit_year = 8914;  // 谷底期
    WorldParams peak = trough;
    peak.spirit_year = 9300;    // 灵潮期最后一年，浓度峰值

    const auto a = generate_world(trough);
    const auto b = generate_world(peak);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }

    CHECK_EQ(a->ley_nodes.size(), b->ley_nodes.size());
    for (std::size_t i = 0; i < a->ley_nodes.size(); ++i)
    {
        CHECK_EQ(a->ley_nodes[i].cell, b->ley_nodes[i].cell);
        CHECK_EQ(a->ley_nodes[i].depth, b->ley_nodes[i].depth);
        CHECK_EQ(a->ley_nodes[i].flow, b->ley_nodes[i].flow);
    }
    for (std::uint32_t i = 0; i < a->ley_strength.size(); ++i)
    {
        CHECK_EQ(a->ley_strength.at(i), b->ley_strength.at(i));
    }

    CHECK(a->spirit_tide.phase == TidePhase::trough);
    CHECK(b->spirit_tide.phase == TidePhase::tide);
    CHECK(a->spirit_tide.level < b->spirit_tide.level);

    bool grade_changed = false;
    for (std::uint32_t i = 0; i < a->domain_grade.size(); ++i)
    {
        if (a->domain_grade.at(i) != b->domain_grade.at(i))
        {
            grade_changed = true;
            break;
        }
    }
    CHECK(grade_changed);
}
}  // namespace

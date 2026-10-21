#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::Element;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::ScarSite;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritVein;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::kNoVein;
using gamev2::world::make_historical_scars;
using gamev2::world::make_land_types;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    return p;
}

/// 阶段 15 的前置状态。
World build_world(std::uint64_t seed, std::uint32_t year)
{
    World w(params(seed, year));
    gamev2::world::make_spirit_tide(w);
    gamev2::world::make_elevation(w);
    gamev2::world::make_rainfall(w);
    gamev2::world::make_temperature(w);
    gamev2::world::make_hydrology(w);
    gamev2::world::make_biomes(w);
    gamev2::world::make_strata(w);
    gamev2::world::make_ley_network(w);
    gamev2::world::make_spirit_veins(w);
    gamev2::world::make_spirit_domains(w);
    make_historical_scars(w);
    make_land_types(w);
    return w;
}

/// 21 个世界：7 个种子 × 3 个年份。
///
/// 灵历 8914 是谷底期、9100 是衰退期、9300 是灵潮期末。构建一个 256x256
/// 的世界要小半秒，五条用例各建一遍太慢，故整份测试只构建一次。
const std::vector<World>& survey_worlds()
{
    static const std::vector<World> worlds = [] {
        const std::uint64_t seeds[] = {1, 2, 3, 7, 42, 101, 777};
        const std::uint32_t years[] = {8914, 9100, 9300};
        std::vector<World> built;
        for (const std::uint64_t seed : seeds)
        {
            for (const std::uint32_t year : years)
            {
                built.push_back(build_world(seed, year));
            }
        }
        return built;
    }();
    return worlds;
}

/// 各疤中心的压制量。数值在这里独立写一遍是刻意的：测试要钉住规格，
/// 不是复述实现。
[[nodiscard]] std::uint8_t centre_suppression(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::sinkhole:
        return 220;
    case Scar::tribulation:
        return 180;
    case Scar::depleted_eye:
        return 200;
    case Scar::turbid:
        return 0;
    case Scar::none:
        return 0;
    }
    return 0;
}

/// 该格与某处疤的中心距是否小于「新疤半径 + 旧疤半径」，即放不下。
[[nodiscard]] bool blocked_by(const World& w, std::uint32_t cell, Scar kind,
                              std::uint32_t new_radius)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t gap =
            static_cast<std::int64_t>(new_radius) + s.radius;
        if (dx * dx + dy * dy < gap * gap)
        {
            return true;
        }
    }
    return false;
}

/// 该格是否落在某处同种疤的圆盘内。
///
/// 网格只记种类、不记归属，所以「半径之外的格不可能挂着这处疤的种类」
/// 只在同类疤仅一处时成立：甲疤半径之外的格完全可能落在乙疤的圆盘里。
/// 种子 3 上就有两处枯眼疤挨得够近，那条断言因此翻红。
[[nodiscard]] bool covered_by_same_kind(const World& w, std::uint32_t cell,
                                        Scar kind)
{
    const std::uint32_t width = w.params().width;
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    for (const ScarSite& s : w.scars)
    {
        if (s.kind != kind)
        {
            continue;
        }
        const std::int64_t dx = cx - static_cast<std::int64_t>(s.cell % width);
        const std::int64_t dy = cy - static_cast<std::int64_t>(s.cell / width);
        const std::int64_t r = static_cast<std::int64_t>(s.radius);
        if (dx * dx + dy * dy < r * r)
        {
            return true;
        }
    }
    return false;
}

/// 疤在顺序契约里的段号。
///
/// 契约规定 `World::scars` 的顺序固定：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
/// 段号单调不减就是「分段」。
[[nodiscard]] int scar_segment(Scar kind) noexcept
{
    switch (kind)
    {
    case Scar::depleted_eye:
        return 0;
    case Scar::turbid:
        return 1;
    case Scar::sinkhole:
        return 2;
    case Scar::tribulation:
        return 3;
    case Scar::none:
        break;
    }
    return -1;
}

/// 该格与某处疤的圆盘是否相交：中心距小于两半径之和。
///
/// 与 `blocked_by` 的区别是这里判的是「某处具体的疤」：重放放置顺序时
/// 已放的疤要按 kind 逐一判过去，不能一锅端。
[[nodiscard]] bool blocked_by_site(std::uint32_t cell, std::uint32_t radius,
                                   const ScarSite& site,
                                   std::uint32_t width) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / width);
    const std::int64_t dx = cx - static_cast<std::int64_t>(site.cell % width);
    const std::int64_t dy = cy - static_cast<std::int64_t>(site.cell / width);
    const std::int64_t gap = static_cast<std::int64_t>(radius) + site.radius;
    return dx * dx + dy * dy < gap * gap;
}

TEST(scar_cells_and_suppression_agree)
{
    const World w = build_world(3, 8914);
    bool agree = true;
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        const std::uint8_t sup = w.spirit_suppression.at(i);
        if (kind == Scar::none || kind == Scar::turbid)
        {
            // 无疤的格没有压制；浊域也不压制——它是浓度高但混乱。
            agree = agree && sup == 0u;
        }
        else
        {
            agree = agree && sup > 0u;
        }
    }
    CHECK(agree);
}

TEST(scar_suppression_is_strongest_at_the_centre)
{
    const World w = build_world(3, 8914);
    bool ok = true;
    for (const ScarSite& site : w.scars)
    {
        // 中心是压制最强处。
        ok = ok && w.spirit_suppression.at(site.cell)
                    == centre_suppression(site.kind);
    }

    // 带疤的格子必须落在某处同种疤的圆盘内：压制在半径处落到 0，圆盘之外
    // 不可能挂着这处疤的种类。
    for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
    {
        const Scar kind = w.scar_kind.at(i);
        if (kind == Scar::none)
        {
            continue;
        }
        ok = ok && covered_by_same_kind(w, i, kind);
    }
    CHECK(ok);
}

TEST(sinkholes_fall_on_overmined_veins)
{
    const World w = build_world(3, 8914);

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind != Scar::sinkhole)
        {
            continue;
        }
        ++sinkholes;
        // 每处沉陷疤都落在一条被越采的灵脉上。
        CHECK(site.target < w.veins.size());
        CHECK(w.veins[site.target].overmined);
    }
    // 处数是规格给的硬上限。
    CHECK(sinkholes <= 3u);
}

TEST(overmined_veins_leave_sinkholes)
{
    const World w = build_world(3, 8914);

    // 触发条件：格落在被越采的灵脉上、地势低平、且没有被先放的枯眼疤
    // 挡住。条件是否凑齐取决于地形，故先把它算出来，再决定断言。
    const int sea_limit = static_cast<int>(w.sea_level()) + 20;
    std::uint32_t eligible = 0;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            if (static_cast<int>(w.elevation.at(i)) >= sea_limit)
            {
                continue;
            }
            const std::uint32_t id = w.vein_id.at(i);
            if (id == kNoVein || id >= w.veins.size())
            {
                continue;
            }
            if (!w.veins[id].overmined)
            {
                continue;
            }
            if (blocked_by(w, i, Scar::depleted_eye, 24u))
            {
                continue;
            }
            ++eligible;
        }
    }

    std::uint32_t sinkholes = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind == Scar::sinkhole)
        {
            ++sinkholes;
        }
    }
    // 触发条件凑不齐时地形说了算，不算失败。
    if (eligible > 0u)
    {
        CHECK(sinkholes > 0u);
    }
}

TEST(depleted_eyes_are_surrounded_by_scars)
{
    const World w = build_world(3, 8914);
    std::uint32_t depleted = 0;
    std::uint32_t scarred = 0;
    for (std::uint32_t e = 0; e < w.eyes.size(); ++e)
    {
        if (!w.eyes[e].depleted)
        {
            continue;
        }
        ++depleted;
        if (w.scar_kind.at(w.eyes[e].cell) == Scar::depleted_eye)
        {
            ++scarred;
        }
    }
    CHECK(depleted > 0u);
    // 枯眼周边成死地是抽干的必然后果。放不下的（与别的疤重叠）才例外。
    CHECK(scarred > 0u);
    CHECK(scarred <= depleted);
}

TEST(scars_are_deterministic)
{
    const World a = build_world(3, 8914);
    const World b = build_world(3, 8914);
    bool same = a.scars.size() == b.scars.size();
    for (std::size_t i = 0; i < a.scars.size() && i < b.scars.size(); ++i)
    {
        same = same && a.scars[i].kind == b.scars[i].kind;
        same = same && a.scars[i].cell == b.scars[i].cell;
        same = same && a.scars[i].radius == b.scars[i].radius;
        same = same && a.scars[i].target == b.scars[i].target;
    }
    for (std::uint32_t i = 0; i < a.scar_kind.size(); ++i)
    {
        same = same && a.scar_kind.at(i) == b.scar_kind.at(i);
        same = same
               && a.spirit_suppression.at(i) == b.spirit_suppression.at(i);
    }
    CHECK(same);
}

/// 渡劫废墟的触发条件：厚域或福地才渡得起劫。
TEST(tribulation_scars_sit_on_thick_or_blessed_cells)
{
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::tribulation)
            {
                continue;
            }
            ++tribulations;
            const DomainGrade grade = w.domain_grade.at(site.cell);
            ok = ok && (grade == DomainGrade::thick
                        || grade == DomainGrade::blessed);
        }
    }
    CHECK(ok);
    // 断言不能空跑：确实要有渡劫疤可查。
    CHECK(tribulations > 0u);
}

/// 沉陷疤的触发条件：地势低平——高程低于海平面加 20。
///
/// 20 是「地势低平」的规格值，在测试里独立写一遍，不引用实现里的常量。
TEST(sinkhole_scars_sit_on_low_flat_ground)
{
    bool ok = true;
    std::uint32_t sinkholes = 0;
    for (const World& w : survey_worlds())
    {
        const int sea_limit = static_cast<int>(w.sea_level()) + 20;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::sinkhole)
            {
                continue;
            }
            ++sinkholes;
            ok = ok && static_cast<int>(w.elevation.at(site.cell)) < sea_limit;
        }
    }
    CHECK(ok);
    CHECK(sinkholes > 0u);
}

/// 两处疤的圆盘互不相交：中心距不低于两半径之和。
///
/// 这条不变式设计 §3.5 与计划 Task 6 都明列着，此前只靠「中心压制量」
/// 间接兜底——而中心压制量只在重叠恰好盖住某个疤中心时才报警。
TEST(scars_never_overlap)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        for (std::size_t i = 0; i < w.scars.size(); ++i)
        {
            for (std::size_t j = i + 1; j < w.scars.size(); ++j)
            {
                const std::int64_t dx =
                    static_cast<std::int64_t>(w.scars[i].cell % width)
                    - static_cast<std::int64_t>(w.scars[j].cell % width);
                const std::int64_t dy =
                    static_cast<std::int64_t>(w.scars[i].cell / width)
                    - static_cast<std::int64_t>(w.scars[j].cell / width);
                const std::int64_t gap =
                    static_cast<std::int64_t>(w.scars[i].radius)
                    + static_cast<std::int64_t>(w.scars[j].radius);
                ok = ok && (dx * dx + dy * dy >= gap * gap);
            }
        }
    }
    CHECK(ok);
}

/// target 的契约：沉陷疤与渡劫疤填灵脉 id，浊域疤与枯眼疤填灵眼下标。
///
/// 没有所属灵脉时填 kNoVein：结构体默认值 0 是有效的第 0 条脉下标，
/// 用 0 表示「无」会让下游把渡劫疤误读成「指向龙脉」。
TEST(scar_targets_are_valid_indices_or_sentinels)
{
    bool ok = true;
    for (const World& w : survey_worlds())
    {
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::sinkhole || site.kind == Scar::tribulation)
            {
                ok = ok && (site.target == kNoVein
                            || site.target < w.veins.size());
            }
            else
            {
                ok = ok && site.target < w.eyes.size();
            }
        }
    }
    CHECK(ok);
}

/// 浊域的回归哨兵：沸海是设定 4.2 点名的具名地貌，每个世界都得有。
///
/// 浊域提前放置之后，它的处数对半径与选点方式都不敏感（半径 14 与 20、
/// greedy 与 uniform 都落在 39 处），所以这条用例顺带把浊域的半径与
/// 「按流量降序取点」钉住——只有这两条规格能挡住那两类回归。
TEST(every_world_has_at_least_one_turbid_scar)
{
    bool every_world_has_one = true;
    bool radius_is_spec = true;
    bool flow_is_descending = true;
    std::uint32_t turbid_total = 0;
    for (const World& w : survey_worlds())
    {
        std::uint32_t turbid = 0;
        std::uint32_t previous_flow = 0;
        bool first = true;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind != Scar::turbid)
            {
                continue;
            }
            ++turbid;
            // 浊域的半径是标定过的规格值，在测试里独立写一遍。
            radius_is_spec = radius_is_spec && site.radius == 14u;
            if (site.target >= w.eyes.size())
            {
                continue;
            }
            const std::uint32_t flow = w.eyes[site.target].flow;
            // greedy 选点：先落的浊域流量最大，后落的流量不更大。
            if (!first)
            {
                flow_is_descending = flow_is_descending && flow <= previous_flow;
            }
            previous_flow = flow;
            first = false;
        }
        every_world_has_one = every_world_has_one && turbid >= 1u;
        turbid_total += turbid;
    }
    CHECK(every_world_has_one);
    // 总量是规格给的下限：浊域不是稀有物。
    CHECK(turbid_total >= 36u);
    CHECK(radius_is_spec);
    CHECK(flow_is_descending);
}

/// 「疤痕压过福地」必须在数据上成立：福地格被死地类疤盖住之后，有效浓度
/// 必须掉到福地门槛以下——否则「死地」只是一张标签，兽潮的前提不存在。
///
/// 断言的是比例而不是「至少一处」：只要福地格被疤盖住却大半没跌到门槛
/// 以下，比例就会掉下来。灵历 9300（灵潮期末）福地最多，谷底期几乎没有。
TEST(a_scar_demotes_a_blessed_cell_below_the_threshold)
{
    std::uint64_t covered = 0;
    std::uint64_t demoted = 0;
    for (const World& w : survey_worlds())
    {
        for (std::uint32_t i = 0; i < w.scar_kind.size(); ++i)
        {
            if (w.domain_grade.at(i) != DomainGrade::blessed)
            {
                continue;
            }
            if (!gamev2::world::is_dead_scar(w.scar_kind.at(i)))
            {
                continue;
            }
            ++covered;
            const std::uint8_t effective = gamev2::world::effective_concentration(
                w.domain_concentration.at(i), w.spirit_suppression.at(i));
            if (effective < gamev2::world::kBlessedThreshold)
            {
                ++demoted;
            }
        }
    }
    // 两个数都要断言：分母不为零（确实有福地格被疤盖住），分子不为零。
    CHECK(covered > 0u);
    CHECK(demoted > 0u);
    // 过半：被死地类疤盖住的福地格，一半以上要跌到福地门槛以下。
    CHECK(demoted * 2u >= covered);
}

/// 「先滤后抽」：渡劫疤只落在当时自由的渡劫候选上，自由候选没用完就不停手。
///
/// MUST-1 的第 2 要素是「先滤掉与已放圆盘不相交的候选，再在自由候选里选」。
/// 自由候选在这里独立复算：渡劫候选是 `domain_grade` 为 thick 或 blessed
/// 的格，自由候选是其中与已放的枯眼/浊域/沉陷疤圆盘不相交的格——按 kind
/// 区分，渡劫疤自己不参与判相交。
TEST(tribulation_scars_take_free_candidates)
{
    constexpr std::uint32_t radius = 8;  // 渡劫疤的半径，规格值
    constexpr std::size_t limit = 6;     // 渡劫疤的处数上限，规格值
    bool ok = true;
    std::uint32_t tribulations = 0;
    for (const World& w : survey_worlds())
    {
        const std::uint32_t width = w.params().width;
        std::vector<ScarSite> others;
        std::vector<ScarSite> tribs;
        for (const ScarSite& site : w.scars)
        {
            if (site.kind == Scar::tribulation)
            {
                tribs.push_back(site);
            }
            else
            {
                others.push_back(site);
            }
        }
        tribulations += static_cast<std::uint32_t>(tribs.size());

        // 自由候选：渡劫候选里与已放的非渡劫疤不相交的格。
        std::vector<std::uint32_t> live;
        for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
        {
            const DomainGrade grade = w.domain_grade.at(i);
            if (grade != DomainGrade::thick && grade != DomainGrade::blessed)
            {
                continue;
            }
            bool blocked = false;
            for (const ScarSite& s : others)
            {
                if (blocked_by_site(i, radius, s, width))
                {
                    blocked = true;
                    break;
                }
            }
            if (!blocked)
            {
                live.push_back(i);
            }
        }

        // 逐处重放：每处渡劫疤都占着一个当时自由的候选。
        for (const ScarSite& site : tribs)
        {
            ok = ok && (std::ranges::find(live, site.cell) != live.end());
            std::vector<std::uint32_t> keep;
            for (const std::uint32_t c : live)
            {
                if (!blocked_by_site(c, radius, site, width))
                {
                    keep.push_back(c);
                }
            }
            live = keep;
        }
        // 自由候选还有剩就不该停手：处数不到上限时自由候选必须已经耗尽。
        ok = ok && (tribs.size() >= limit || live.empty());
        ok = ok && (tribs.size() <= limit);
    }
    CHECK(ok);
    // 总量是第二道防线：抽中即丢会把渡劫名额白白消耗掉（实测 99 -> 96）。
    CHECK(tribulations >= 98u);
}

/// `World::scars` 的顺序契约：枯眼疤 → 浊域疤 → 沉陷疤 → 渡劫疤。
///
/// 计划 Task 6 的契约段明写了这个顺序，此前没有任何用例检查它。段号单调
/// 不减就是「分段」：同一种疤连成一段，段与段之间不交错。
TEST(scars_are_grouped_by_kind)
{
    bool grouped = true;
    for (const World& w : survey_worlds())
    {
        int previous = 0;
        for (const ScarSite& site : w.scars)
        {
            const int segment = scar_segment(site.kind);
            grouped = grouped && segment >= previous;
            previous = segment;
        }
    }
    CHECK(grouped);
}

TEST(land_type_follows_the_ordered_rules)
{
    // 取灵潮期末（灵历 9300，潮位 99）：当代是谷底期，那里没有福地，
    // 「福地」这条规则会整条空跑。设定 2.6 说福地随灵潮出现并扩张。
    const World w = build_world(3, 9300);
    bool ok = true;
    for (std::uint32_t i = 0; i < w.land_type.size(); ++i)
    {
        const Scar scar = w.scar_kind.at(i);
        const LandType land = w.land_type.at(i);
        const bool dead_scar =
            scar == Scar::sinkhole || scar == Scar::tribulation
            || scar == Scar::depleted_eye;
        ok = ok && (land == LandType::dead) == dead_scar;
        ok = ok && (land == LandType::turbid) == (scar == Scar::turbid);
        ok = ok
             && (land == LandType::blessed)
                == (scar == Scar::none
                    && w.domain_grade.at(i) == DomainGrade::blessed);
    }
    CHECK(ok);
}

/// 当代（灵历 8914，谷底期）在默认种子上没有福地地貌。
///
/// 理由要说准：谷底期灵脉半死，**多数**种子的福地格数是 0，但不是所有
/// 种子——seed 777 在 8914 有 286 个福地格（复审实测）。所以这条只钉住
/// 默认种子，不声称「谷底期没有福地」这件事本身。
TEST(the_nadir_year_has_no_blessed_land_type)
{
    const World w = build_world(3, 8914);
    std::uint32_t blessed = 0;
    for (std::uint32_t i = 0; i < w.land_type.size(); ++i)
    {
        if (w.land_type.at(i) == LandType::blessed)
        {
            ++blessed;
        }
    }
    CHECK_EQ(blessed, 0u);
}

TEST(a_dead_scar_overrides_a_blessed_domain)
{
    // 合成：同一格既是福地又被疤盖住。规则 1 压过规则 3，地貌必须是
    // 死地，而分等不动——读取方要能分清「这里的网有多强」与「这里还能
    // 不能住」。设定 7.2 的兽潮前提正是这一格。
    WorldParams p;
    p.width = 16;
    p.height = 16;

    World w(p);
    const std::uint32_t i = w.elevation.index_of(8, 8);
    w.domain_grade.at(i) = DomainGrade::blessed;
    w.scar_kind.at(i) = Scar::sinkhole;

    make_land_types(w);

    CHECK(w.land_type.at(i) == LandType::dead);
    CHECK(w.domain_grade.at(i) == DomainGrade::blessed);
}

/// 「死地压过福地」必须在数据上成立：分等说这是福地的格，地貌却是死地。
///
/// 设定 7.2 的兽潮前提正是这一格——灵域还是那个灵域，地已经死了。
/// 复审指出旧写法在死地格上断言「它不是福地格」，那是编译期恒真的比较，
/// 永远不可能失败；这条反过来断言「有分等福地格被压成了死地」。
TEST(a_blessed_domain_can_be_dead_ground)
{
    // 灵潮期末才有福地：谷底期分等里几乎没有 blessed，这条会整条空跑。
    const World w = build_world(3, 9300);
    std::uint32_t blessed_grade = 0;
    std::uint32_t blessed_grade_but_dead = 0;
    for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
    {
        if (w.domain_grade.at(i) != DomainGrade::blessed)
        {
            continue;
        }
        ++blessed_grade;
        if (w.land_type.at(i) == LandType::dead)
        {
            ++blessed_grade_but_dead;
        }
    }
    CHECK(blessed_grade > 0u);
    // 死地压过福地不是理论上的可能，数据里必须真的发生。
    CHECK(blessed_grade_but_dead > 0u);
}

TEST(poison_and_dark_ley_elements_make_miasma_and_yin)
{
    // 合成世界：左半边铺毒属性地脉，右半边铺暗属性地脉。
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            w.ley_element.at(i) =
                x < p.width / 2 ? Element::poison : Element::dark;
        }
    }
    make_land_types(w);

    bool ok = true;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            // 探测半径 6，边界带两侧的属性互相看得见，故只断言两侧的
            // 纯净部分。
            if (x + 6u < p.width / 2)
            {
                ok = ok && w.land_type.at(i) == LandType::miasma;
            }
            else if (x == p.width / 2 + 5u)
            {
                // 探测半径 6 的边界：这一列的方邻域还能看见 x = 31 的毒属性
                // 地脉（31 - 6 = 25，邻域右缘 37 覆盖到 31），所以它是瘴域
                // 而不是阴地。半径若改成 5，它就看不见毒脉、变成阴地，这
                // 条断言会翻红——半径因此被钉死。
                ok = ok && w.land_type.at(i) == LandType::miasma;
            }
            else if (x >= p.width / 2 + 6u)
            {
                ok = ok && w.land_type.at(i) == LandType::yin;
            }
        }
    }
    CHECK(ok);
}

TEST(miasma_and_yin_cells_sit_on_matching_ley_elements)
{
    const World w = build_world(3, 8914);
    bool ok = true;
    for (std::uint32_t y = 0; y < w.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < w.params().width; ++x)
        {
            const std::uint32_t i = w.elevation.index_of(x, y);
            const LandType land = w.land_type.at(i);
            if (land != LandType::miasma && land != LandType::yin)
            {
                continue;
            }
            bool poison = false;
            bool dark = false;
            for (int dy = -6; dy <= 6; ++dy)
            {
                const int ny = static_cast<int>(y) + dy;
                if (ny < 0 || ny >= static_cast<int>(w.params().height))
                {
                    continue;
                }
                for (int dx = -6; dx <= 6; ++dx)
                {
                    const int nx = static_cast<int>(x) + dx;
                    if (nx < 0 || nx >= static_cast<int>(w.params().width))
                    {
                        continue;
                    }
                    const std::uint32_t j = w.elevation.index_of(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny));
                    const Element e = w.ley_element.at(j);
                    poison = poison || e == Element::poison;
                    dark = dark || e == Element::dark;
                }
            }
            if (land == LandType::miasma)
            {
                ok = ok && poison;
            }
            else
            {
                ok = ok && dark;
            }
        }
    }
    CHECK(ok);
}

TEST(eye_purity_follows_the_ley_element_and_its_neighbourhood)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    // 一条五行脉：基线纯度 200。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::metal;

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = 0;  // 下面按尺寸填
    eye.flow = 100;

    auto make_world = [&p, &vein, &eye]() {
        World w(p);
        w.veins.push_back(vein);
        SpiritEye e = eye;
        e.cell = w.elevation.index_of(32, 32);
        w.eyes.push_back(e);
        return w;
    };

    // 一、周围无污染：纯度就是基线。
    World clean = make_world();
    make_land_types(clean);
    CHECK_EQ(clean.eye_purity.at(clean.eyes[0].cell), 200u);

    // 二、周围全是瘴域：每格扣 60，扣穿为 0。
    World miasma = make_world();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            miasma.ley_element.at(miasma.elevation.index_of(x, y)) =
                Element::poison;
        }
    }
    make_land_types(miasma);
    CHECK_EQ(miasma.eye_purity.at(miasma.eyes[0].cell), 0u);

    // 三、周围全是浊域：浊域不扣纯度。设定 4.1 说沸海是「天下最纯的原
    // 料产地」——那里灵气被搅动而不是被污染，纯得不能用。这条反直觉，
    // 改动前先读这里的注释。
    World turbid = make_world();
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = turbid.elevation.index_of(x, y);
            turbid.scar_kind.at(i) = Scar::turbid;
        }
    }
    make_land_types(turbid);
    CHECK_EQ(turbid.eye_purity.at(turbid.eyes[0].cell), 200u);
}

TEST(eye_purity_of_a_hetero_vein_is_lower)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    // 异质脉难驾驭，代价就是纯度低：基线 120。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::poison;
    w.veins.push_back(vein);

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 120u);
}

/// 查不到地脉属性时基线纯度是中性值 160，既不冒充五行也不冒充异质。
///
/// 这条分支在真实世界里走不到（实测 21 个世界 1050 只灵眼，属性全部查得
/// 到），所以只能合成。少了它，「查不到脉时纯度悄悄变了」没人会发现。
TEST(eye_purity_without_a_known_element_is_neutral)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    // 灵脉本身没有属性：查不到就是查不到，不许当成异质。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::none;
    w.veins.push_back(vein);

    SpiritEye eye;
    eye.vein = 0;
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 160u);
}

/// 灵眼找不到所属灵脉（下标越界）时同样是中性基线。
///
/// 与上一条是两条不同的路：上一条是「脉在、但没属性」，这一条是「脉根本
/// 不存在」。两条都走 `Element::none`，但走的是不同的判断。
TEST(eye_purity_without_a_vein_is_neutral)
{
    WorldParams p;
    p.width = 32;
    p.height = 32;

    World w(p);
    SpiritEye eye;
    eye.vein = 99;  // 越界：vein_element 返回 none
    eye.cell = w.elevation.index_of(16, 16);
    eye.flow = 100;
    w.eyes.push_back(eye);

    make_land_types(w);

    CHECK_EQ(w.eye_purity.at(eye.cell), 160u);
}

TEST(land_types_are_deterministic)
{
    const World a = build_world(3, 8914);
    const World b = build_world(3, 8914);
    bool same = true;
    for (std::uint32_t i = 0; i < a.land_type.size(); ++i)
    {
        same = same && a.land_type.at(i) == b.land_type.at(i);
        same = same && a.eye_purity.at(i) == b.eye_purity.at(i);
    }
    CHECK(same);
}

/// 纯度的三个常量：探测半径 8、瘴域每格扣 8、阴地每格扣 4。
///
/// 这三个数写在计划的交付物清单里，但此前的测试一条都依赖不到它们——
/// 复审造的四个变异体（半径 8→4、8→20，瘴域幅度 60→61，阴地 40→41）
/// 全部全绿漏网。
///
/// 期望值怎么来的：瘴域/阴地的判定半径是 6，毒属性地脉产生的瘴域块是
/// 13x13 格；灵眼在 (32,32)，纯度窗口是 17x17（x ∈ [24,40]）。
/// 毒属性地脉放在 (46,32)，瘴域块是 x ∈ [40,52]，与窗口只交出 x = 40
/// 这一列 —— 13 格。于是纯度 = 200 - 13 x 8 = 96，阴地 200 - 13 x 4 = 148。
///
/// 半径若改成 4，窗口够不着那一列 ⇒ 纯度 200；改成 20，整块 13x13 都
/// 进窗口 ⇒ 169 格 ⇒ 扣穿到 0。幅度若改成 9 或 5，13 格分别扣到 83 与
/// 135。四个变异体因此全部变红。
TEST(eye_purity_pins_the_probe_radius_and_the_penalties)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    // 一、五行脉的灵眼：基线纯度 200。
    SpiritVein vein;
    vein.id = 0;
    vein.element = Element::metal;
    SpiritEye eye;
    eye.vein = 0;
    eye.cell = 0;
    eye.flow = 100;

    const auto build = [&p, &vein, &eye](Element around) {
        World w(p);
        w.veins.push_back(vein);
        SpiritEye e = eye;
        e.cell = w.elevation.index_of(32, 32);
        w.eyes.push_back(e);
        // 只在 (46,32) 放一格属性地脉：它自己变成瘴域/阴地，并在半径 6
        // 内带出一整块 13x13 的同属性地貌。
        w.ley_element.at(w.elevation.index_of(46, 32)) = around;
        make_land_types(w);
        return w;
    };

    const World miasma = build(Element::poison);
    CHECK_EQ(miasma.eye_purity.at(miasma.eyes[0].cell), 96u);

    const World yin = build(Element::dark);
    CHECK_EQ(yin.eye_purity.at(yin.eyes[0].cell), 148u);
}

/// 同流量的活眼之间必须有确定的次序。
///
/// 「挑哪一只」直接决定浊域落在哪里。不定次序，选点就落在 std::sort 的
/// 内部实现上——换一个标准库，同一个 seed 会给出不同的浊域疤。真实世界
/// 里同流量的活眼很常见（21 个世界每个都有 8~13 组并列，涉及 32~39 只
/// 眼），所以这条规格必须写死。
///
/// 合成：42 只活眼，眼 20 与眼 21 的流量并列最高，其余更低 ⇒ 浊域必须
/// 落在编号小的那只（眼 20）上。这一组是控制者实测挑出来的：比较器只比
/// 流量、没有全序 tiebreak 的现实现会挑到眼 21，改成「同流量按编号升序」
/// 之后才落到眼 20。
///
/// 眼位排成 7 列、间距 8：42 只眼铺满 6 行，全部落在 64x64 之内。
TEST(turbid_picks_the_lowest_indexed_eye_when_the_flows_tie)
{
    WorldParams p;
    p.width = 64;
    p.height = 64;

    World w(p);
    constexpr std::uint32_t kEyeCount = 42;
    for (std::uint32_t e = 0; e < kEyeCount; ++e)
    {
        const std::uint32_t x = 2u + (e % 7u) * 8u;
        const std::uint32_t y = 2u + (e / 7u) * 8u;
        const std::uint32_t cell = w.elevation.index_of(x, y);
        // 全部临水：浊域的候选是「临水的活眼」，不临水就进不了候选表。
        w.lake_depth.at(cell) = 1;
        SpiritEye eye;
        eye.vein = 0;
        eye.node = 0;
        eye.cell = cell;
        eye.flow = 1000u;
        eye.depleted = false;
        w.eyes.push_back(eye);
    }
    // 眼 20 与眼 21 并列最高：现实现挑到眼 21，规格要眼 20。
    w.eyes[20].flow = 9000u;
    w.eyes[21].flow = 9000u;

    make_historical_scars(w);

    std::uint32_t turbid_count = 0;
    std::uint32_t first_target = 0;
    for (const ScarSite& site : w.scars)
    {
        if (site.kind != Scar::turbid)
        {
            continue;
        }
        if (turbid_count == 0)
        {
            first_target = site.target;
        }
        ++turbid_count;
    }
    CHECK(turbid_count >= 1u);
    CHECK_EQ(first_target, 20u);
}

}  // namespace

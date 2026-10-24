#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 沉陷疤的处数。设定 4.2 的枯墟是「三百里地陷」，折算成格数是调参项。
constexpr std::uint32_t kSinkholeCount = 3;
/// 沉陷疤的半径，格。
constexpr std::uint32_t kSinkholeRadius = 24;
/// 沉陷疤要求的「地势低平」：高程须低于海平面加这么多。
constexpr std::uint32_t kSinkholeMaxRelief = 20;
/// 渡劫废墟的处数。高阶修士才渡得起劫，疤因此小而多。
constexpr std::uint32_t kTribulationCount = 6;
/// 浊域的处数。沸海式的灵潮搅动区。
constexpr std::uint32_t kTurbidCount = 2;
/// 浊域的半径，格。两处浊域要求中心距不低于两半径之和，半径直接决定
/// 第二个浊域放不放得下。
constexpr std::uint32_t kTurbidRadius = 14;
/// 枯眼周边的半径。
constexpr std::uint32_t kDepletedEyeRadius = 10;

/// 各疤在中心的灵气压制量。死地之所以是死地，是因为那里的灵气真的
/// 没有了：压制是数据，不是标签。
constexpr std::uint8_t kSuppressionSinkhole = 220;
constexpr std::uint8_t kSuppressionTribulation = 180;
constexpr std::uint8_t kSuppressionDepletedEye = 200;
/// 浊域不压制灵气：它是浓度高但混乱，与「灵气枯竭」是两回事。
constexpr std::uint8_t kSuppressionTurbid = 0;

/// 判瘴域与阴地时的地脉属性探测半径，格。取方邻域（切比雪夫距离），
/// 边界行为明确。
constexpr std::uint32_t kLandProbeRadius = 6;

/// 自由候选的选法。
enum class ScarPick
{
    uniform,  ///< 在自由候选里均匀随机抽
    greedy,   ///< 取自由候选里的第一个
};

/// 两处疤的圆盘是否相交。中心距小于半径之和即相交——重叠会让同一格
/// 挂上两种疤，压制量也会互相覆盖。
///
/// 同类疤的相交不算冲突（`same_kind_ok`）：两只枯眼挨得近时，它们
/// 各自的死地环都要落下。压制量因此由 stamp_scar 取格上已有值与本次
/// 值的较大者，而不是让后落的一处盖掉前一处。
[[nodiscard]] bool overlaps_any(const std::vector<ScarSite>& placed,
                                std::uint32_t cell, std::uint32_t radius,
                                const WorldParams& p, Scar kind,
                                bool same_kind_ok) noexcept
{
    const std::int64_t cx = static_cast<std::int64_t>(cell % p.width);
    const std::int64_t cy = static_cast<std::int64_t>(cell / p.width);
    for (const ScarSite& s : placed)
    {
        if (same_kind_ok && s.kind == kind)
        {
            continue;
        }
        const std::int64_t dx =
            cx - static_cast<std::int64_t>(s.cell % p.width);
        const std::int64_t dy =
            cy - static_cast<std::int64_t>(s.cell / p.width);
        const std::int64_t gap = static_cast<std::int64_t>(radius) + s.radius;
        if (dx * dx + dy * dy < gap * gap)
        {
            return true;
        }
    }
    return false;
}

/// 把一处疤盖到网格上。
///
/// 压制量在半径处正好落到 0，因此只标记严格在半径内的格：疤的格必有
/// 压制量，「有疤即有压制」这条不变式才立得住。
///
/// 压制量取格上已有值与本次值的**较大者**：同类疤可以重叠（两只枯眼
/// 挨得近时两处死地环都落下），叠放处若按后落的一处取值，远离该处中心
/// 的压制会被削掉一截。
void stamp_scar(World& world, const ScarSite& site,
                std::uint8_t centre_suppression)
{
    const WorldParams& p = world.params();
    const int cx = static_cast<int>(site.cell % p.width);
    const int cy = static_cast<int>(site.cell / p.width);
    const int r = static_cast<int>(site.radius);
    const double radius = static_cast<double>(r);
    for (int dy = -r; dy <= r; ++dy)
    {
        const int ny = cy + dy;
        if (ny < 0 || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int nx = cx + dx;
            if (nx < 0 || nx >= static_cast<int>(p.width))
            {
                continue;
            }
            const double dist = std::sqrt(static_cast<double>(dx) * dx
                                         + static_cast<double>(dy) * dy);
            if (dist >= radius)
            {
                continue;
            }
            const std::uint32_t i = world.elevation.index_of(
                static_cast<std::uint32_t>(nx),
                static_cast<std::uint32_t>(ny));
            world.scar_kind.at(i) = site.kind;
            const double scaled = static_cast<double>(centre_suppression)
                                  * (radius - dist) / radius;
            const long rounded = std::lround(scaled);
            const auto suppression = static_cast<std::uint8_t>(
                rounded < 0L ? 0L : (rounded > 255L ? 255L : rounded));
            if (suppression > world.spirit_suppression.at(i))
            {
                world.spirit_suppression.at(i) = suppression;
            }
        }
    }
}

/// 眼格或八邻域里有湖或海。
[[nodiscard]] bool near_water(const World& world,
                              std::uint32_t cell) noexcept
{
    const WorldParams& p = world.params();
    const int cx = static_cast<int>(cell % p.width);
    const int cy = static_cast<int>(cell / p.width);
    for (int dy = -1; dy <= 1; ++dy)
    {
        const int ny = cy + dy;
        if (ny < 0 || ny >= static_cast<int>(p.height))
        {
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int nx = cx + dx;
            if (nx < 0 || nx >= static_cast<int>(p.width))
            {
                continue;
            }
            const auto ux = static_cast<std::uint32_t>(nx);
            const auto uy = static_cast<std::uint32_t>(ny);
            const std::uint32_t i = world.elevation.index_of(ux, uy);
            if (world.lake_depth.at(i) > 0 || world.is_ocean(ux, uy))
            {
                return true;
            }
        }
    }
    return false;
}

/// 活灵眼的下标，按流量降序。
///
/// 不截断：临水的活眼只占活眼的一小半（实测 42 只活眼里只有 2~20 只
/// 临水），先按流量取前一半等于把大半候选凭空扔掉，很多世界的浊域候选
/// 会直接归零。
[[nodiscard]] std::vector<std::uint32_t> live_eyes_by_flow(const World& world)
{
    std::vector<std::uint32_t> live;
    for (std::uint32_t e = 0; e < world.eyes.size(); ++e)
    {
        const SpiritEye& eye = world.eyes[e];
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        live.push_back(e);
    }
    // 同流量按下标升序：流量相等时眼与眼没有区别，但「挑哪一只」会
    // 决定浊域落在哪里。不定一个次序，选点就落在 std::sort 的内部实现
    // 上——换一个标准库，同一个 seed 会给出不同的浊域落点。
    std::ranges::sort(live, [&world](std::uint32_t a, std::uint32_t b) {
        if (world.eyes[a].flow != world.eyes[b].flow)
        {
            return world.eyes[a].flow > world.eyes[b].flow;
        }
        return a < b;
    });
    return live;
}

/// 在候选格里落下若干处疤。
///
/// 两阶段：先把与已放圆盘不相交的候选筛成自由候选，再在自由候选里选。
/// 抽中冲突就丢弃等于把「这个位置放不下」当成「这个候选不存在」，凭空
/// 消耗候选名额。
///
/// `ScarPick::greedy` 取自由候选里的第一个：候选表按流量降序排好时，
/// 第一处疤就落在流量最大的候选上。rng 的消耗序列完全由候选表决定，与
/// 地形无关，同一个 seed 必然抽出同一串结果。
void place_scars(World& world, Pcg32& rng, Scar kind, std::uint32_t count,
                 std::uint32_t radius, std::uint8_t suppression,
                 const std::vector<std::uint32_t>& cells,
                 const std::vector<std::uint32_t>& targets, ScarPick pick)
{
    std::uint32_t placed = 0;
    while (placed < count && !cells.empty())
    {
        std::vector<std::uint32_t> free_slots;
        free_slots.reserve(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            if (!overlaps_any(world.scars, cells[i], radius, world.params(),
                              kind, false))
            {
                free_slots.push_back(static_cast<std::uint32_t>(i));
            }
        }
        if (free_slots.empty())
        {
            break;
        }
        const std::uint32_t slot = pick == ScarPick::greedy
            ? free_slots.front()
            : free_slots[rng.below(static_cast<std::uint32_t>(free_slots.size()))];
        const ScarSite site{kind, cells[slot], radius, targets[slot]};
        stamp_scar(world, site, suppression);
        world.scars.push_back(site);
        ++placed;
    }
}

/// 灵眼所在灵脉的地脉属性。灵眼自己没有存属性，去它所属的灵脉上取。
[[nodiscard]] Element vein_element(const World& world,
                                   const SpiritEye& eye) noexcept
{
    if (eye.vein >= world.veins.size())
    {
        return Element::none;
    }
    return world.veins[eye.vein].element;
}

}  // namespace

void make_historical_scars(World& world)
{
    const WorldParams& p = world.params();
    const std::uint32_t n = world.scar_kind.size();

    world.scars.clear();
    world.scar_kind = Grid<Scar>(p.width, p.height, Scar::none);
    world.spirit_suppression = Grid<std::uint8_t>(p.width, p.height, 0);

    Pcg32 rng(stage_seed(p.seed, static_cast<std::uint32_t>(Stream::scar)));

    // 灵脉 id -> 该脉覆盖的格。一次扫描建表，免得每条脉再扫一遍全图。
    std::vector<std::vector<std::uint32_t>> vein_cells(world.veins.size());
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const std::uint32_t id = world.vein_id.at(i);
            if (id == kNoVein || id >= world.veins.size())
            {
                continue;
            }
            vein_cells[id].push_back(i);
        }
    }

    // ── 枯眼疤 ──
    // 枯眼周边的死地是抽干的必然后果，不是一次选择，因此先放：它不该
    // 被后面三类疤挤掉。
    //
    // 同类枯眼疤允许重叠：两只枯眼挨得近时，两处死地环都要落下。早先
    // 的写法把「与已放圆盘相交」一律当成放不下，于是后一只枯眼整处疤
    // 都被丢掉——实测 90 个 256x256 世界里有 10.4% 的枯眼拿不到疤，
    // 周边既不成死地也没有压制，「福地塌成死地」这条设定前提因此凭空
    // 少了几千格。
    for (std::uint32_t e = 0; e < world.eyes.size(); ++e)
    {
        const SpiritEye& eye = world.eyes[e];
        if (!eye.depleted)
        {
            continue;
        }
        if (overlaps_any(world.scars, eye.cell, kDepletedEyeRadius, p,
                         Scar::depleted_eye, true))
        {
            continue;
        }
        const ScarSite site{Scar::depleted_eye, eye.cell, kDepletedEyeRadius, e};
        stamp_scar(world, site, kSuppressionDepletedEye);
        world.scars.push_back(site);
    }

    // ── 浊域 ──
    // 临水的活灵眼：沸海式的灵潮搅动区。候选不截断，按流量降序排好，
    // 于是第一处浊域落在流量最大的临水活眼上。
    //
    // 浊域排在沉陷与渡劫之前：沸海是当世现象（灵潮搅动），渡劫废墟与
    // 沉陷是历史残留，让当世现象给历史残留让路是顺序倒置。这样浊域只
    // 需避开枯眼疤——枯眼疤的落点完全确定、不消耗 rng——于是「这个世界
    // 有临水活眼」就等于「这个世界有浊域」，不再取决于 rng 的落点。
    std::vector<std::uint32_t> turbid_cells;
    std::vector<std::uint32_t> turbid_targets;
    for (const std::uint32_t e : live_eyes_by_flow(world))
    {
        if (!near_water(world, world.eyes[e].cell))
        {
            continue;
        }
        turbid_cells.push_back(world.eyes[e].cell);
        turbid_targets.push_back(e);
    }
    place_scars(world, rng, Scar::turbid, kTurbidCount, kTurbidRadius,
                kSuppressionTurbid, turbid_cells, turbid_targets,
                ScarPick::greedy);

    // ── 沉陷疤 ──
    // 触发条件：被越采的灵脉 + 地势低平。「被采」那一步在阶段 13，本阶段
    // 只负责「塌」的那一半——因果方向是先采后塌。
    const int sea_limit = static_cast<int>(world.sea_level())
                          + static_cast<int>(kSinkholeMaxRelief);
    std::vector<std::uint32_t> sinkhole_cells;
    std::vector<std::uint32_t> sinkhole_targets;
    for (std::uint32_t v = 0; v < world.veins.size(); ++v)
    {
        const SpiritVein& vein = world.veins[v];
        if (!vein.overmined || vein.id >= vein_cells.size())
        {
            continue;
        }
        for (const std::uint32_t i : vein_cells[vein.id])
        {
            if (static_cast<int>(world.elevation.at(i)) >= sea_limit)
            {
                continue;
            }
            sinkhole_cells.push_back(i);
            sinkhole_targets.push_back(vein.id);
        }
    }
    place_scars(world, rng, Scar::sinkhole, kSinkholeCount, kSinkholeRadius,
                kSuppressionSinkhole, sinkhole_cells, sinkhole_targets,
                ScarPick::uniform);

    // ── 渡劫废墟 ──
    // 厚域与福地才渡得起劫。目标填该格所属的灵脉，没有所属脉则填
    // kNoVein：结构体默认值 0 是有效的第 0 条脉下标，不能用它表示「无」。
    std::vector<std::uint32_t> tribulation_cells;
    std::vector<std::uint32_t> tribulation_targets;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        const DomainGrade grade = world.domain_grade.at(i);
        if (grade != DomainGrade::thick && grade != DomainGrade::blessed)
        {
            continue;
        }
        tribulation_cells.push_back(i);
        tribulation_targets.push_back(world.vein_id.at(i));
    }
    place_scars(world, rng, Scar::tribulation, kTribulationCount,
                kTribulationRadius, kSuppressionTribulation,
                tribulation_cells, tribulation_targets, ScarPick::uniform);
}

void make_land_types(World& world)
{
    const WorldParams& p = world.params();
    world.land_type = Grid<LandType>(p.width, p.height, LandType::mundane);
    // n 是纯度循环的越界闸门，必须取重建后的尺寸：放在重建之前等于依赖
    // World 构造函数恰好预分配了 land_type，那是个隐式约定。
    const std::uint32_t n = world.land_type.size();
    world.eye_purity = Grid<std::uint8_t>(p.width, p.height, 0);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const Scar scar = world.scar_kind.at(i);

            // 规则 1：死地压过一切。死地是「灵气枯竭，寸草不生」，它是一
            // 个否定，否定压过任何正面属性。
            if (is_dead_scar(scar))
            {
                world.land_type.at(i) = LandType::dead;
                continue;
            }
            // 规则 2：浊域优先于福地。它是高浓度但坏掉，与福地性质相反。
            if (scar == Scar::turbid)
            {
                world.land_type.at(i) = LandType::turbid;
                continue;
            }
            // 规则 3：福地。
            if (world.domain_grade.at(i) == DomainGrade::blessed)
            {
                world.land_type.at(i) = LandType::blessed;
                continue;
            }
            // 规则 4、5：瘴域与阴地看地脉属性，不看浓度——一片浓度很高的
            // 毒脉区是瘴域，不是福地。
            bool poison = false;
            bool dark = false;
            for (int dy = -static_cast<int>(kLandProbeRadius);
                 dy <= static_cast<int>(kLandProbeRadius); ++dy)
            {
                const int ny = static_cast<int>(y) + dy;
                if (ny < 0 || ny >= static_cast<int>(p.height))
                {
                    continue;
                }
                for (int dx = -static_cast<int>(kLandProbeRadius);
                     dx <= static_cast<int>(kLandProbeRadius); ++dx)
                {
                    const int nx = static_cast<int>(x) + dx;
                    if (nx < 0 || nx >= static_cast<int>(p.width))
                    {
                        continue;
                    }
                    const std::uint32_t j = world.elevation.index_of(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny));
                    const Element e = world.ley_element.at(j);
                    poison = poison || e == Element::poison;
                    dark = dark || e == Element::dark;
                }
            }
            if (poison)
            {
                world.land_type.at(i) = LandType::miasma;
            }
            else if (dark)
            {
                world.land_type.at(i) = LandType::yin;
            }
            else
            {
                world.land_type.at(i) = LandType::mundane;
            }
        }
    }

    // 灵眼纯度：基线纯度扣掉周边地貌的污染。
    //
    // 纯度必须等周边环境定下来之后才算，所以它落在本阶段而不是阶段 13。
    for (const SpiritEye& eye : world.eyes)
    {
        if (eye.cell >= n)
        {
            continue;
        }
        const Element element = vein_element(world, eye);
        // 查不到属性（Element::none）与「查到了是异质」必须分开：
        // 混成同一个基线，日后查不到脉时没人会发现纯度悄悄变了。
        int purity = kPurityBaseHetero;
        if (is_fivefold(element))
        {
            purity = kPurityBaseFivefold;
        }
        else if (element == Element::none)
        {
            purity = kPurityBaseUnknown;
        }
        const int cx = static_cast<int>(eye.cell % p.width);
        const int cy = static_cast<int>(eye.cell / p.width);
        for (int dy = -static_cast<int>(kPurityProbeRadius);
             dy <= static_cast<int>(kPurityProbeRadius); ++dy)
        {
            const int ny = cy + dy;
            if (ny < 0 || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            for (int dx = -static_cast<int>(kPurityProbeRadius);
                 dx <= static_cast<int>(kPurityProbeRadius); ++dx)
            {
                const int nx = cx + dx;
                if (nx < 0 || nx >= static_cast<int>(p.width))
                {
                    continue;
                }
                const std::uint32_t j = world.elevation.index_of(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny));
                const LandType land = world.land_type.at(j);
                if (land == LandType::miasma)
                {
                    purity -= kPurityMiasmaPenalty;
                }
                else if (land == LandType::yin)
                {
                    purity -= kPurityYinPenalty;
                }
                // 浊域不扣纯度：设定 4.1 说沸海是「天下最纯的原料产地」。
                // 那里灵气被搅动而不是被污染，纯得不能用。把它当污染
                // 扣减是常见的误读，后来者会当成 bug 修掉。
            }
        }
        if (purity < 0)
        {
            purity = 0;
        }
        if (purity > 255)
        {
            purity = 255;
        }
        world.eye_purity.at(eye.cell) = static_cast<std::uint8_t>(purity);
    }
}

}  // namespace gamev2::world

#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace {

using gamev2::world::DomainGrade;
using gamev2::world::SpiritEye;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::domain_grade_of;
using gamev2::world::kReferenceFlow;
using gamev2::world::kThinThreshold;
using gamev2::world::make_spirit_domains;

WorldParams params(std::uint64_t seed, std::uint32_t year)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.seed = seed;
    p.spirit_year = year;
    // 阶段 14 只读地表量与灵眼，z_levels 取 1 让这个用例跑得快些。
    p.z_levels = 1;
    return p;
}

/// 阶段 14 的前置状态。
///
/// 地脉网络与灵脉灵眼是阶段 12/13 的产出，阶段 14 只读它们。这里显式按
/// 依赖顺序调用各阶段，而不是用 generate_world()——那条线要到编排任务
/// 才接上。
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
    make_spirit_domains(w);
    return w;
}

std::uint64_t total_concentration(const World& w)
{
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        sum += w.domain_concentration.at(i);
    }
    return sum;
}

std::uint32_t count_grade(const World& w, DomainGrade grade)
{
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < w.domain_grade.size(); ++i)
    {
        if (w.domain_grade.at(i) == grade)
        {
            ++n;
        }
    }
    return n;
}

/// 福地格的四邻接连通块数。标定回归要数它：福地块数对应「几处福地」，
/// 面积只说得出「多大」。
std::uint32_t blessed_components(const World& w)
{
    const auto width = static_cast<std::size_t>(w.domain_concentration.width());
    const auto height = static_cast<std::size_t>(w.domain_concentration.height());
    std::vector<bool> seen(w.domain_concentration.size(), false);
    std::uint32_t components = 0;
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            const std::size_t start = y * width + x;
            if (seen[start]
                || w.domain_grade.at(static_cast<std::uint32_t>(start))
                       != DomainGrade::blessed)
            {
                continue;
            }
            ++components;
            std::deque<std::size_t> queue{start};
            seen[start] = true;
            while (!queue.empty())
            {
                const std::size_t cell = queue.front();
                queue.pop_front();
                const std::size_t cx = cell % width;
                const std::size_t cy = cell / width;
                const std::size_t neighbours[4] = {
                    cy > 0 ? cell - width : cell,
                    cy + 1 < height ? cell + width : cell,
                    cx > 0 ? cell - 1 : cell,
                    cx + 1 < width ? cell + 1 : cell};
                for (const std::size_t n : neighbours)
                {
                    if (n == cell || seen[n])
                    {
                        continue;
                    }
                    if (w.domain_grade.at(static_cast<std::uint32_t>(n))
                        != DomainGrade::blessed)
                    {
                        continue;
                    }
                    seen[n] = true;
                    queue.push_back(n);
                }
            }
        }
    }
    return components;
}

/// 浓度场的分布快照。百分比是面积占比，连通块数是绝对值。
struct Calibration
{
    double barren = 0.0;
    double thin = 0.0;
    double medium = 0.0;
    double thick = 0.0;
    double blessed = 0.0;
    double saturated = 0.0;
    double components = 0.0;

    void add(const Calibration& other)
    {
        barren += other.barren;
        thin += other.thin;
        medium += other.medium;
        thick += other.thick;
        blessed += other.blessed;
        saturated += other.saturated;
        components += other.components;
    }

    void average(std::uint32_t count)
    {
        const auto k = 1.0 / static_cast<double>(count);
        barren *= k;
        thin *= k;
        medium *= k;
        thick *= k;
        blessed *= k;
        saturated *= k;
        components *= k;
    }
};

Calibration measure(const World& w)
{
    const double total = static_cast<double>(w.domain_concentration.size());
    Calibration c;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        const std::uint8_t v = w.domain_concentration.at(i);
        if (v == 255u)
        {
            c.saturated += 1.0;
        }
        switch (w.domain_grade.at(i))
        {
        case DomainGrade::barren: c.barren += 1.0; break;
        case DomainGrade::thin: c.thin += 1.0; break;
        case DomainGrade::medium: c.medium += 1.0; break;
        case DomainGrade::thick: c.thick += 1.0; break;
        case DomainGrade::blessed: c.blessed += 1.0; break;
        }
    }
    c.barren = c.barren * 100.0 / total;
    c.thin = c.thin * 100.0 / total;
    c.medium = c.medium * 100.0 / total;
    c.thick = c.thick * 100.0 / total;
    c.blessed = c.blessed * 100.0 / total;
    c.saturated = c.saturated * 100.0 / total;
    c.components = static_cast<double>(blessed_components(w));
    return c;
}

TEST(domain_concentration_is_the_eye_decay_field)
{
    // 合成一只眼：流量等于参考流量，于是眼心的贡献正好是满格浓度。
    // 灵历 300 是元潮的第三百年，潮位 255，眼心浓度就应当正好是潮位。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 300;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK_EQ(w.spirit_tide.level, 255u);

    SpiritEye eye;
    eye.cell = w.elevation.index_of(64, 64);
    eye.flow = kReferenceFlow;
    eye.depleted = false;
    w.eyes.push_back(eye);

    make_spirit_domains(w);

    CHECK_EQ(w.domain_concentration.at(eye.cell), w.spirit_tide.level);
    CHECK(w.domain_grade.at(eye.cell) == DomainGrade::blessed);

    // 指数衰减：离眼越远，浓度越低。
    const std::uint32_t one = w.elevation.index_of(65, 64);
    const std::uint32_t ten = w.elevation.index_of(74, 64);
    CHECK(w.domain_concentration.at(one)
          < w.domain_concentration.at(eye.cell));
    CHECK(w.domain_concentration.at(ten)
          < w.domain_concentration.at(one));

    // 盖章半径之外的格没有贡献。
    CHECK_EQ(w.domain_concentration.at(w.elevation.index_of(0, 0)), 0u);
}

TEST(domain_grade_always_agrees_with_the_concentration)
{
    const World w = build_world(7, 9300);
    bool agrees = true;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        agrees = agrees
                 && w.domain_grade.at(i)
                    == domain_grade_of(w.domain_concentration.at(i));
    }
    CHECK(agrees);
}

TEST(domain_grade_is_monotone_in_concentration)
{
    // 纯函数层面：浓度更高处，分等不低于更低处。
    DomainGrade prev = DomainGrade::barren;
    for (std::uint32_t c = 0; c <= 255u; ++c)
    {
        const auto g = domain_grade_of(static_cast<std::uint8_t>(c));
        CHECK(static_cast<std::size_t>(g) >= static_cast<std::size_t>(prev));
        prev = g;
    }
}

TEST(every_live_eye_sits_in_a_concentrated_cell)
{
    // 潮位 ≥ 71 时，每一个活眼都落在薄域以上——不取「最强的一批」。
    //
    // 谷底期（潮位 33）这条不成立：弱眼自己的贡献只有 flow × 33 / 2000，
    // flow ≤ 400 时是 5~7，够不到薄域门槛 8。见
    // a_weak_eye_falls_below_the_thin_threshold_in_the_trough。
    // 潮位 99 时最弱的活眼（流量 200）自己就有 9，于是这条是标定给的，
    // 不是碰巧。
    const World w = build_world(7, 9300);
    CHECK(w.spirit_tide.level >= 71u);

    std::uint32_t live = 0;
    bool floor_holds = true;
    bool all_in_thin = true;
    std::uint32_t weakest_flow = 0xFFFFFFFFu;
    for (const SpiritEye& eye : w.eyes)
    {
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        ++live;
        const std::uint8_t c = w.domain_concentration.at(eye.cell);
        // 浓度下限由公式给出：眼心至少是 flow * 潮位 / 参考流量。
        const std::uint32_t floor_c =
            static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
            / kReferenceFlow;
        floor_holds = floor_holds && c >= floor_c;
        all_in_thin = all_in_thin && c > 0u && c >= kThinThreshold;
        weakest_flow = std::min(weakest_flow, eye.flow);
    }
    CHECK(live > 0u);
    CHECK(floor_holds);
    CHECK(all_in_thin);

    // 上游契约检查（阶段 13 的标定，寄居在这里）：潮位 99 时一只眼要
    // 自己越过薄域门槛需要 flow ≥ 162，最弱的活眼也得够这个数，否则
    // 上面那条就得靠邻居凑。
    const std::uint32_t needed =
        (static_cast<std::uint32_t>(kThinThreshold) * kReferenceFlow
         + w.spirit_tide.level - 1u)
        / w.spirit_tide.level;
    CHECK(weakest_flow >= needed);
}

TEST(every_live_eye_crosses_the_thin_threshold_at_the_tide_peak)
{
    // 设计文档 §6 第 22 条：每个非枯眼灵眼格的浓度 > 0 且 ≥ 薄域阈值，
    // 潮位 ≥ 71 时成立。
    //
    // 谷底期的潮位只有灵潮期的三分之一，最弱的一只眼自己的贡献只有
    // flow × 33 / 2000，flow ≤ 400 时只有 5~7，够不到薄域门槛 8。
    // 所以这条不变式在潮位 ≥ 71 时成立，在谷底期不成立——这不是实现的
    // 缺陷，是「灵脉半死」的读法。设计 §6 第 22 条已按此改写。
    const World w = build_world(7, 9300);
    CHECK_EQ(w.spirit_tide.level, 99u);

    std::uint32_t live = 0;
    bool all_above = true;
    bool field_follows_flow = true;
    std::uint32_t weakest_flow = 0xFFFFFFFFu;
    for (const SpiritEye& eye : w.eyes)
    {
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        ++live;
        const std::uint8_t c = w.domain_concentration.at(eye.cell);
        all_above = all_above && c > 0u && c >= kThinThreshold;
        // 眼心浓度至少是这只眼自己的贡献 flow × 潮位 / 参考流量。
        // 光写「≥ 薄域门槛」的话，任意一份够大的浓度场都能蒙混过关；
        // 这条把浓度场钉回「它是灵眼的函数」这条链路上。
        const std::uint32_t own =
            static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
            / kReferenceFlow;
        field_follows_flow = field_follows_flow && c >= own;
        weakest_flow = std::min(weakest_flow, eye.flow);
    }
    CHECK(live > 0u);
    CHECK(all_above);
    CHECK(field_follows_flow);

    // 上面的不变式不是靠邻居凑出来的：最弱的一只活眼，它自己的贡献
    // flow × 潮位 / 参考流量 就足以越过薄域门槛。潮位 99 时这需要
    // flow ≥ 162，实测最弱的一只有 200，于是这条下限本身就站得住。
    const std::uint64_t own =
        static_cast<std::uint64_t>(weakest_flow) * w.spirit_tide.level
        / kReferenceFlow;
    CHECK(own >= kThinThreshold);
}

TEST(every_live_eye_stays_positive_at_every_tide)
{
    // 潮位只改读法，不改地脉网络，于是两条逐眼不变量在所有潮位都成立：
    // 眼心浓度 > 0，且 ≥ 这只眼自己的那份贡献 flow × 潮位 / 参考流量。
    //
    // 「≥ 薄域门槛」是潮位 ≥ 71 才有的额外性质，谷底期弱眼够不到
    // （见 a_weak_eye_falls_below_the_thin_threshold_in_the_trough）。
    for (const std::uint32_t year : {9300u, 8914u})
    {
        const World w = build_world(7, year);

        std::uint32_t live = 0;
        bool all_positive = true;
        bool field_follows_flow = true;
        for (const SpiritEye& eye : w.eyes)
        {
            if (eye.depleted || eye.flow == 0)
            {
                continue;
            }
            ++live;
            const std::uint8_t c = w.domain_concentration.at(eye.cell);
            all_positive = all_positive && c > 0u;
            const std::uint32_t own =
                static_cast<std::uint32_t>(eye.flow) * w.spirit_tide.level
                / kReferenceFlow;
            field_follows_flow = field_follows_flow && c >= own;
        }
        CHECK(live > 0u);
        CHECK(all_positive);
        CHECK(field_follows_flow);
    }
}

TEST(a_weak_eye_falls_below_the_thin_threshold_in_the_trough)
{
    // 谷底期（潮位 33）一只孤立的弱眼：眼心浓度 = 300 × 33 / 2000
    // = 4.95 → 5，够不到薄域门槛 8。
    //
    // 把「谷底期弱眼够不到薄域门槛」写成规格，而不是让它靠运气成立：
    // 真实世界里确实有活眼跌破门槛（实测 256² 每世界至多 2 只），
    // 但那是眼与眼之间够不着，不是实现漏算了这只眼。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 8914;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK_EQ(w.spirit_tide.level, 33u);

    SpiritEye eye;
    eye.cell = w.elevation.index_of(64, 64);
    eye.flow = 300;
    eye.depleted = false;
    w.eyes.push_back(eye);

    make_spirit_domains(w);

    const std::uint8_t c = w.domain_concentration.at(eye.cell);
    CHECK(c > 0u);
    CHECK(c < kThinThreshold);
    CHECK(c >= 4u);
    CHECK(c <= 7u);
}

TEST(a_world_without_eyes_is_all_barren)
{
    // 灵脉数量凑不齐不是生成失败（设计文档 §5），极端参数下可能一条灵脉
    // 都没有。阶段 14 对这种世界必须有定义：浓度全 0，分等全废域。
    WorldParams p;
    p.width = 128;
    p.height = 128;
    p.spirit_year = 9300;

    World w(p);
    gamev2::world::make_spirit_tide(w);
    CHECK(w.eyes.empty());
    make_spirit_domains(w);

    bool all_zero = true;
    bool all_barren = true;
    for (std::uint32_t i = 0; i < w.domain_concentration.size(); ++i)
    {
        all_zero = all_zero && w.domain_concentration.at(i) == 0u;
        all_barren = all_barren
                     && w.domain_grade.at(i) == DomainGrade::barren;
    }
    CHECK(all_zero);
    CHECK(all_barren);
}

TEST(the_tide_level_gives_one_network_two_readings)
{
    const World trough = build_world(11, 8914);
    const World peak = build_world(11, 9300);
    CHECK_EQ(trough.spirit_tide.level, 33u);
    CHECK_EQ(peak.spirit_tide.level, 99u);

    // 地脉网络必须逐字节相同：潮位只改读法，不改网络。
    bool same_strength = true;
    for (std::uint32_t i = 0; i < trough.ley_strength.size(); ++i)
    {
        same_strength = same_strength
                        && trough.ley_strength.at(i)
                               == peak.ley_strength.at(i);
    }
    CHECK(same_strength);
    CHECK_EQ(trough.ley_nodes.size(), peak.ley_nodes.size());

    bool same_nodes = true;
    for (std::size_t i = 0; i < trough.ley_nodes.size(); ++i)
    {
        same_nodes = same_nodes
                     && trough.ley_nodes[i].cell == peak.ley_nodes[i].cell;
        same_nodes = same_nodes
                     && trough.ley_nodes[i].flow == peak.ley_nodes[i].flow;
        same_nodes = same_nodes
                     && trough.ley_nodes[i].element == peak.ley_nodes[i].element;
    }
    CHECK(same_nodes);

    // 浓度总量随潮位上升：潮位是乘子，逐格单调不减。
    const std::uint64_t trough_mass = total_concentration(trough);
    const std::uint64_t peak_mass = total_concentration(peak);
    CHECK(peak_mass >= trough_mass);
    CHECK(peak_mass > trough_mass);

    // 福地面积只增不减：浓度逐格单调，阈值只可能被向上跨越。
    CHECK(count_grade(peak, DomainGrade::blessed)
          >= count_grade(trough, DomainGrade::blessed));
}

TEST(the_tide_level_separates_the_blessed_area)
{
    // 合成一只大流量的眼：谷底期眼心落在厚域，灵潮期落在福地。
    // 同一张网、同一只眼，两个潮位读出的福地面积必然不同。
    WorldParams p;
    p.width = 128;
    p.height = 128;

    p.spirit_year = 8914;
    World trough(p);
    gamev2::world::make_spirit_tide(trough);
    SpiritEye eye;
    eye.cell = trough.elevation.index_of(64, 64);
    eye.flow = 2u * kReferenceFlow;
    trough.eyes.push_back(eye);
    make_spirit_domains(trough);

    p.spirit_year = 9300;
    World peak(p);
    gamev2::world::make_spirit_tide(peak);
    peak.eyes.push_back(eye);
    make_spirit_domains(peak);

    CHECK_EQ(count_grade(trough, DomainGrade::blessed), 0u);
    CHECK(count_grade(peak, DomainGrade::blessed) > 0u);
}

TEST(domains_are_deterministic)
{
    const World a = build_world(5, 8914);
    const World b = build_world(5, 8914);
    bool same = true;
    for (std::uint32_t i = 0; i < a.domain_concentration.size(); ++i)
    {
        same = same
               && a.domain_concentration.at(i) == b.domain_concentration.at(i);
        same = same && a.domain_grade.at(i) == b.domain_grade.at(i);
    }
    CHECK(same);
}

TEST(the_concentration_field_keeps_its_calibration)
{
    // 标定回归：这几条数值锁住阶段 14 的分布形状。
    // 没有它们，一次「忠实于简报」的实现就能把福地糊成 21%~36%，
    // 而所有旧用例照样全绿。
    //
    // 断言的是**跨种子平均**：福地面积取决于眼落在哪儿，单个种子落在
    // 区间外是常事，要锁的是形状而不是某一个种子的巧合。
    constexpr std::uint32_t kSeeds = 5;
    Calibration peak;
    Calibration trough;
    for (std::uint32_t i = 0; i < kSeeds; ++i)
    {
        peak.add(measure(build_world(i + 1u, 9300)));
        trough.add(measure(build_world(i + 1u, 8914)));
    }
    peak.average(kSeeds);
    trough.average(kSeeds);

    // 灵潮期末（潮位 99）的分布形状：
    //
    // **废域过半是设定要的**：天下灵气本就稀薄，凡人是这个世界的主体
    // （设定 2.4 / 7.3）。这一条同时是**背景高原回归的哨兵**——尾巴
    // 留到 3λ 时废域会塌到 32%~38%，半径放回 24 时塌到 38%~41%，
    // 而正确标定下它稳定在 57%~62%。实测 7 组不同的 5~6 种子集，
    // 正确态与两个回归态在这一条上完全不重叠。
    CHECK(peak.barren >= 45.0);
    // 薄/中两档都得有量，任何一档塌掉都说明阈值阶梯切歪了。
    CHECK(peak.thin >= 3.0);
    CHECK(peak.medium >= 3.0);
    // 厚域是第二条哨针：尾巴留到 3λ 或半径放回 24 时它会涨到 15%~21%
    // （背景高原的顶），正确标定下只有 8.6%~12.2%。
    CHECK(peak.thick >= 3.0);
    CHECK(peak.thick <= 14.0);
    // 福地是唯一一档要「少」的：红线 8 说稀缺性不为剧情让路，设定里天下
    // 只有几处福地，铺开两成就不是「几处」。
    //
    // 下界只能取 1.0%：福地面积对种子集极其敏感。实测 7 组不同的 5~6
    // 种子集，256² 灵潮期末的福地占比在 1.383%（种子 101..105）到
    // 4.384%（种子 201..205）之间摆动，3.2 倍的差距——断言若按「五档
    // 都 >= 3%」写，就只对某一批种子成立，换一批种子便假红。
    // 「几处福地」这个语义由下面的**连通块数**承载，它比面积稳得多
    // （实测 7.6~9.4）。
    CHECK(peak.blessed >= 1.0);
    CHECK(peak.blessed <= 6.0);
    // 饱和说明浓度场被削平成一片白，是标定崩掉的信号。
    CHECK(peak.saturated < 1.0);
    // 连通块数对应「几处福地」：九主脉一龙脉，一脉一处。
    CHECK(peak.components >= 5.0);
    CHECK(peak.components <= 15.0);

    // 谷底期（潮位 33）没有福地：设定说福地与洞天是随灵潮出现并扩张的
    // 东西，「灵脉半死」的读法里没有洞天。
    CHECK_EQ(trough.blessed, 0.0);
    CHECK_EQ(trough.components, 0.0);
}

}  // namespace

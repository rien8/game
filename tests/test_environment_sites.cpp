#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/resource.hpp"
#include "gamev2/environment/sites.hpp"

#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using gamev2::environment::BreedingSite;
using gamev2::environment::CellIndex;
using gamev2::environment::ClimateField;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::Forager;
using gamev2::environment::ResourceField;
using gamev2::environment::build_sites;
using gamev2::environment::run_season;

namespace {

/// 把一批个体全部塞进同一格附近，方便构造「拥挤」与「空旷」两种极端。
/// 位置在格内抖动，避免所有人落在同一个浮点坐标上。
std::vector<Forager> crowd(std::size_t n, float demand)
{
    std::vector<Forager> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto jitter = static_cast<float>(i % 60) * 0.01f;
        out.push_back(Forager{1.0f + jitter, 1.0f, demand});
    }
    return out;
}

/// 顺序敏感夹具：全部个体挤在同一格内，但 demand 随下标递增，
/// 从 0.25 跨到 10.0，因此既有未饱和（want < share）也有饱和个体。
/// 异质是必要的：同质 demand 下个体之间无从区分，反转输入后无法判断
/// 拿到结果的到底是不是同一个体。
std::vector<Forager> ramp_crowd(std::size_t n)
{
    std::vector<Forager> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto jitter = static_cast<float>(i % 60) * 0.01f;
        const auto demand = 0.25f * static_cast<float>(i + 1);
        out.push_back(Forager{1.0f + jitter, 1.0f, demand});
    }
    return out;
}

/// 把 n 个个体塞进指定格，供「先读出 capacity 再反推放几个」的夹具使用。
/// demand 恒为 1：只有 build_sites 用得到这个夹具，而它不读 demand。
std::vector<Forager> crowd_in(const CellIndex cell, const std::size_t n)
{
    const auto x =
        static_cast<float>(cell.value % gamev2::environment::kGridSide);
    const auto y =
        static_cast<float>(cell.value / gamev2::environment::kGridSide);
    std::vector<Forager> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto jitter = static_cast<float>(i % 60) * 0.01f;
        out.push_back(Forager{x + jitter, y, 1.0f});
    }
    return out;
}

/// 本夹具下唯一无法严格相等的量：第二趟的 per_cell 是同格 n 个 got 的浮点
/// 累加，反转输入即反转加数顺序，末位会有舍入差。累加和的上界是 capacity
/// 量级，而 capacity = capacity_base × base_fertility = 3.0 × [0.25, 1.0)，
/// 故上界是 3.0（本配置实测全 4096 格为 [0.750, 2.999]）。
/// 顺序求和的严格误差界是 (n−1)·u·Σ|xᵢ|（u = 2⁻²⁴ ≈ 5.96e-8），
/// 40 项取 Σ ≈ 3.0 得 ≈ 7e-6；feeding 是 [0,1] 上的比值，传播后远小于此。
/// 取 1e-5 贴着这个界，而不是随手挑一个「看起来够大」的值。
///
/// 这个界随同格个体数 n 线性增长：capacity = 3.0 时 n ≈ 57 就突破 1e-5。
/// 1e-5 因此不是普适容差，换夹具须重推。
///
/// 一旦 run_season 改成按与输入无关的规范序累加 per_cell，这个界会塌缩到 0，
/// 届时应当把断言收回严格相等。
constexpr float kOrderEpsilon = 1e-5f;

std::vector<BreedingSite> sites_of(
    const EnvironmentConfig& config, const std::vector<Forager>& foragers,
    const std::vector<std::uint32_t>& keys)
{
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    field.count_occupants(foragers);
    return build_sites(field, climate, foragers, keys, config);
}

std::uint32_t progeny_of(const std::vector<BreedingSite>& sites,
                         const std::uint32_t species_key)
{
    std::uint32_t total = 0;
    for (const auto& s : sites)
    {
        if (s.species_key == species_key)
        {
            total += s.progeny;
        }
    }
    return total;
}

}  // namespace

TEST(feeding_is_one_when_supply_exceeds_demand)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    // 一个个体在近乎空旷的格子里，存量足够它随便吃。
    const auto foragers = crowd(1, 0.1f);
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    CHECK_EQ(feeding.size(), std::size_t{1});
    if (feeding.empty())
    {
        return;
    }
    CHECK_EQ(feeding[0], 1.0f);
}

TEST(feeding_drops_when_crowding_exceeds_capacity)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    // 个体数远超该格承载力，且每个都很能吃。
    const auto foragers = crowd(60, 5.0f);
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    if (feeding.empty())
    {
        return;
    }
    CHECK(feeding[0] < 1.0f);
    CHECK(feeding[0] > 0.0f);
}

TEST(feeding_is_independent_of_forager_order)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    const auto foragers = ramp_crowd(40);

    ResourceField a{climate, config};
    a.count_occupants(foragers);
    const auto first = run_season(a, foragers, config);

    auto shuffled = foragers;
    std::ranges::reverse(shuffled);
    ResourceField b{climate, config};
    b.count_occupants(shuffled);
    const auto second = run_season(b, shuffled, config);

    // 反转输入后，第 i 个个体在新列表里的位置是 n-1-i。直接比 first[i] 与
    // second[i] 是拿两个不同个体互比，只有全体同质时才成立。
    //
    // 这条只守顺序不变性，守不住「两趟是否分开」：got/want 在饱和时等于
    // 1/(rate×demand)、未饱和时等于 1，两种情况都与 share 无关，所以
    // 合并两趟后 feeding 精确算术下相同、浮点下至多相差一个 ulp，
    // 且与遍历顺序无关。分开那一条由下一条测试守。
    // CHECK 只记失败、不中止；长度不符时继续按下标取会越界，把整轮带崩。
    CHECK_EQ(first.size(), second.size());
    if (first.size() != second.size())
    {
        return;
    }
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        const auto mirrored = second[first.size() - 1 - i];
        CHECK(std::abs(first[i] - mirrored) <= kOrderEpsilon);
    }
    for (std::size_t i = 0; i < gamev2::environment::kCellCount; ++i)
    {
        // CellIndex::value 是 std::uint16_t，聚合初始化不隐式收窄：
        // 直接传 std::size_t 在 -Wc++11-narrowing 下是编译错误，不是告警。
        const auto index =
            CellIndex{static_cast<std::uint16_t>(i)};
        CHECK(std::abs(a.stock(index) - b.stock(index)) <= kOrderEpsilon);
    }
}

TEST(two_pass_feeding_uses_pre_draw_stock)
{
    EnvironmentConfig config{};
    // 必须是两步。一步时满格上 regrow 是精确空操作
    // （stock + 0.25 × (capacity − stock) 括号内恰为 0），于是把
    // run_season 里的 field.regrow() 整行删掉，这条测试照样绿——
    // regrow 唯一能被观测到的地方就是第二步开头那次非空回归。
    config.steps_per_season = 2;
    const ClimateField climate{5, config};
    ResourceField field{climate, config};

    // 闭式解的前提：夹具全员未饱和，否则 got 会走 share 分支而 Σ 公式失效。
    CHECK(config.demand_rate * 2.0f <= 1.0f);

    // 全体未饱和（demand_rate × demand ≤ 0.7 < 1）：want < share，got 取 want。
    // 于是「先算后扣」每步的扣减量有闭式解 存量 × rate × Σdemand / n，
    // 与遍历顺序无关。边算边扣则让后面的个体读到已被扣过的存量，
    // 扣减量必然小于此式——这才是两趟必须分开的可观测后果。
    //
    // 注：feeding 向量抓不到这一条。got/want 在饱和时等于 1/(rate×demand)、
    // 在未饱和时等于 1，两种情况都与 share 无关，所以合并两趟后 feeding
    // 精确算术下相同、浮点下至多相差一个 ulp，且与遍历顺序无关。
    // 只有绝对扣减量（进而终态存量）会变。
    std::vector<Forager> foragers;
    constexpr std::size_t n = 40;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto jitter = static_cast<float>(i % 60) * 0.01f;
        const auto demand = 0.05f * static_cast<float>(i + 1);
        foragers.push_back(Forager{1.0f + jitter, 1.0f, demand});
    }
    field.count_occupants(foragers);

    // 夹具全员未饱和，got 取 want，故每步 ratio 恰为 1，两步后 feeding 恰为 1。
    const auto feeding = run_season(field, foragers, config);
    for (const float value : feeding)
    {
        CHECK_EQ(value, 1.0f);
    }

    const auto cell = CellIndex::of(1.0f, 1.0f);
    const float capacity = field.capacity(cell);
    // Σdemand = 0.05 × (1+…+40) = 41。每步的扣减量 = 该步开头存量 × 0.35875。
    const float rate = config.demand_rate * 41.0f / 40.0f;
    // 第一步：满格上 regrow 是空操作，扣减后剩 64.125% capacity。
    const float s1 = capacity - capacity * rate;
    // 第二步开头的 regrow 把它抬到 73.09%，扣减后再剩其中 64.125%。
    const float s2 = s1 + config.regen_rate * (capacity - s1);
    const float expected = s2 - s2 * rate;
    // 两步的终态都远高于 0（0.641 / 0.731 / 0.469 × capacity），不触发
    // draw 的夹零，也不越出 [0, capacity]。
    //
    // 边算边扣会让存量多留约 0.0575 × capacity ≈ 0.14，是 1e-4 的上千倍；
    // 删掉 regrow 则偏差同量级。二者都被这一条挡住。
    CHECK(std::abs(field.stock(cell) - expected) <= 1e-4f);
}

TEST(zero_demand_forager_is_never_food_limited)
{
    const EnvironmentConfig config{};
    const ClimateField climate{5, config};
    ResourceField field{climate, config};
    const std::vector<Forager> foragers{Forager{1.0f, 1.0f, 0.0f}};
    field.count_occupants(foragers);

    const auto feeding = run_season(field, foragers, config);

    // 什么都不需要的个体不该被饿死。
    if (feeding.empty())
    {
        return;
    }
    CHECK_EQ(feeding[0], 1.0f);
}

TEST(run_season_returns_one_entry_per_forager)
{
    const EnvironmentConfig config{};
    const ClimateField climate{6, config};
    ResourceField field{climate, config};
    const auto foragers = crowd(17, 1.0f);
    field.count_occupants(foragers);

    CHECK_EQ(run_season(field, foragers, config).size(), foragers.size());
}

TEST(sites_are_empty_when_no_creatures)
{
    const auto sites = sites_of(EnvironmentConfig{}, {}, {});
    CHECK(sites.empty());
}

TEST(over_capacity_cell_emits_no_site)
{
    const EnvironmentConfig config{};
    // capacity_base = 3，base_fertility <= 1，单格容量不超过 3 个体。
    // 塞 20 个进去必然过载，headroom 归零。
    auto foragers = crowd(20, 1.0f);
    std::vector<std::uint32_t> keys(foragers.size(), 1);

    const auto sites = sites_of(config, foragers, keys);

    // 不发站点，而不是发一个 progeny = 0 的站点：settle 把
    // ParentPool::progeny == 0 按 1 处理，0 预算送进去会凭空生出一个后代，
    // 承载力封顶因此失效。
    CHECK(sites.empty());
}

TEST(progeny_of_a_lone_creature_is_capped_by_offspring_scale)
{
    EnvironmentConfig config{};
    // 默认 capacity_base = 3 时本格不够发站点：cell(1,1) 的
    // base_fertility 实测 0.399 ⇒ capacity = 1.197，减去 1 个占位者只剩
    // headroom = 0.197，round 归零 ⇒ build_sites 不发站点。
    // 下面的 CHECK 只记失败、不中止，接着 sites[0] 就是对空 vector 取下标，
    // 崩溃点在这里而不是在被测代码里——不同种子的 base_fertility 不同，
    // 于是这条测试在不同种子下时绿时崩。提到 40（同
    // each_site_carries_its_cell_weather 的推导），使 1 个占位者的 headroom
    // 恒 >= 6，站点在任意种子下必发。
    config.capacity_base = 40.0f;
    // offspring_scale 取 2.5 而不是默认的 2.0：这里要同时守繁殖上限与
    // lround 的取整方向。默认 2.0 时 here × offspring_scale 恒为偶整数，
    // lround 与 floor 结果相同，「上限取整」这一族根本没被覆盖。
    // 2.5 下 lround(2.5) = 3 而 floor(2.5) = 2，两者差 1。
    config.offspring_scale = 2.5f;
    const std::vector<Forager> foragers{Forager{1.0f, 1.0f, 1.0f}};
    const auto sites = sites_of(config, foragers, {1});

    CHECK_EQ(sites.size(), std::size_t{1});
    if (sites.empty())
    {
        // 长度不符时上面已经记过失败；继续下标会越界，把整轮测试带崩。
        return;
    }
    // 繁殖上限：独苗不能产出一满窝后代，那会是一次纯自交。
    const auto cap =
        static_cast<std::uint32_t>(std::lround(config.offspring_scale));
    CHECK(sites[0].progeny <= cap);
    CHECK(sites[0].progeny > 0);
    // 生态上限远高于繁殖上限（headroom ∈ [9, 39) 对 3），所以 progeny 恰好
    // 是 parent_ceiling = lround(1 × 2.5)：这一条把取整方向钉死为 lround，
    // 换成 floor 会拿到 2。
    CHECK_EQ(sites[0].progeny, cap);
}

TEST(capacity_is_split_between_species_by_current_share)
{
    EnvironmentConfig config{};
    // capacity = capacity_base × base_fertility，而 base_fertility ∈ [0.25, 1)。
    // 取 40：两轮夹具都要在任意 f 下成立（下界 f = 0.25 最不利），推导写在
    // 各自处。24 是原始 4 占位者夹具的下限，40 留余量。
    config.capacity_base = 40.0f;
    const ClimateField climate{5, config};

    // 第一轮：3 个 A + 1 个 B，守的是繁殖上限。
    // capacity ∈ [10, 40)，headroom = capacity − 4 ∈ [6, 36)，
    // eco_ceiling(A) = round(headroom × 3/4) ∈ [5, 27) 远高于
    // parent_ceiling = round(3 × 2) = 6，所以这里 parent_ceiling 是紧的：
    // 去掉 min(eco, parent) 会让 A 拿到 20+，被 <= 6u 挡住。
    {
        std::vector<Forager> foragers;
        std::vector<std::uint32_t> keys;
        for (int i = 0; i < 3; ++i)
        {
            foragers.push_back(
                Forager{1.0f + static_cast<float>(i) * 0.01f, 1.0f, 1.0f});
            keys.push_back(1);
        }
        foragers.push_back(Forager{1.5f, 1.0f, 1.0f});
        keys.push_back(2);

        const auto sites = sites_of(config, foragers, keys);

        CHECK_EQ(sites.size(), std::size_t{2});
        CHECK(progeny_of(sites, 1) <= 6u);
        CHECK(progeny_of(sites, 2) <= 2u);
    }

    // 第二轮：把格占到 headroom ∈ [1, 2)，此时 share 才可观测。
    // 上一轮的断言守不住 share——A、B 都被 parent_ceiling 封到 6 与 2，
    // `A > B` 于是恒成立，与 share 无关，share 被整段抹掉照样全绿。
    //
    // 现在 headroom ∈ [1, 2)：少数派 B（here = 1，share = 1/n，n = 全体占位者）
    // 拿到 round(headroom / n) ≤ round(2/9) = 0，必然不发站点；
    // 多数派 A（here = n−1）拿到 round(headroom × (n−1)/n) ∈ [1, 2)，
    // 必然发站点。于是站点数恰好 1，这个数只由 share 决定：
    //     share 恒为 1（all = here）⇒ B 拿到 round(headroom) >= 1 ⇒ 2 个站点
    //     抹掉 share（乘 headroom）  ⇒ B 同样拿到 round(headroom) >= 1
    ResourceField field{climate, config};
    const auto cell = CellIndex::of(1.0f, 1.0f);
    const auto capacity = field.capacity(cell);
    // n = floor(capacity) − 1 ⇒ headroom = capacity − n = 小数部分 + 1 ∈ [1, 2)。
    const auto n = static_cast<std::size_t>(std::floor(capacity)) - 1;
    // 上面的下界依赖 n >= 9：capacity ∈ [10, 40) ⇒ floor(capacity) >= 10。
    // n 低于 9 时 headroom / n 可能越过 0.5，B 就会发站点，断言随之失效。
    CHECK(n >= 9);
    const auto packed = crowd_in(cell, n);
    std::vector<std::uint32_t> packed_keys(n - 1, 1);
    packed_keys.push_back(2);
    field.count_occupants(packed);

    const auto sites =
        build_sites(field, climate, packed, packed_keys, config);

    // headroom ∈ [1,2) 时少数派的 share × headroom < 0.5，必然 round 到 0
    // 而不发站点；share 若被抹掉它会拿到 round(headroom) = 1，站点变成 2 个。
    CHECK_EQ(sites.size(), std::size_t{1});
    if (sites.empty())
    {
        return;
    }
    // 发站点的是多数派 A。
    CHECK_EQ(sites[0].species_key, std::uint32_t{1});
}

TEST(sites_are_identical_under_forager_permutation)
{
    const EnvironmentConfig config{};
    std::vector<Forager> foragers;
    std::vector<std::uint32_t> keys;
    for (int i = 0; i < 12; ++i)
    {
        foragers.push_back(
            Forager{static_cast<float>(i % 4), static_cast<float>(i / 4), 1.0f});
        keys.push_back(static_cast<std::uint32_t>(1 + i % 3));
    }
    const auto straight = sites_of(config, foragers, keys);

    std::vector<Forager> flipped = foragers;
    std::ranges::reverse(flipped);
    const std::vector<std::uint32_t> flipped_keys(keys.rbegin(), keys.rend());
    const auto shuffled = sites_of(config, flipped, flipped_keys);

    CHECK_EQ(straight.size(), shuffled.size());
    if (straight.size() != shuffled.size())
    {
        // 长度不符时上面已经记过失败；下面的按下标并排比较会越界。
        return;
    }
    for (std::size_t i = 0; i < straight.size(); ++i)
    {
        CHECK_EQ(straight[i].cell.value, shuffled[i].cell.value);
        CHECK_EQ(straight[i].species_key, shuffled[i].species_key);
        CHECK_EQ(straight[i].progeny, shuffled[i].progeny);
    }
}

TEST(each_site_carries_its_cell_weather)
{
    EnvironmentConfig config{};
    config.temperature_amplitude = 0.0f;
    // 默认 capacity_base = 3 时本格不够发站点：cell(7,9) 的
    // base_fertility = 0.399 ⇒ capacity = 1.197，减去 1 个占位者只剩
    // headroom = 0.197，round 归零 ⇒ build_sites 不发站点，这条测试无从断言。
    // 提到 40 留出余量（同 capacity_is_split_between_species_by_current_share
    // 的推导），使 1 个占位者的 headroom 恒 ≥ 6，站点必发。
    config.capacity_base = 40.0f;
    const ClimateField climate{5, config};
    ResourceField field{climate, config};

    const std::vector<Forager> foragers{Forager{7.0f, 9.0f, 1.0f}};
    field.count_occupants(foragers);
    const auto sites = build_sites(field, climate, foragers,
                                   std::vector<std::uint32_t>{1}, config);

    CHECK_EQ(sites.size(), std::size_t{1});
    if (sites.empty())
    {
        // 长度不符时上面已经记过失败；继续下标会越界，把整轮测试带崩。
        return;
    }
    const auto expected = climate.at(CellIndex::of(7.0f, 9.0f));
    CHECK_EQ(sites[0].environment.fertility, expected.fertility);
    CHECK_EQ(sites[0].environment.temperature, expected.temperature);
    CHECK_EQ(sites[0].environment.light, expected.light);
}



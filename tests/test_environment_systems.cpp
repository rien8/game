#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/genetics_systems.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/scheduler.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using gamev2::ecs::CommandBuffer;
using gamev2::ecs::EntityId;
using gamev2::ecs::Scheduler;
using gamev2::ecs::World;
using gamev2::ecs::adapt::AgingSystem;
using gamev2::ecs::adapt::BreedingSystem;
using gamev2::ecs::adapt::BreedingTarget;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::EnvironmentSystem;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::Feeding;
using gamev2::ecs::adapt::PopulationStatisticsSystem;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::genetics::GeneticsConfig;
using gamev2::genetics::LocusKind;
using gamev2::genetics::LocusTable;
using gamev2::genetics::Phenotype;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::SpeciesRegistry;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kLocusCount;
using gamev2::genetics::trait_index;

namespace {

/// 每格一个个体：occupants 恒为 1，人均可得就是整格存量，于是
/// 「同一批个体只改存量」这件事在夹具层面严格成立。
constexpr std::size_t kOnePerCell = 200;

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 造一张只签发了 id 1 的登记表。EnvironmentSystem 与 BreedingSystem 的
/// 物种守卫都以它为准，因此每条用到两者的测试都必须经它取登记表。
SpeciesRegistry one_species()
{
    SpeciesRegistry r;
    (void)r.register_species(all_loci());
    return r;
}

EntityId place_creature(World& world, const float x, const float y,
                        const SpeciesId species)
{
    const auto e = world.create();
    (void)world.add<Position>(e, Position{x, y});
    (void)world.add<SpeciesTag>(e, SpeciesTag{species});

    // SenseRange 与 Fecundity 各自 0.5，因此 demand 恒为 0.25。
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::SenseRange)] = 0.5f;
    p.traits[trait_index(TraitKind::Fecundity)] = 0.5f;
    (void)world.add<CreaturePhenotype>(e, CreaturePhenotype{p});
    return e;
}

/// 随机撒一批个体，用于「分布不集中」的观察。
void seed_creatures(World& world, const std::size_t count,
                    const std::uint64_t seed)
{
    std::mt19937_64 rng{seed};
    std::uniform_real_distribution<float> pos{0.0f, 63.9f};

    for (std::size_t i = 0; i < count; ++i)
    {
        (void)place_creature(world, pos(rng), pos(rng), SpeciesId{1});
    }
}

/// 每个个体独占一格：x = (i % 64) + 0.5 截断后恰好落回 (i % 64, i / 64)。
void seed_one_per_cell(World& world, const std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto x = static_cast<float>(i % kGridSide) + 0.5f;
        const auto y = static_cast<float>(i / kGridSide) + 0.5f;
        (void)place_creature(world, x, y, SpeciesId{1});
    }
}

std::vector<float> collect_feeding(World& world)
{
    std::vector<float> out;
    world.for_each_entity([&](const EntityId e)
    {
        const auto* f = world.get_if<Feeding>(e);
        if (f != nullptr)
        {
            out.push_back(f->satisfaction);
        }
    });
    return out;
}

bool has_feeding(World& world, const EntityId e)
{
    return world.get_if<Feeding>(e) != nullptr;
}

std::uint64_t total_progeny(const std::vector<BreedingTarget>& targets)
{
    std::uint64_t budget = 0;
    for (const auto& t : targets)
    {
        budget += t.progeny;
    }
    return budget;
}

LocusTable neutral_table()
{
    LocusTable t{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        t.loci[i].kind = LocusKind::Trait;
        t.loci[i].allele_count = 2;
        t.loci[i].scale = 1.0f;
        t.loci[i].switch_rate = 0.0f;
        t.loci[i].alleles[1].dom_rank = 1;
    }
    return t;
}

GeneticsConfig default_genetics()
{
    GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;
    return c;
}

}  // namespace

TEST(environment_system_writes_feeding_for_every_creature)
{
    World world;
    seed_creatures(world, 200, 1);

    auto species = one_species();
    EnvironmentWorld env{31, EnvironmentConfig{}};
    EnvironmentSystem system{env, species, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    std::size_t seen = 0;
    world.for_each_entity([&](const EntityId e)
    {
        const auto* f = world.get_if<Feeding>(e);
        if (f == nullptr)
        {
            return;
        }
        ++seen;
        CHECK(f->satisfaction >= 0.0f);
        CHECK(f->satisfaction <= 1.0f);
    });
    CHECK_EQ(seen, std::size_t{200});
}

TEST(environment_system_emits_targets_for_every_cell_and_species)
{
    World world;
    seed_creatures(world, 50, 2);

    auto species = one_species();
    EnvironmentWorld env{32, EnvironmentConfig{}};
    EnvironmentSystem system{env, species, 0};

    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    const auto& targets = system.targets();
    CHECK(!targets.empty());
    for (const auto& t : targets)
    {
        CHECK_EQ(t.species.value, std::uint32_t{1});
        CHECK(t.progeny > 0);
    }
}

TEST(environment_system_is_deterministic)
{
    World a;
    seed_creatures(a, 120, 9);
    auto species = one_species();
    EnvironmentWorld env_a{77, EnvironmentConfig{}};
    EnvironmentSystem sys_a{env_a, species, 3};

    CommandBuffer cmd_a;
    sys_a.execute(a, cmd_a);
    cmd_a.flush(a);

    World b;
    seed_creatures(b, 120, 9);
    EnvironmentWorld env_b{77, EnvironmentConfig{}};
    EnvironmentSystem sys_b{env_b, species, 3};

    CommandBuffer cmd_b;
    sys_b.execute(b, cmd_b);
    cmd_b.flush(b);

    const auto& ta = sys_a.targets();
    const auto& tb = sys_b.targets();
    CHECK_EQ(ta.size(), tb.size());
    if (ta.size() != tb.size())
    {
        // CHECK_EQ 只记失败、不中止。下面按 ta.size() 循环却索引 tb，
        // 长度不等时就是越界读——崩在夹具里而不是被测代码里。
        return;
    }
    for (std::size_t i = 0; i < ta.size(); ++i)
    {
        CHECK_EQ(ta[i].cell.value, tb[i].cell.value);
        CHECK_EQ(ta[i].species.value, tb[i].species.value);
        CHECK_EQ(ta[i].progeny, tb[i].progeny);
        CHECK_EQ(ta[i].environment.temperature,
                 tb[i].environment.temperature);
        CHECK_EQ(ta[i].environment.fertility, tb[i].environment.fertility);
        CHECK_EQ(ta[i].environment.light, tb[i].environment.light);
    }
}

TEST(feeding_varies_with_available_stock)
{
    World world;
    seed_one_per_cell(world, kOnePerCell);
    auto species = one_species();

    // 只改存量：ClimateField 不读 capacity_base，同一 root_seed 下气候场逐位
    // 相同；occupants 与 demand 也相同。喂完再喂富格世界，两次跑的是同一批
    // 实体、同一批 want，差别只在 capacity 上。
    //
    // 这是设计文档登记的测试 5b：want 取绝对量后 ratio = share / want，
    // share 由存量决定，于是 feeding 必须随存量走。把写回改成恒 1.0f
    // 会让贫格那组断言变红。
    EnvironmentConfig scarce{};
    scarce.capacity_base = 0.001f;
    EnvironmentWorld poor{31, scarce};
    EnvironmentSystem poor_system{poor, species, 0};
    CommandBuffer poor_cmd;
    poor_system.execute(world, poor_cmd);
    poor_cmd.flush(world);
    const auto poor_feeding = collect_feeding(world);

    EnvironmentWorld rich{31, EnvironmentConfig{}};
    EnvironmentSystem rich_system{rich, species, 0};
    CommandBuffer rich_cmd;
    rich_system.execute(world, rich_cmd);
    rich_cmd.flush(world);
    const auto rich_feeding = collect_feeding(world);

    // 两个长度都要记失败：守卫在长度不符时直接 return，只记 poor 那一条会
    // 让 rich 长度错时整条用例零失败通过——CHECK_EQ 非致命，return 不检查。
    CHECK_EQ(poor_feeding.size(), kOnePerCell);
    CHECK_EQ(rich_feeding.size(), kOnePerCell);
    if (poor_feeding.size() != kOnePerCell
        || rich_feeding.size() != kOnePerCell)
    {
        // 长度不符时上面已记过失败；继续按下标取会越界。
        return;
    }

    for (std::size_t i = 0; i < kOnePerCell; ++i)
    {
        // 富格：capacity ∈ [0.975, 3.9)，人均一份远超 want = 0.35 × 0.25。
        CHECK_EQ(rich_feeding[i], 1.0f);
        // 贫格：capacity ∈ [0.00025, 0.001)，人均一份不足 want 的 1.2%。
        CHECK(poor_feeding[i] > 0.0f);
        CHECK(poor_feeding[i] < 0.05f);
        CHECK(poor_feeding[i] < rich_feeding[i]);
    }
}

TEST(environment_system_uses_the_config_given_to_environment_world)
{
    World world;
    seed_creatures(world, 200, 3);
    auto species = one_species();

    // 对照组：默认配置必须发得出站点，否则下面那条 empty() 是恒真。
    EnvironmentWorld plain{41, EnvironmentConfig{}};
    EnvironmentSystem plain_system{plain, species, 0};
    CommandBuffer plain_cmd;
    plain_system.execute(world, plain_cmd);
    plain_cmd.flush(world);
    CHECK(!plain_system.targets().empty());

    // 关掉繁殖上限：build_sites 的 parent_ceiling = lround(here × 0) = 0，
    // 于是每格都发不出站点。execute 若传的是默认构造的 config 而不是
    // 持有值，这里会有站点——与 Task 2 的 regen_rate 是同一个坑。
    EnvironmentConfig sterile = plain.config();
    sterile.offspring_scale = 0.0f;
    EnvironmentWorld barren{41, sterile};
    EnvironmentSystem barren_system{barren, species, 0};
    CommandBuffer barren_cmd;
    barren_system.execute(world, barren_cmd);
    barren_cmd.flush(world);
    CHECK(barren_system.targets().empty());

    // 摄食侧同理：demand_rate = 0 让每个 want 归零，于是全程 ratio = 0。
    EnvironmentConfig mute = plain.config();
    mute.demand_rate = 0.0f;
    EnvironmentWorld silent{41, mute};
    EnvironmentSystem silent_system{silent, species, 0};
    CommandBuffer silent_cmd;
    silent_system.execute(world, silent_cmd);
    silent_cmd.flush(world);
    for (const float value : collect_feeding(world))
    {
        CHECK_EQ(value, 0.0f);
    }
}

TEST(unregistered_species_do_not_participate)
{
    auto species = one_species();

    // 对照组与实验组跑同一批已登记个体，差别只有两个未登记 id。
    World control;
    seed_creatures(control, 200, 5);

    World polluted;
    seed_creatures(polluted, 200, 5);
    const auto reserved =
        place_creature(polluted, 3.5f, 3.5f, SpeciesId{0});
    const auto ghost =
        place_creature(polluted, 3.5f, 3.5f, SpeciesId{7});

    EnvironmentWorld env_a{43, EnvironmentConfig{}};
    EnvironmentSystem clean_system{env_a, species, 0};
    CommandBuffer clean_cmd;
    clean_system.execute(control, clean_cmd);
    clean_cmd.flush(control);

    EnvironmentWorld env_b{43, EnvironmentConfig{}};
    EnvironmentSystem polluted_system{env_b, species, 0};
    CommandBuffer polluted_cmd;
    polluted_system.execute(polluted, polluted_cmd);
    polluted_cmd.flush(polluted);

    // 未登记的 id 既不占位、也不吃存量、更不写 Feeding——否则它就是一个
    // 只消耗不繁殖的幽灵物种，而 BreedingSystem 会把它整个跳过。
    CHECK(!has_feeding(polluted, reserved));
    CHECK(!has_feeding(polluted, ghost));

    const auto& clean = clean_system.targets();
    const auto& dirty = polluted_system.targets();
    CHECK(!clean.empty());
    CHECK_EQ(dirty.size(), clean.size());
    if (dirty.size() != clean.size())
    {
        // CHECK_EQ 非致命，长度不等时继续按 clean.size() 索引 dirty 会越界。
        return;
    }
    for (std::size_t i = 0; i < clean.size(); ++i)
    {
        CHECK_EQ(dirty[i].cell.value, clean[i].cell.value);
        CHECK_EQ(dirty[i].species.value, clean[i].species.value);
        CHECK_EQ(dirty[i].progeny, clean[i].progeny);
    }
}

TEST(total_birth_budget_stays_under_total_capacity)
{
    EnvironmentConfig config{};
    // 占位率必须贴近 1 但不超过，headroom 才是紧的。capacity =
    // capacity_base × base_fertility 而 base_fertility ∈ [0.25, 1)，故
    // capacity_base = 40 时单格容量落在 [10, 40)，floor(C/2) >= 5。
    config.capacity_base = 40.0f;

    EnvironmentWorld env{64, config};
    auto species = one_species();

    // 逐格读出真实容量再决定放几个。夹具必须按承载力走：6000 个体撒在
    // 4096 格上时多数格 headroom 为 0，正确值（2440）与「headroom 误算成
    // capacity」的 mutant（6788）双双远在旧的 24576 阈值之下，阈值形同虚设。
    constexpr std::size_t kStride = 256;
    constexpr std::size_t kCells = 16;
    std::vector<Position> spots;
    double capacity_sum = 0.0;
    for (std::size_t c = 0; c < kCells; ++c)
    {
        const auto cell =
            CellIndex{static_cast<std::uint16_t>(c * kStride)};
        const float cap = env.resources().capacity(cell);
        capacity_sum += cap;
        const auto n = static_cast<std::size_t>(std::floor(cap / 2.0f));
        const auto x = static_cast<float>(cell.value % kGridSide);
        const auto y = static_cast<float>(cell.value / kGridSide);
        for (std::size_t i = 0; i < n; ++i)
        {
            spots.push_back(Position{x + 0.1f
                                         + static_cast<float>(i % 60) * 0.01f,
                                     y + 0.5f});
        }
    }
    // 每格至少 5 个占位者，于是 Σoccupants >= 80，远大于取整余量。
    CHECK(spots.size() >= 5u * kCells);

    World world;
    for (const auto& spot : spots)
    {
        (void)place_creature(world, spot.x, spot.y, SpeciesId{1});
    }

    EnvironmentSystem system{env, species, 0};
    CommandBuffer cmd;
    system.execute(world, cmd);
    cmd.flush(world);

    const auto budget = total_progeny(system.targets());

    // 出生数加上现有个体数不得超过总承载力——这是本模块存在的全部理由。
    // 余量 kCells/2 + 1 来自 build_sites 逐格 lround(headroom) 的取整误差
    // （<= 0.5/格）。这点余量比 Σoccupants 小一个数量级，因此把 headroom
    // 误算成 capacity 的 mutant 必然越界。
    const auto limit = static_cast<std::uint64_t>(capacity_sum)
                       + kCells / 2 + 1;
    CHECK(budget + static_cast<std::uint64_t>(spots.size()) <= limit);
}

TEST(environment_and_breeding_systems_are_separated_by_the_scheduler)
{
    // 全部内容就是 EnvironmentSystem::mutates_structure：它让本 system 独占
    // 一个批次。删掉那个标记，而 BreedingSystem::ReadSet 里的
    // `const Feeding` 又被改掉，batch_count() 会掉回 1。
    auto species = one_species();
    auto table = neutral_table();
    auto config = default_genetics();

    EnvironmentWorld env{51, EnvironmentConfig{}};
    EnvironmentSystem environment{env, species, 0};
    BreedingSystem breeding{table, species, config, 51, 0};

    Scheduler scheduler{2};
    scheduler.add(environment);
    scheduler.add(breeding);
    scheduler.plan();

    CHECK(scheduler.batch_count() >= 2);
}

TEST(adapt_systems_declare_their_execution_order)
{
    // 编译期钉住跨 system 的先后声明。批次归属无法从公开接口观测
    // （Scheduler 只暴露 batch_count，不暴露哪个 system 在哪一批），
    // 因此这里至少保证声明本身没被删。「order 真的驱动分层」由
    // test_scheduler.cpp 的执行序测试覆盖，「三个 adapt system 排成三批」
    // 由下面那条覆盖。
    static_assert(AgingSystem::order < BreedingSystem::order);
    static_assert(EnvironmentSystem::order < BreedingSystem::order);
    static_assert(EnvironmentSystem::mutates_structure);
}

TEST(adapt_systems_plan_into_three_singleton_batches)
{
    // 世界模块装配进调度器的正是这三个 system（PopulationStatisticsSystem
    // 刻意不进，见 doc/2026-10-16-world-design.md 第 2 节），而它们必须排成
    // 三个**单 system** 批：
    //
    // - EnvironmentSystem 带 mutates_structure，因此独占一批；
    // - AgingSystem 与 BreedingSystem 同写 Vitality，WriteSet 强制分层；
    // - BreedingSystem 的 order = 1 让它落进更晚的那一批。
    //
    // 「单 system」这一半才是关键：Scheduler::run 只在批内多于一个 system 时
    // 才向线程池派发，所以三个单 system 批意味着并行在这个装配下永不发生。
    //
    // 这条断言替掉了原先挂在 world_tests 上的 worker_count 用例。那条用例
    // 比较两个不同 worker 数的世界各跑三季、断言结果一致，但正因为并行在这
    // 个装配下永不发生，它恒绿、没有任何鉴别力。值得守的是「并行不发生」这个
    // 结构前提，而它只能从 Scheduler 的公开接口观测——放在这里才可能变红。
    auto species = one_species();
    auto table = neutral_table();
    auto config = default_genetics();

    EnvironmentWorld env{51, EnvironmentConfig{}};
    EnvironmentSystem environment{env, species, 0};
    AgingSystem aging;
    BreedingSystem breeding{table, species, config, 51, 0};

    Scheduler scheduler{2};
    scheduler.add(environment);
    scheduler.add(aging);
    scheduler.add(breeding);
    scheduler.plan();

    CHECK_EQ(scheduler.batch_count(), std::size_t{3});
}

TEST(environment_and_aging_systems_are_separated_by_the_scheduler)
{
    // 与上面那条同一个机制，但对手换成 AgingSystem：它的访问集是
    // CreaturePhenotype + Vitality，与 EnvironmentSystem 的
    // Position / CreaturePhenotype / SpeciesTag / Feeding 本来毫无交集，
    // Scheduler::plan 于是把二者放进同一批并行派发。而
    // EnvironmentSystem::execute 首次见到个体时走的是 World::add，即
    // archetype 迁移，会重写 slots_ 里的 archetype/chunk/row 并可能重分配
    // 列存储；AgingSystem 的 for_each_entity 是在 slots_ 上的无锁裸下标循环，
    // 两者并发撞上就是 use-after-free，而不只是读到过期值。
    //
    // 全部内容是 EnvironmentSystem::mutates_structure。AgingSystem::ReadSet
    // 里没有 `const Feeding` 占位声明（本 system 根本读不到它），访问集与
    // EnvironmentSystem 完全不相交；标记让后者独占一个批次，因此 batch_count()
    // 必 >= 2。
    auto species = one_species();

    EnvironmentWorld env{61, EnvironmentConfig{}};
    EnvironmentSystem environment{env, species, 0};
    AgingSystem aging;

    Scheduler scheduler{2};
    scheduler.add(aging);
    scheduler.add(environment);
    scheduler.plan();

    CHECK(scheduler.batch_count() >= 2);

    // 登记顺序反过来也必须分批：贪心分层逐条扫描登记序，分批结果不该依赖
    // 登记顺序，否则「谁先 add」就成了一条隐形的正确性前提。
    Scheduler reversed{2};
    reversed.add(environment);
    reversed.add(aging);
    reversed.plan();

    CHECK(reversed.batch_count() >= 2);
}

TEST(environment_and_statistics_systems_are_separated_by_the_scheduler)
{
    // 占位声明这条手工纪律漏掉了同一个文件里的第三个 system：
    // PopulationStatisticsSystem 的 ReadSet 是 {const Genome, const SpeciesTag}、
    // WriteSet 为空，与 EnvironmentSystem 唯一的重叠 SpeciesTag 双方都只读。
    // Scheduler::plan 的判据是「写 ∩ 写 || 写 ∩ 读 || 读 ∩ 写」，读/读不算
    // 冲突，于是二者被放进同一批并行派发。
    //
    // 撞上的后果不是读到过期值：EnvironmentSystem 首次见到个体时走
    // World::add，即 archetype 迁移，可能重分配 storage_ 的列缓冲并改写
    // slots_ 里的 archetype/chunk/row；PopulationStatisticsSystem 的
    // for_each_entity 是在同一张 slots_ 上的无锁裸下标循环。
    //
    // 这条用例守的是 mutates_structure：做结构变更的 system 与所有 system
    // 冲突，结果不得依赖登记顺序。
    auto species = one_species();

    EnvironmentWorld env{71, EnvironmentConfig{}};
    EnvironmentSystem environment{env, species, 0};
    PopulationStatisticsSystem statistics{species};

    Scheduler forward{2};
    forward.add(environment);
    forward.add(statistics);
    forward.plan();

    CHECK(forward.batch_count() >= 2);

    Scheduler reversed{2};
    reversed.add(statistics);
    reversed.add(environment);
    reversed.plan();

    CHECK(reversed.batch_count() >= 2);
}

TEST(advance_season_replaces_the_season_index)
{
    EnvironmentConfig config{};
    EnvironmentWorld a{64, config};
    EnvironmentWorld b{64, config};
    auto species = one_species();

    World wa;
    World wb;
    seed_one_per_cell(wa, 64);
    seed_one_per_cell(wb, 64);

    // 直接构造到季 3，与「构造到季 0 再推进到季 3」必须得到同一份气候。
    // 两者不一致就说明 advance_season 没写进真正被消费的那个成员。
    EnvironmentSystem direct{b, species, 3};
    EnvironmentSystem pushed{a, species, 0};
    pushed.advance_season(3);

    CommandBuffer cmd_a;
    CommandBuffer cmd_b;
    direct.execute(wa, cmd_a);
    cmd_a.flush(wa);
    pushed.execute(wb, cmd_b);
    cmd_b.flush(wb);

    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const CellIndex cell{static_cast<std::uint16_t>(c)};
        CHECK_EQ(a.climate().at(cell).temperature,
                 b.climate().at(cell).temperature);
    }
}

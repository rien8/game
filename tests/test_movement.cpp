#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/adapt/environment_systems.hpp"
#include "gamev2/ecs/adapt/movement_system.hpp"
#include "gamev2/ecs/command_buffer.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/environment/terrain.hpp"

#include "gamev2/genetics/phenotype.hpp"

#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include <vector>

namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::CommandBuffer;
using gamev2::ecs::World;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::EnvironmentWorld;
using gamev2::ecs::adapt::MovementSystem;
using gamev2::ecs::adapt::Position;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::environment::TerrainField;
using gamev2::genetics::Phenotype;
using gamev2::genetics::TraitKind;
using gamev2::genetics::trait_index;

[[nodiscard]] CellIndex cell_at(const std::uint16_t x, const std::uint16_t y)
{
    return CellIndex{static_cast<std::uint16_t>(y * kGridSide + x)};
}

/// 把 keep 判假的格全淹掉，只留下想要的陆块。夹具要摆的是「被水分隔的
/// 两块地」，逐格 flood 写出来读不懂它们互不相连这件事。
template <typename Keep>
void carve(TerrainField& terrain, Keep keep)
{
    for (std::uint16_t y = 0; y < kGridSide; ++y)
    {
        for (std::uint16_t x = 0; x < kGridSide; ++x)
        {
            if (!keep(x, y))
            {
                terrain.flood(cell_at(x, y));
            }
        }
    }
}

/// 只有 Speed 非默认，其余全给中性值——本测试只关心移动。
Phenotype mover(const float speed)
{
    Phenotype p{};
    p.traits.fill(1.0f);
    p.traits[trait_index(TraitKind::Speed)] = speed;
    return p;
}

/// 把一个个体放进指定坐标。缺组件会被 MovementSystem 跳过，因此六个里
/// 只需要 Position 与 CreaturePhenotype 两个。
EntityId place(World& world, const float x, const float y, const float speed)
{
    const EntityId e = world.create();
    (void)world.add<Position>(e, Position{x, y});
    (void)world.add<CreaturePhenotype>(e, CreaturePhenotype{mover(speed)});
    return e;
}

/// ecs::World 只有 get_if，没有 get。本文件造出来的个体都挂了 Position，
/// 因此这里不解引用 nullptr——测试若改到别处造个体，要先把这一点守住。
[[nodiscard]] const Position& at(World& world, const EntityId e)
{
    return *world.get_if<Position>(e);
}

/// 随机游走 20 季的平均位移，speed 固定。
[[nodiscard]] float mean_displacement(const float speed,
                                      const EnvironmentConfig& config,
                                      const std::uint64_t seed)
{
    EnvironmentWorld env{seed, config};
    World entities;
    const EntityId e = place(entities, 32.0f, 32.0f, speed);

    MovementSystem movement{seed, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
    CommandBuffer cmd;
    float total = 0.0f;
    for (std::uint32_t season = 0; season < 20; ++season)
    {
        movement.advance_season(season);
        const auto before = at(entities, e);
        const float bx = before.x;
        const float by = before.y;
        movement.execute(entities, cmd);
        const auto after = at(entities, e);
        total += std::hypot(after.x - bx, after.y - by);
    }
    cmd.flush(entities);
    return total / 20.0f;
}

}  // namespace

TEST(movement_is_deterministic_per_seed)
{
    // 逐位确定性契约。地形一旦引入就有可能把同一个种子的两次运行拉开，
    // 因此这条在重写当天就要立起来。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;  // 排除地形，只测随机流本身
    config.gradient_weight = 0.5f;

    const auto run = [&]()
    {
        EnvironmentWorld env{31, config};
        World entities;
        const std::vector<EntityId> ids{
            place(entities, 10.5f, 10.5f, 0.4f),
            place(entities, 32.5f, 20.5f, 1.5f),
            place(entities, 50.5f, 44.5f, 0.9f),
        };
        MovementSystem movement{31, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
        CommandBuffer cmd;
        for (std::uint32_t s = 0; s < 12; ++s)
        {
            movement.advance_season(s);
            movement.execute(entities, cmd);
        }
        std::vector<float> out;
        for (const auto e : ids)
        {
            const auto& p = at(entities, e);
            out.push_back(p.x);
            out.push_back(p.y);
        }
        return out;
    };

    const auto a = run();
    const auto b = run();
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK_EQ(a[i], b[i]);
    }
}

TEST(movement_never_leaves_a_creature_in_water)
{
    // 设计文档 §9 测试 2。地形是本阶段唯一的新障碍，这条是它的全部意义。
    EnvironmentConfig config{};  // 默认 water_fraction = 0.70
    EnvironmentWorld env{20261016, config};
    World entities;

    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < 40; ++i)
    {
        const auto e = place(entities, static_cast<float>(i % 64) + 0.5f,
                             static_cast<float>(i / 64) + 0.5f, 1.6f);
        ids.push_back(e);
    }

    MovementSystem movement{20261016, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
    CommandBuffer cmd;
    for (std::uint32_t s = 0; s < 50; ++s)
    {
        movement.advance_season(s);
        movement.execute(entities, cmd);
    }
    cmd.flush(entities);

    // 起点里本来就有落在水上的——那些必须被挤出水面，这是设计文档
    // §7.2 契约 3 的静默钳回。
    for (const auto e : ids)
    {
        const auto& p = at(entities, e);
        CHECK(env.terrain().passable(CellIndex::of(p.x, p.y)));
    }
}

TEST(movement_respects_the_speed_trait)
{
    // 设计文档 §9 测试 3，**可变异性守卫**：没有它，趋向写死了也全绿。
    //
    // 取值必须落在 **sim 实际产出的区间**内：`src/sim/main.cpp` 给 Speed 定的是
    // [0.3, 1.6]，而 `max(1, lround(clamp(speed, 0, 3)))` 在这个区间上只落得到
    // **1 或 2** 两档。曾经用 0.3 对 2.9 量出一个 1.5 倍的比值，但那测的是
    // 现实里根本不存在的 3 格步 —— 且钉不住取整方向：把 lround 换成 floor，
    // 0.3→1、2.9→2，比值 2.0 仍然 > 1.5，测试照样全绿；而在真实区间里
    // floor 会让几乎所有个体恒定 stride 1，Speed 完全失去可遗传的扩散力。
    //
    // 1.4 与 1.6 跨在 lround 的分界上：lround 下分别是 1 与 2，比值 2.0；
    // floor 下两者都是 1，比值 1.0 —— **那条把 floor 挡在外**。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    config.gradient_weight = 0.0f;  // 关掉趋向，只量步长本身

    const auto slow = mean_displacement(1.4f, config, 31);
    const auto fast = mean_displacement(1.6f, config, 31);

    CHECK(fast > slow * 1.5f);
}

TEST(the_gradient_direction_is_symmetric_when_no_neighbour_is_richer)
{
    // **这条是四向均匀性回归守卫。** 它对应本阶段实测到的一个真 bug：
    // `richest_neighbour` 的初值曾是 -1.0f，于是存量全同时 `0 > -1` 只对遍历序
    // 第一个可通行邻格成立，「梯度」退化成一条恒定向东的漂移 —— 实测 200 个
    // 种子的位移方向是 128 / 22 / 21 / 29，**64% 朝 +x**，与资源毫无关系。
    //
    // 那个 bug 读代码看不出来（`-1.0f` 看着完全正常），所以只能靠「四向是否
    // 均匀」这个不变量钉住它。其它任何一条测试都看不见它：修好前的读数
    // 1.2154 与修好后的 1.1379 都过 1.05 的下限。
    //
    // 夹具刻意选「全陆地 + 存量全 0」：四邻存量完全相同，于是**任何**梯度
    // 选择都只能是遍历序的产物，均匀是唯一正确的结果。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    config.gradient_weight = 1.0f;
    EnvironmentWorld env{31, config};
    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        (void)env.resources().draw(cell, env.resources().capacity(cell));
    }

    std::array<int, 4> by_dir{};  // 遍历序 +x / -x / +y / -y
    const auto dirs = std::array<std::pair<float, float>, 4>{
        std::pair<float, float>{1.0f, 0.0f},
        std::pair<float, float>{-1.0f, 0.0f},
        std::pair<float, float>{0.0f, 1.0f},
        std::pair<float, float>{0.0f, -1.0f},
    };
    constexpr int kSeeds = 80;
    for (int i = 0; i < kSeeds; ++i)
    {
        const auto seed = static_cast<std::uint64_t>(i + 1);
        World entities;
        const EntityId e = place(entities, 32.0f, 32.0f, 1.0f);
        MovementSystem movement{seed, 0, env.resources(), env.terrain(), 1.0f};
        CommandBuffer cmd;
        const auto before = at(entities, e);
        movement.execute(entities, cmd);
        cmd.flush(entities);
        const auto after = at(entities, e);

        const auto dx = after.x - before.x;
        const auto dy = after.y - before.y;
        for (std::size_t i = 0; i < dirs.size(); ++i)
        {
            if (dx == dirs[i].first && dy == dirs[i].second)
            {
                ++by_dir[i];
            }
        }
    }

    // **判据为什么是卡方而不是逐方向上下界。** 曾经用过方框
    // `total/8 <= n <= total/2`，两个方向都不合格：
    //
    //   - 假红 6.4%。正确代码下四向各 25%，均值 `total/4 = 10`、
    //     σ = 2.74；而下界 `total/8 = 5` 距那个均值只有 1.82σ，所以 6.4% 的
    //     种子组合会误报。**下界才是紧的那一侧**，不是「安全垫」。
    //   - 漏检 95.4%。对任何比 64% 更轻的方向性偏置近乎无效。
    //
    // 卡方把「四向是否均匀」当成一个整体来问，不去管每个方向各自的余量：
    // 80 个种子、4 自由度、p ≈ 0.003 时假红 **0.099%**、本 bug 检出
    // **99.97%** —— 两个方向都严格优于方框。种子翻倍在 sim 里是纯 CPU 成本
    // （本条只有一个个体跑一季）。
    //
    // 期望计数就是 N/4，因此 `χ² = Σ(nᵢ − N/4)² / (N/4)`。
    const int total = kSeeds;
    // 总数对得上：防「方向被判错、谁都没被计数」那种空过。
    CHECK(by_dir[0] + by_dir[1] + by_dir[2] + by_dir[3] == total);

    double chi2 = 0.0;
    for (const auto n : by_dir)
    {
        const double d = static_cast<double>(n) - static_cast<double>(total) / 4.0;
        chi2 += d * d / (static_cast<double>(total) / 4.0);
    }
    CHECK(chi2 < 16.3);
}

TEST(a_gradient_bearing_creature_repeatedly_returns_to_rich_cells)
{
    // **这条只守「梯度开关起作用」，不守类头承诺的负反馈。**
    //
    // 夹具只跑 MovementSystem、不跑 EnvironmentSystem，于是 `stock` 从头到尾
    // **纹丝不动**，恒等于 capacity。所以在这个夹具里「追存量」与「追容量」
    // 是同一件事 —— 类头注释 `movement_system.hpp` 里那条「挤进富格 → 吃光 →
    // 梯度翻转 → 离开」的负反馈**从未在这里被触发过**，本条也守不住它。
    // 真要守它，得让存量产生真实差异（跑一轮摄食），那是世界级的量，由 `sim`
    // 的按陆块分组输出间接观察。
    //
    // 正因为它守的范围窄，判据必须是**效应量**而不是裸 `>`：裸 `>` 在梯度完全
    // 不起作用时也能过（两臂读数接近，裸 `>` 就是掷硬币）。1.05 按实测比值
    // 1.1379 取，留了余量。
    //
    // 注意本条对 `-1.0f` 那类缺陷**恒绿**（缺陷版读数 1.2154 > 1.05）。守住
    // 那个的是 `the_gradient_direction_is_symmetric_when_no_neighbour_is_richer`。
    const auto settle = [](const float weight)
    {
        EnvironmentConfig config{};
        config.gradient_weight = weight;
        // 钉全陆地。默认的 0.70 会让下面按**固定坐标**铺的个体有一部分开局就
        // 落在水格上，被 MovementSystem 的 nearest_standable 静默搬运。两臂
        // 受完全相同的搬运，对称性保住、比较仍成立，但那时测的是「两组同样
        // 被搬运后的落点存量」，不再干净。钉死之后才是真的在比较趋向。
        config.water_fraction = 0.0f;
        EnvironmentWorld env{20261016, config};
        World entities;

        std::vector<EntityId> ids;
        for (std::size_t i = 0; i < 120; ++i)
        {
            ids.push_back(place(entities, static_cast<float>(i % 12) * 5.0f + 0.5f,
                                static_cast<float>(i / 12) * 5.0f + 0.5f, 1.0f));
        }

        MovementSystem movement{20261016, 0, env.resources(), env.terrain(),
                                 config.gradient_weight};
        CommandBuffer cmd;
        for (std::uint32_t s = 0; s < 30; ++s)
        {
            movement.advance_season(s);
            movement.execute(entities, cmd);
        }
        cmd.flush(entities);

        float sum = 0.0f;
        std::size_t n = 0;
        for (const auto e : ids)
        {
            const auto& p = at(entities, e);
            sum += env.resources().stock(CellIndex::of(p.x, p.y));
            ++n;
        }
        return sum / static_cast<float>(n);
    };

    const auto wandering = settle(0.0f);
    const auto seeking = settle(1.0f);

    // 效应量下限而不是裸 > ：裸 > 在梯度完全不起作用时也能过（两臂读数
    // 接近，裸 > 就是掷硬币）。1.05 按实测比值 1.1379 取，留了余量。
    CHECK(seeking > wandering * 1.05f);
}

TEST(a_stranded_creature_is_pulled_back_into_its_own_landmass)
{
    // 决策 19：钳回**不许跨陆块**。原实现用切比雪夫方环找**全局**最近的可
    // 通行格，淹一块地就可能把它上面的个体静默扔到另一个陆块 —— 玩家的干
    // 预能无声地拆掉屏障。
    //
    // 这条在旧实现下**也绿**（下面那条才是它真正的判别力所在）：个体脚下
    // 被淹时，它四邻里只要还有陆地，那一格必属同一分量，全局最近与分量内
    // 最近必然重合。它守的是另一半——钳回**不许退化成销毁**，也别把个体
    // 送到另一块地上。夹具把两块地摆成 x < 16 与 x >= 48，中间全是水。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    EnvironmentWorld env{20261016, config};
    auto& terrain = env.terrain();
    carve(terrain, [](const std::uint16_t x, const std::uint16_t y)
          { return x < 16 || x >= 48 || (x == 16 && y == 32); });

    World entities;
    const EntityId e = place(entities, 16.5f, 32.5f, 0.0f);
    MovementSystem movement{20261016, 0, env.resources(), terrain, 0.0f};
    CommandBuffer cmd;
    // 先走一季：MovementSystem 由此记下「个体脚下这块地的拓扑」，那才是
    // 地形被改写之后唯一还能追溯到原来哪块地的凭据。
    movement.execute(entities, cmd);
    cmd.flush(entities);

    terrain.flood(cell_at(16, 32));
    movement.advance_season(1);
    movement.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(entities.is_alive(e));
    const auto& p = at(entities, e);
    // 没被送到 x >= 48 那块地上，也没被留在水里。
    CHECK(p.x < 16.0f);
    CHECK(terrain.passable(CellIndex::of(p.x, p.y)));
}

TEST(a_creature_whose_whole_landmass_is_flooded_is_destroyed)
{
    // 决策 19 的后半句：分量被整个淹没时个体无处可去，销毁。走
    // `CommandBuffer::destroy`，与其他死亡路径同一条。
    //
    // **旧实现在这里才真的错**：它找全局最近，于是把个体静默搬到几十格外的
    // 大陆上，世界从此多出一批「本该淹死却凭空活下来并开始繁殖」的个体。
    // 夹具刻意留下那块大陆 —— 没有它，「销毁」与「搬运」会给出同一个可
    // 观察结果。
    EnvironmentConfig config{};
    config.water_fraction = 0.0f;
    EnvironmentWorld env{20261016, config};
    auto& terrain = env.terrain();
    carve(terrain, [](const std::uint16_t x, const std::uint16_t y)
          {
              return x >= 40 || (x >= 14 && x < 17 && y >= 31 && y < 34);
          });

    World entities;
    const EntityId e = place(entities, 15.5f, 32.5f, 0.0f);
    MovementSystem movement{20261016, 0, env.resources(), terrain, 0.0f};
    CommandBuffer cmd;
    movement.execute(entities, cmd);
    cmd.flush(entities);
    CHECK(entities.is_alive(e));

    for (std::uint16_t y = 31; y < 34; ++y)
    {
        for (std::uint16_t x = 14; x < 17; ++x)
        {
            terrain.flood(cell_at(x, y));
        }
    }
    movement.advance_season(1);
    movement.execute(entities, cmd);
    cmd.flush(entities);

    CHECK(!entities.is_alive(e));
}

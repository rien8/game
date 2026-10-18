#include "gamev2/ecs/adapt/movement_system.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <ranges>
#include <utility>

namespace gamev2::ecs::adapt {

namespace {

/// 移动自用的用途标签。遗传那 5 条子流用的是 0..4，这里取一个够不着的
/// 标签，两条流因此不会因为共用标签而互相干扰。
constexpr std::uint64_t kMovementPurpose = 0x4D4F5645ull;

/// 坐标上界取 kGridSide - 1：CellIndex::of 截断取整，x == 64.0 会被当成
/// 第 64 列，算出的下标落进下一行、越出 kCellCount。
constexpr float kMapLimit =
    static_cast<float>(gamev2::environment::kGridSide) - 1.0f;

}  // namespace

bool MovementSystem::standable(const float x, const float y) const noexcept
{
    if (x < 0.0f || x > kMapLimit || y < 0.0f || y > kMapLimit)
    {
        return false;
    }
    return terrain_.passable(gamev2::environment::CellIndex::of(x, y));
}

std::pair<float, float>
MovementSystem::richest_neighbour(const float x, const float y) const noexcept
{
    static constexpr std::array<std::pair<float, float>, 4> kDirs{
        std::pair<float, float>{1.0f, 0.0f},
        std::pair<float, float>{-1.0f, 0.0f},
        std::pair<float, float>{0.0f, 1.0f},
        std::pair<float, float>{0.0f, -1.0f},
    };

    std::pair<float, float> best{0.0f, 0.0f};
    // 以**本格**存量为初值，而不是 -1：判据是「比本格更富」，不是「4 邻里的
    // 局部最大」。两者在个体站上局部峰顶时分开——那时 4 邻全比本格贫，以 -1
    // 起算照样迈出去，个体只在峰顶邻域里原地抖动；类头承诺的负反馈
    // 「吃光 → 梯度翻转 → 离开」因此落空，回填中低格的驱动力也不存在。
    // 邻格与本格打平同样不算更富，一律留在 best = {0, 0}，调用方退回随机
    // 游走。
    float best_stock = resources_.stock(
        gamev2::environment::CellIndex::of(x, y));
    for (const auto& [dx, dy] : kDirs)
    {
        const float nx = x + dx;
        const float ny = y + dy;
        if (!standable(nx, ny))
        {
            continue;
        }
        const auto stock = resources_.stock(
            gamev2::environment::CellIndex::of(nx, ny));
        // 严格大于：平手留给先来的那个，遍历序固定因此结果确定。
        if (stock > best_stock)
        {
            best_stock = stock;
            best = {dx, dy};
        }
    }
    return best;
}

std::pair<float, float>
MovementSystem::nearest_standable(const float x, const float y) const noexcept
{
    using gamev2::environment::CellIndex;
    using gamev2::environment::kCellCount;
    using gamev2::environment::kGridSide;

    const auto home = CellIndex::of(x, y);
    const auto cx = static_cast<int>(home.value % kGridSide);
    const auto cy = static_cast<int>(home.value / kGridSide);
    // 方环（切比雪夫）距离。度量只决定「哪些格算最近」，平手由下面那句的
    // 遍历序打破——换成人曼哈顿同样取到最近格，差别只在等距的一批里先取到
    // 哪个。选方环是因为本 system 的移动是四邻、轴对齐的，方环与那套模型
    // 同构。
    const auto cell_of = [](const std::size_t c)
    {
        return CellIndex{static_cast<std::uint16_t>(c)};
    };
    const auto ring = [&](const std::size_t c)
    {
        const auto gx = static_cast<int>(c % kGridSide);
        const auto gy = static_cast<int>(c / kGridSide);
        return std::max(std::abs(gx - cx), std::abs(gy - cy));
    };
    // 遍历序就是格号，而 min_element 取第一个最小元，因此同距离的平手有
    // 确定答案——否则同种子的两次运行可能各挑一个不同的落点。
    const auto nearest = std::ranges::min_element(
        std::views::iota(std::size_t{0}, kCellCount),
        [&](const std::size_t a, const std::size_t b)
        {
            const auto passable_a = terrain_.passable(cell_of(a));
            const auto passable_b = terrain_.passable(cell_of(b));
            // 不可通行的格恒不优于任何格，因此只有整张图没有陆地时它才会
            // 被选中——那时钳回无处可去，调用方只能把它留在原地。
            return passable_a && (!passable_b || ring(a) < ring(b));
        });
    const auto c = *nearest;
    return {static_cast<float>(c % kGridSide) + 0.5f,
            static_cast<float>(c / kGridSide) + 0.5f};
}

void MovementSystem::advance(Position& position, const float dx,
                             const float dy, const int stride) const noexcept
{
    float x = position.x;
    float y = position.y;
    for (int s = 0; s < stride; ++s)
    {
        const float nx = x + dx;
        const float ny = y + dy;
        if (standable(nx, ny))
        {
            x = nx;
            y = ny;
            continue;
        }
        // 沿墙滑：先试只走 x，再试只走 y。两个都不通说明顶死了，停在这里。
        if (standable(nx, y))
        {
            x = nx;
            continue;
        }
        if (standable(x, ny))
        {
            y = ny;
            continue;
        }
        break;
    }
    position.x = x;
    position.y = y;
}

void MovementSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    using gamev2::genetics::TraitKind;
    using gamev2::genetics::mix_seed;
    using gamev2::genetics::trait_index;
    using gamev2::genetics::uniform_index;

    // 自成一条流。移动每季每人至少抽一次，若与繁殖共用一条，改一次移动
    // 规则就会连带改掉全部出生结果，而那种整体漂移极难定位到是哪一处改动。
    std::mt19937_64 rng{
        mix_seed(mix_seed(root_seed_, season_index_), kMovementPurpose)};

    world.for_each_entity([&](const EntityId e)
    {
        auto* position = world.get_if<Position>(e);
        const auto* phenotype = world.get_if<CreaturePhenotype>(e);
        if (position == nullptr || phenotype == nullptr)
        {
            return;
        }
        // 缺 Position 的实体原地不动，与 EnvironmentSystem 和
        // BreedingSystem 对它的处理保持一致——三者对「谁参与世界运转」
        // 必须给出同一个答案，否则会凭空多出一个只消耗不生产的幽灵。
        //
        // 起点落在水上（地形生成后由调用方播种时踩中，或阶段 4 玩家改写
        // 地形）时先钳回最近的可行走格，否则它会在水里原地站一整季——
        // 被水围死又撞着图边界的那一格连滑行都出不去。
        if (!standable(position->x, position->y))
        {
            const auto landed = nearest_standable(position->x, position->y);
            position->x = landed.first;
            position->y = landed.second;
        }

        // Speed 允许为负（表型区间配错时），先夹到 [0, 3] 再取整。
        // 至少一格：不足一格时个体可能整季留在原格，空格照旧填不上。
        const float speed = phenotype->phenotype.traits[trait_index(
            TraitKind::Speed)];
        const auto stride = static_cast<int>(std::max(
            1.0f,
            static_cast<float>(std::lround(std::clamp(speed, 0.0f, 3.0f)))));

        float dx = 0.0f;
        float dy = 0.0f;
        // 掷一次硬币决定本季跟不跟梯度。随机流仍只有这一条——硬币取自它，
        // 因此同种子逐位可复现（设计文档 §8 的确定性一节）。
        if (gradient_weight_ > 0.0f && uniform_index(rng, 2) == 0)
        {
            const auto richer = richest_neighbour(position->x, position->y);
            dx = richer.first;
            dy = richer.second;
        }
        if (dx == 0.0f && dy == 0.0f)
        {
            switch (uniform_index(rng, 4))
            {
            case 0:
                dx = 1.0f;
                break;
            case 1:
                dx = -1.0f;
                break;
            case 2:
                dy = 1.0f;
                break;
            default:
                dy = -1.0f;
                break;
            }
        }

        advance(*position, dx, dy, stride);
    });
}

}  // namespace gamev2::ecs::adapt

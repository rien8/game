#include "gamev2/ecs/adapt/movement_system.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/random.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace gamev2::ecs::adapt {

namespace {

/// 移动自用的用途标签。遗传那 5 条子流用的是 0..4，这里取一个够不着的
/// 标签，两条流因此不会因为共用标签而互相干扰。
constexpr std::uint64_t kMovementPurpose = 0x4D4F5645ull;

/// 坐标上界取 kGridSide - 1 而不是 kGridSide：CellIndex::of 截断取整，
/// x == 64.0 会被当成第 64 列，算出的下标落进下一行、越出 kCellCount。
[[nodiscard]] float clamp_to_map(const float v) noexcept
{
    return std::clamp(v, 0.0f,
                      static_cast<float>(gamev2::environment::kGridSide) - 1.0f);
}

}  // namespace

void MovementSystem::execute(World& world, CommandBuffer& /*cmd*/)
{
    using gamev2::genetics::TraitKind;
    using gamev2::genetics::mix_seed;
    using gamev2::genetics::trait_index;
    using gamev2::genetics::uniform_index;

    // 自成一条流。移动每季每人抽一次，若与繁殖共用一条，改一次移动规则
    // 就会连带改掉全部出生结果，而那种整体漂移极难定位到是哪一处改动。
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
        // 步长至少一格：不足一格时个体可能整季留在原格，空格照旧填不上。
        // Speed 允许为负（表型区间配错时），先夹到非负再加。
        const float speed = std::max(
            0.0f,
            phenotype->phenotype.traits[trait_index(TraitKind::Speed)]);
        const float step = 1.0f + speed;

        float x = position->x;
        float y = position->y;
        switch (uniform_index(rng, 4))
        {
        case 0:
            x += step;
            break;
        case 1:
            x -= step;
            break;
        case 2:
            y += step;
            break;
        default:
            y -= step;
            break;
        }
        position->x = clamp_to_map(x);
        position->y = clamp_to_map(y);
    });
}

}  // namespace gamev2::ecs::adapt

#include "simulation.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/genetics/phenotype.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>

namespace gamev2::gui {

namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::Vitality;
using gamev2::genetics::TraitKind;
using gamev2::genetics::trait_index;

/// 坐标上界取 kGridSide - 1 而不是 kGridSide：与 MovementSystem 的
/// clamp_to_map 同一条口径，边界上不能差这一格。
[[nodiscard]] float clamp_to_map(const float v) noexcept
{
    return std::clamp(v, 0.0f,
                      static_cast<float>(gamev2::environment::kGridSide) - 1.0f);
}

}  // namespace

Simulation::Simulation(const std::uint64_t seed,
                       const gamev2::demo::Preset preset)
{
    reset(seed, preset);
}

void Simulation::reset(const std::uint64_t seed,
                       const gamev2::demo::Preset preset)
{
    // 顺序要紧：scenario_ 先换成新的，ecology_ 再就位。Ecology 持有
    // scenario_.table 的引用，两者都指向同一块存储——赋值不搬地址，emplace
    // 才让引用重新绑一次。反过来做，Ecology 会拿着刚被覆盖掉的表去构造。
    scenario_ = gamev2::demo::build_scenario(preset);
    seed_ = seed;
    preset_ = preset;

    if (const auto v = gamev2::demo::validate(scenario_); !v)
    {
        throw std::runtime_error("蓝图配置非法：" +
                                 std::to_string(static_cast<int>(v.error())));
    }

    ecology_.emplace(scenario_.table, scenario_.genetics, scenario_.environment,
                     seed);
    const auto sid = gamev2::demo::register_full_species(*ecology_);
    if (!sid)
    {
        throw std::runtime_error("物种登记失败：" +
                                 std::to_string(static_cast<int>(sid.error())));
    }

    std::mt19937_64 rng{seed};
    gamev2::demo::seed_population(*ecology_, *sid,
                                  ecology_->species().mask(*sid), scenario_, rng);

    season_ = 0;
    population_ = 0;
    heterozygosity_ = 0.0f;
    inbreeding_ = 0.0f;
    player_ = EntityId{};
    player_alive_ = false;
    pending_dx_ = 0;
    pending_dy_ = 0;
}

void Simulation::step()
{
    apply_intent();
    const auto report = ecology_->run_season();
    season_ = report.season + 1;
    population_ = report.population;
    if (!report.species.empty())
    {
        heterozygosity_ = report.species.front().heterozygosity;
        inbreeding_ = report.species.front().f_coefficient;
    }
    if (player_)
    {
        player_alive_ = ecology_->entities().is_alive(player_);
    }
}

void Simulation::apply_intent()
{
    // 先把意图取到局部变量再清空。清空了再拿，拿到的是 0——箭头画出来了，
    // 个体却不挪地方。
    const int dx = pending_dx_;
    const int dy = pending_dy_;
    pending_dx_ = 0;
    pending_dy_ = 0;
    if (dx == 0 && dy == 0)
    {
        return;
    }

    if (!player_ || !ecology_->entities().is_alive(player_))
    {
        return;
    }
    auto* position = ecology_->entities().get_if<Position>(player_);
    if (position == nullptr)
    {
        return;
    }
    // 步长与 MovementSystem 同一套公式（1 + Speed）。玩家因此跑得与其他
    // 个体一样快，不会因为被接管就多出一份机动。
    const auto* phenotype =
        ecology_->entities().get_if<CreaturePhenotype>(player_);
    const float speed =
        phenotype == nullptr
            ? 0.0f
            : std::max(0.0f, phenotype->phenotype
                                  .traits[trait_index(TraitKind::Speed)]);
    const float step = 1.0f + speed;

    position->x = clamp_to_map(position->x + static_cast<float>(dx) * step);
    position->y = clamp_to_map(position->y + static_cast<float>(dy) * step);
}

void Simulation::take_control(const EntityId e)
{
    player_ = e;
    player_alive_ = e && ecology_->entities().is_alive(e);
    pending_dx_ = 0;
    pending_dy_ = 0;
    if (player_alive_)
    {
        ecology_->set_controlled_entity(e);
    }
    else
    {
        ecology_->clear_controlled_entity();
    }
}

void Simulation::release_control() noexcept
{
    player_ = EntityId{};
    player_alive_ = false;
    pending_dx_ = 0;
    pending_dy_ = 0;
    ecology_->clear_controlled_entity();
}

void Simulation::nudge(const int dx, const int dy) noexcept
{
    pending_dx_ = dx;
    pending_dy_ = dy;
}

CreatureView Simulation::view_of(const EntityId e)
{
    auto& entities = ecology_->entities();
    const auto* phenotype = entities.get_if<CreaturePhenotype>(e);
    const auto* position = entities.get_if<Position>(e);
    if (phenotype == nullptr || position == nullptr)
    {
        return {};
    }
    const auto* vitality = entities.get_if<Vitality>(e);

    CreatureView view;
    view.id = e;
    view.age = vitality == nullptr ? 0u : vitality->age_seasons;
    view.fitness = vitality == nullptr ? 1.0f : vitality->fitness;
    view.survival_rate =
        vitality == nullptr ? 1.0f : vitality->survival_rate;
    view.x = position->x;
    view.y = position->y;
    view.body_size = phenotype->phenotype.traits[trait_index(TraitKind::BodySize)];
    view.speed = phenotype->phenotype.traits[trait_index(TraitKind::Speed)];
    view.lifespan = phenotype->phenotype.traits[trait_index(TraitKind::Lifespan)];
    view.resilience =
        phenotype->phenotype.traits[trait_index(TraitKind::Resilience)];
    return view;
}

CreatureView Simulation::player_view()
{
    if (!player_)
    {
        return {};
    }
    return view_of(player_);
}

std::vector<CreatureView> Simulation::candidates(const std::size_t limit)
{
    std::vector<CreatureView> out;
    std::vector<CreatureView> newborns;
    auto& entities = ecology_->entities();
    entities.for_each_entity(
        [&](const EntityId e)
        {
            const auto* vitality = entities.get_if<Vitality>(e);
            if (vitality == nullptr)
            {
                return;
            }
            if (vitality->age_seasons == 0)
            {
                newborns.push_back(view_of(e));
                return;
            }
            out.push_back(view_of(e));
        });

    // 一季都没活过的个体只在没有别的可选时才拿来垫底：它们的适应力恒为
    // 1.0（Vitality 的默认值），排在前面会让整张表看着像全是满分个体。
    // 开局正是这种情况——世界刚造出来，谁都还没活过一季。
    if (out.empty())
    {
        out = std::move(newborns);
    }

    // 存活率高的排前面。这是玩家能不能玩下去的关键：开局那几季环境对个体
    // 很不利，存活率可能只有 0.1 出头——接管它等于每季抽签赌命，弹窗会反复
    // 打断玩家。年龄只作次级排序键，因为同一批候选的年龄几乎一样。
    std::ranges::sort(out, [](const CreatureView& a, const CreatureView& b)
                           {
                               if (a.survival_rate != b.survival_rate)
                               {
                                   return a.survival_rate > b.survival_rate;
                               }
                               return a.age < b.age;
                           });
    if (out.size() > limit)
    {
        out.resize(limit);
    }
    return out;
}

}  // namespace gamev2::gui

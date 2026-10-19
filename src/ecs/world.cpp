#include "gamev2/ecs/world.hpp"

#include <thread>
#include <utility>

namespace gamev2::ecs {

namespace {

/// 留一个主线程给调用方，其余核分给 worker。
[[nodiscard]] std::size_t default_worker_count()
{
    const auto cores = std::thread::hardware_concurrency();
    if (cores <= 2)
    {
        return 0;
    }
    return static_cast<std::size_t>(cores) - 1;
}

}  // namespace

World::World()
    : diag_{default_diagnostic_handler()},
      pool_{default_worker_count()}
{
    empty_archetype_ = storage_.find_or_create(std::span<const ComponentId>{});
}

World::~World() = default;

void World::set_diagnostic_handler(DiagnosticHandler handler)
{
    diag_ = std::move(handler);
}

const EntitySlot* World::slot_of(EntityId e) const noexcept
{
    const auto idx = e.index();
    if (idx >= slots_.size())
    {
        return nullptr;
    }
    const auto& slot = slots_[idx];
    if (slot.version != e.version() || slot.archetype == kInvalidArchetype)
    {
        return nullptr;
    }
    return &slot;
}

EntityId World::create()
{
    std::uint32_t idx = 0;
    if (!free_indices_.empty())
    {
        idx = free_indices_.back();
        free_indices_.pop_back();
    }
    else
    {
        idx = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(EntitySlot{});
        // 版本号从 1 起始，否则索引 0 会与 EntityId{} 哨兵撞值。
        slots_.back().version = 1;
    }

    auto& slot = slots_[idx];
    // 回收槽位带着 destroy 留下的 kInvalidArchetype，两条路径都必须在
    // 取存储引用之前重置，否则下面 storage_.at() 越界。
    slot.archetype = empty_archetype_;
    auto& arche = storage_.at(slot.archetype);
    const auto e = EntityId::make(idx, slot.version);
    arche.push_back(e);
    slot.chunk = arche.back_chunk();
    slot.row = arche.back_row();

    ++alive_;
    return e;
}

void World::destroy(EntityId e)
{
    if (slot_of(e) == nullptr)
    {
        return;
    }

    auto& slot = slots_[e.index()];
    auto& arche = storage_.at(slot.archetype);

    // 逐列析构把该行变回 RAW，fill_hole 才能安全地移动构造填补。
    arche.destroy_row(slot.chunk, slot.row);

    const EntityId moved = storage_.fill_hole(arche, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = kInvalidArchetype;
    ++slot.version;
    if (slot.version == 0)
    {
        // 跳过与 EntityId{} 非法哨兵撞值的版本号。
        ++slot.version;
    }
    free_indices_.push_back(e.index());
    --alive_;
}

}  // namespace gamev2::ecs

#include "gamev2/ecs/storage.hpp"

#include <algorithm>
#include <cassert>

namespace gamev2::ecs {

namespace {

/// ComponentId 只定义了相等关系，排不出顺序，必须显式投影到 value。
void sort_key(std::vector<ComponentId>& ids)
{
    std::ranges::sort(ids, std::ranges::less{}, &ComponentId::value);
}

}  // namespace

std::size_t compute_chunk_capacity(std::span<const ComponentId> ids)
{
    std::size_t largest = 1;
    const auto types = global_component_registry().types();
    for (const auto id : ids)
    {
        const auto it = std::ranges::find_if(types, [id](const auto& info)
                                             { return info.id == id; });
        if (it != types.end())
        {
            largest = std::max(largest, it->size);
        }
    }
    return std::max(std::size_t{1}, kChunkTargetBytes / largest);
}

Column::Column(const ComponentTypeInfo& info, std::size_t chunk_capacity)
    : info_{info}, capacity_{chunk_capacity}
{
    assert(chunk_capacity >= 1);
}

void Column::ensure_chunk(std::uint32_t chunk)
{
    if (chunk == chunks_.size())
    {
        chunks_.emplace_back(capacity_ * info_.size);
    }
    assert(chunk < chunks_.size());
}

void Column::pop_chunk()
{
    chunks_.pop_back();
}

void Column::move_construct_into_raw(std::uint32_t dst_chunk,
                                     std::uint32_t dst_row,
                                     std::uint32_t src_chunk,
                                     std::uint32_t src_row)
{
    // 旧实现有一句 dst == src 的提前返回：那时两端都是已构造对象，
    // 自我赋值无害。现在目标端必须是 RAW 而源端必须是 LIVE，同一行
    // 不可能同时是两者，重合即不变式被破坏，不该被静默吞掉。
    info_.move_construct(chunk_data(dst_chunk) + dst_row * info_.size,
                         chunk_data(src_chunk) + src_row * info_.size);
}

void Column::move_construct_into_raw_from(const Column& src,
                                          std::uint32_t dst_chunk,
                                          std::uint32_t dst_row,
                                          std::uint32_t src_chunk,
                                          std::uint32_t src_row)
{
    // 跨列搬运没有「两对坐标同属本列」这个构造保证，类型同一性只能
    // 靠断言兜住：传错列会按错误步长取错偏移。
    assert(src.info_.id == info_.id);
    info_.move_construct(chunk_data(dst_chunk) + dst_row * info_.size,
                         raw_ptr(src, src_chunk, src_row));
}

void Column::destroy_row(std::uint32_t chunk, std::uint32_t row)
{
    info_.destroy(chunk_data(chunk) + row * info_.size);
}

Archetype::Archetype(std::vector<ComponentId> ids,
                     std::size_t chunk_capacity)
    : key_{std::move(ids)}, capacity_{chunk_capacity}
{
    sort_key(key_);
    key_.erase(std::ranges::unique(key_).begin(), key_.end());

    columns_.reserve(key_.size());
    const auto types = global_component_registry().types();
    for (const auto id : key_)
    {
        const auto it = std::ranges::find_if(types, [id](const auto& info)
                                             { return info.id == id; });
        assert(it != types.end());
        columns_.emplace_back(*it, capacity_);
    }
}

Archetype::~Archetype()
{
    // 逐列遍历，每个 chunk 按 count_in(c) 逐行析构。行数取自 count_in 而非
    // 列的物理容量：除尾块外的 chunk 满载，最后一块只活到 count_in 为止，
    // 多析构尾块之外那些从未构造的 RAW 行同样是未定义行为。
    //
    // 前提是进入析构时没有 RAW 空洞（见 storage.hpp 的声明注释）：行只有
    // RAW / LIVE 两态，本循环无从分辨，上层留下的空洞会被这里再析构一次。
    //
    // chunk 上界取自各列自身的 chunk_count，而不是本对象的 count_：
    // Archetype 会被 std::vector 移动，count_ 作为标量被逐值复制到源对象，
    // 而列的缓冲已被搬空。以列为准，被移空的源对象就析构不到任何行。
    for (auto& col : columns_)
    {
        for (std::uint32_t c = 0; c < col.chunk_count(); ++c)
        {
            const auto rows = count_in(c);
            for (std::uint32_t r = 0; r < rows; ++r)
            {
                col.destroy_row(c, r);
            }
        }
    }
}

std::uint32_t Archetype::chunk_count() const noexcept
{
    if (count_ == 0)
    {
        return 0;
    }
    return static_cast<std::uint32_t>((count_ + capacity_ - 1) / capacity_);
}

std::size_t Archetype::count_in(std::uint32_t chunk) const noexcept
{
    const auto total = chunk_count();
    if (chunk >= total)
    {
        return 0;
    }
    if (chunk + 1 == total)
    {
        const auto rem = count_ % capacity_;
        return rem == 0 ? capacity_ : rem;
    }
    return capacity_;
}

void Archetype::push_back(EntityId e)
{
    const auto c = static_cast<std::uint32_t>(count_ / capacity_);
    for (auto& col : columns_)
    {
        col.ensure_chunk(c);
    }
    entities_.push_back(e);
    ++count_;
}

void Archetype::pop_back()
{
    assert(count_ > 0);
    entities_.pop_back();
    --count_;
    // 回收条件必须作用在自减之后的 count_ 上。被删的行是 count_-1，它所在
    // 的 chunk 删完还剩 (count_-1) % capacity_ + 1 行存活；若拿自减前的
    // count_ 去判，整数倍那一刻会把仍装着存活行的 chunk 整块释放掉。
    if (count_ % capacity_ == 0)
    {
        for (auto& col : columns_)
        {
            col.pop_chunk();
        }
    }
}

std::uint32_t Archetype::back_chunk() const noexcept
{
    // 与 back_row 取同一基准：count_ 为容量整数倍时，刚写满的是上一个
    // chunk 的最后一行，用 count_ / capacity_ 会指到尚未写入的空 chunk。
    return static_cast<std::uint32_t>((count_ - 1) / capacity_);
}

std::uint32_t Archetype::back_row() const noexcept
{
    const auto rem = count_ % capacity_;
    return static_cast<std::uint32_t>(rem == 0 ? capacity_ - 1 : rem - 1);
}

void Archetype::destroy_row(std::uint32_t chunk, std::uint32_t row)
{
    for (auto& col : columns_)
    {
        col.destroy_row(chunk, row);
    }
}

ArchetypeId Storage::find_or_create(std::span<const ComponentId> ids)
{
    ComponentKey key{ids.begin(), ids.end()};
    sort_key(key);
    key.erase(std::ranges::unique(key).begin(), key.end());

    if (const auto it = index_.find(key); it != index_.end())
    {
        return it->second;
    }

    const auto id = static_cast<ArchetypeId>(archetypes_.size());
    archetypes_.emplace_back(key, compute_chunk_capacity(key));
    index_.emplace(std::move(key), id);
    return id;
}

ArchetypeId Storage::with(ArchetypeId current, ComponentId id, bool add)
{
    ComponentKey key{at(current).key().begin(), at(current).key().end()};
    if (add)
    {
        key.push_back(id);
        sort_key(key);
    }
    else
    {
        std::erase(key, id);
    }
    return find_or_create(key);
}

void Storage::copy_shared(const Archetype& src, std::uint32_t src_chunk,
                          std::uint32_t src_row, Archetype& dst,
                          std::uint32_t dst_chunk, std::uint32_t dst_row)
{
    for (const auto id : dst.key())
    {
        const auto si = src.find(id);
        if (si == kNoIndex)
        {
            continue;
        }
        dst.column_at(dst.find(id))
            .move_construct_into_raw_from(src.column_at(si), dst_chunk,
                                          dst_row, src_chunk, src_row);
    }
    dst.set_entity(dst_chunk, dst_row, src.entity_at(src_chunk, src_row));
}

EntityId Storage::fill_hole(Archetype& src, std::uint32_t chunk,
                            std::uint32_t row)
{
    // 空 archetype 上 total - 1 会下成巨大下标，随后取尾行随即越界。
    assert(src.entity_count() > 0);
    const auto total = src.entity_count();
    const auto cap = src.chunk_capacity();
    const auto last_chunk = static_cast<std::uint32_t>((total - 1) / cap);
    const auto last_row = static_cast<std::uint32_t>((total - 1) % cap);

    // 空洞恰在尾部：没有可填补的实体，count_ == 1 也落在这里。
    // 这一分支必须在移动构造之前，否则会拿 RAW 空洞当源行搬运。
    if (last_chunk == chunk && last_row == row)
    {
        src.pop_back();
        return {};
    }

    const EntityId moved = src.entity_at(last_chunk, last_row);
    for (std::size_t i = 0; i < src.key().size(); ++i)
    {
        src.column_at(i).move_construct_into_raw(chunk, row,
                                                 last_chunk, last_row);
    }
    src.set_entity(chunk, row, moved);
    // 尾行的 payload 已构造出副本，原位那些仍是 LIVE 对象：必须析构，
    // 否则既漏析构又会在 pop_back 回收 chunk 后留下悬垂资源。
    src.destroy_row(last_chunk, last_row);
    src.pop_back();
    return moved;
}

}  // namespace gamev2::ecs

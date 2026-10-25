/// @file world.hpp
/// @brief ECS 世界：实体的权威所有者，管理槽位表、组件存储、诊断出口与并行
///        线程池，并提供 create / destroy / add / remove / get_if 等结构与访问
///        操作，是外部接触本模块的主入口。
///
/// 结构操作以 std::expected 返回失败原因而非抛异常（见 EcsError）；执行期的
/// 结构性变更应经 CommandBuffer 排队到帧末，create() 的文档写明了唯一允许
/// 直接改结构的例外条件。EntityId 到存储位置（archetype / chunk / row）的
/// 换算由槽位表负责，World 因此也是全部实体搬移的知情方。
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/diagnostic.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/storage.hpp"
#include "gamev2/ecs/thread_pool.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 结构操作失败的原因。诊断 message 的文案随调用点变化，code 才是稳定标识。
enum class EcsError
{
    entity_invalid,
    component_missing,
    component_exists,
};

/// EntityId 索引到这张表即可定位存储位置。
struct EntitySlot
{
    ArchetypeId archetype = kInvalidArchetype;
    std::uint32_t version = 0;
    std::uint32_t chunk = 0;
    std::uint32_t row = 0;
};

/// ECS 的世界容器。实体的权威所有者，同时也是并行遍历所用线程池的持有者，
/// 因此 World 不可拷贝也不可移动。
class World
{
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void set_diagnostic_handler(DiagnosticHandler handler);

    /// 占用一个槽位并把实体追加到空 archetype，返回其句柄。
    ///
    /// 前置条件：只在主线程调用，不得在 System 内并发调用。create 要改写
    /// slots_ / free_indices_ 并向 archetype 追加行，这三处都不是原子量、
    /// 也没有锁保护。System 执行期间的实体变更一律走 CommandBuffer（见
    /// system.hpp 里 execute 的结构性变更要求），这条正是那条要求的例外点：
    /// 直接调用是允许的，但调用点必须自己站在主线程上。
    [[nodiscard]] EntityId create();

    /// 重复销毁静默忽略：游戏逻辑中 destroy 被多次调用是常态。
    void destroy(EntityId e);

    template<typename T, typename... Args>
    std::expected<void, EcsError> add(EntityId e, Args&&... args);

    template<typename T>
    std::expected<void, EcsError> remove(EntityId e);

    /// 实体失效或组件缺失时返回 nullptr。热路径，不做错误检查。
    template<typename T>
    [[nodiscard]] T* get_if(EntityId e) noexcept
    {
        const auto* s = slot_of(e);
        if (s == nullptr)
        {
            return nullptr;
        }
        auto& arche = storage_.at(s->archetype);
        const auto ci = arche.find(component_id_of<T>());
        if (ci == kNoIndex)
        {
            return nullptr;
        }
        return arche.column_at(ci).template at<T>(s->chunk, s->row);
    }

    template<typename T>
    [[nodiscard]] bool has(EntityId e) const noexcept
    {
        const auto* s = slot_of(e);
        if (s == nullptr)
        {
            return false;
        }
        return storage_.at(s->archetype).has(component_id_of<T>());
    }

    [[nodiscard]] std::size_t entity_count() const noexcept
    {
        return alive_;
    }

    [[nodiscard]] bool is_alive(EntityId e) const noexcept
    {
        return slot_of(e) != nullptr;
    }

    /// 遍历全部存活实体，回调收到真实句柄。
    ///
    /// 存活判据与 slot_of 一致：archetype 为 kInvalidArchetype 即已销毁。
    /// 版本号取自 slot 自身，因此造出的句柄一定能通过 slot_of 的校验。
    template<typename F>
    void for_each_entity(F&& f)
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].archetype == kInvalidArchetype)
            {
                continue;
            }
            f(EntityId::make(i, slots_[i].version));
        }
    }

    void report(Diagnostic diag) const
    {
        emit_diagnostic(diag, diag_);
    }

    [[nodiscard]] Storage& storage() noexcept { return storage_; }
    [[nodiscard]] const Storage& storage() const noexcept
    {
        return storage_;
    }

    [[nodiscard]] const EntitySlot* slot_of(EntityId e) const noexcept;

    /// 返回 Ts 组件的视图。定义随 view.hpp 一并给出：返回类型 View<Ts...>
    /// 在此不可见，只能留给调用方所在翻译单元去实例化。
    template<typename... Ts>
    [[nodiscard]] auto view();

    [[nodiscard]] ThreadPool& parallel_pool() noexcept { return pool_; }

    /// System 执行上下文的进入与退出，供 View 判断能否并行。
    void enter_system_execution() const noexcept
    {
        in_system_.store(true, std::memory_order_relaxed);
    }

    void exit_system_execution() const noexcept
    {
        in_system_.store(false, std::memory_order_relaxed);
    }

    [[nodiscard]] bool parallelize_allowed() const noexcept
    {
        return !in_system_.load(std::memory_order_relaxed);
    }

private:
    /// 填补空洞后把被移动实体的位置更新回它的 slot。
    void relocate(EntityId moved, std::uint32_t chunk, std::uint32_t row)
    {
        auto& ms = slots_[moved.index()];
        ms.chunk = chunk;
        ms.row = row;
    }

    Storage storage_;
    ArchetypeId empty_archetype_ = kInvalidArchetype;
    std::vector<EntitySlot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t alive_ = 0;
    DiagnosticHandler diag_;
    mutable std::atomic<bool> in_system_{false};
    ThreadPool pool_;
};

}  // namespace gamev2::ecs

namespace gamev2::ecs {

/// add 与 remove 的实现也在此处，跟随头文件隐式实例化。
/// 若放在 world.cpp 中，非模板翻译单元无法为新组件类型生成实例。

template<typename T, typename... Args>
std::expected<void, EcsError> World::add(EntityId e, Args&&... args)
{
    if (slot_of(e) == nullptr)
    {
        return std::unexpected(EcsError::entity_invalid);
    }

    auto& slot = slots_[e.index()];
    const auto cid = component_id_of<T>();
    const auto src_id = slot.archetype;

    if (storage_.at(src_id).has(cid))
    {
        return std::unexpected(EcsError::component_exists);
    }

    const auto target = storage_.with(src_id, cid, true);
    // with() 可能新建 archetype 并触发 archetypes_ 重新分配，
    // 因此对存储的引用必须在它之后再取，否则悬垂。
    auto& src = storage_.at(src_id);
    auto& dst = storage_.at(target);

    dst.push_back(e);
    const auto dc = dst.back_chunk();
    const auto dr = dst.back_row();

    Storage::copy_shared(src, slot.chunk, slot.row, dst, dc, dr);
    dst.column_at(dst.find(cid))
        .template construct<T>(dc, dr, std::forward<Args>(args)...);

    // 先把源行析构回 RAW 再填洞：填补是对 RAW 存储做移动构造，对仍处于
    // LIVE 的旧对象赋值既是非法操作，也会漏掉旧对象的资源释放。
    src.destroy_row(slot.chunk, slot.row);
    const EntityId moved = storage_.fill_hole(src, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = target;
    slot.chunk = dc;
    slot.row = dr;
    return {};
}

template<typename T>
std::expected<void, EcsError> World::remove(EntityId e)
{
    if (slot_of(e) == nullptr)
    {
        return std::unexpected(EcsError::entity_invalid);
    }

    auto& slot = slots_[e.index()];
    const auto cid = component_id_of<T>();
    const auto src_id = slot.archetype;

    const auto si = storage_.at(src_id).find(cid);
    if (si == kNoIndex)
    {
        return std::unexpected(EcsError::component_missing);
    }

    const auto target = storage_.with(src_id, cid, false);
    // 与 add 相同：引用必须在 with() 之后再取。
    auto& src = storage_.at(src_id);
    auto& dst = storage_.at(target);

    dst.push_back(e);
    const auto dc = dst.back_chunk();
    const auto dr = dst.back_row();

    Storage::copy_shared(src, slot.chunk, slot.row, dst, dc, dr);
    // 必须析构整行而非只析构被移除的那一列：留下 LIVE 的旧对象，再对
    // 这块存储做移动构造填补，是未定义行为。
    src.destroy_row(slot.chunk, slot.row);
    const EntityId moved = storage_.fill_hole(src, slot.chunk, slot.row);
    if (moved)
    {
        relocate(moved, slot.chunk, slot.row);
    }

    slot.archetype = target;
    slot.chunk = dc;
    slot.row = dr;
    return {};
}

}  // namespace gamev2::ecs

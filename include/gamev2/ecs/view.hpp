/// @file view.hpp
/// @brief 类型化查询视图：按构造时给定的组件类型集合找出所有匹配的实体行，
///        以可写引用（模板参数带 const 时为只读引用）交给回调逐行处理。
///
/// View 在构造时一次性算出匹配的 archetype 与非空 chunk 列表，遍历期不再做
/// 任何集合运算，因此扫描代价与实体总数无关，只与命中行数有关。并行版本按
/// chunk 提交到 World 的线程池，已处于 System 执行上下文时自动退化为串行。
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/storage.hpp"
#include "gamev2/ecs/world.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 编译期类型化的实体视图。Ts 中的 const 表示只读访问。
///
/// 构造时预先算出匹配的 archetype 与 chunk 列表，遍历时不再做集合运算。
template<typename... Ts>
class View
{
public:
    static_assert(sizeof...(Ts) > 0, "View 至少需要一个组件类型");

    struct ChunkTask
    {
        ArchetypeId archetype;
        std::uint32_t chunk;
    };

    explicit View(World& world);

    template<typename F>
    void each(F&& f)
    {
        for (const auto& task : tasks_)
        {
            run_chunk(task, f);
        }
    }

    /// 按 chunk 提交线程池并行遍历。
    ///
    /// 当前线程已在执行 System 时退化为串行。这里防的不是死锁：本函数提交到
    /// 的是 World 的池，批内 System 由 Scheduler 自己的另一个池派发，两层之间
    /// 不存在环形等待。要挡的是同池并发——批内两个 System 各自调用本函数时，
    /// 两个 run() 会并发落到 World 的同一个池上，覆盖 ThreadPool 共享的
    /// jobs_ / next_ / done_ / generation_，任务成批丢失。
    template<typename F>
    void each_parallel(F&& f)
    {
        if (!world_->parallelize_allowed())
        {
            each(std::forward<F>(f));
            return;
        }

        std::vector<std::function<void()>> jobs;
        jobs.reserve(tasks_.size());
        for (const auto& task : tasks_)
        {
            jobs.emplace_back([this, &f, task] { run_chunk(task, f); });
        }
        world_->parallel_pool().run(jobs);
    }

private:
    /// 一次调用的实参元组。std::tuple 的推导指南是按值形参，CTAD 不保留
    /// 左值的引用，推出来的是值元组，所以这里必须显式点名。Ts 上原样保留
    /// const：列里取到的是可写引用，绑到 const& 才兑现只读承诺。
    using Row = std::tuple<Ts&...>;

    template<std::size_t I>
    [[nodiscard]] auto column_base(ArchetypeId aid, std::uint32_t chunk)
        noexcept
    {
        using T =
            std::remove_const_t<std::tuple_element_t<I, std::tuple<Ts...>>>;
        auto& arche = world_->storage().at(aid);
        return arche.column_at(arche.find(component_id_of<T>()))
            .template at<T>(chunk, 0);
    }

    template<typename F>
    void run_chunk(const ChunkTask& task, F& f)
    {
        const auto n = world_->storage().at(task.archetype).count_in(
            task.chunk);
        if (n == 0)
        {
            return;
        }

        // 基址必须随 I 逐列展开：写成 column_base<0>(...) ... 会让每个
        // 元素都绑到第 0 列，多组件 View 的全部参数指向同一个类型。
        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            auto bases = std::tuple{
                column_base<I>(task.archetype, task.chunk)...};
            for (std::size_t r = 0; r < n; ++r)
            {
                std::apply(f, Row{std::get<I>(bases)[r]...});
            }
        }(std::index_sequence_for<Ts...>{});
    }

    World* world_ = nullptr;
    std::vector<ChunkTask> tasks_;
};

template<typename... Ts>
View<Ts...>::View(World& world)
    : world_{&world}
{
    // 展开点必须落在函数调用的实参位。写成
    // component_id_of<remove_const_t<Ts>()...> 会把整个类型包塞进一个
    // 单参数模板的尖括号里。
    const std::vector<ComponentId> required{
        component_id_of<std::remove_const_t<Ts>>()...};

    // archetypes() 返回 span<const Archetype>，按值迭代拿到的是引用而非
    // ArchetypeId，交给 at() 会去拷贝一个不可拷贝的 Archetype。
    const auto archetypes = world.storage().archetypes();
    for (std::size_t i = 0; i < archetypes.size(); ++i)
    {
        const auto aid = static_cast<ArchetypeId>(i);
        const auto& arche = archetypes[i];
        if (!std::ranges::all_of(required, [&arche](ComponentId id)
                                 { return arche.has(id); }))
        {
            continue;
        }
        for (std::uint32_t c = 0; c < arche.chunk_count(); ++c)
        {
            if (arche.count_in(c) > 0)
            {
                tasks_.push_back(ChunkTask{aid, c});
            }
        }
    }
}

}  // namespace gamev2::ecs

namespace gamev2::ecs {

/// 放在 View 完整定义之后：world.hpp 只声明 World::view()，auto 返回类型
/// 必须在这里推导。
template<typename... Ts>
[[nodiscard]] auto World::view()
{
    return View<Ts...>{*this};
}

}  // namespace gamev2::ecs

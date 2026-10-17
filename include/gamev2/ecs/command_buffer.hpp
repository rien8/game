/// @file command_buffer.hpp
/// @brief 命令缓冲：记录实体与组件变更，直到 flush 前都不触碰 World 的真实状态。
#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 延迟结构性变更的队列。
///
/// 命令按入队顺序执行。flush 前目标实体若已失效，该命令被拒绝并记诊断，
/// 因此 destroy 之后 add 的语义是可预测的。
///
/// **destroy 是唯一的例外**：目标已失效时它静默跳过。这不是放松而是与
/// World 对齐——World::destroy 本身对已失效的句柄就直接返回、不作声。两个
/// system 各自独立判死同一个体（年龄与存活抽签都判它出局）是合法形状，
/// 两条 destroy 进同一个队列时后一条必然撞上「已失效」；若记成 error，一条
/// 正常运行的季循环就能刷出几千条假诊断，把真正的错误淹没。add / remove
/// 没有这个豁免：往一个已经不存在的实体上挂组件一定是 bug。
class CommandBuffer
{
public:
    /// 在 flush 期创建实体，并把它挂上给定组件，新句柄回写到 out。
    ///
    /// World::create() 改写 slots_ / free_indices_ 且非原子，其文档明确
    /// 禁止在 System 执行期调用；而队列里没有任何创建实体的命令，
    /// BreedingSystem 就只能绕开队列直接改结构。
    ///
    /// 组件必须随 create 同一条命令提交，不能事后补 add：add 在入队时
    /// 就按值捕获了目标句柄，而句柄要到 flush 才由本命令写进 out，那
    /// 时 add 的 target 仍是空的 EntityId{}，会被它自己的「目标已失效
    /// 则拒绝」判据驳回。
    ///
    /// \pre `out` 引用的对象必须活到 flush 结束。命令体只能靠这个引用把
    /// 新句柄回写给调用方，因此调用方不能传 execute 的栈上局部量。
    template<typename... Ts>
    void create(EntityId& out, Ts&&... components);

    void destroy(EntityId e);

    template<typename T, typename... Args>
    void add(EntityId e, Args&&... args);

    template<typename T>
    void remove(EntityId e);

    /// 把另一个缓冲区的命令按顺序接到自己后面。
    void append(CommandBuffer&& other);

    [[nodiscard]] std::size_t pending() const noexcept
    {
        return commands_.size();
    }

    /// 帧末主线程单线程执行。
    ///
    /// 前置条件：flush 期间不得向本缓冲器再入队。range-for 持有的迭代器
    /// 会被 push_back 扩容失效，未扩容时新命令又会被末尾的 clear() 静默
    /// 销毁 —— 两条路都不产生诊断，所以必须由调用方保证。
    void flush(World& world);

private:
    /// 命令体直接持有 std::function 而非类型擦除的字节负载：项目禁用智能指针，
    /// 而队列必须跨 T 持有各自的构造参数，可调用对象是唯一无需所有权类型就能
    /// 承载它们的载体。
    struct Command
    {
        /// 以 create 与 nullopt：它的目标就是待创建的新实体本身，句柄要到
        /// flush 才由命令体写出来，因此「没有目标」是它的真值而非缺省。
        /// 其余命令一律持有实打实的句柄——哪怕调用方误传 EntityId{}，
        /// 它仍是一个已失效的目标，flush 会照常报 ecs.entity_invalid。
        /// 早先用「空 EntityId 当 create 的标记」正是把这两者混成一件：
        /// add(EntityId{}) / destroy(EntityId{}) 会跟着跳过判据，内部
        /// 静默失败。
        std::optional<EntityId> target;
        /// 仅 destroy 置位：目标已失效时跳过而不记诊断（理由见类文档）。
        bool idempotent_if_dead = false;
        std::function<void(World&)> apply;
    };

    std::vector<Command> commands_;
};

template<typename... Ts>
void CommandBuffer::create(EntityId& out, Ts&&... components)
{
    static_assert(
        (std::is_constructible_v<std::decay_t<Ts>, std::decay_t<Ts>&&> && ...),
        "组件类型无法用给定参数构造");
    auto tuple = std::make_tuple(std::forward<Ts>(components)...);
    commands_.push_back(Command{
        std::nullopt,
        false,
        [&out, t = std::move(tuple)](World& w) mutable
        {
            out = w.create();
            // std::apply 只给得出「值」，给不出「这个值该当成什么组件
            // 类型」，因此下标要显式展开才能把 Ts 逐个配回 add<T>。
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                ((void)w.template add<
                     std::decay_t<std::tuple_element_t<I, decltype(t)>>>(
                    out, std::get<I>(std::move(t))),
                 ...);
            }(std::make_index_sequence<sizeof...(Ts)>{});
        }});
}

inline void CommandBuffer::destroy(EntityId e)
{
    commands_.push_back(Command{e, true, [e](World& w) { w.destroy(e); }});
}

template<typename T, typename... Args>
void CommandBuffer::add(EntityId e, Args&&... args)
{
    // 按 decay 后的类型判断可构造性：make_tuple 与 flush 期的 std::apply
    // 都以 decay_t<Args>&& 真正构造，未衰减的 Args（左值推成 U&）会给出
    // 不同的答案，让诊断与真实构造条件脱节。
    static_assert(std::is_constructible_v<T, std::decay_t<Args>&&...>,
                  "组件类型无法用给定参数构造");
    auto tuple = std::make_tuple(std::forward<Args>(args)...);
    commands_.push_back(Command{
        e, false, [e, t = std::move(tuple)](World& w) mutable
        {
            std::apply(
                [&w, e](auto&&... a)
                {
                    (void)w.template add<T>(
                        e, std::forward<decltype(a)>(a)...);
                },
                std::move(t));
        }});
}

template<typename T>
void CommandBuffer::remove(EntityId e)
{
    commands_.push_back(Command{
        e, false, [e](World& w) { (void)w.template remove<T>(e); }});
}

inline void CommandBuffer::append(CommandBuffer&& other)
{
    commands_.insert(commands_.end(),
                     std::make_move_iterator(other.commands_.begin()),
                     std::make_move_iterator(other.commands_.end()));
    // 移动进插不会缩短 other，被搬空的 std::function 仍占着槽位：不显式清空，
    // 它的 pending() 会继续报错数，再 flush 会在调用空目标时把 bad_function_call。
    other.commands_.clear();
}

}  // namespace gamev2::ecs

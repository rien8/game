#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/entity.hpp"
#include "gamev2/ecs/world.hpp"

#include <cstddef>
#include <functional>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 延迟结构性变更的队列。
///
/// 命令按入队顺序执行。flush 前目标实体若已失效，该命令被拒绝并记诊断，
/// 因此 destroy 之后 add 的语义是可预测的。
class CommandBuffer
{
public:
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
        EntityId target;
        std::function<void(World&)> apply;
    };

    std::vector<Command> commands_;
};

inline void CommandBuffer::destroy(EntityId e)
{
    commands_.push_back(Command{e, [e](World& w) { w.destroy(e); }});
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
        e, [e, t = std::move(tuple)](World& w) mutable
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
        e, [e](World& w) { (void)w.template remove<T>(e); }});
}

inline void CommandBuffer::append(CommandBuffer&& other)
{
    commands_.insert(commands_.end(),
                     std::make_move_iterator(other.commands_.begin()),
                     std::make_move_iterator(other.commands_.end()));
    // 移动进插不会缩短 other，被搬空的 std::function 仍占着槽位：不显式清空，
    // 它的 pending() 会继续报错数，再 flush 会在调用空目标时抛 bad_function_call。
    other.commands_.clear();
}

}  // namespace gamev2::ecs

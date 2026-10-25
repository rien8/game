#include "gamev2/ecs/component.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace gamev2::ecs {

namespace {

/// ID 计数器刻意留在注册表之外：它是文件级全局量，不受 ComponentRegistry 的
/// mutex 保护，因此自增仍必须是原子的。并发注册的安全性由注册表自己的锁负责，
/// 两者各管一段，见 register_type_info。
std::atomic<std::uint32_t> g_next_component_id{1};

}  // namespace

ComponentRegistry& global_component_registry()
{
    static ComponentRegistry registry;
    return registry;
}

ComponentId ComponentRegistry::register_type_info(
    std::size_t size, std::size_t alignment, const char* name,
    void (*destroy)(void*), void (*move_construct)(void*, void*))
{
    // 计数与追加必须同处一个临界区：分开加锁的话，另一个线程可能在本线程
    // 取到 raw 之后、push_back 之前插进来，让下标与实际存储错开。
    const std::lock_guard lock{mutex_};

    const auto raw = g_next_component_id.fetch_add(1);
    if (raw == std::numeric_limits<std::uint32_t>::max())
    {
        /// std::print 只能写 stdout，std::print_to 在本工具链的 MSVC STL
        /// 中尚未提供，因此这条耗尽路径只能退回 fprintf。
        std::fprintf(stderr, "component id space exhausted\n");
        std::abort();
    }

    const ComponentId id{raw};
    infos_.push_back(
        ComponentTypeInfo{id, size, alignment, name, destroy, move_construct});
    return id;
}

}  // namespace gamev2::ecs

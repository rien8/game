#include "gamev2/ecs/component.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace gamev2::ecs {

namespace {

/// 注册只发生在单线程启动期；原子量仅用于阻止编译器或硬件重排自增与
/// 后续 push_back，从而保证分配出的 ID 不重复，并不提供并发注册安全性。
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

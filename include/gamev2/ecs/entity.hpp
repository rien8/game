/// @file entity.hpp
/// @brief 实体句柄：把 32 位槽位索引与 32 位版本号压进一个 64 位值，作为 ECS
///        中指代实体的唯一凭据。
///
/// 句柄是值语义的轻量整数，可自由拷贝、比较、存入容器；判活不归它管，而是由
/// World 的槽位表按「索引 + 版本」核对（见 world.hpp 的 EntitySlot），本文件
/// 不持有任何存储。实体销毁时版本号递增，旧句柄由此自然失效，无需回收列表。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gamev2::ecs {

/// 销毁实体时递增版本号，使旧句柄自然失效。value 为 0 是保留的非法值，
/// 因此版本号必须从 1 起始，否则索引 0 的实体会与非法哨兵撞值。
struct EntityId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr std::uint32_t index() const noexcept
    {
        return static_cast<std::uint32_t>(value & 0xFFFFFFFFull);
    }

    [[nodiscard]] constexpr std::uint32_t version() const noexcept
    {
        return static_cast<std::uint32_t>(value >> 32);
    }

    [[nodiscard]] static constexpr EntityId make(std::uint32_t idx,
                                                 std::uint32_t ver) noexcept
    {
        return EntityId{(static_cast<std::uint64_t>(ver) << 32) | idx};
    }

    constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }

    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
};

}  // namespace gamev2::ecs

template<>
struct std::hash<gamev2::ecs::EntityId>
{
    [[nodiscard]] std::size_t
    operator()(gamev2::ecs::EntityId e) const noexcept
    {
        return std::hash<std::uint64_t>{}(e.value);
    }
};

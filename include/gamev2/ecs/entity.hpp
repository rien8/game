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

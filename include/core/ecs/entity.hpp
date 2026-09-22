// include/core/ecs/entity.hpp
#pragma once

#include <cstdint>

namespace game::ecs {

struct Entity {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] friend auto operator==(Entity, Entity) noexcept -> bool = default;
};

}  // namespace game::ecs

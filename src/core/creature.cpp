#include "core/creature.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace game {

namespace {

constexpr std::array<std::string_view, 12> kPrefixes = {
    "鳞", "焰", "霜", "暗", "星", "血", "岩", "风", "雷", "影", "冥", "翠",
};

constexpr std::array<std::string_view, 12> kSuffixes = {
    "噬渊者", "织梦者", "守誓者", "裂空者", "焚天者", "葬海者",
    "司命者", "逐日者", "镇岳者", "踏云者", "噬神者", "冥河者",
};

}  // namespace

auto make_name(const Traits& gene) -> std::string {
    // FNV-1a 哈希基因 → 音节。
    std::uint32_t hash = 0x811C9DC5u;
    for (const float value : gene.values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        hash = (hash ^ bits) * 0x01000193u;
    }
    const std::size_t prefix = hash % kPrefixes.size();
    const std::size_t suffix = (hash >> 8) % kSuffixes.size();
    return std::string{kPrefixes[prefix]} + "·" + std::string{kSuffixes[suffix]};
}

}  // namespace game

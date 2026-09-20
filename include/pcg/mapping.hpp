#pragma once

#include <algorithm>
#include <span>
#include <utility>

namespace game::pcg {

// 把 [-1,1] 归一化到 [0,1]。
constexpr auto normalize(float value) noexcept -> float {
    return std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
}

// 单变量阈值分类：thresholds 为升序 (上界, 输出) 表，value < 上界 → 输出。
template <typename Output>
auto classify(float value, std::span<const std::pair<float, Output>> thresholds,
              Output fallback) -> Output {
    for (const auto& [bound, output] : thresholds) {
        if (value < bound) {
            return output;
        }
    }
    return fallback;
}

}  // namespace game::pcg

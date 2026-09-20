#pragma once

#include "pcg/concepts.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <tuple>
#include <utility>

namespace game::pcg {

// 变参模板：组合任意多个噪声场，按权重合成 [0,1] 值（权重构造时归一化）。
// 自身也满足 NoiseField2D，可递归嵌套。
template <NoiseField2D... Fields>
class NoiseComposite {
public:
    NoiseComposite(std::array<float, sizeof...(Fields)> weights, Fields... fields)
        : fields_(std::move(fields)...), weights_(weights) {
        const float total = std::accumulate(weights_.begin(), weights_.end(), 0.0f);
        if (total > 0.0f) {
            for (auto& w : weights_) {
                w /= total;
            }
        }
    }

    [[nodiscard]] auto sample(float x, float y) const -> float {
        float result = 0.0f;
        std::size_t index = 0;
        std::apply(
            [&](const auto&... field) {
                ((result += field.sample(x, y) * weights_[index++]), ...);
            },
            fields_);
        return std::clamp(result, 0.0f, 1.0f);
    }

private:
    std::tuple<Fields...> fields_;
    std::array<float, sizeof...(Fields)> weights_;
};

}  // namespace game::pcg

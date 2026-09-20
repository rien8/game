#pragma once

#include "FastNoiseLite.h"
#include "pcg/mapping.hpp"

#include <cstdint>

namespace game::pcg {

// 二维噪声场，封装 FastNoiseLite，builder 风格链式配置。
// sample 返回归一化到 [0,1] 的值。满足 NoiseField2D 概念。
class NoiseField {
public:
    auto with_seed(std::uint64_t seed) -> NoiseField& {
        noise_.SetSeed(static_cast<int>(seed & 0xFFFFFFFFu));
        return *this;
    }
    auto with_noise_type(FastNoiseLite::NoiseType type) -> NoiseField& {
        noise_.SetNoiseType(type);
        return *this;
    }
    auto with_fractal(FastNoiseLite::FractalType type) -> NoiseField& {
        noise_.SetFractalType(type);
        return *this;
    }
    auto with_octaves(int octaves) -> NoiseField& {
        noise_.SetFractalOctaves(octaves);
        return *this;
    }
    auto with_frequency(float frequency) -> NoiseField& {
        noise_.SetFrequency(frequency);
        return *this;
    }

    [[nodiscard]] auto sample(float x, float y) const -> float {
        return normalize(noise_.GetNoise(x, y));
    }

private:
    FastNoiseLite noise_;
};

}  // namespace game::pcg

#include "world/world_generator.hpp"

#include "FastNoiseLite.h"
#include "core/creature.hpp"
#include "core/gene.hpp"
#include "pcg/mapping.hpp"
#include "pcg/noise_field.hpp"
#include "pcg/seed.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace game {

namespace {

// 海拔阈值（elevation 归一化到 [0, 1]）。
constexpr float kDeepOceanMax = 0.40f;
constexpr float kOceanMax = 0.48f;
constexpr float kBeachMax = 0.52f;
constexpr float kMountainMin = 0.72f;
constexpr float kSnowcapMin = 0.82f;

// seed salt：区分不同噪声层。
constexpr std::uint32_t kElevationSalt = 0xE1E1u;
constexpr std::uint32_t kMoistureSalt = 0x5A17u;

// 水域的单变量海拔阈值分类表。
constexpr std::array<std::pair<float, Biome>, 3> kWaterThresholds = {{
    {kDeepOceanMax, Biome::DeepOcean},
    {kOceanMax, Biome::Ocean},
    {kBeachMax, Biome::Beach},
}};

// Whittaker 生物群系图的简化映射。
struct BiomassProfile {
    float target;
    float regrowth;
};

constexpr auto biomass_for(Biome b) -> BiomassProfile {
    switch (b) {
        case Biome::DeepOcean:  return {0.0f, 0.0f};
        case Biome::Ocean:      return {0.0f, 0.0f};
        case Biome::Beach:      return {0.3f, 0.10f};
        case Biome::Grassland:  return {0.7f, 0.15f};
        case Biome::Forest:     return {0.9f, 0.10f};
        case Biome::Rainforest: return {1.0f, 0.10f};
        case Biome::Desert:     return {0.1f, 0.03f};
        case Biome::Mountain:   return {0.1f, 0.03f};
        case Biome::Snowcap:    return {0.1f, 0.03f};
        case Biome::Swamp:      return {0.6f, 0.08f};
        case Biome::Tundra:     return {0.2f, 0.05f};
    }
    return {0.0f, 0.0f};
}

auto classify_biome(float elevation, float temperature, float moisture) -> Biome {
    // 水域：单变量海拔阈值分类。
    if (elevation < kBeachMax) {
        return pcg::classify<Biome>(
            elevation, std::span<const std::pair<float, Biome>>{kWaterThresholds}, Biome::Beach);
    }

    // 山地与雪线。
    if (elevation >= kSnowcapMin) {
        return Biome::Snowcap;
    }
    if (elevation >= kMountainMin) {
        return Biome::Mountain;
    }

    // 陆地 Whittaker（多变量）。
    if (temperature < 0.20f) {
        return Biome::Tundra;
    }
    if (moisture < 0.25f) {
        return Biome::Desert;
    }
    if (moisture > 0.65f) {
        return temperature > 0.55f ? Biome::Rainforest : Biome::Swamp;
    }
    if (temperature > 0.45f && moisture > 0.40f) {
        return Biome::Forest;
    }
    return Biome::Grassland;
}

}  // namespace

WorldGenerator::WorldGenerator(std::size_t width, std::size_t height)
    : width_(width), height_(height) {}

auto WorldGenerator::generate(std::uint64_t seed) const -> World {
    World world{width_, height_};

    // 海拔噪声：分形 Perlin。
    pcg::NoiseField elevation_noise;
    elevation_noise.with_seed(pcg::derive_seed(seed, kElevationSalt))
        .with_noise_type(FastNoiseLite::NoiseType_Perlin)
        .with_fractal(FastNoiseLite::FractalType_FBm)
        .with_octaves(5)
        .with_frequency(0.015f);

    // 湿度噪声：分形 OpenSimplex2。
    pcg::NoiseField moisture_noise;
    moisture_noise.with_seed(pcg::derive_seed(seed, kMoistureSalt))
        .with_noise_type(FastNoiseLite::NoiseType_OpenSimplex2)
        .with_fractal(FastNoiseLite::FractalType_FBm)
        .with_octaves(4)
        .with_frequency(0.012f);

    const float height_float = static_cast<float>(height_);

    for (std::size_t y = 0; y < height_; ++y) {
        // 纬度：北(0) → 南(1)，北冷南热。
        const float latitude = static_cast<float>(y) / (height_float - 1.0f);
        for (std::size_t x = 0; x < width_; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);

            const float elevation = elevation_noise.sample(fx, fy);
            const float moisture = moisture_noise.sample(fx, fy);
            // 温度：纬度梯度 + 海拔修正（越高越冷）。
            const float temperature =
                std::clamp((1.0f - latitude) - elevation * 0.4f, 0.0f, 1.0f);

            auto& tile = world.at(x, y);
            tile.elevation = elevation;
            tile.moisture = moisture;
            tile.temperature = temperature;
            tile.biome = classify_biome(elevation, temperature, moisture);
            const auto profile = biomass_for(tile.biome);
            tile.biomass_target = profile.target;
            tile.biomass_regrowth = profile.regrowth;
            tile.biomass = profile.target;  // 初始即满载
        }
    }

    return world;
}

constexpr std::uint32_t kInitialCreatureSalt = 0x1C7Eu;

auto populate_initial_creatures(const World& world,
                                std::size_t count,
                                std::uint64_t seed) -> std::vector<Creature> {
    std::vector<Creature> out;
    out.reserve(count);

    // 收集所有陆地坐标。
    std::vector<Position> land_tiles;
    for (std::size_t y = 0; y < world.height(); ++y) {
        for (std::size_t x = 0; x < world.width(); ++x) {
            if (world.is_land(x, y)) land_tiles.push_back({x, y});
        }
    }
    if (land_tiles.empty()) return out;

    auto rng = std::mt19937_64{pcg::derive_seed(seed, kInitialCreatureSalt)};
    std::uniform_int_distribution<std::size_t> tile_pick(0, land_tiles.size() - 1);
    std::uniform_real_distribution<float> gene_pick(0.0f, 1.0f);

    for (std::size_t i = 0; i < count; ++i) {
        const Position& p = land_tiles[tile_pick(rng)];
        Traits gene;
        for (auto& v : gene.values) v = gene_pick(rng);
        Creature c;
        c.id = i + 1;
        c.pos = p;
        c.gene = gene;
        c.name = make_name(gene);
        c.hunger = 1.0f;
        c.energy = 1.0f;
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace game

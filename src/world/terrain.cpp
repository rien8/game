#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gamev2::world {
namespace {

/// 噪声特征横跨全图的个数，不是每格多少。取固定个数而非固定格距，
/// 是为了让「世界尺度」在任何图幅上都指同一件事：特征跟着图幅一起放大，
/// 汇水盆地才随图幅变大。固定格距会让盆地面积被特征尺寸钉死，
/// 水文阶段因而攒不出超过 kRiverThreshold 的汇流量。
constexpr std::uint32_t kElevationFeatures = 4;
constexpr double kElevationSpan = 100.0;

/// 由高程分位数反解海平面：取第 ocean_percent 分位以下的格子为海洋。
/// 反解而非固定常量，是为了让海陆比不随种子漂移。
std::uint8_t solve_sea_level(const Grid<std::uint8_t>& elevation,
                             std::uint8_t ocean_percent)
{
    std::array<std::uint32_t, 256> histogram{};
    for (std::uint32_t i = 0; i < elevation.size(); ++i)
    {
        ++histogram[elevation.at(i)];
    }

    const std::uint32_t cut =
        static_cast<std::uint32_t>(elevation.size()) * ocean_percent / 100;
    std::uint32_t seen = 0;
    for (std::uint32_t v = 0; v < histogram.size(); ++v)
    {
        seen += histogram[v];
        if (seen >= cut)
        {
            return static_cast<std::uint8_t>(v);
        }
    }
    return 255;
}

constexpr double kRainFreq = 6.0;
/// 降水衰减到零所需的离海格数。
constexpr double kRainReach = 40.0;
/// 雨影累积量除以它得到遮挡比例，120 格高度对应九成遮挡。
constexpr double kShadowGain = 120.0;

/// 每升高一个高程单位降多少温度 byte。最大落差约 50 个单位，
/// 对应约 32°C，与真实垂直递减率的量级一致。
constexpr double kLapseRate = 0.9;
/// 干燥区额外的温度惩罚上限。
constexpr double kDryPenalty = 20.0;
/// 赤道基线温度 byte。取 210 而非 198：198 按 celsius_of 读回只有 29°C，
/// 比热带覆写阈值低整整一度，三个热带群系因此在任何生成的世界上都不可达。
/// 210 读回 34°C，热带带才真正落在赤道附近而不是空集。
constexpr double kTropicalBaseline = 210.0;
/// 极地基线温度 byte，对应约 -25°C。
constexpr double kPolarBaseline = 42.0;

}  // namespace

void make_elevation(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::elevation));
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    const double freq = static_cast<double>(std::max(p.width, p.height))
                        / kElevationFeatures;

    std::vector<double> raw(n);
    double lo = 1.0;
    double hi = -1.0;
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const double v = fbm(seed, x / freq, y / freq, 3, 2.0, 0.5);
            const std::uint32_t i = world.elevation.index_of(x, y);
            raw[i] = v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }

    const double span = hi - lo;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = span > 0.0 ? (raw[i] - lo) / span : 0.0;
        world.elevation.at(static_cast<std::uint32_t>(i)) =
            static_cast<std::uint8_t>(std::lround(t * kElevationSpan));
    }

    world.sea_level_ = solve_sea_level(world.elevation, p.ocean_percent);
}

void make_rainfall(World& world)
{
    const WorldParams& p = world.params();
    const std::uint64_t seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::rain));
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    std::vector<std::uint32_t> distance(n, 0);
    std::vector<std::uint32_t> queue;
    queue.reserve(n);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            const double v = fbm(seed, x / kRainFreq, y / kRainFreq,
                                 3, 2.0, 0.5);
            world.rainfall.at(i) =
                static_cast<std::uint8_t>(std::lround((v + 1.0) * 127.5));
            if (world.is_ocean(x, y))
            {
                queue.push_back(i);
            }
        }
    }

    // 多源 BFS 求离海曼哈顿距离。4 邻域而非 8 邻域：与衰减按曼哈顿
    // 距离线性配套，两者必须一致。
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dy[4] = {0, 0, 1, -1};
    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const std::uint32_t i = queue[head];
        const auto x = static_cast<int>(i % p.width);
        const auto y = static_cast<int>(i / p.width);
        for (int d = 0; d < 4; ++d)
        {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            const std::uint32_t j =
                world.elevation.index_of(static_cast<std::uint32_t>(nx),
                                        static_cast<std::uint32_t>(ny));
            if (distance[j] != 0 || world.is_ocean(
                    static_cast<std::uint32_t>(nx),
                    static_cast<std::uint32_t>(ny)))
            {
                continue;
            }
            distance[j] = distance[i] + 1;
            queue.push_back(j);
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        const auto d = static_cast<double>(distance[i]);
        const double keep = std::max(0.0, 1.0 - d / kRainReach);
        world.rainfall.at(i) = static_cast<std::uint8_t>(
            std::lround(static_cast<double>(world.rainfall.at(i)) * keep));
    }

    // 雨影：沿固定主导风向逐行扫描，累积沿途抬升，越过高地后后续格
    // 降水被压低。风向固定是简化，DF 的风向随季节变化。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        double shadow = 0.0;
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            if (world.is_ocean(x, y))
            {
                shadow = 0.0;
                continue;
            }
            const double rise =
                static_cast<double>(world.elevation(x, y)) - world.sea_level();
            shadow = std::max(0.0, shadow - 1.0) + std::max(0.0, rise);
            const double shade = 1.0 - std::min(0.9, shadow / kShadowGain);
            const std::uint32_t i = world.elevation.index_of(x, y);
            world.rainfall.at(i) = static_cast<std::uint8_t>(std::lround(
                static_cast<double>(world.rainfall.at(i)) * shade));
        }
    }
}

void make_temperature(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    std::vector<double> baseline(n, 0.0);
    std::vector<double> ocean_sum(p.height, 0.0);
    std::vector<std::uint32_t> ocean_count(p.height, 0);

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        const double v =
            (static_cast<double>(y) + 0.5) / static_cast<double>(p.height);
        // v=0.5 为赤道，0 与 1 为两极。平方而非线性：赤道到中纬的温差
        // 大于中纬到极地。
        const double lat = std::abs(v - 0.5) * 2.0;
        const double t = kPolarBaseline
                         + (kTropicalBaseline - kPolarBaseline)
                                   * (1.0 - lat * lat);
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            baseline[i] = t;
            if (world.is_ocean(x, y))
            {
                ocean_sum[y] += t;
                ++ocean_count[y];
            }
        }
    }

    // 海洋向该纬度均值靠拢。没有这一步，内陆会被高度递减压得比同纬度
    // 海洋低一大截，而海洋会被纬度高估，呈现出明显失真的结果。
    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        if (ocean_count[y] == 0)
        {
            continue;
        }
        const double mean = ocean_sum[y] / ocean_count[y];
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            if (!world.is_ocean(x, y))
            {
                continue;
            }
            baseline[world.elevation.index_of(x, y)] = mean;
        }
    }

    for (std::uint32_t i = 0; i < n; ++i)
    {
        const double above = std::max(
            0.0, static_cast<double>(world.elevation.at(i)) - world.sea_level());
        const double dry =
            1.0 - static_cast<double>(world.rainfall.at(i)) / 255.0;
        const double t = baseline[i] - kLapseRate * above
                         - kDryPenalty * dry;
        world.temperature.at(i) = static_cast<std::uint8_t>(
            std::lround(std::clamp(t, 0.0, 255.0)));
    }
}

}  // namespace gamev2::world

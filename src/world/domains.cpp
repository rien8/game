#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {
namespace {

/// 贡献衰减到可以忽略的截止距离，以衰减尺度 lambda 为单位。
///
/// 截止距离按单只眼算与按叠加场算是两回事：单眼而言 exp(-3) 只有
/// 0.05，看着微不足道，但天下几十只眼叠起来就是 0.05 × 50 = 2.5 倍
/// 于一只眼的峰值，背景被抬成一片高原，福地门槛于是被「众眼尾巴」
/// 越过。1.5 的意思是尾巴仍留 22% 贡献（exp(-1.5) ≈ 0.22），为的是
/// 浓度场处处连续、没有硬边（设计 §3.4），不是为了把覆盖范围铺满全图。
constexpr double kDomainFalloffLambdas = 1.5;

/// 满流量灵眼在眼心贡献的字节数。流量除以参考流量后乘 255，于是
/// 「一只满流量的眼」在眼心贡献 255——这是**潮位缩放之前**的量，
/// 潮位 33 时它最终只落 33。
constexpr double kDomainByteScale = 255.0;

/// 潮位换算的除数。浓度场整体乘 level / 255。
constexpr double kTideByteScale = 255.0;

}  // namespace

void make_spirit_domains(World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;

    // 累加值用 double 存：单眼贡献可以远超 255，**逐眼从不封顶**，
    // 只有总和在潮位缩放之后才封顶。先夹紧会把灵潮期与谷底期读成
    // 同一张图。
    std::vector<double> acc(n, 0.0);

    for (const SpiritEye& eye : world.eyes)
    {
        // 枯眼没有流量，也不再生：它不贡献任何灵气。
        if (eye.depleted || eye.flow == 0)
        {
            continue;
        }
        if (eye.cell >= n)
        {
            // 眼位越界说明阶段 13 违约。跳过比越界写内存便宜。
            continue;
        }

        const double flow = static_cast<double>(eye.flow);
        const double lambda =
            kDomainRadius
            * std::sqrt(flow / static_cast<double>(kReferenceFlow));
        // 流量大的灵眼影响半径更大；上限是 kDomainMaxRadius。
        const int radius = static_cast<int>(
            std::min(static_cast<double>(kDomainMaxRadius),
                     std::ceil(lambda * kDomainFalloffLambdas)));
        if (radius <= 0)
        {
            continue;
        }

        const double peak =
            flow * kDomainByteScale / static_cast<double>(kReferenceFlow);
        const int ex = static_cast<int>(eye.cell % p.width);
        const int ey = static_cast<int>(eye.cell / p.width);

        for (int dy = -radius; dy <= radius; ++dy)
        {
            const int ny = ey + dy;
            if (ny < 0 || ny >= static_cast<int>(p.height))
            {
                continue;
            }
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int nx = ex + dx;
                if (nx < 0 || nx >= static_cast<int>(p.width))
                {
                    continue;
                }
                const double dist = std::sqrt(static_cast<double>(dx) * dx
                                             + static_cast<double>(dy) * dy);
                if (dist > static_cast<double>(radius))
                {
                    continue;
                }
                // 线性下标与眼位解码用的是同一个宽度，两者必须一致。
                const auto index =
                    static_cast<std::size_t>(ny) * p.width
                    + static_cast<std::size_t>(nx);
                acc[index] += peak * std::exp(-dist / lambda);
            }
        }
    }

    // 潮位缩放放在夹紧之前：累加值可以远超 255，先乘比例再夹，灵潮期与
    // 谷底期读出的才是同一张地脉网络的两种浓度。
    const double tide =
        static_cast<double>(world.spirit_tide.level) / kTideByteScale;
    for (std::size_t i = 0; i < n; ++i)
    {
        const long rounded = std::lround(acc[i] * tide);
        const auto byte = static_cast<std::uint8_t>(
            rounded < 0L ? 0L : (rounded > 255L ? 255L : rounded));
        world.domain_concentration.at(static_cast<std::uint32_t>(i)) = byte;
        world.domain_grade.at(static_cast<std::uint32_t>(i)) =
            domain_grade_of(byte);
    }
}

}  // namespace gamev2::world

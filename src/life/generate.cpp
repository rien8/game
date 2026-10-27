/// @file generate.cpp
/// @brief 众生层：人口生成与职位指派。
#include "generate.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/rng.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gamev2::life {
namespace {

/// 给一个宗门指派职位。[begin, begin+n) 是该宗在表里的区间。
void assign_offices(std::vector<PersonInit>& people, std::size_t begin,
                    std::uint32_t n)
{
    // 宗主与长老从筑基以上挑：境界是资历的硬门槛——金丹才脱离得了
    // 宗门供给体系，掌门至少得是能自立的人（设定 §5.1）。
    std::vector<std::size_t> leaders;
    for (std::size_t i = begin; i < begin + n; ++i)
    {
        const auto realm = static_cast<std::size_t>(people[i].realm);
        if (realm >= static_cast<std::size_t>(Realm::zhuji))
        {
            leaders.push_back(i);
        }
    }
    // 境界高的在前；stable_sort 让同境界保持创建序（资历）。
    std::stable_sort(leaders.begin(), leaders.end(),
                     [&](std::size_t a, std::size_t b) {
                         return people[a].realm > people[b].realm;
                     });

    const std::uint32_t elders =
        std::max(kMinElders, n / kElderDivisor);
    /// 逐境界截断求和：筑基 60×1200/10000=7、金丹 2、元婴 0，最小宗也有 9 人。
    /// 常数表若改到撑不起职位，先在这里红。
    assert(leaders.size() >= 1u + elders);

    people[leaders[0]].office = OfficeKind::sect_master;
    for (std::uint32_t i = 1; i <= elders; ++i)
    {
        people[leaders[i]].office = OfficeKind::elder;
    }

    // 其余人按创建序填：内门先来，余下的全是外门。
    const std::uint32_t inner_quota = n * kInnerNum / kInnerDen;
    std::uint32_t inner = 0;
    for (std::size_t i = begin; i < begin + n; ++i)
    {
        if (people[i].office != OfficeKind::none)
        {
            continue;
        }
        people[i].office =
            (inner < inner_quota) ? OfficeKind::inner : OfficeKind::outer;
        ++inner;
    }
}

}  // namespace

/// 一个人的全部随机字段。无状态派生：只依赖 (seed, 流号, 序号)，
/// 与遍历顺序无关（设计 §4.1——调整循环次序不得换一个世界）。
PersonInit make_person(std::uint64_t seed, std::uint32_t index,
                       history::Sect sect, Realm realm)
{
    const auto stream = static_cast<std::uint32_t>(world::Stream::life);
    world::Pcg32 rng(
        world::stage_seed(world::stage_seed(seed, stream), index));

    PersonInit p;
    p.name = std::string(kSurnames[rng.below(
                 static_cast<std::uint32_t>(kSurnames.size()))])
             + std::string(kGivenNames[rng.below(
                 static_cast<std::uint32_t>(kGivenNames.size()))]);
    p.sect = sect;
    p.realm = realm;

    // 年龄折半封顶：名册里不该有「行将就木还只是引气」的人；寿元的
    // 硬上限由 kRealmLifespan 的测试从名册侧独立复算。
    const auto lifespan = kRealmLifespan[static_cast<std::size_t>(realm)];
    p.age = kMinAge + rng.below(lifespan / 2u - kMinAge);

    p.temper.greed = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.valor = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.order = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.mercy = static_cast<std::uint8_t>(1u + rng.below(5));
    p.temper.vigor = static_cast<std::uint8_t>(1u + rng.below(5));
    p.aptitude.comprehension = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.sense = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.body = static_cast<std::uint8_t>(1u + rng.below(9));
    p.aptitude.fortitude = static_cast<std::uint8_t>(1u + rng.below(9));
    // 性别 1:1 均匀，标定值（设定未给分布，男女对玩法无别）。派生放
    // 抽签序列末尾：插进中间会平移同 seed 的全部既有名册，刀 1/2 钉住
    // 的世界会无故换掉。
    p.sex = rng.below(2u) == 0u ? Sex::male : Sex::female;
    p.office = OfficeKind::none;
    return p;
}

std::vector<PersonInit> generate_population(std::uint64_t seed)
{
    std::vector<PersonInit> out;
    out.reserve(970);  // kPopulation 之和；只是保留，改常数表不破

    for (std::size_t s = 0; s < kPopulation.size(); ++s)
    {
        const auto sect = static_cast<history::Sect>(s);
        const std::uint32_t n = kPopulation[s];
        const auto& pyramid = (sect == history::Sect::unowned)
                                  ? kFreePyramid
                                  : kSectPyramid;

        const std::size_t begin = out.size();

        // 金字塔按万分比截断：余数给引气，总数必须等于 n——截断偏差
        // 不许改变人口规模，否则名册规模会随常数表漂移出测试的预期。
        std::array<std::uint32_t, static_cast<std::size_t>(Realm::count)>
            realms{};
        std::uint32_t placed = 0;
        for (std::size_t r = 0; r < realms.size(); ++r)
        {
            realms[r] = n * pyramid[r] / 10000u;
            placed += realms[r];
        }
        realms[0] += n - placed;

        for (std::size_t r = 0; r < realms.size(); ++r)
        {
            for (std::uint32_t i = 0; i < realms[r]; ++i)
            {
                out.push_back(make_person(
                    seed, static_cast<std::uint32_t>(out.size()), sect,
                    static_cast<Realm>(r)));
            }
        }

        if (sect != history::Sect::unowned)
        {
            assign_offices(out, begin, n);
        }
    }
    return out;
}

}  // namespace gamev2::life

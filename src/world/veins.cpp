#include "gamev2/world/generate.hpp"
#include "gamev2/world/rng.hpp"
#include "gamev2/world/spirit_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::world {
namespace {

/// 浅埋权重。分越浅，得分越高。
constexpr std::uint32_t kShallownessScale = 1000;

/// 汇流加成。每多一条通道汇入，浓度跳升一档。
constexpr std::uint32_t kConfluenceBonus = 2;

/// 稳定年限的基数与每节点增量。浓度持续得越久，这条脉越稳定。
constexpr std::uint32_t kStabilityBase = 100;
constexpr std::uint32_t kStabilityPerNode = 50;

/// 被越采后储量的削减比例（百分比）。
constexpr std::uint32_t kOverminePenalty = 60;

/// 一段候选灵脉：某条通道上的一段连续节点。
struct Segment
{
    std::uint32_t channel = 0;
    std::uint32_t first = 0;   ///< 段首在 channel.nodes 中的下标
    std::uint32_t last = 0;    ///< 段尾，闭区间
    std::uint32_t reserve = 0;
    std::uint32_t stability = 0;
    std::uint32_t peak_node = 0;  ///< 段内得分最高的节点，即灵眼位置
    bool overmined = false;
};

/// 浓度得分。三个因子相乘：任何一项为零则该格不成脉，
/// 符合「灵脉是地脉抬升段」的定义。
std::uint64_t raw_score(const LeyNode& node)
{
    const std::uint32_t shallowness =
        kShallownessScale - node.depth * kShallownessScale / kMaxLeyDepth;
    const std::uint32_t confluence =
        1u + static_cast<std::uint32_t>(kConfluenceBonus) * node.merges;
    return static_cast<std::uint64_t>(node.flow) * shallowness * confluence;
}

/// 按阈值把每条通道切成若干段。
std::vector<Segment> segment_channels(const World& world,
                                      const std::vector<std::uint32_t>& scores,
                                      std::uint8_t threshold)
{
    std::vector<Segment> segments;
    for (std::uint32_t c = 0; c < world.ley_channels.size(); ++c)
    {
        const std::vector<std::uint32_t>& nodes = world.ley_channels[c].nodes;
        std::uint32_t start = 0;
        bool inside = false;
        for (std::uint32_t k = 0; k < nodes.size(); ++k)
        {
            const bool high = scores[nodes[k]] >= threshold;
            if (high && !inside)
            {
                start = k;
                inside = true;
            }
            else if (!high && inside)
            {
                Segment s;
                s.channel = c;
                s.first = start;
                s.last = k - 1;
                s.peak_node = nodes[start];
                for (std::uint32_t j = start; j <= s.last; ++j)
                {
                    s.reserve += scores[nodes[j]];
                    if (scores[nodes[j]] > scores[s.peak_node])
                    {
                        s.peak_node = nodes[j];
                    }
                }
                s.stability = kStabilityBase
                              + kStabilityPerNode * (s.last - s.first + 1);
                segments.push_back(s);
                inside = false;
            }
        }
        if (inside)
        {
            Segment s;
            s.channel = c;
            s.first = start;
            s.last = static_cast<std::uint32_t>(nodes.size()) - 1;
            s.peak_node = nodes[start];
            for (std::uint32_t j = start; j <= s.last; ++j)
            {
                s.reserve += scores[nodes[j]];
                if (scores[nodes[j]] > scores[s.peak_node])
                {
                    s.peak_node = nodes[j];
                }
            }
            s.stability = kStabilityBase
                          + kStabilityPerNode * (s.last - s.first + 1);
            segments.push_back(s);
        }
    }
    return segments;
}

/// 段的身份种子。段的身份是「通道号 + 段首下标」：与名次无关，也与段
/// 被发现的顺序无关，因此同一个段在任何世界里都得到同一个种子。
std::uint64_t segment_seed(std::uint64_t vein_seed, const Segment& s)
{
    const std::uint64_t by_channel = stage_seed(vein_seed, s.channel);
    return stage_seed(by_channel, s.first);
}

/// 标记被越采的段，并相应压低储量。
///
/// 判据只看段的身份（通道号 + 段首下标）而**不看名次**：名次要靠储量与
/// 稳定年限算出来，若「谁被采」又依赖名次，就成了一圈套。
///
/// 抽样走 Pcg32 的均匀源，不走 value_noise：value_noise 是四个格点哈希
/// 的插值，样本挤在 0.5 附近，拿它跟 0.15 比实际只命中约 3%。
///
/// 越采只削储量，不动稳定年限：设计文档步骤 5.1 只说削减储量。两处「被采」
/// 各打一个品阶因子——越采打储量，枯眼才打稳定年限。
void mark_overmined(std::vector<Segment>& segments, std::uint64_t vein_seed)
{
    for (Segment& s : segments)
    {
        Pcg32 rng(segment_seed(vein_seed, s));
        if (rng.unit() >= kOverminedChance)
        {
            continue;
        }
        s.overmined = true;
        s.reserve = s.reserve * (100u - kOverminePenalty) / 100u;
    }
}

/// 灵眼的身份键。灵眼的身份是它的出露点节点：与名次无关，也与灵眼在
/// 列表里的位置无关，因此同一个出露点在任何世界里都得到同一个键。
double eye_key(std::uint64_t vein_seed, std::uint32_t node)
{
    Pcg32 rng(stage_seed(vein_seed, node));
    return rng.unit();
}

/// 挑出枯眼。枯眼数 = round(灵眼数 × kDepletedRatio)。
///
/// 判据是灵眼的**身份**（出露点所在的节点），不是名次：灵眼集合本身就是
/// 排名的产物，按名次挑等于「排在前面的更容易被抽干」。给每个灵眼一个
/// 身份键、再取键最小的若干个，等价于从全部灵眼里均匀抽一个子集。
///
/// 枯眼必须排在排名之后：它会让该脉的稳定年限骤降，若在排名前定，这些
/// 脉会被挤出名次之外，而没有品阶就没有灵眼，枯眼反而无从产生。
void mark_depleted_eyes(World& world, std::uint64_t vein_seed)
{
    if (world.eyes.empty())
    {
        return;
    }
    const auto wanted = static_cast<std::size_t>(std::lround(
        static_cast<double>(world.eyes.size()) * kDepletedRatio));
    if (wanted == 0)
    {
        return;
    }

    std::vector<std::pair<double, std::uint32_t>> keyed;
    keyed.reserve(world.eyes.size());
    for (std::uint32_t i = 0; i < world.eyes.size(); ++i)
    {
        keyed.emplace_back(eye_key(vein_seed, world.eyes[i].node), i);
    }
    // 键相等时按下标定序，保证同一批键总是挑出同一批眼。
    std::sort(keyed.begin(), keyed.end());

    const std::size_t take = std::min(wanted, keyed.size());
    for (std::size_t k = 0; k < take; ++k)
    {
        SpiritEye& eye = world.eyes[keyed[k].second];
        eye.depleted = true;
        eye.flow = 0;
        // 稳定年限降下来是「本轮看到的状态」，不回灌本轮的排名。
        world.veins[eye.vein].stability = kStabilityMined;
    }
}

}  // namespace

void make_spirit_veins(World& world)
{
    const WorldParams& p = world.params();
    world.veins.clear();
    world.eyes.clear();
    for (std::uint32_t i = 0; i < world.vein_rank.size(); ++i)
    {
        world.vein_rank.at(i) = VeinRank::none;
        world.vein_id.at(i) = kNoVein;
        world.eye_flow.at(i) = 0u;
    }
    if (world.ley_nodes.empty())
    {
        return;
    }

    // 得分归一化到 0..255。归一化让切段阈值不随图幅规模漂移。
    std::vector<std::uint64_t> raws(world.ley_nodes.size(), 0);
    std::uint64_t max_raw = 0;
    for (std::uint32_t i = 0; i < world.ley_nodes.size(); ++i)
    {
        raws[i] = raw_score(world.ley_nodes[i]);
        max_raw = std::max(max_raw, raws[i]);
    }
    std::vector<std::uint32_t> scores(world.ley_nodes.size(), 0);
    if (max_raw > 0)
    {
        for (std::uint32_t i = 0; i < world.ley_nodes.size(); ++i)
        {
            scores[i] = static_cast<std::uint32_t>(raws[i] * 255u / max_raw);
        }
    }

    const auto target = vein_targets(p.width * p.height);
    const std::uint32_t wanted =
        target.dragon + target.main + target.branch;

    // 切段。阈值下调会扩大合格集，但也会把原本分开的极大区间并起来，
    // 段数因此未必单调增加——所以每次切段都和「目前最好的那组」比较，
    // 留下段数最多的那组，段数相同时保留先到的那组。
    //
    // 目标是「比定额多一条」：多出来的那条进荒脉桶，否则荒脉可能一条都
    // 没有，设定里的「残余矿化」就无处落地。
    std::uint8_t threshold = kVeinThreshold;
    std::vector<Segment> segments;
    for (;;)
    {
        std::vector<Segment> cut = segment_channels(world, scores, threshold);
        if (cut.size() > segments.size())
        {
            segments = std::move(cut);
        }
        if (segments.size() > wanted)
        {
            break;
        }
        if (threshold <= kVeinThresholdFloor)
        {
            break;
        }
        const auto next = static_cast<int>(threshold) - kVeinThresholdStep;
        threshold = static_cast<std::uint8_t>(
            std::max<int>(next, kVeinThresholdFloor));
    }

    const std::uint64_t vein_seed =
        stage_seed(p.seed, static_cast<std::uint32_t>(Stream::vein));
    mark_overmined(segments, vein_seed);

    // 排名键：储量 × 稳定年限。设定 2.2 的品阶标准。
    std::vector<std::uint32_t> order(segments.size());
    for (std::uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&segments](std::uint32_t a, std::uint32_t b) {
                  const std::uint64_t ka =
                      static_cast<std::uint64_t>(segments[a].reserve)
                      * segments[a].stability;
                  const std::uint64_t kb =
                      static_cast<std::uint64_t>(segments[b].reserve)
                      * segments[b].stability;
                  if (ka != kb)
                  {
                      return ka > kb;
                  }
                  return a < b;
              });

    for (std::uint32_t rank = 0; rank < order.size(); ++rank)
    {
        const Segment& s = segments[order[rank]];
        SpiritVein vein;
        vein.id = rank;
        vein.channel = s.channel;
        vein.first_node = s.first;
        vein.last_node = s.last;
        vein.reserve = s.reserve;
        vein.stability = s.stability;
        vein.overmined = s.overmined;
        vein.element = world.ley_nodes[s.peak_node].element;

        if (rank == 0)
        {
            vein.rank = VeinRank::dragon;
        }
        else if (rank < target.dragon + target.main)
        {
            vein.rank = VeinRank::main;
        }
        else if (rank < target.dragon + target.main + target.branch)
        {
            vein.rank = VeinRank::branch;
        }
        else
        {
            vein.rank = VeinRank::barren;
        }

        // 灵眼只出露在龙、主、支三条品阶上；荒脉储量太低，采不出东西。
        if (vein.rank != VeinRank::barren)
        {
            SpiritEye eye;
            eye.vein = vein.id;
            eye.node = s.peak_node;
            eye.cell = world.ley_nodes[s.peak_node].cell;
            // 流量是这条脉的物理吞吐，不随潮位变。潮位缩放的是环境浓度场
            // （阶段 14），两处都乘就等于把灵潮算了两遍。
            eye.flow = world.ley_nodes[s.peak_node].flow;
            vein.eye = static_cast<std::uint32_t>(world.eyes.size());
            world.eyes.push_back(eye);
        }

        // 镜像是格级的：段覆盖的每一格都标上品阶与编号。
        const std::vector<std::uint32_t>& nodes =
            world.ley_channels[s.channel].nodes;
        for (std::uint32_t j = s.first; j <= s.last; ++j)
        {
            const std::uint32_t cell = world.ley_nodes[nodes[j]].cell;
            world.vein_rank.at(cell) = vein.rank;
            world.vein_id.at(cell) = vein.id;
        }

        world.veins.push_back(vein);
    }

    // 枯眼在灵眼集合确定之后挑：它压低稳定年限，而稳定年限是排名键。
    mark_depleted_eyes(world, vein_seed);

    // 灵眼流量也写进格级网格，方便按坐标查询。枯眼的流量已经是零。
    for (const SpiritEye& eye : world.eyes)
    {
        world.eye_flow.at(eye.cell) = eye.flow;
    }
}

VeinCensus vein_census(const World& world)
{
    const WorldParams& p = world.params();
    const auto target = vein_targets(p.width * p.height);

    VeinCensus census;
    census.dragon.expected = target.dragon;
    census.main.expected = target.main;
    census.branch.expected = target.branch;
    for (const SpiritVein& v : world.veins)
    {
        switch (v.rank)
        {
        case VeinRank::dragon:
            ++census.dragon.got;
            break;
        case VeinRank::main:
            ++census.main.got;
            break;
        case VeinRank::branch:
            ++census.branch.got;
            break;
        case VeinRank::barren:
            ++census.barren;
            break;
        case VeinRank::none:
            break;
        }
    }

    census.eyes = static_cast<std::uint32_t>(world.eyes.size());
    for (const SpiritEye& e : world.eyes)
    {
        if (e.depleted)
        {
            ++census.depleted.got;
        }
    }
    // 应得枯眼数由灵眼数定：灵眼数是固定的（有品阶的脉恒为 1+9+40 条），
    // 枯眼数因此也是固定的。小图幅上灵眼少，应得枯眼数可能不到一条——
    // 那时「应得 0、实得 0」比沉默更清楚。
    census.depleted.expected = static_cast<std::uint32_t>(std::lround(
        static_cast<double>(census.eyes) * kDepletedRatio));
    return census;
}

bool veins_complete(const World& world)
{
    // 无地脉的世界（极端参数下源点选不出）灵脉数自然为 0，缺口等于应得
    // 数——这是设计文档 §5 说的「退化」，世界仍然有效，缺口由诊断量报出，
    // 不是生成失败。
    return vein_census(world).total_shortfall() == 0u;
}

}  // namespace gamev2::world

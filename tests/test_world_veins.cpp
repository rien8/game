#include "gamev2/world/generate.hpp"
#include "gamev2/world/spirit.hpp"
#include "gamev2/world/spirit_tables.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using gamev2::world::LeyNode;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritVein;
using gamev2::world::VeinRank;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::kDepletedRatio;
using gamev2::world::kNoEye;
using gamev2::world::kOverminedChance;
using gamev2::world::kStabilityMined;
using gamev2::world::raw_score;
using gamev2::world::vein_targets;

WorldParams big_params(std::uint64_t seed)
{
    WorldParams p;
    p.width = 256;
    p.height = 256;
    // 灵气层只吃地表与岩层，z_levels 取 1 让这个用例跑得快些。
    p.z_levels = 1;
    p.seed = seed;
    return p;
}

std::uint32_t count_rank(const World& w, VeinRank rank)
{
    std::uint32_t n = 0;
    for (const SpiritVein& v : w.veins)
    {
        if (v.rank == rank)
        {
            ++n;
        }
    }
    return n;
}

std::uint32_t count_depleted(const World& w)
{
    std::uint32_t n = 0;
    for (const SpiritEye& e : w.eyes)
    {
        if (e.depleted)
        {
            ++n;
        }
    }
    return n;
}

/// 这条脉有没有被抽干。枯眼是「灵眼被抽干」的结果，它压低该脉的稳定年限。
bool is_depleted(const World& w, const SpiritVein& v)
{
    return v.eye != kNoEye && w.eyes[v.eye].depleted;
}

TEST(vein_counts_match_the_setting_exactly)
{
    // 设定 2.2：龙脉 1、主脉 9、支脉约 40。这三个数是硬约束，
    // 由排名保证，不靠阈值碰巧产生。
    const WorldParams p = big_params(2026);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    CHECK_EQ(count_rank(w, VeinRank::dragon), 1u);
    CHECK_EQ(count_rank(w, VeinRank::main), 9u);
    CHECK_EQ(count_rank(w, VeinRank::branch), 40u);
}

TEST(vein_target_formula_agrees_with_the_generated_world)
{
    const WorldParams p = big_params(4242);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const auto target = vein_targets(p.width * p.height);
    CHECK_EQ(count_rank(w, VeinRank::dragon), target.dragon);
    CHECK_EQ(count_rank(w, VeinRank::main), target.main);
    CHECK_EQ(count_rank(w, VeinRank::branch), target.branch);
}

TEST(every_vein_is_a_contiguous_run_of_one_channel)
{
    const WorldParams p = big_params(7);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const SpiritVein& v : w.veins)
    {
        CHECK(v.channel < w.ley_channels.size());
        CHECK(v.first_node <= v.last_node);
        const auto& nodes = w.ley_channels[v.channel].nodes;
        CHECK(v.last_node < nodes.size());
        // 不变式 19：这条脉覆盖的节点在所属通道里必须是**相邻的一段**。
        // 段内每一格都标着它，段外紧挨着的格不能标着它。
        for (std::uint32_t j = v.first_node; j <= v.last_node; ++j)
        {
            const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
            CHECK(w.vein_id.at(cell) == v.id);
            CHECK(w.vein_rank.at(cell) == v.rank);
        }
        if (v.first_node > 0)
        {
            const std::uint32_t before =
                w.ley_nodes[nodes[v.first_node - 1]].cell;
            CHECK(w.vein_id.at(before) != v.id);
        }
        if (v.last_node + 1 < nodes.size())
        {
            const std::uint32_t after =
                w.ley_nodes[nodes[v.last_node + 1]].cell;
            CHECK(w.vein_id.at(after) != v.id);
        }
    }
}

TEST(dragon_main_and_branch_veins_each_have_one_eye)
{
    const WorldParams p = big_params(31337);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t eyes = 0;
    for (const SpiritVein& v : w.veins)
    {
        if (v.rank == VeinRank::barren || v.rank == VeinRank::none)
        {
            // 荒脉储量太低，出不了灵眼。
            CHECK(v.eye == kNoEye);
            continue;
        }
        CHECK(v.eye != kNoEye);
        CHECK(v.eye < w.eyes.size());
        ++eyes;
    }
    CHECK_EQ(eyes, w.eyes.size());
}

TEST(barren_veins_exist)
{
    // 荒脉「不可考」——数量不定，但必须存在，否则「残余矿化」无处落地。
    // 它是「曾经有油、被采完了」的残骸，因此一条都不能没有。
    //
    // 覆盖多个种子：切段循环若退回「段数达标即停」（`>=` 而不是 `>`），
    // 恰好切出定额条数时荒脉就是 0 条。单一种子未必撞得上那个数，
    // 所以这里把几个已知会撞上的种子一并钉住。
    for (const std::uint64_t seed : {555ull, 15202ull, 23306ull, 27358ull,
                                     40527ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        CHECK(count_rank(*generated, VeinRank::barren) > 0u);
    }
}

TEST(eye_flow_is_positive_unless_the_eye_is_depleted)
{
    const WorldParams p = big_params(8888);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    std::uint32_t depleted = 0;
    for (const SpiritEye& e : w.eyes)
    {
        if (e.depleted)
        {
            // 枯眼：抽干后不可复生，流量必须是零。
            CHECK_EQ(e.flow, 0u);
            CHECK_EQ(w.eye_flow.at(e.cell), 0u);
            ++depleted;
        }
        else
        {
            CHECK(e.flow > 0u);
            CHECK_EQ(w.eye_flow.at(e.cell), e.flow);
        }
    }
    // 枯眼必须真的存在：设定 2.3 专门为它写了一条红线。
    CHECK(depleted > 0u);
}

TEST(depleted_eye_count_follows_the_setting_ratio)
{
    // 不变式 21：枯眼数 == round(灵眼数 × kDepletedRatio)。灵眼数是固定的
    // （有品阶的脉恒为 1+9+40 = 50），所以枯眼数也是固定的。
    const WorldParams p = big_params(4242);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const auto expected = static_cast<std::uint32_t>(std::lround(
        static_cast<double>(w.eyes.size()) * kDepletedRatio));
    CHECK_EQ(count_depleted(w), expected);
    CHECK(expected > 0u);
}

TEST(every_world_has_its_depleted_eyes)
{
    // 设定 2.3 的红线不是「看运气」：枯眼数只由灵眼数决定，所以每个世界
    // 都必须有枯眼。越采与否影响不到这一点——两者是两条独立的因果。
    for (const std::uint64_t seed : {2026ull, 4242ull, 7ull, 31337ull, 555ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        CHECK(count_depleted(*generated) > 0u);
    }
}

TEST(depleted_eyes_crush_the_vein_stability)
{
    // 设定 2.2：被采过的脉稳定年限骤降。现在「被采」有两处，各打一个品阶
    // 因子——越采打储量、枯眼打稳定年限，因此稳定年限骤降的是枯眼那条脉。
    const WorldParams p = big_params(24680);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (const SpiritVein& v : w.veins)
    {
        if (is_depleted(w, v))
        {
            CHECK_EQ(v.stability, kStabilityMined);
        }
        else
        {
            CHECK(v.stability > kStabilityMined);
        }
    }
    // 反方向同样要成立：稳定年限塌到 kStabilityMined 的，一定是枯眼那条脉。
    // 枯眼一个都没有时，就不该有任何脉的稳定年限塌到那个值。
    for (const SpiritVein& v : w.veins)
    {
        if (v.stability == kStabilityMined)
        {
            CHECK(is_depleted(w, v));
        }
    }
}

TEST(some_veins_are_overmined)
{
    // 越采判据是均匀抽样，命中率应当接近 kOverminedChance。抽样源若换成
    // 插值噪声，命中率会塌到 3% 左右——这条用例就是把那种退化钉住。
    // 这里是聚合统计：四个世界两百来个样本上的 15% 期望值，区间取
    // ±3.3σ 以外才算异常。
    std::uint32_t total = 0;
    std::uint32_t overmined = 0;
    for (const std::uint64_t seed : {2026ull, 4242ull, 7ull, 31337ull})
    {
        const auto generated = gamev2::world::generate_world(big_params(seed));
        CHECK(generated.has_value());
        if (!generated)
        {
            return;
        }
        const World& w = *generated;
        for (const SpiritVein& v : w.veins)
        {
            ++total;
            if (v.overmined)
            {
                ++overmined;
            }
        }
    }
    CHECK(total > 0u);
    const double rate = static_cast<double>(overmined)
                        / static_cast<double>(total);
    CHECK(rate > 0.07);
    CHECK(rate < 0.24);
    // 名义概率本身也要钉住：改小它，这条用例就该红。
    CHECK(kOverminedChance > 0.0);
}

TEST(overmined_vein_reserve_is_the_penalised_score_sum)
{
    // 越采削掉的比例是**定值**（kOverminePenalty），不是「约略削一点」。
    // 时序层要靠它反推 8300 年的承载上限，那边有一条反推断言；但削减本身
    // 只有 worldgen 能看见，改这个数必须两侧一起红。
    //
    // 储量就是这条脉自己那几个节点的得分和，越采后再乘 (100 − 它)/100，
    // 所以这里只需复算得分，不必复刻切段与排名：
    //   未越采：reserve == Σ得分
    //   越采：  reserve == floor(Σ得分 × (100 − 它) / 100)
    //
    // 未越采那一半是另一半的判据：得分复算一旦与实现脱节，先红的是它。
    const WorldParams p = big_params(2026);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;

    // 得分归一化与 veins.cpp 一致：raw × 255 ÷ 全图最大值。
    std::vector<std::uint64_t> raws(w.ley_nodes.size(), 0);
    std::uint64_t max_raw = 0;
    for (std::uint32_t i = 0; i < w.ley_nodes.size(); ++i)
    {
        raws[i] = raw_score(w.ley_nodes[i]);
        if (raws[i] > max_raw)
        {
            max_raw = raws[i];
        }
    }
    std::vector<std::uint32_t> scores(w.ley_nodes.size(), 0);
    if (max_raw > 0)
    {
        for (std::uint32_t i = 0; i < w.ley_nodes.size(); ++i)
        {
            scores[i] = static_cast<std::uint32_t>(raws[i] * 255u / max_raw);
        }
    }

    std::uint32_t penalised = 0;
    for (const SpiritVein& v : w.veins)
    {
        const std::vector<std::uint32_t>& nodes =
            w.ley_channels[v.channel].nodes;
        std::uint32_t sum = 0;
        for (std::uint32_t j = v.first_node; j <= v.last_node; ++j)
        {
            sum += scores[nodes[j]];
        }
        if (v.overmined)
        {
            ++penalised;
            // 40 是字面量，**不是** kOverminePenalty 的别名：引用同一个
            // 常量算期望值的话，改了它生产与断言一起漂移，这条永远红不了。
            // 改这个数必须同时改这里与时序层的反推（那边同样是字面量 40）。
            CHECK_EQ(v.reserve, sum * 40u / 100u);
        }
        else
        {
            CHECK_EQ(v.reserve, sum);
        }
    }
    // 守卫：fixture 一旦不产出越采脉，前一半断言整条空跑，比例就没人钉。
    CHECK(penalised > 0);
}

TEST(vein_grids_agree_with_the_vein_list)
{
    const WorldParams p = big_params(99);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    for (std::uint32_t i = 0; i < w.vein_rank.size(); ++i)
    {
        const auto rank = w.vein_rank.at(i);
        if (rank == VeinRank::none)
        {
            CHECK(w.vein_id.at(i) == gamev2::world::kNoVein);
            continue;
        }
        CHECK(w.vein_id.at(i) < w.veins.size());
        CHECK(w.veins[w.vein_id.at(i)].rank == rank);
    }
    // 反方向：每条脉覆盖的格都标着它，而且格数不多不少。
    std::uint32_t grid_marked = 0;
    for (std::uint32_t i = 0; i < w.vein_id.size(); ++i)
    {
        if (w.vein_id.at(i) != gamev2::world::kNoVein)
        {
            ++grid_marked;
        }
    }
    std::uint32_t vein_marked = 0;
    for (const SpiritVein& v : w.veins)
    {
        const auto& nodes = w.ley_channels[v.channel].nodes;
        for (std::uint32_t j = v.first_node; j <= v.last_node; ++j)
        {
            const std::uint32_t cell = w.ley_nodes[nodes[j]].cell;
            CHECK(w.vein_id.at(cell) == v.id);
            CHECK(w.vein_rank.at(cell) == v.rank);
        }
        vein_marked += v.last_node - v.first_node + 1;
    }
    CHECK_EQ(grid_marked, vein_marked);
}

TEST(vein_layer_is_deterministic)
{
    const WorldParams p = big_params(2718);
    const auto a = gamev2::world::generate_world(p);
    const auto b = gamev2::world::generate_world(p);
    CHECK(a.has_value());
    CHECK(b.has_value());
    if (!a || !b)
    {
        return;
    }
    CHECK_EQ(a->veins.size(), b->veins.size());
    for (std::size_t i = 0; i < a->veins.size(); ++i)
    {
        CHECK_EQ(a->veins[i].rank, b->veins[i].rank);
        CHECK_EQ(a->veins[i].reserve, b->veins[i].reserve);
        CHECK_EQ(a->veins[i].stability, b->veins[i].stability);
        CHECK_EQ(a->veins[i].overmined, b->veins[i].overmined);
        // 位置与身份也要比对：灵眼落在哪、属于哪条脉、覆盖哪一段，
        // 都不能在两次生成之间漂移。
        CHECK_EQ(a->veins[i].channel, b->veins[i].channel);
        CHECK_EQ(a->veins[i].first_node, b->veins[i].first_node);
        CHECK_EQ(a->veins[i].last_node, b->veins[i].last_node);
        CHECK_EQ(a->veins[i].element, b->veins[i].element);
        CHECK_EQ(a->veins[i].eye, b->veins[i].eye);
    }
    CHECK_EQ(a->eyes.size(), b->eyes.size());
    for (std::size_t i = 0; i < a->eyes.size(); ++i)
    {
        CHECK_EQ(a->eyes[i].vein, b->eyes[i].vein);
        CHECK_EQ(a->eyes[i].node, b->eyes[i].node);
        CHECK_EQ(a->eyes[i].cell, b->eyes[i].cell);
        CHECK_EQ(a->eyes[i].flow, b->eyes[i].flow);
        CHECK_EQ(a->eyes[i].depleted, b->eyes[i].depleted);
    }
    for (std::uint32_t i = 0; i < a->eye_flow.size(); ++i)
    {
        CHECK_EQ(a->eye_flow.at(i), b->eye_flow.at(i));
    }
}

TEST(an_incomplete_vein_layer_is_reported_as_incomplete)
{
    // 数量不足必须**看得见**：vein_census 要报出缺口，veins_complete 要
    // 据此说「不够」。设计文档 §5 说灵气层不引入新的失败模式，所以这
    // 是诊断信号而不是生成失败——世界照常产出。
    WorldParams p;
    p.width = 256;
    p.height = 256;
    p.z_levels = 1;
    World w(p);
    // 什么都没有：三条定额一条都不满足，缺口等于应得数。
    const gamev2::world::VeinCensus empty = gamev2::world::vein_census(w);
    CHECK_EQ(empty.dragon.got, 0u);
    CHECK_EQ(empty.dragon.expected, 1u);
    CHECK_EQ(empty.dragon.shortfall(), 1u);
    CHECK_EQ(empty.main.shortfall(), 9u);
    CHECK_EQ(empty.branch.shortfall(), 40u);
    CHECK_EQ(empty.total_shortfall(), 50u);
    CHECK(!gamev2::world::veins_complete(w));

    // 有地脉却切不出灵脉：同样按定额报缺口。
    w.ley_nodes.push_back(LeyNode{});
    const gamev2::world::VeinCensus short1 = gamev2::world::vein_census(w);
    CHECK_EQ(short1.total_shortfall(), 50u);
    CHECK(!gamev2::world::veins_complete(w));
}

TEST(a_complete_vein_layer_reports_no_shortfall)
{
    // 参考图幅上定额必须凑齐：缺口为零，且实得数与 vein_targets 一致。
    const WorldParams p = big_params(79203);
    const auto generated = gamev2::world::generate_world(p);
    CHECK(generated.has_value());
    if (!generated)
    {
        return;
    }
    const World& w = *generated;
    const gamev2::world::VeinCensus census = gamev2::world::vein_census(w);
    CHECK_EQ(census.dragon.got, 1u);
    CHECK_EQ(census.main.got, 9u);
    CHECK_EQ(census.branch.got, 40u);
    CHECK_EQ(census.total_shortfall(), 0u);
    CHECK(gamev2::world::veins_complete(w));
    // 荒脉与枯眼也要在诊断量里看得见：小图幅上应得数可能不到一条，
    // 那时「应得 0、实得 0」比沉默更清楚。
    CHECK(census.barren > 0u);
    CHECK_EQ(census.eyes, 50u);
    CHECK_EQ(census.depleted.expected, 8u);
    CHECK_EQ(census.depleted.got, 8u);
    CHECK_EQ(census.depleted.shortfall(), 0u);
}

}  // namespace

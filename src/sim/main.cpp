/// @file main.cpp
/// @brief 演示程序：造一个能长期存续的世界，逐季打印季末报告。
///
/// 本文件是 world 模块的**调用方**，不是它的一部分。初始种群生成刻意不落进
/// world（见 doc/2026-10-16-world-design.md §1.2 的非目标）：那一层只做编排，
/// 至于怎么造 Genome 与 Phenotype 是调用方的知识。
///
/// 用法：sim [evolve|persist|fragile|intervene] [季数] [种子]
///
/// `evolve` 与 `persist` 只差 Lifespan 的基准（8 对 80），因此它们问的是
/// 不同的问题：前者世代换得快、看得见漂变与多样性磨损，后者长寿命、看得见
/// 生态平衡与「超载格停产」那条结构性性质。默认 evolve；第一个参数是数字
/// 时按老写法解析成季数。
///
/// `fragile` 与 `evolve` 只差近交衰退强度（0 对 0.7），在当前地形上第 25 季
/// 灭绝（`inbreeding_load` 的注释解释了它为什么不再是临界点）。
///
/// `intervene` 与 `evolve` 的遗传与环境配置逐项相同，差别只有一处：跑到
/// 第 30 季时用 `queue_terrain_edit` 在地图正中灌一条 2 格宽的水道，把主大陆
/// 切成两半，再跑完剩下的季数并按水道两侧分别报物种数与 census。它演示的
/// 是阶段 4 的干预队列 —— 刀是玩家落的，但要等下一季开季才生效。
///
/// **实测（种子 20261016，`sim intervene 60`）：** 第 30 季落刀，队列实际入队
/// 27 格 —— 2 列共 128 格，其余 101 格本来就是水，`queue_terrain_edit` 因此
/// 不收它们。末季 2224 个体、9 个物种、覆盖率 75.0%；切开之后西侧
/// （x < 32）6 个物种 908 个体，东侧 4 个物种 1316 个体，两侧不共有的
/// 物种占 8/9。
///
/// **物种 1 横跨两侧不是钳回穿帮**：那张图上有 12 块陆块，而物种 1 在落刀
/// 之前就同时占据水道两侧的不同陆块（跨陆块的基因流结构上不可能发生，落刀
/// 只是让这条不变量在一块大陆的内部也成立）。
#include "gamev2/world/world.hpp"

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/world.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/environment/config.hpp"

#include "gamev2/genetics/expression.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/phenotype.hpp"
#include "gamev2/genetics/species.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::CreaturePhenotype;
using gamev2::ecs::adapt::Environment;
using gamev2::ecs::adapt::Position;
using gamev2::ecs::adapt::SpeciesTag;
using gamev2::ecs::adapt::Vitality;
using gamev2::environment::CellIndex;
using gamev2::environment::EnvironmentConfig;
using gamev2::environment::kCellCount;
using gamev2::environment::kGridSide;
using gamev2::genetics::AlleleCall;
using gamev2::genetics::AlleleId;
using gamev2::genetics::Genome;
using gamev2::genetics::GeneticsConfig;
using gamev2::genetics::LocusKind;
using gamev2::genetics::LocusTable;
using gamev2::genetics::SpeciesId;
using gamev2::genetics::SpeciesMask;
using gamev2::genetics::TraitKind;
using gamev2::genetics::kLocusCount;
using gamev2::genetics::kTraitCount;
using gamev2::genetics::trait_index;

constexpr std::uint32_t kDefaultSeasons = 200;
constexpr std::uint64_t kDefaultSeed = 20261016;

/// `intervene` 在这一季落刀：已经完整结算的季数。此前世界按 evolve 跑，
/// 两侧的群体已有可辨的分化，切开之后读数才有东西可说。
constexpr std::uint32_t kInterveneSeason = 30;
/// 水道占的列。切在正中，因此两侧各有 31 列与 30 列，面积接近而规格不等。
constexpr std::uint16_t kGapFirst = 32;
constexpr std::uint16_t kGapWidth = 2;

/// 两套具名配置。**它们只差一个量**——Lifespan 的基准——因为预设的价值在于
/// 对照：一次只动一个变量，差出来的数字才可归因。其余参数（thermal_base_width
/// 在 18、其余全部）两边一致。
///
/// **本分支（阶段 3：SpeciationSystem 落地）之后的实测读数，种子 20261016，
/// 跑 200 季取末季。地形由 root_seed 生成，水陆 70/30（决策 15/16）：**
///
/// - 初始个体 2460（只铺可通行格），Σcapacity 2958，可通行格 1230。
/// - `evolve`：短寿命。population 2434，Σcapacity 的 82%，**裂成 10 个物种**
///   （1251 / 566 / 192 / 100 / 89 / 71 / 50 / 42 / 40 / 33），覆盖率 78.0%，
///   最挤一格 11 个体。born 605 / died 600。
/// - `persist`：长寿命。population 3526，**越过 Σcapacity 达 119%**——超载格里
///   的长寿命个体不被清空、那部分承载力就永久停产。10 个物种
///   （1852 / 806 / 270 / 156 / 111 / 91 / 81 / 70 / 46 / 43），覆盖率 87.6%，
///   最挤一格 22 个体。
/// - `fragile`：第 **50** 季归零（`inbreeding_load` 的注释记了 0.55 是怎么
///   扫出来的，以及为什么 0.7 已越过悬崖）。
///
/// **逐季表里的 H_obs / F / Ne 是物种 1 自己的，不是全世界的** —— 阶段 3
/// 之前只有一个物种所以两者等价，现在不等价了，列名已写成 `H_obs(sp1)`。
///
/// **两个最大的物种对应两块最大的陆块，且它们始终不合并**：连通分量在移动下
/// 是不变量（4 邻域逐格判通行），繁殖侧同样封闭（水格 capacity = 0 ⇒
/// headroom ≤ 0 ⇒ 不发站点），所以跨陆块的基因流**结构上不可能**发生。
///
/// **覆盖率仍在上升后趋平**（20 季 71.3% → 200 季 78.0%），设计文档 §7.1 的
/// 「空掉的格会被重新殖民」在三个阶段之后依然成立。
///
/// **此前挂在这里的所有读数组已全部作废**（决策 14 作废过一组、阶段 2 作废过
/// 一组、这里第三次）。下面世代数换算里的平均年龄沿用更早那组读数，只作量级
/// 示意。
///
/// **世代数是按周转率算的，不是按寿命上限算的**：平均年龄 = 1/死亡率，短
/// 寿命那套换代约比长寿命那套快一倍，而寿命上限是 8 对 80（10:1）。绝大
/// 多数个体在活到寿命上限之前就被抽签选掉了，所以寿命上限几乎不影响世代
/// 数——它影响的是超载格停不停产。
///
/// 短寿命那套看得见演化是因为世代换得快；长寿命那套种群更大更稳，是超载格
/// 停产换来的。两者不是「更好的和更差的」，是问不同问题的两个世界。
enum class Preset
{
    Evolve,
    Persist,
    Fragile,
    Intervene
};

[[nodiscard]] constexpr float lifespan_base(const Preset p) noexcept
{
    return p == Preset::Persist ? 80.0f : 8.0f;
}

/// 近交衰退强度。只有 fragile 打开，另两套为 0（整季行为与引入之前逐位相同）。
[[nodiscard]] constexpr float inbreeding_load(const Preset p) noexcept
{
    return p == Preset::Fragile ? 0.55f : 0.0f;
}

[[nodiscard]] const char* preset_name(const Preset p) noexcept
{
    switch (p)
    {
    case Preset::Evolve:
        return "evolve";
    case Preset::Persist:
        return "persist";
    case Preset::Fragile:
        return "fragile";
    case Preset::Intervene:
        return "intervene";
    }
    return "evolve";
}

/// 每个等位基因的加性效应，取一正一负。两个等位基因的显性等级必须不同，
/// 否则 trait_value 走纯合分支、dominance 完全不参与（本模块 validate 也拒
/// 查重复等级）。
constexpr float kAlleleEffect = 1.0f;

/// 位点权重。300 个位点摊到 8 条性状轴上，平均每轴约 38 个；随机等位基因
/// 下 trait_value 的标准差约 0.515，于是性状的遗传标准差约
/// √38 × 0.515 × scale ≈ 3.2 × scale。取 0.05 得到约 0.16——足够让选择看得见，
/// 又远在 clamp 区间之内。
constexpr float kLocusScale = 0.05f;

/// 每格按真实承载力放多少比例的初始个体。低于 1 是因为 run_season 的摄食
/// 存量在季内被吃空：铺满承载力等于让初始种群第一季就陷进饥荒。
///
/// 另有一个下限 2，与比例无关。build_sites 的出生预算取
/// min(eco_ceiling, parent_ceiling) 而 parent_ceiling = lround(here × 2)，
/// 因此**空格里没有亲代就产不出子代**（here = 0 直接产出 0）。MovementSystem
/// 接入后，空格可以被走进来的个体重新殖民，这条坡已经拆掉；但初始状态仍
/// 不该把一半的格留空——承载力 [0.975, 3.9] 下 floor(0.6 × cap) 有近三成
/// 的格算出 0，而它们要等到有 wanderer 走进去才重新开始生产。
constexpr float kSeedFill = 0.6f;
constexpr std::size_t kMinPerCell = 2;

LocusTable build_table()
{
    LocusTable table{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        auto& locus = table.loci[i];
        locus.kind = LocusKind::Trait;
        locus.allele_count = 2;
        // target 不设的话全部落回 0，即 BodySize——300 个位点挤在一条轴上，
        // 那不是「多样性」，是「只有体型在动」。
        locus.target = static_cast<std::uint8_t>(i % kTraitCount);
        locus.scale = kLocusScale;
        locus.dominance = 0.25f;
        // 突变率按「每代每位点」计，取千分之二：一季一条子代链上约 300 个
        // 位点，期望 0.6 次突变，够维持一点变异又不至于把表型搅成噪声。
        locus.switch_rate = 0.002f;
        locus.alleles[0].additive = -kAlleleEffect;
        locus.alleles[0].dom_rank = 0;
        locus.alleles[1].additive = kAlleleEffect;
        locus.alleles[1].dom_rank = 1;
    }
    return table;
}

GeneticsConfig build_genetics(const Preset preset)
{
    GeneticsConfig c{};
    c.trait_base.fill(1.0f);
    c.trait_min.fill(0.0f);
    c.trait_max.fill(10.0f);
    c.jitter_step = 0;

    // 能量收支：intake = SenseRange × Fecundity × feeding，
    // burn = Metabolism × BodySize × (1 + Speed)。
    c.trait_base[trait_index(TraitKind::BodySize)] = 1.0f;
    c.trait_min[trait_index(TraitKind::BodySize)] = 0.4f;
    c.trait_max[trait_index(TraitKind::BodySize)] = 2.5f;

    // Speed 同时进 burn 的 (1 + Speed) 与 escape 的 clamp(Speed, 0, 1)，
    // 因此它是一面收税又一面给分的性状。
    c.trait_base[trait_index(TraitKind::Speed)] = 0.9f;
    c.trait_min[trait_index(TraitKind::Speed)] = 0.3f;
    c.trait_max[trait_index(TraitKind::Speed)] = 1.6f;

    // 全仓库没有任何机制读 Strength（survival 只看 BodySize / Speed /
    // Metabolism / SenseRange / Fecundity / Resilience / Lifespan，
    // build_sites 只吃一个标量 demand）。仍按目录给它分配位点，
    // 于是这部分遗传方差目前是死的——记在这里以免后来者以为它在起作用。
    c.trait_base[trait_index(TraitKind::Strength)] = 1.0f;
    c.trait_min[trait_index(TraitKind::Strength)] = 0.0f;
    c.trait_max[trait_index(TraitKind::Strength)] = 3.0f;

    c.trait_base[trait_index(TraitKind::Metabolism)] = 0.15f;
    c.trait_min[trait_index(TraitKind::Metabolism)] = 0.02f;
    c.trait_max[trait_index(TraitKind::Metabolism)] = 1.2f;

    // Lifespan 的下界必须 > 0：outlived 判的是 age >= lifespan，而
    // AgingSystem 把 age 递增到 1 才判，lifespan = 0 会让本季全体在
    // flush 时被销毁、亲代池恒空、整季零子代，且不报错（locus.hpp 对
    // BadTraitRange 的说明）。上下界跟着基准一起放大，否则夹取会把长寿命
    // 那一档悄悄拉回 max。
    //
    // 基准由预设决定，这是**世代尺度旋钮**：8 季意味着 500 季翻 60 代，
    // 多样性早就收敛；80 季只翻 6 代，看得到的是生态结构而不是遗传漂变。
    const float life = lifespan_base(preset);
    c.trait_base[trait_index(TraitKind::Lifespan)] = life;
    c.trait_min[trait_index(TraitKind::Lifespan)] = life * 0.375f;
    c.trait_max[trait_index(TraitKind::Lifespan)] = life * 18.75f;

    c.trait_base[trait_index(TraitKind::SenseRange)] = 1.0f;
    c.trait_min[trait_index(TraitKind::SenseRange)] = 0.2f;
    c.trait_max[trait_index(TraitKind::SenseRange)] = 2.0f;

    // Fecundity 的下界同样必须 > 0：demand = SenseRange × Fecundity，
    // demand 归零时 run_season 把它判成「什么都不需要」，那条个体免疫饥荒。
    c.trait_base[trait_index(TraitKind::Fecundity)] = 0.7f;
    c.trait_min[trait_index(TraitKind::Fecundity)] = 0.05f;
    c.trait_max[trait_index(TraitKind::Fecundity)] = 1.6f;

    c.trait_base[trait_index(TraitKind::Resilience)] = 1.0f;
    c.trait_min[trait_index(TraitKind::Resilience)] = 0.1f;
    c.trait_max[trait_index(TraitKind::Resilience)] = 2.5f;

    // 气候场跨 ±14 °C（每格基准 ±8 叠季节摆动 ±6，见 climate.cpp）。
    // 默认的 thermal_base_width = 1.0 配 Resilience ≈ 1 只有 ±2 °C 的耐受，
    // thermal_survival 因此在绝大多数格子直接归零，子代出生即死。放宽之后
    // 气候回到一个温和的空间梯度而不是死刑——thermal 仍在给 Resilience 施加
    // 选择，只是从「死刑」降到「偏好」。
    //
    // **18 与 30 的取舍是实测出来的，两组数都在下面这个 demo 里跑过 200 季：**
    //
    // - 30：thermal 稳在 0.85 上下，气候几乎被关掉。平台种群 ≈ 9710
    //   （Σcapacity 的 97.3%），死亡率 ≈ 19.9%。
    // - 18：thermal ≈ 0.83，气候开始真正咬人。平台种群 ≈ 8680（86.9%），
    //   死亡率 ≈ 24.3%，H_obs 与 F 都略差一档。
    //
    // 差 10% 种群换来 4.4 个百分点的额外死亡率——这就是「气候当选择压力」的
    // 价码。要一个几乎不死的世界取 30，要一个气候真的重要的世界取 18。
    // 再往下调会怎样没有测过，而 thermal_base_width ≤ 2 时子代会直接出生即死。
    c.thermal_base_width = 18.0f;

    // escape = clamp((Speed − threat × BodySize) × escape_responsiveness, 0, 1)
    // 而 threats 恒为 0（genetics_systems.cpp 的硬编码，world 设计文档 §8
    // 列为未决），于是 escape 退化成 clamp(Speed × resp)。默认 resp = 1.0
    // 时它要求 Speed ≥ 1 才取满，而 Speed 的基准是 0.9 —— 一个乘性的、
    // 恒小于 1 的因子叠在 thermal 与 energy 上，把整条 survival 压到 0.2
    // 上下。提到 1.5 让 Speed ≥ 0.67 即取满：仍保留对速度的选择，
    // 但不再无条件抽成。
    c.escape_responsiveness = 1.5f;

    // 近交衰退：存活率按 `1 - load × F` 线性下降。F 由 genetics::summarize
    // 从杂合度反推，是**种群级**的量（genetics 设计文档 §2 决定不追踪家谱），
    // 整群共用同一个数，因此这一项表达的是「群体近交程度整体压低存活」。
    //
    // 0.55 这个值在**阶段 3 的地形地图**上重扫过。悬崖夹在 0.50 与 0.55 之间：
    //
    //   0.50  872 → 768 → 619 → 733 → 728（跑满 200 季，低位企稳）
    //   0.52  834 → 547 → 280 → 234 → 0（第 150~200 季之间灭绝）
    //   0.55  第 50 季归零
    //   0.60  第 31 季归零
    //   0.70  第 17 季归零
    //
    // **0.7 曾经也在这张表上，而且不是临界点。** 阶段 2 把可用面积砍到 30%、
    // 阶段 3 播种改成 32 个创始基因型的强瓶颈之后（Σw² 基线 = 1/N，而 N 由
    // 亲代池大小决定），0.7 压得太狠 —— 它比 0.55 更远离悬崖而不是更靠近。
    // 0.55 取「第 50 季灭绝」，与阶段 2 之前的 52 季几乎一致，demo 的性格没变。
    //
    // **它与 mating_radius_cells 耦合，改邻域就得重调。** Σw² 的基线就是 1/N，
    // 而 N 直接由邻域大小决定。半径调到 4 相当于把每个亲代池撑大九倍、
    // Σw² 压到九分之一，这一档就再也压不垮任何东西。这条耦合由 world_tests 的
    // mating_radius_widens_the_pool_and_relieves_inbreeding 守着——它盯的是
    // 「半径变宽 ⇒ 惩罚变轻」这个方向，不是某个具体阈值。
    //
    c.inbreeding_load = inbreeding_load(preset);

    // g(E) 会把气候偏移折成性状增量：|temperature| × load 加进 Metabolism，
    // (fertility − 1) 加进 Fecundity。后两项的默认增益是 1.0，而格子的
    // base_fertility ∈ [0.25, 1]、光照再乘季节项，扣减幅度能吃掉 Fecundity
    // 的一半以上。压到 0.4 让环境修正可感而不致命；温度那一项压到 0.03，
    // 于是最冷的格子也只给 Metabolism 加 0.42。
    c.temperature_metabolic_load = 0.03f;
    c.fertility_fecundity_gain = 0.4f;
    c.light_sense_gain = 0.4f;

    return c;
}

EnvironmentConfig build_environment()
{
    EnvironmentConfig c{};
    // **全陆地上** Σcapacity = 4096 × capacity_base × mean(base_fertility)
    // = 4096 × 3.9 × 0.625 ≈ 9984。阶段 2 之后地形生效、水陆 70/30，
    // 水格 capacity 归零，实际 Σcapacity 落到约 2958（种子 20261016）——
    // 9984 现在只是全陆地上的上限，**不是天花板**，写死会让百分比读数出错。
    //
    // build_sites 的出生预算取 eco_ceiling（headroom 份额）与 parent_ceiling
    // （在场数 × 2.0）的较小者，因此满占用下真正的刹车是承载力而不是繁殖力。
    c.capacity_base = 3.9f;
    return c;
}

SpeciesMask all_loci()
{
    SpeciesMask m{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        m.active.set(i);
    }
    return m;
}

/// 随机起点。全零 Genome 是全体纯合、零遗传方差的世界——那样只剩漂变，
/// 没有选择可言。
/// 取一个 0/1 等位基因。`random_genome` 与 `seed` 的播种突变共用它 ——
/// 同一件事不留两份同义实现。
[[nodiscard]] AlleleId random_allele(std::mt19937_64& rng)
{
    std::uniform_int_distribution<int> pick{0, 1};
    return static_cast<AlleleId>(pick(rng));
}

Genome random_genome(std::mt19937_64& rng)
{
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i] = AlleleCall{random_allele(rng), 0};
        g.b[i] = AlleleCall{random_allele(rng), 0};
    }
    return g;
}

/// 按各格真实承载力铺初始种群。
///
/// 父代走 phenotype_of 而非手写表型模板：BreedingSystem 给子代挂的是出生格
/// 的真实气候快照（f(G) ⊕ g(E)），手工搭的父代却只能拿 Environment 的默认值
/// （temperature 恒 0，完全免疫气候）。让父代走同一条路径，父子才面对同一套
/// 选择压力。
void seed(gamev2::world::World& world, const SpeciesId sid,
          const SpeciesMask& mask, const LocusTable& table,
          const GeneticsConfig& genetics, std::mt19937_64& rng)
{
    auto& entities = world.entities();
    const auto& env = world.environment();
    std::uniform_real_distribution<float> jitter{0.05f, 0.95f};

    // 全球单一创始池：全图 ≈ 2400 个个体 descended from 这一小撮。原来的
    // 「每体一份独立随机基因型」让同格的邻居毫无亲缘关系，主大陆从第 0 季起
    // 就是几千个互不相关谱系的混合物 —— 实测第 0 季 divergence 就 1.75
    // （值域 [0, 150]），真分化与初始抽样噪声根本分不开。实测见设计文档 §2.3。
    std::vector<Genome> founders;
    founders.reserve(env.config().seed_founder_pool);
    for (std::uint32_t f = 0; f < env.config().seed_founder_pool; ++f)
    {
        founders.push_back(random_genome(rng));
    }
    std::uniform_int_distribution<std::size_t> pick_founder{
        0, founders.size() - 1};
    std::uniform_real_distribution<float> mutate{0.0f, 1.0f};

    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        // 水格站不了人，在这里铺个体等于开机就把一堆幽灵丢进水里 ——
        // 它们会被 MovementSystem 的静默钳回挤上岸，但那是纠错，不是初始化。
        if (!env.terrain().passable(cell))
        {
            continue;
        }
        const auto fill =
            static_cast<std::size_t>(env.resources().capacity(cell)
                                     * kSeedFill);
        const auto n = std::max(kMinPerCell, fill);
        const auto snapshot = env.climate().at(cell);
        const auto base_x = static_cast<float>(c % kGridSide);
        const auto base_y = static_cast<float>(c / kGridSide);
        for (std::size_t i = 0; i < n; ++i)
        {
            auto genome = founders[pick_founder(rng)];
            // 每个个体带一点点私有差异，不足以让它变成独立的谱系。
            for (std::size_t l = 0; l < kLocusCount; ++l)
            {
                if (mutate(rng) < env.config().seed_mutation_rate)
                {
                    genome.a[l].allele = random_allele(rng);
                    genome.b[l].allele = random_allele(rng);
                }
            }
            const EntityId e = entities.create();
            (void)entities.add<SpeciesTag>(e, SpeciesTag{sid});
            (void)entities.add<Genome>(e, genome);
            (void)entities.add<Environment>(e, Environment{snapshot});
            (void)entities.add<CreaturePhenotype>(
                e, CreaturePhenotype{gamev2::genetics::phenotype_of(
                       genome, table, mask, snapshot, genetics)});
            (void)entities.add<Vitality>(e, Vitality{});
            (void)entities.add<Position>(e, Position{base_x + jitter(rng),
                                                     base_y + jitter(rng)});
        }
    }
}

std::uint64_t parse_arg(const char* text, const std::uint64_t fallback)
{
    std::uint64_t value = 0;
    const std::string_view view{text};
    const auto [end, ec] =
        std::from_chars(view.data(), view.data() + view.size(), value);
    if (ec != std::errc{} || end != view.data() + view.size())
    {
        std::println(stderr, "无法解析参数 '{}'，改用默认值 {}", text, fallback);
        return fallback;
    }
    return value;
}

/// 位置参数的第一格既可能是预设名也可能是季数，靠「首字符是不是数字」区分。
/// 这样 `sim 500` 仍然按老写法解析成 500 季，不必为了加预设改掉已有习惯。
bool looks_numeric(const char* text) noexcept
{
    return text[0] >= '0' && text[0] <= '9';
}

bool parse_preset(const char* text, Preset& out) noexcept
{
    const std::string_view view{text};
    if (view == "evolve")
    {
        out = Preset::Evolve;
        return true;
    }
    if (view == "persist")
    {
        out = Preset::Persist;
        return true;
    }
    if (view == "fragile")
    {
        out = Preset::Fragile;
        return true;
    }
    if (view == "intervene")
    {
        out = Preset::Intervene;
        return true;
    }
    return false;
}

}  // namespace

int main(const int argc, char** argv)
{
    Preset preset = Preset::Evolve;
    int next = 1;
    if (argc > 1 && !looks_numeric(argv[1]))
    {
        if (!parse_preset(argv[1], preset))
        {
            std::println(stderr, "未知预设 '{}'。", argv[1]);
            std::println(stderr,
                         "用法：sim [evolve|persist|fragile|intervene] [季数] [种子]");
            return 1;
        }
        next = 2;
    }
    const auto seasons = static_cast<std::uint32_t>(
        argc > next ? parse_arg(argv[next], kDefaultSeasons) : kDefaultSeasons);
    const auto root_seed =
        argc > next + 1 ? parse_arg(argv[next + 1], kDefaultSeed) : kDefaultSeed;
    if (seasons == 0)
    {
        std::println(stderr, "季数至少为 1。");
        return 1;
    }

    const LocusTable table = build_table();
    const GeneticsConfig genetics = build_genetics(preset);
    const EnvironmentConfig environment = build_environment();

    // 蓝图配置的校验只在加载期做一次，热路径零检查——这是 locus.hpp 明写的
    // 契约，不调则配置错误会推迟到第 3 季第 4000 个个体身上才炸。
    if (const auto v = gamev2::genetics::validate(table, genetics); !v)
    {
        std::println(stderr, "蓝图配置非法：{}", static_cast<int>(v.error()));
        return 1;
    }

    gamev2::world::World world{table, genetics, environment, root_seed};
    const auto sid = world.species().register_species(all_loci());
    if (!sid)
    {
        std::println(stderr, "物种登记失败：{}", static_cast<int>(sid.error()));
        return 1;
    }

    std::mt19937_64 rng{root_seed};
    seed(world, *sid, world.species().mask(*sid), table, genetics, rng);

    const std::size_t initial = world.entities().entity_count();
    std::println("预设 {}  seed {}  初始个体 {}  跑 {} 季", preset_name(preset),
                 root_seed, initial, seasons);

    // 逐季推进而非 run_seasons(n)，且**当季就打印**：出生与死亡只有跑完一季
    // 之后才拿得到，而攒到末尾再统一打印会让运行期间屏幕上一片空白——200 季
    // 是将近一分钟的干等。age_seasons 是亲子代的分割线：AgingSystem 在 flush
    // 之前就跑完了，flush 才创建的子代因此挂着默认构造的 Vitality（age 为 0），
    // 而活过本季的个体都被递增过（world 测试用的是同一把尺子）。
    // H_obs / F / Ne 取的是**物种 1 自己的**统计，而 pop 是全部物种之和。
    // 阶段 3 之前只有一个物种，两者等价；裂变之后不等价了，列名必须说清楚，
    // 否则 H_obs 一栏会让人误以为是全世界的多样性。逐物种的数字在末尾的
    // 「物种 N census ...」那一段里。
    std::println("\n{:>6}  {:>7}  {:>7}  {:>7}  {:>9}  {:>8}  {:>6}",
                 "season", "pop", "born", "died", "H_obs(sp1)", "F(sp1)",
                 "Ne(sp1)");

    // 干预演示只跑 intervene 那一套。刀走 queue_terrain_edit 而不是直接
    // flood：队列正是要演示的东西 —— 它在下一季开季才落地，因此这一季的
    // 读数与「立刻改地形」不同。
    const bool intervene = preset == Preset::Intervene;

    std::size_t previous = initial;
    std::size_t low_pop = 0;
    std::size_t high_pop = 0;
    std::uint32_t low_season = 0;
    std::uint32_t high_season = 0;
    for (std::uint32_t s = 0; s < seasons; ++s)
    {
        const auto report = world.run_season();
        const auto& stats = report.species.front();
        std::size_t newborn = 0;
        world.entities().for_each_entity(
            [&world, &newborn](const EntityId e)
            {
                if (world.entities().get_if<Vitality>(e)->age_seasons == 0)
                {
                    ++newborn;
                }
            });
        // 死亡数是恒等式推出来的而不是数出来的：本季少了多少，补回本季生了
        // 多少，就是本季死了多少。
        const std::size_t deaths = previous - report.population + newborn;

        std::println("{:>6}  {:>7}  {:>7}  {:>7}  {:>8.4f}  {:>7.4f}  {:>5.1f}",
                     report.season, report.population, newborn, deaths,
                     stats.heterozygosity, stats.f_coefficient,
                     stats.effective_population);

        if (s == 0)
        {
            low_pop = high_pop = report.population;
        }
        if (report.population < low_pop)
        {
            low_pop = report.population;
            low_season = report.season;
        }
        if (report.population > high_pop)
        {
            high_pop = report.population;
            high_season = report.season;
        }
        previous = report.population;

        if (intervene && report.season + 1 == kInterveneSeason)
        {
            std::vector<CellIndex> canal;
            for (std::uint16_t x = kGapFirst; x < kGapFirst + kGapWidth; ++x)
            {
                for (std::uint16_t y = 0; y < kGridSide; ++y)
                {
                    canal.push_back(
                        CellIndex{static_cast<std::uint16_t>(y * kGridSide + x)});
                }
            }
            // 已经是水的格不入队，所以返回值是「这一刀真的淹掉了多少格」。
            const auto flooded = world.queue_terrain_edit(canal, false);
            std::println(
                "\n第 {} 季落刀：正中 {} 列的水道排进队列（{} 格），"
                "下一季开季才生效。",
                report.season + 1, kGapWidth, flooded);
        }
    }

    // 物种逐项列出。阶段 3 之后物种数不再恒为 1，这是本阶段最直观的观测量。
    // 用 `world.last_season()` 而不是循环里的局部 `report` —— 后者出了循环
    // 就不在作用域里了，而 `last_season()` 拿的正是同一份（World 在
    // season_report 里缓存它）。
    const auto& report = world.last_season();
    {
        std::size_t coexisting = 0;
        for (const auto& s : report.species)
        {
            if (s.census > 0)
            {
                ++coexisting;
            }
        }
        std::println("\n物种 {}（census > 0 的 {} 个）：",
                     world.species().species_count(), coexisting);
        for (std::size_t k = 0; k < report.species.size(); ++k)
        {
            const auto& s = report.species[k];
            if (s.census == 0)
            {
                continue;
            }
            std::println("  物种 {}  census {:>5}  H_obs {:.4f}  F {:.4f}",
                         k + 1, s.census, s.heterozygosity, s.f_coefficient);
        }
    }
    if (intervene)
    {
        // 按水道两侧分别报物种数与 census。全图口径的物种数在这里没有信息
        // 量：切开之后**两侧的基因流在结构上不可能发生**（跨陆块移动被水
        // 隔断，繁殖侧同样封闭），所以「几个物种」必须分侧数才有意义。
        //
        // 落在水道两列上的个体一个也没有 —— 落刀那一季它们要么被钳回自己
        // 那侧，要么在自己那块地被淹光时销毁，因此按 x < 32 归西侧不会串。
        const auto side = [&world](const bool east)
        {
            std::vector<std::pair<SpeciesId, std::size_t>> by_species;
            std::size_t total = 0;
            world.entities().for_each_entity(
                [&world, &by_species, &total, east](const EntityId e)
                {
                    const auto* pos = world.entities().get_if<Position>(e);
                    const auto* tag = world.entities().get_if<SpeciesTag>(e);
                    if (pos == nullptr || tag == nullptr)
                    {
                        return;
                    }
                    const auto x = static_cast<std::size_t>(pos->x);
                    if ((x >= kGapFirst) != east)
                    {
                        return;
                    }
                    ++total;
                    const auto it = std::ranges::find_if(
                        by_species,
                        [&tag](const auto& p)
                        {
                            return p.first.value == tag->species.value;
                        });
                    if (it == by_species.end())
                    {
                        by_species.emplace_back(tag->species, std::size_t{1});
                    }
                    else
                    {
                        ++it->second;
                    }
                });
            std::ranges::sort(by_species,
                              [](const auto& a, const auto& b)
                              {
                                  return a.first.value < b.first.value;
                              });
            return std::make_pair(total, by_species);
        };

        const auto [west_pop, west] = side(false);
        const auto [east_pop, east] = side(true);
        std::println(
            "\n水道两侧（x < {} 与 x >= {}）：西侧 {} 个物种 {} 个体，"
            "东侧 {} 个物种 {} 个体。",
            kGapFirst, kGapFirst, west.size(), west_pop, east.size(), east_pop);
        for (const auto& [id, n] : west)
        {
            std::println("  西侧 物种 {}  census {}", id.value, n);
        }
        for (const auto& [id, n] : east)
        {
            std::println("  东侧 物种 {}  census {}", id.value, n);
        }
    }

    if (previous == 0)
    {
        std::println("\n第 {} 季种群归零。", low_season);
    }
    else
    {
        // 覆盖率：有个体的可通行格占全部可通行格的比例。它守的是设计文档
        // §7.1 承诺的「空掉的格会被重新殖民」——world_tests 只跑几季，看不出
        // 长期下来地图会不会单调收缩，sim 的长局才看得出。
        //
        // 最挤那一格也要报：覆盖率可以靠「少数格塞满」凑出来。
        //
        // Σcapacity 按实地图求和，不能写死：地形生成后水格 capacity 为 0，
        // 全陆地的 4096 × capacity_base × mean(base_fertility) 只是上限，
        // 写死会让下面那句百分比静默变成错的 —— 不报任何错，只给离谱的数。
        std::size_t passable = 0;
        std::size_t occupied = 0;
        std::uint32_t worst = 0;
        double sum_capacity = 0.0;
        for (std::size_t c = 0; c < kCellCount; ++c)
        {
            const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
            if (!world.environment().terrain().passable(cell))
            {
                continue;
            }
            ++passable;
            sum_capacity += world.environment().resources().capacity(cell);
            const auto occ = world.environment().resources().occupants(cell);
            if (occ > 0)
            {
                ++occupied;
                worst = std::max<std::uint32_t>(worst, occ);
            }
        }
        const auto cover = static_cast<double>(occupied)
                           / static_cast<double>(passable) * 100.0;
        std::println(
            "\n覆盖率 {:.1f}%（{}/{} 个可通行格有主），最挤一格 {} 个体。",
            cover, occupied, passable, worst);

        // 按陆块分组的个体数。4 邻域移动逐格判通行 ⇒ 个体所在陆块的连通分量
        // 在移动下是不变量；繁殖侧同样封闭（水格 capacity = 0 ⇒ headroom ≤ 0
        // ⇒ 不发站点）。所以**跨陆块扩散在结构上不可能发生**，两个陆块上的
        // 群体是真正隔离的 —— 阶段 2 招牌产出「屏障两侧分成两群」的观测点
        // 在这里，也是阶段 3 `divergence` 指标的前置数据源。
        //
        // 之所以现在就要：上面那个覆盖率是**全图口径**，看不出按陆块分组之后
        // 是什么样，而这个游戏唯一的看点正是分组之后的样子。
        std::vector<bool> seen(kCellCount, false);
        std::vector<std::size_t> block_sizes;
        std::vector<std::size_t> stack;
        for (std::size_t seed_cell = 0; seed_cell < kCellCount; ++seed_cell)
        {
            if (seen[seed_cell]
                || !world.environment().terrain().passable(
                    CellIndex{static_cast<std::uint16_t>(seed_cell)}))
            {
                continue;
            }
            std::size_t members = 0;
            stack.assign(1, seed_cell);
            while (!stack.empty())
            {
                const auto i = stack.back();
                stack.pop_back();
                const auto cell = CellIndex{static_cast<std::uint16_t>(i)};
                if (seen[i] || !world.environment().terrain().passable(cell))
                {
                    continue;
                }
                seen[i] = true;
                members += world.environment().resources().occupants(cell);

                const auto gx = i % kGridSide;
                const auto gy = i / kGridSide;
                if (gx > 0)
                {
                    stack.push_back(i - 1);
                }
                if (gx + 1 < kGridSide)
                {
                    stack.push_back(i + 1);
                }
                if (gy > 0)
                {
                    stack.push_back(i - kGridSide);
                }
                if (gy + 1 < kGridSide)
                {
                    stack.push_back(i + kGridSide);
                }
            }
            if (members > 0)
            {
                block_sizes.push_back(members);
            }
        }
        std::ranges::sort(block_sizes, std::ranges::greater{});
        // **时点必须写清楚**：`occupants` 全仓只在 EnvironmentSystem::
        // begin_season 被写一次，而 begin_season 在 open_season 里、早于本季的
        // 摄食与换代。所以这里读到的是**最后一季开季时**的分布，与同屏打印的
        // 「末季 population」差一整季的净变化（本例 −29，约 1.2%）。想把两者
        // 对齐得让 World 暴露可变的环境场，那是阶段 3 自己的事，不在这里放宽。
        std::string line = "有主陆块（开季快照，落后末季一季）";
        for (std::size_t i = 0; i < block_sizes.size() && i < 6; ++i)
        {
            line += std::format(" {} 个", block_sizes[i]);
        }
        if (block_sizes.size() > 6)
        {
            line += std::format(" …（共 {} 块）", block_sizes.size());
        }
        std::println("{}", line);

        std::println("种群区间 [{}({}), {}({})]，末季 {}。", low_season,
                     low_pop, high_season, high_pop, previous);
        std::println(
            "Ne 恒在 1 附近不是退化：effective_population = 激活位点数 /\n"
            "(2 × Σ(1−H_exp))，而两等位基因位点在 50/50 时 H_exp = 0.5 已达\n"
            "上限，于是 Ne = 1 恰是「多样性最好」的状态。");
        if (previous < initial)
        {
            std::println(
                "种群在缓慢下滑。移动已让空格能被重新殖民，所以这条坡多半不是\n"
                "承载面积在流失——更可能是存活率或出生预算本身偏低。逐季对照\n"
                "born / died 两列看是哪一侧先失衡。");
        }
        else
        {
            const auto pct =
                static_cast<double>(previous) / sum_capacity * 100.0;
            std::println("\n末季 {:.0f} = Σcapacity({:.0f}) 的 {:.0f}%。",
                         static_cast<double>(previous), sum_capacity, pct);
            if (pct > 100.0)
            {
                std::println(
                    "越过 100% 不是失控，而是 Σcapacity 根本不是硬上限：\n"
                    "headroom 逐格取 max(0, capacity − occupants)，超载格贡献\n"
                    "0，而它多占的个体不会被赶走、只是不再产子；空着的格照常\n"
                    "产出，于是总量能长期停在承载力之和之上。个体寿命越长缺口\n"
                    "越大——evolve 停在 82%，persist 停在 120%。");
            }
            else
            {
                std::println(
                    "停在 100% 以下说明还有余量格。短寿命个体在超载格里会被\n"
                    "抽签与寿命持续清空，清空后格子重新拿回 headroom；persist\n"
                    "那套长寿命个体占住不走，超载格就永久停产，总量因此反而\n"
                    "越过 Σcapacity。");
            }
        }
    }
    return 0;
}

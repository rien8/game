/// @file main.cpp
/// @brief 演示程序：造一个能长期存续的世界，逐季打印季末报告。
///
/// 本文件是 world 模块的**调用方**，不是它的一部分。初始种群生成刻意不落进
/// world（见 doc/2026-10-16-world-design.md §1.2 的非目标）：那一层只做编排，
/// 至于怎么造 Genome 与 Phenotype 是调用方的知识。
///
/// 用法：sim [evolve|persist|fragile] [季数] [种子]
///
/// `evolve` 与 `persist` 只差 Lifespan 的基准（8 对 80），因此它们问的是
/// 不同的问题：前者世代换得快、看得见漂变与多样性磨损，后者长寿命、看得见
/// 生态平衡与「超载格停产」那条结构性性质。默认 evolve；第一个参数是数字
/// 时按老写法解析成季数。
///
/// `fragile` 与 `evolve` 只差近交衰退强度（0 对 4），8 季灭绝。
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
#include <print>
#include <random>
#include <string_view>

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

/// 两套具名配置。**它们只差一个量**——Lifespan 的基准——因为预设的价值在于
/// 对照：一次只动一个变量，差出来的数字才可归因。其余参数（thermal_base_width
/// 在 18、其余全部）两边一致。
///
/// 两套都实测过 500 季（种子 20261016）：
///
/// - `evolve`：短寿命。种群停在 Σcapacity 的 89.6%，死亡率 23.3%，
///   杂合度从 0.481 一路磨到 0.425（−0.0057/100 季），近交系数升到 0.061。
/// - `persist`：长寿命。种群停在 Σcapacity 的 **124.4%**——超载格里的长寿命
///   个体不被清空、那部分承载力就永久停产。死亡率只有 11.4%，杂合度几乎磨
///   不动（−0.0023/100 季），近交系数反而降到 0.028。
///
/// **世代数是按周转率算的，不是按寿命上限算的**：平均年龄 = 1/死亡率，两套
/// 分别是 4.3 与 8.7 季，500 季折合约 117 代对 57 代（约 2:1），而寿命上限
/// 是 8 对 80（10:1）。绝大多数个体在活到寿命上限之前就被抽签选掉了，所以
/// 寿命上限几乎不影响世代数——它影响的是超载格停不停产。
///
/// 短寿命那套看得见演化是因为世代换得快；长寿命那套种群更大更稳，是超载格
/// 停产换来的。两者不是「更好的和更差的」，是问不同问题的两个世界。
enum class Preset
{
    Evolve,
    Persist,
    Fragile
};

[[nodiscard]] constexpr float lifespan_base(const Preset p) noexcept
{
    return p == Preset::Persist ? 80.0f : 8.0f;
}

/// 近交衰退强度。只有 fragile 打开，另两套为 0（整季行为与引入之前逐位相同）。
[[nodiscard]] constexpr float inbreeding_load(const Preset p) noexcept
{
    return p == Preset::Fragile ? 0.7f : 0.0f;
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
    // 0.7 这个值是实测出来的，阈值在 0.5 与 0.7 之间，是一次干净的分岔：
    // 0.25 / 0.4 / 0.5 都能跑满 200 季（末季 8099 / 7816 / 7288，逐级被压低），
    // 0.7 则第 44 季归零。
    //
    // 它比旧口径（走 genetics::summarize 的 f_coefficient）小一个量级，因为
    // Σw² 的基线在 0.1 ~ 0.5，而那一路的读数只有 0.03 ~ 0.07。
    //
    // **它与 mating_radius_cells 耦合，改邻域就得重调。** Σw² 的基线就是 1/N，
    // 而 N 直接由邻域大小决定：上面那个分岔只对当前
    // EnvironmentConfig::mating_radius_cells = 1 成立；半径调到 4 相当于把每个
    // 亲代池撑大九倍、Σw² 压到九分之一，0.7 就再也压不垮任何东西。
    // 这条耦合由 world_tests 的
    // mating_radius_widens_the_pool_and_relieves_inbreeding 守着——它盯的是
    // 「半径变宽 ⇒ 惩罚变轻」这个方向，不是某个具体阈值。
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
    // Σcapacity = 4096 × capacity_base × mean(base_fertility) = 4096 × 3.9 ×
    // 0.625 ≈ 9984，即种群的天花板。这是默认值，而 build_sites 的出生预算
    // 取 eco_ceiling（headroom 份额）与 parent_ceiling（在场数 × 2.0）的
    // 较小者，因此满占用下真正的刹车是承载力而不是繁殖力。
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
Genome random_genome(std::mt19937_64& rng)
{
    std::uniform_int_distribution<int> pick{0, 1};
    Genome g{};
    for (std::size_t i = 0; i < kLocusCount; ++i)
    {
        g.a[i] = AlleleCall{static_cast<AlleleId>(pick(rng)), 0};
        g.b[i] = AlleleCall{static_cast<AlleleId>(pick(rng)), 0};
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

    for (std::size_t c = 0; c < kCellCount; ++c)
    {
        const auto cell = CellIndex{static_cast<std::uint16_t>(c)};
        const auto fill =
            static_cast<std::size_t>(env.resources().capacity(cell)
                                     * kSeedFill);
        const auto n = std::max(kMinPerCell, fill);
        const auto snapshot = env.climate().at(cell);
        const auto base_x = static_cast<float>(c % kGridSide);
        const auto base_y = static_cast<float>(c / kGridSide);
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto genome = random_genome(rng);
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
            std::println(stderr, "用法：sim [evolve|persist|fragile] [季数] [种子]");
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
    std::println("\n{:>6}  {:>7}  {:>7}  {:>7}  {:>8}  {:>7}  {:>5}",
                 "season", "pop", "born", "died", "H_obs", "F", "Ne");

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
    }

    if (previous == 0)
    {
        std::println("\n第 {} 季种群归零。", low_season);
    }
    else
    {
        std::println("\n种群区间 [{}({}), {}({})]，末季 {}。", low_season,
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
            const auto pct = static_cast<double>(previous) / 9984.0 * 100.0;
            std::println("\n末季 {:.0f} = Σcapacity(9984) 的 {:.0f}%。",
                         static_cast<double>(previous), pct);
            if (pct > 100.0)
            {
                std::println(
                    "越过 100% 不是失控，而是 Σcapacity 根本不是硬上限：\n"
                    "headroom 逐格取 max(0, capacity − occupants)，超载格贡献\n"
                    "0，而它多占的个体不会被赶走、只是不再产子；空着的格照常\n"
                    "产出，于是总量能长期停在承载力之和之上。个体寿命越长缺口\n"
                    "越大——evolve 停在 89%，persist 停在 125%。");
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

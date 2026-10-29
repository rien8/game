/// @file main.cpp
/// @brief 演示程序：造一个能长期存续的世界，逐季打印季末报告。
///
/// 本文件是 ecology 模块的**调用方**，不是它的一部分。初始种群生成刻意不落进
/// ecology（见 doc/2026-10-16-world-design.md §1.2 的非目标）：那一层只做编排，
/// 至于怎么造 Genome 与 Phenotype 是调用方的知识。
///
/// 场景配置（位点蓝图、遗传与环境参数、初始种群）在 gamev2/demo/scenario.hpp
/// 里，与 demo_gui 共用同一份——界面上看到的数字与这里打印的必须是同一个世界。
///
/// 用法：sim [evolve|persist|fragile] [季数] [种子]
///
/// `evolve` 与 `persist` 只差 Lifespan 的基准（8 对 80），因此它们问的是
/// 不同的问题：前者世代换得快、看得见漂变与多样性磨损，后者长寿命、看得见
/// 生态平衡与「超载格停产」那条结构性性质。默认 evolve；第一个参数是数字
/// 时按老写法解析成季数。
///
/// `fragile` 与 `evolve` 只差近交衰退强度（0 对 4），8 季灭绝。
#include "gamev2/demo/scenario.hpp"

#include "gamev2/ecs/adapt/components.hpp"
#include "gamev2/ecs/world.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <print>
#include <random>
#include <string_view>

namespace {

using gamev2::demo::Preset;
using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::Vitality;

constexpr std::uint32_t kDefaultSeasons = 200;
constexpr std::uint64_t kDefaultSeed = 20261016;

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

    const gamev2::demo::Scenario scenario =
        gamev2::demo::build_scenario(preset);

    // 蓝图配置的校验只在加载期做一次，热路径零检查——这是 locus.hpp 明写的
    // 契约，不调则配置错误会推迟到第 3 季第 4000 个个体身上才炸。
    if (const auto v = gamev2::demo::validate(scenario); !v)
    {
        std::println(stderr, "蓝图配置非法：{}", static_cast<int>(v.error()));
        return 1;
    }

    gamev2::ecology::Ecology world{scenario.table, scenario.genetics,
                                   scenario.environment, root_seed};
    const auto sid = gamev2::demo::register_full_species(world);
    if (!sid)
    {
        std::println(stderr, "物种登记失败：{}", static_cast<int>(sid.error()));
        return 1;
    }

    std::mt19937_64 rng{root_seed};
    gamev2::demo::seed_population(world, *sid, world.species().mask(*sid),
                                   scenario, rng);

    const std::size_t initial = world.entities().entity_count();
    std::println("预设 {}  seed {}  初始个体 {}  跑 {} 季",
                 gamev2::demo::preset_name(preset), root_seed, initial, seasons);

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

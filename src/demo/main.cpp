/// @file main.cpp
/// @brief 世界生成的统计与 PPM 导出。生成器的阈值是经验值，只能看着图调，
///        这个程序就是那条反馈回路。
#include "gamev2/history/ledger.hpp"
#include "gamev2/world/generate.hpp"
#include "gamev2/world/tables.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gamev2::world::Biome;
using gamev2::world::Deposit;
using gamev2::world::Grid;
using gamev2::world::kBiomeCount;
using gamev2::world::Stone;
using gamev2::world::World;
using gamev2::world::WorldParams;
using gamev2::world::generate_world;

using gamev2::world::CaveHeaven;
using gamev2::world::DomainGrade;
using gamev2::world::LandType;
using gamev2::world::Scar;
using gamev2::world::SpiritEye;
using gamev2::world::SpiritTide;
using gamev2::world::SpiritVein;
using gamev2::world::TidePhase;
using gamev2::world::VeinRank;
using gamev2::world::kLandTypeCount;
using gamev2::world::kTribulationRadius;

constexpr std::array<std::string_view, kBiomeCount> kBiomeNames{
    "ocean", "lake", "mountain", "marshland", "swamp",
    "temperate_coniferous", "temperate_broadleaf", "tropical_coniferous",
    "tropical_dry_broadleaf", "tropical_moist_broadleaf", "grassland",
    "meadowland", "savanna", "shrubland", "pine_barrens", "sand_desert",
    "rocky_wasteland", "badlands", "tundra", "glacier",
};

/// 名字表按枚举值索引，因此必须与枚举逐项对齐。头尾与几处中间项被钉在
/// 这里：改动枚举却忘了改表，会在编译期失败，而不是把整张图涂错。
static_assert(kBiomeNames.size() == kBiomeCount);
static_assert(kBiomeNames[static_cast<std::size_t>(Biome::ocean)] == "ocean");
static_assert(kBiomeNames[static_cast<std::size_t>(Biome::swamp)] == "swamp");
static_assert(kBiomeNames[static_cast<std::size_t>(Biome::pine_barrens)]
              == "pine_barrens");
static_assert(kBiomeNames[static_cast<std::size_t>(Biome::glacier)] == "glacier");

constexpr std::array<std::array<std::uint8_t, 3>, kBiomeCount> kBiomeColor{{
    {20, 40, 120}, {30, 90, 160}, {130, 130, 130}, {90, 110, 70},
    {70, 100, 60}, {40, 80, 60}, {30, 100, 40}, {20, 120, 50},
    {150, 130, 60}, {20, 140, 40}, {120, 170, 70}, {150, 190, 90},
    {190, 180, 90}, {160, 150, 90}, {60, 100, 60}, {220, 200, 130},
    {150, 130, 110}, {170, 110, 90}, {200, 210, 210}, {235, 240, 245},
}};
static_assert(kBiomeColor.size() == kBiomeCount);

/// 六类地貌的配色。与群系配色分开：两者正交，叠加显示会把地脉压掉。
constexpr std::array<std::array<std::uint8_t, 3>, kLandTypeCount> kLandTypeColor{{
    {240, 220, 120},  // blessed 福地
    {120, 180, 90},   // miasma 瘴域
    {90, 70, 120},    // yin 阴地
    {30, 30, 30},     // dead 死地
    {110, 150, 200},  // turbid 浊域
    {205, 205, 205},  // mundane 凡地
}};
static_assert(kLandTypeColor.size() == kLandTypeCount);

constexpr std::array<std::string_view, kLandTypeCount> kLandTypeNames{
    "blessed", "miasma", "yin", "dead", "turbid", "mundane",
};
static_assert(kLandTypeNames.size() == kLandTypeCount);
static_assert(kLandTypeNames[static_cast<std::size_t>(LandType::dead)] == "dead");

constexpr std::array<std::string_view, 12> kStoneNames{
    "sandstone", "shale", "limestone", "granite", "basalt", "slate",
    "gravel", "marble", "clay", "ice", "igneous", "igneous_intrusive",
};
static_assert(kStoneNames.size()
              == static_cast<std::size_t>(Stone::igneous_intrusive) + 1);
static_assert(kStoneNames[static_cast<std::size_t>(Stone::sandstone)]
              == "sandstone");
static_assert(kStoneNames[static_cast<std::size_t>(Stone::ice)] == "ice");
static_assert(kStoneNames[static_cast<std::size_t>(Stone::igneous_intrusive)]
              == "igneous_intrusive");

constexpr std::array<std::string_view, 13> kDepositNames{
    "none", "copper", "iron", "gold", "silver", "tin", "lead", "zinc",
    "coal", "diamond", "sapphire", "emerald", "adamantine",
};
static_assert(kDepositNames.size()
              == static_cast<std::size_t>(Deposit::adamantine) + 1);
static_assert(kDepositNames[static_cast<std::size_t>(Deposit::none)] == "none");
static_assert(kDepositNames[static_cast<std::size_t>(Deposit::coal)] == "coal");
static_assert(kDepositNames[static_cast<std::size_t>(Deposit::adamantine)]
              == "adamantine");

using Rgb = std::vector<std::uint8_t>;

constexpr int kDx4[4] = {1, -1, 0, 0};
constexpr int kDy4[4] = {0, 0, 1, -1};

/// 写失败的图片数。导出失败必须体现在退出码上，否则一张图都没写出来时
/// 「图像已导出到 …」照样会打印，调用方无从分辨。
std::uint32_t g_write_failures = 0;

void write_ppm(const std::filesystem::path& path, std::uint32_t width,
               std::uint32_t height, const Rgb& rgb)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::println(stderr, "写不了 {}", path.string());
        ++g_write_failures;
        return;
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
    if (!out)
    {
        std::println(stderr, "写坏了 {}", path.string());
        ++g_write_failures;
    }
}

/// 深蓝到白的五段渐变，中间过渡蓝、绿、黄三色。够看出趋势即可，
/// 不必追求视觉准确。
Rgb ramp(std::uint8_t value)
{
    constexpr std::array<std::array<std::uint8_t, 3>, 5> kRamp{{
        {0, 0, 40}, {0, 90, 160}, {40, 150, 60}, {210, 200, 60}, {255, 255, 255},
    }};
    const double t = static_cast<double>(value) / 255.0 * (kRamp.size() - 1);
    const auto lo = static_cast<std::size_t>(t);
    const auto hi = std::min(lo + 1, kRamp.size() - 1);
    const double f = t - static_cast<double>(lo);

    Rgb rgb;
    rgb.reserve(3);
    for (std::size_t c = 0; c < 3; ++c)
    {
        const double mixed = kRamp[lo][c] * (1.0 - f) + kRamp[hi][c] * f;
        rgb.push_back(static_cast<std::uint8_t>(mixed));
    }
    return rgb;
}

/// 岩性是枚举，取值 0..11，直接喂给 ramp 会全落在最低一段看不出差别，
/// 因此放大到整个渐变范围。
constexpr std::uint8_t kStoneRampScale = 20;

template<typename T>
void export_scalars(const World& world, const std::filesystem::path& dir,
                    std::string_view label, const Grid<T>& grid)
{
    // 按本图最大值归一化：elevation 的上限是 100 而 flow 能到几万，
    // 固定刻度会让其中一张全黑。
    std::uint32_t peak = 1;
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        peak = std::max(peak, static_cast<std::uint32_t>(grid.at(i)));
    }

    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(grid.size()) * 3);
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        // 乘法用 64 位：flow 是 uint32_t，值可达上万，*255 会在 uint32 里回绕。
        const auto scaled = static_cast<std::uint8_t>(
            static_cast<std::uint64_t>(grid.at(i)) * 255 / peak);
        const Rgb px = ramp(scaled);
        rgb.insert(rgb.end(), px.begin(), px.end());
    }
    write_ppm(dir / (std::string(label) + ".ppm"), world.params().width,
              world.params().height, rgb);
}

void append_flag(Rgb& target, bool on)
{
    const Rgb px = ramp(on ? 255 : 0);
    target.insert(target.end(), px.begin(), px.end());
}

void export_layers(const World& world, const std::filesystem::path& dir)
{
    // 每层四张图分别写循环而不是拼成一张表：Grid<Stone> 与 Grid<bool> 元素
    // 类型不同，混在一个容器里既推不出公共类型也没法统一取值。
    for (std::uint32_t z = 1; z < world.params().z_levels; ++z)
    {
        const auto n = world.stone[z].size();
        Rgb stone;
        Rgb cavern;
        Rgb aquifer;
        Rgb deposit;
        stone.reserve(static_cast<std::size_t>(n) * 3);
        cavern.reserve(static_cast<std::size_t>(n) * 3);
        aquifer.reserve(static_cast<std::size_t>(n) * 3);
        deposit.reserve(static_cast<std::size_t>(n) * 3);

        for (std::uint32_t i = 0; i < n; ++i)
        {
            const Rgb rock = ramp(static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(world.stone[z].at(i))
                * kStoneRampScale));
            stone.insert(stone.end(), rock.begin(), rock.end());
            append_flag(cavern, world.cavern[z].at(i));
            append_flag(aquifer, world.aquifer[z].at(i));
            const Rgb ore = ramp(static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(world.deposit[z].at(i))
                * kStoneRampScale));
            deposit.insert(deposit.end(), ore.begin(), ore.end());
        }

        const std::string suffix = "_z" + std::to_string(z) + ".ppm";
        write_ppm(dir / ("stone" + suffix), world.params().width,
                  world.params().height, stone);
        write_ppm(dir / ("cavern" + suffix), world.params().width,
                  world.params().height, cavern);
        write_ppm(dir / ("aquifer" + suffix), world.params().width,
                  world.params().height, aquifer);
        write_ppm(dir / ("deposit" + suffix), world.params().width,
                  world.params().height, deposit);
    }
}

/// 湖泊按 4 连通分量计数。
///
/// 洼底不止一格：priority-flood 把整片洼底抬到同一溢流高度，最陡下降在
/// 洼底的每一格都找不到严格更低的邻居，于是每一格都成了「终点」。按终点
/// 计数会把一个湖数成几十上百「处」，窄洼湖反而可能是 0 处。生成阶段就是
/// 按 4 连通洪泛成湖的，所以连通分量才是「处」。
std::uint32_t count_lake_bodies(const World& world)
{
    const WorldParams& p = world.params();
    const auto n = static_cast<std::size_t>(p.width) * p.height;
    std::vector<bool> seen(n, false);
    std::vector<std::uint32_t> queue;
    std::uint32_t bodies = 0;

    for (std::uint32_t y = 0; y < p.height; ++y)
    {
        for (std::uint32_t x = 0; x < p.width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            if (world.lake_depth.at(i) == 0 || seen[i])
            {
                continue;
            }
            ++bodies;
            queue.clear();
            queue.push_back(i);
            seen[i] = true;
            for (std::size_t head = 0; head < queue.size(); ++head)
            {
                const std::uint32_t j = queue[head];
                const auto jx = static_cast<int>(j % p.width);
                const auto jy = static_cast<int>(j / p.width);
                for (int d = 0; d < 4; ++d)
                {
                    const int nx = jx + kDx4[d];
                    const int ny = jy + kDy4[d];
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(p.width)
                        || ny >= static_cast<int>(p.height))
                    {
                        continue;
                    }
                    const std::uint32_t k = world.elevation.index_of(
                        static_cast<std::uint32_t>(nx),
                        static_cast<std::uint32_t>(ny));
                    if (world.lake_depth.at(k) == 0 || seen[k])
                    {
                        continue;
                    }
                    seen[k] = true;
                    queue.push_back(k);
                }
            }
        }
    }
    return bodies;
}

void print_surface_stats(const World& world)
{
    std::array<std::uint32_t, kBiomeCount> biomes{};
    std::uint32_t rivers = 0;
    std::uint8_t widest = 0;
    std::uint32_t lake_cells = 0;
    std::uint32_t aquifer_top_sum = 0;
    std::uint32_t aquifer_count = 0;

    for (std::uint32_t y = 0; y < world.params().height; ++y)
    {
        for (std::uint32_t x = 0; x < world.params().width; ++x)
        {
            const std::uint32_t i = world.elevation.index_of(x, y);
            ++biomes[static_cast<std::size_t>(world.biome.at(i))];
            if (world.river_width.at(i) > 0)
            {
                ++rivers;
                widest = std::max(widest, world.river_width.at(i));
            }
            if (world.lake_depth.at(i) > 0)
            {
                ++lake_cells;
            }
            if (world.aquifer_top.at(i) != 0)
            {
                aquifer_top_sum += world.aquifer_top.at(i);
                ++aquifer_count;
            }
        }
    }

    std::println("地表 {} x {}，海平面 {}", world.params().width,
                 world.params().height, world.sea_level());
    std::println("群系面积占比：");
    for (std::size_t i = 0; i < kBiomeCount; ++i)
    {
        if (biomes[i] == 0)
        {
            continue;
        }
        const double pct = 100.0 * biomes[i] / world.elevation.size();
        std::println("  {:<26} {:>8} 格  {:>5.1f}%", kBiomeNames[i],
                     biomes[i], pct);
    }
    std::println("河流：{} 格，最宽 {}，湖泊 {} 格 / {} 处", rivers, widest,
                 lake_cells, count_lake_bodies(world));
    if (aquifer_count > 0)
    {
        std::println("平均含水层顶面：{:.1f} 格深",
                     static_cast<double>(aquifer_top_sum) / aquifer_count);
    }
    else
    {
        std::println("平均含水层顶面：无含水层");
    }
}

void print_underground_stats(const World& world)
{
    std::array<std::uint32_t, kStoneNames.size()> stones{};
    std::array<std::uint32_t, kDepositNames.size()> deposits{};
    std::uint32_t cavern_cells = 0;
    std::uint32_t aquifer_cells = 0;
    std::uint32_t total = 0;

    // z=0 是地表层，不是地下。统计口径与导出的 stone_z1..z5 保持一致，
    // 这样打印出来的百分比可以直接从图像复算，也与项目里其它地方的
    // 「地下」口径（z=1..z_levels-1）相同。
    for (std::uint32_t z = 1; z < world.params().z_levels; ++z)
    {
        for (std::uint32_t i = 0; i < world.stone[z].size(); ++i)
        {
            ++stones[static_cast<std::size_t>(world.stone[z].at(i))];
            ++deposits[static_cast<std::size_t>(world.deposit[z].at(i))];
            cavern_cells += world.cavern[z].at(i) ? 1u : 0u;
            aquifer_cells += world.aquifer[z].at(i) ? 1u : 0u;
            ++total;
        }
    }

    std::println("地下 {} 层，共 {} 格", world.params().z_levels - 1, total);
    std::println("岩性占比：");
    for (std::size_t i = 0; i < kStoneNames.size(); ++i)
    {
        if (stones[i] == 0)
        {
            continue;
        }
        std::println("  {:<20} {:>8} 格  {:>5.1f}%", kStoneNames[i], stones[i],
                     100.0 * stones[i] / total);
    }
    std::println("矿脉：");
    for (std::size_t i = 1; i < kDepositNames.size(); ++i)
    {
        if (deposits[i] == 0)
        {
            continue;
        }
        std::println("  {:<14} {:>8} 格", kDepositNames[i], deposits[i]);
    }
    std::println("洞室 {:.1f}%，含水层 {:.1f}%",
                 100.0 * cavern_cells / total,
                 100.0 * aquifer_cells / total);
}

std::string_view phase_name(TidePhase phase)
{
    switch (phase)
    {
    case TidePhase::tide:
        return "灵潮期";
    case TidePhase::decline:
        return "衰退期";
    case TidePhase::trough:
        return "谷底期";
    }
    return "未知";
}

void print_spirit_stats(const World& world)
{
    const SpiritTide& tide = world.spirit_tide;
    std::println("灵潮：灵历 {} 年，第 {} 轮，{}第 {} 年，距下轮 {} 年",
                 tide.year, tide.cycle + 1, phase_name(tide.phase),
                 tide.year_in_phase, tide.years_to_next_tide);
    std::println("  浓度 {} / 峰值 {}", tide.level, tide.peak);

    std::array<std::uint32_t, 5> ranks{};
    std::uint32_t eye_flow_total = 0;
    std::uint32_t depleted = 0;
    for (const SpiritVein& v : world.veins)
    {
        ++ranks[static_cast<std::size_t>(v.rank)];
    }
    for (const SpiritEye& e : world.eyes)
    {
        eye_flow_total += e.flow;
        if (e.depleted)
        {
            ++depleted;
        }
    }
    std::println("灵脉：龙 {}，主 {}，支 {}，荒 {}", ranks[4], ranks[3],
                 ranks[2], ranks[1]);
    std::println("灵眼：{} 个（枯眼 {}），总流量 {}", world.eyes.size(),
                 depleted, eye_flow_total);

    std::array<std::uint32_t, kLandTypeCount> lands{};
    for (std::uint32_t i = 0; i < world.land_type.size(); ++i)
    {
        ++lands[static_cast<std::size_t>(world.land_type.at(i))];
    }
    std::println("地貌面积占比：");
    for (std::size_t i = 0; i < kLandTypeCount; ++i)
    {
        if (lands[i] == 0)
        {
            continue;
        }
        std::println("  {:<10} {:>8} 格  {:>5.1f}%", kLandTypeNames[i], lands[i],
                     100.0 * lands[i] / world.land_type.size());
    }

    std::uint32_t upkeep_total = 0;
    for (const CaveHeaven& h : world.cave_heavens)
    {
        upkeep_total += h.upkeep;
    }
    std::println("洞天：{} 处，年耗灵石合计 {}", world.cave_heavens.size(),
                 upkeep_total);

    std::println("渡劫场：{} 处，劫云半径 {} 格（= 百里）",
                 world.tribulation_sites.size(), kTribulationRadius);

    // 图幅随行：灵气层的浓度场统计量只在默认图幅下可比（设计文档 §9.2），
    // 不写图幅，读者就不知道自己拿的是哪一口径的数。
    std::println("图幅：{} x {}（灵气层统计量只在 256 x 256 默认图幅下可比）",
                 world.params().width, world.params().height);
}

/// UTF-8 串的终端显示宽度。
///
/// 汉字占两个终端列，而 `{:10}` 之类按码点数补位——补出来的表头会随
/// 名字长短错开。所以自己数：UTF-8 的续字节（10xxxxxx）不占码位，
/// 每个码位按两个终端列算。
std::size_t display_width(std::string_view text)
{
    std::size_t code_points = 0;
    for (const char byte : text)
    {
        if ((static_cast<unsigned char>(byte) & 0xC0u) != 0x80u)
        {
            ++code_points;
        }
    }
    return code_points * 2u;
}

/// 按显示宽度左对齐补空格。宽度够了就不再补。
std::string pad_to_width(std::string_view text, std::size_t width)
{
    std::string padded(text);
    const std::size_t used = display_width(text);
    if (used < width)
    {
        padded.append(width - used, ' ');
    }
    return padded;
}

void print_ledger_stats(const gamev2::history::Ledger& ledger,
                        const gamev2::history::LedgerDiagnostics& diagnostics)
{
    // 跨度可能为负（now < epoch）：与导出同一套钳位，别让 demo 印出
    // 一个巨大的年数。
    const std::uint32_t span =
        (ledger.now > ledger.epoch) ? (ledger.now - ledger.epoch) : 0u;
    std::println("灵脉台账：纪元 {} —— 当代 {}（{} 年）",
                 ledger.epoch, ledger.now, span);
    // 荒脉没有灵眼、不可采，不进台账：台账的脉数比世界的脉数少，
    // 差额在这里说出来，读者才不会以为台账漏了账。
    std::println("  脉 {} 条（荒脉 {} 条不入账），事件 {} 笔，未登记开采 {}",
                 ledger.veins.size(), diagnostics.barren_veins,
                 ledger.events.size(), ledger.unregistered_extracted);
    std::println("七宗与无主配额与当代余额：");
    // 宗门名按终端列宽补位，不按码点数：汉字占两列，按码点数补出来的
    // 表头会随名字长短错开。
    constexpr std::size_t kNameWidth = 10u;
    for (const auto& sect : ledger.sects)
    {
        std::println("  {} 配额 {:>6}  当代 {:>6}  配额与余额差 {:>7}  越采 {:>5}",
                     pad_to_width(gamev2::history::sect_name(sect.sect),
                                  kNameWidth),
                     sect.quota_total, sect.current_total,
                     sect.deviation_total, sect.overmined_amount);
    }
    std::println("  口径差 {}，越采差 {}", ledger.structural_deviation,
                 ledger.overmining_deviation);
    // 诊断量是台账与 worldgen 之间的接缝：对不上就报出来，不藏。
    if (diagnostics.depletion_claim_unbacked > 0u
        || diagnostics.depletion_unmarked > 0u
        || diagnostics.missing_yin_vein > 0u
        || diagnostics.short_main_veins > 0u
        || diagnostics.overmine_before_epoch > 0u
        || diagnostics.flow_conversion_collapsed > 0u
        || diagnostics.span_too_large > 0u)
    {
        std::println("  诊断：枯眼无账 {}，枯竭未标 {}，缺阴脉 {}，主脉缺 {}，"
                     "越采早于纪元 {}，换算塌缩 {}，跨度超限 {}",
                     diagnostics.depletion_claim_unbacked,
                     diagnostics.depletion_unmarked,
                     diagnostics.missing_yin_vein,
                     diagnostics.short_main_veins,
                     diagnostics.overmine_before_epoch,
                     diagnostics.flow_conversion_collapsed,
                     diagnostics.span_too_large);
    }
}

void export_biome(const World& world, const std::filesystem::path& dir)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(world.biome.size()) * 3);
    for (std::uint32_t i = 0; i < world.biome.size(); ++i)
    {
        const auto& c = kBiomeColor[static_cast<std::size_t>(world.biome.at(i))];
        rgb.push_back(c[0]);
        rgb.push_back(c[1]);
        rgb.push_back(c[2]);
    }
    write_ppm(dir / "biome.ppm", world.params().width, world.params().height,
              rgb);
}

void export_land_type(const World& world, const std::filesystem::path& dir)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(world.land_type.size()) * 3);
    for (std::uint32_t i = 0; i < world.land_type.size(); ++i)
    {
        const auto& c =
            kLandTypeColor[static_cast<std::size_t>(world.land_type.at(i))];
        rgb.push_back(c[0]);
        rgb.push_back(c[1]);
        rgb.push_back(c[2]);
    }
    write_ppm(dir / "land_type.ppm", world.params().width,
              world.params().height, rgb);
}

/// 渡劫场的劫心。两色即可：劫心是点状目标，位置比颜色重要。
void export_tribulation_site(const World& world,
                             const std::filesystem::path& dir)
{
    constexpr std::array<std::uint8_t, 3> kPlain{45, 45, 55};
    constexpr std::array<std::uint8_t, 3> kSite{230, 70, 45};
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(world.tribulation_site.size()) * 3);
    for (std::uint32_t i = 0; i < world.tribulation_site.size(); ++i)
    {
        const auto& c = world.tribulation_site.at(i) ? kSite : kPlain;
        rgb.push_back(c[0]);
        rgb.push_back(c[1]);
        rgb.push_back(c[2]);
    }
    write_ppm(dir / "tribulation_site.ppm", world.params().width,
              world.params().height, rgb);
}

/// 绝对刻度导出。灵气层这两张网格本来就是 0..255 的字节，浓度的分档
/// 阈值（8/24/60/120）有意义；按最大值归一化会把潮位差与阈值关系一起
/// 抹掉，那张图就再也分不出灵潮期与谷底期。
void export_bytes(const World& world, const std::filesystem::path& dir,
                  std::string_view label, const Grid<std::uint8_t>& grid)
{
    Rgb rgb;
    rgb.reserve(static_cast<std::size_t>(grid.size()) * 3);
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        const Rgb px = ramp(grid.at(i));
        rgb.insert(rgb.end(), px.begin(), px.end());
    }
    write_ppm(dir / (std::string(label) + ".ppm"), world.params().width,
              world.params().height, rgb);
}

}  // namespace

int main(int argc, char** argv)
{
    WorldParams params;
    // std::stoull 遇到非数字会抛 std::invalid_argument，接住它比让进程
    // 直接 terminate 友好。
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "--seed" && i + 1 < argc)
            {
                params.seed = std::stoull(argv[++i]);
            }
            else if (arg == "--size" && i + 1 < argc)
            {
                params.width = std::stoul(argv[++i]);
                params.height = params.width;
            }
            else if (arg == "--year" && i + 1 < argc)
            {
                params.spirit_year = std::stoul(argv[++i]);
            }
            else
            {
                std::println(stderr,
                             "用法: worldgen_demo [--seed N] [--size N] [--year N]");
                return 2;
            }
        }
    }
    catch (const std::exception&)
    {
        std::println(stderr,
                     "用法: worldgen_demo [--seed N] [--size N] [--year N]");
        return 2;
    }

    // 格子总数的上限。超过它，分配要么直接失败，要么在 CRT 内部触发
    // fail-fast（进程 abort，屏幕上没有任何提示，而且 catch 不到）——
    // 所以要在分配之前挡下来，给出可读的提示。
    constexpr std::uint64_t kMaxCells = 1ull << 26;
    const auto cells = static_cast<std::uint64_t>(params.width) * params.height
                       * params.z_levels;
    if (cells > kMaxCells)
    {
        std::println(stderr, "格子总数 {} 超过上限 {}（{} x {} x {}）", cells,
                     kMaxCells, params.width, params.height, params.z_levels);
        return 2;
    }

    auto generated = generate_world(params);
    if (!generated)
    {
        std::println(stderr, "生成失败：参数非法");
        return 1;
    }
    const World& world = *generated;

    const std::filesystem::path dir = "agent-temp/worldgen";
    std::filesystem::create_directories(dir);

    print_surface_stats(world);
    print_underground_stats(world);
    print_spirit_stats(world);

    gamev2::history::LedgerDiagnostics diagnostics;
    const gamev2::history::Ledger ledger =
        gamev2::history::build_ledger(world, diagnostics);
    print_ledger_stats(ledger, diagnostics);
    if (!gamev2::history::export_ledger(ledger, diagnostics,
                                        dir / "ledger.txt"))
    {
        std::println(stderr, "写不了 {}", (dir / "ledger.txt").string());
        ++g_write_failures;
    }

    export_scalars(world, dir, "elevation", world.elevation);
    export_scalars(world, dir, "rainfall", world.rainfall);
    export_scalars(world, dir, "temperature", world.temperature);
    export_scalars(world, dir, "drainage", world.drainage);
    export_scalars(world, dir, "flow", world.flow);
    export_biome(world, dir);
    export_land_type(world, dir);
    export_bytes(world, dir, "ley_strength", world.ley_strength);
    export_bytes(world, dir, "domain_concentration",
                 world.domain_concentration);
    export_tribulation_site(world, dir);
    export_layers(world, dir);

    // 输出目录是相对路径，取决于从哪儿启动。打印绝对路径，调用方才知道
    // 图像究竟落在哪里。
    std::println("图像已导出到 {}", std::filesystem::absolute(dir).string());
    // 写失败必须体现在退出码上：否则「图像已导出到」会在一张图都没写出来的
    // 情况下照样打印，调用方无从分辨。
    return g_write_failures == 0 ? 0 : 1;
}

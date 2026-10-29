/// @file scenario.hpp
/// @brief 生态演示的场景配置：三套预设、位点蓝图、遗传与环境配置、初始种群。
///
/// 这套参数被 sim（无界面）与 demo_gui（有界面）共用。分成两份会让那些实测
/// 出来的调平衡数字各走各的，界面上的种群曲线就与 sim 打印的对不上——而
/// 「对不上」恰恰是玩家唯一会去核对的事。
#pragma once

#include "gamev2/ecology/ecology.hpp"
#include "gamev2/environment/config.hpp"
#include "gamev2/genetics/genome.hpp"
#include "gamev2/genetics/locus.hpp"
#include "gamev2/genetics/species.hpp"

#include <cstdint>
#include <expected>
#include <random>

namespace gamev2::demo {

/// 三套具名配置。**它们只差一个量**——Lifespan 的基准——因为预设的价值在于
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

/// 预设的名字。命令行与界面上都用它。
[[nodiscard]] const char* preset_name(Preset p) noexcept;

/// 一局的全部配置。table 必须在 Ecology 之前就位——Ecology 持有它的引用，
/// 引用比字段早一步拿到，因此 Scenario 必须比 Ecology 活得久。
struct Scenario
{
    gamev2::genetics::LocusTable table;
    gamev2::genetics::GeneticsConfig genetics;
    gamev2::environment::EnvironmentConfig environment;
};

/// 按预设装配配置。
[[nodiscard]] Scenario build_scenario(Preset preset);

/// 校验蓝图配置。返回值是 genetics::validate 的原样转发——调用方能报出
/// 具体错误码，而不是一句「非法」。
///
/// 校验只在加载期做一次，热路径零检查——这是 locus.hpp 明写的契约，不调则
/// 配置错误会推迟到第 3 季第 4000 个个体身上才炸。
[[nodiscard]] std::expected<void, gamev2::genetics::LocusConfigError>
validate(const Scenario& scenario);

/// 登记一个启用全部位点的物种。
[[nodiscard]] std::expected<gamev2::genetics::SpeciesId,
                            gamev2::genetics::SpeciesError>
register_full_species(gamev2::ecology::Ecology& world);

/// 按各格真实承载力铺初始种群。
void seed_population(gamev2::ecology::Ecology& world,
                     gamev2::genetics::SpeciesId sid,
                     const gamev2::genetics::SpeciesMask& mask,
                     const Scenario& scenario,
                     std::mt19937_64& rng);

}  // namespace gamev2::demo

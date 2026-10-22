/// @file ledger.hpp
/// @brief 时序层：灵脉产权与开采台账。
///
/// 台账是「一串有署名的增减」而不是「某一时刻的状态」：老配额为什么
/// 不公，答案不在终态里，在六百年里一笔笔累积出来的差。
///
/// 本模块只读 worldgen 的终态，不回写 `World` 的任何字段，也不挂在
/// `World` 上——历史是一次性推演的结果，与空间无关。
#pragma once

#include "gamev2/world/spirit.hpp"
#include "gamev2/world/world.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gamev2::history {

/// 七宗。顺序固定：与设定 8.1 的表格一致，索引即宗门编号。
enum class Sect : std::uint8_t
{
    taixuan,    ///< 太玄宗：龙脉 + 三条主脉，执掌盟约
    daxia,      ///< 丹霞谷：两条主脉
    hanjiang,   ///< 寒江剑府：一条阴脉
    wanshou,    ///< 万兽山：一条主脉
    qiangnang,  ///< 青囊谷：一条主脉
    tianji,     ///< 天机阁：无地，执历算
    jiuyou,     ///< 九幽宫：一条主脉，不登记
    unowned,    ///< 无主：散修与凡人共享的支脉
    count,
};

/// 一条脉的账。
struct QuotaEntry
{
    std::uint32_t vein = 0;
    Sect holder = Sect::unowned;
    gamev2::world::VeinRank rank = gamev2::world::VeinRank::none;

    /// 8300 年《脉律》核定的总量配额。越采脉取反推区间的下界。
    std::uint32_t surveyed_total = 0;
    /// 反推区间的开区间上界。非越采脉等于 surveyed_total。
    std::uint32_t surveyed_total_hi = 0;
    /// 8300 年核定的安全流速。
    std::uint32_t surveyed_rate = 0;

    /// 8300→当代累计开采量。
    std::uint32_t extracted = 0;
    /// 其中越采的部分。
    std::uint32_t overmined_amount = 0;
    /// 当代余额 = surveyed_total − extracted。
    ///
    /// 无符号成立的前提是 extracted ≤ surveyed_total：越采那一笔按当年
    /// 余额封顶，所以余额永远不会为负。本账算不出「已被采穿」这一侧。
    std::uint32_t balance = 0;

    /// 归零年。0 表示未枯竭。
    std::uint32_t depleted_year = 0;
    /// 该脉的脉律配额是否登记在册。九幽宫那条不登记。
    bool registered = true;
};

/// 一笔开采。
struct ExtractionEvent
{
    std::uint32_t vein = 0;
    std::uint32_t year = 0;
    std::uint32_t amount = 0;
    bool over_quota = false;
};

/// 一宗的汇总账。
struct SectLedger
{
    Sect sect = Sect::unowned;
    /// 名下脉的 8300 年配额总和。
    std::uint32_t quota_total = 0;
    /// 名下脉的 8300 年流速配额总和。
    std::uint32_t quota_rate = 0;
    /// 名下脉的当代余额总和。
    std::uint32_t current_total = 0;
    /// 名下累计开采量（≡ 配额 − 当代余额）。
    ///
    /// 恒 ≥ 0：配额封顶让越采不可能发生，余额永远不会低于 0，欠账这一侧
    /// 本账算不出来。
    std::int64_t deviation_total = 0;
    /// 流速偏差：核定流速按当代潮位折下来的差额，当代潮位高于 8300 时
    /// 为负。
    std::int64_t deviation_rate = 0;
    /// 累计越采量。
    std::uint32_t overmined_amount = 0;
    /// 该宗的脉律配额是否登记在册。九幽宫为 false。
    bool registered = true;
};

/// 整本台账。
struct Ledger
{
    std::uint32_t epoch = 0;
    std::uint32_t now = 0;
    std::vector<QuotaEntry> veins;
    std::vector<ExtractionEvent> events;
    std::vector<SectLedger> sects;
    /// 未登记开采的总量（九幽宫那条脉）。
    std::uint32_t unregistered_extracted = 0;
    /// 口径差：按 8300 峰值口径核定、按当代谷底口径衡量的差额。
    std::int64_t structural_deviation = 0;
    /// 人为差：越采总量。
    std::int64_t overmining_deviation = 0;
};

/// 台账推演的诊断量。退化不是错误，由这里报出。
struct LedgerDiagnostics
{
    /// 寒江剑府没拿到阴脉：主脉够六条（寒江能分到一条主脉），
    /// 但里面没有阴地。主脉不足六条时寒江连脉都分不到，
    /// 那时由 short_main_veins 报。
    std::uint32_t missing_yin_vein = 0;
    /// 主脉不足九条，差几条报几条。
    std::uint32_t short_main_veins = 0;
    /// worldgen 标了枯眼但推演中余额没归零。
    std::uint32_t depletion_claim_unbacked = 0;
    /// 推演中余额归零但 worldgen 没标枯竭。
    std::uint32_t depletion_unmarked = 0;
    /// [epoch+1, now) 年里没有哪一年的年初余额装得下这笔越采，
    /// 越采只能回落到 8300。
    std::uint32_t overmine_before_epoch = 0;
    /// 荒脉数量：不进台账，只计数。
    std::uint32_t barren_veins = 0;
    /// 换算系数整除成 0：流速与储量的比例离谱，推演不出开采量。
    std::uint32_t flow_conversion_collapsed = 0;
    /// 推演跨度超过上限：台账按「没有推演」处理，余额停在基数上。
    std::uint32_t span_too_large = 0;
};

/// 由 worldgen 的终态推演出灵脉台账。
///
/// 只读 world，不回写它的任何字段。退化不是错误：退化量由
/// LedgerDiagnostics 报出，由调用方决定要不要打印。
[[nodiscard]] Ledger build_ledger(const gamev2::world::World& world);

/// 同上，但把诊断量一并填出。
[[nodiscard]] Ledger build_ledger(const gamev2::world::World& world,
                                   LedgerDiagnostics& diagnostics);

/// 导出人可读台账文本。返回 false 表示文件写不动。
///
/// 诊断量一并写出：退化不是错误，但台账是给人读的文档，读者必须能在
/// 里面看见每一处对不上的地方。
[[nodiscard]] bool export_ledger(const Ledger& ledger,
                                 const LedgerDiagnostics& diagnostics,
                                 const std::filesystem::path& path);

/// 宗门的汉字名。台账是给人读的，编号对机器友好、对人无意义。
///
/// 导出的台账与 demo 的对照表都要用这张表，所以放在公共头里：
/// 两处各留一份的话，改了名字就会有一处对不上。
[[nodiscard]] const char* sect_name(Sect sect);

}  // namespace gamev2::history

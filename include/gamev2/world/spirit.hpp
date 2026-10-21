/// @file spirit.hpp
/// @brief 灵气层的数据类型：地脉、灵脉、灵眼、灵域、地貌与灵潮状态。
///
/// 单独成头文件而不并入 enums.hpp：那里的枚举全部服务于地表/地下自然层，
/// 灵气层是另一个子系统，挤在一起会让两个子系统互相牵连。
#pragma once

#include "gamev2/world/enums.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 地脉属性。五行为主，异质稀有（设定 2.1）。
enum class Element : std::uint8_t
{
    none,
    metal, wood, water, fire, earth,
    thunder, ice, wind, light, dark, sound, poison,
};

inline constexpr std::size_t kElementCount = 13;

/// 灵域分等（设定 2.4）。
enum class DomainGrade : std::uint8_t
{
    barren,    ///< 废域：几近无灵，凡人域
    thin,      ///< 薄域
    medium,    ///< 中域
    thick,     ///< 厚域
    blessed,   ///< 福地：可开洞天
};

inline constexpr std::size_t kDomainGradeCount = 5;

/// 六类地貌（设定 4.1）。与 Biome 正交，不互相覆盖。
enum class LandType : std::uint8_t
{
    blessed,   ///< 福地
    miasma,    ///< 瘴域
    yin,       ///< 阴地
    dead,      ///< 死地
    turbid,    ///< 浊域
    mundane,   ///< 凡地
};

inline constexpr std::size_t kLandTypeCount = 6;

/// 历史疤痕。设定只点名三种具象（枯墟、渡劫废墟、沸海）；
/// 第四种「枯眼周边」是枯眼的必然后果。
enum class Scar : std::uint8_t
{
    none,
    sinkhole,      ///< 越采塌陷（枯墟式）
    tribulation,   ///< 渡劫失败
    turbid,        ///< 灵潮搅动（沸海式）
    depleted_eye,  ///< 枯眼周边
};

inline constexpr std::size_t kScarKindCount = 5;

/// 灵潮周期阶段（设定 2.6）。
enum class TidePhase : std::uint8_t
{
    tide,     ///< 灵潮期：约 300 年
    decline,  ///< 衰退期：约 500 年
    trough,   ///< 谷底期：约 200 年
};

/// 灵脉品阶（设定 2.2）。数值即排序，dragon 最大。
enum class VeinRank : std::uint8_t
{
    none = 0,
    barren = 1,  ///< 荒脉：残余矿化，储量极低
    branch = 2,  ///< 支脉：郡县级
    main = 3,    ///< 主脉：跨州级
    dragon = 4,  ///< 龙脉：贯通全境
};

/// 灵脉、节点、灵眼编号的哨兵值。沿用既有 kNoDownstream 的做法：不用
/// optional，因为这些字段与地脉图是一一对应的稠密索引。
inline constexpr std::uint32_t kNoVein = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoEye = 0xFFFFFFFFu;

/// 灵潮周期状态。世界的「年」以第一轮灵潮之始为元年。
struct SpiritTide
{
    std::uint32_t year = 0;
    std::uint32_t cycle = 0;  ///< 轮次索引，0 = 元潮
    TidePhase phase = TidePhase::trough;
    /// 相位内第几年，按「与相位起点的差」计：灵历 8914 落谷底期，起点
    /// 8801，差 113，设定称「谷底期第一百一十三年」。所以它已经是年数，
    /// 打印时不能再加一。
    std::uint32_t year_in_phase = 0;
    std::uint32_t years_to_next_tide = 0;
    std::uint8_t level = 0;   ///< 当下浓度比例 0..255
    std::uint8_t peak = 0;    ///< 本轮峰值比例 0..255
};

/// 地脉节点。节点是三维的：cell 给出地表投影，depth 给出埋深。
struct LeyNode
{
    std::uint32_t cell = 0;       ///< 地表格子的线性索引
    std::uint32_t depth = 0;      ///< 埋深，格
    std::uint32_t flow = 0;       ///< 累计流量
    std::uint32_t next = kNoNode; ///< 下游节点；汇流点与下游共用同一节点
    std::uint8_t merges = 0;      ///< 汇入本节点的通道数
    Element element = Element::none;
};

/// 一条地脉。nodes 是沿线有序的节点索引序列。
struct LeyChannel
{
    std::vector<std::uint32_t> nodes;
};

/// 一条灵脉：某条地脉上的一段。
struct SpiritVein
{
    std::uint32_t id = 0;
    VeinRank rank = VeinRank::none;
    std::uint32_t channel = 0;
    std::uint32_t first_node = 0;  ///< 段首，闭区间
    std::uint32_t last_node = 0;   ///< 段尾，闭区间
    Element element = Element::none;
    std::uint32_t reserve = 0;     ///< 储量
    std::uint32_t stability = 0;   ///< 稳定年限
    std::uint32_t eye = kNoEye;    ///< 灵眼索引
    /// 已被越采。设定 4.2 的枯墟正是「灵脉被越采，三百里地陷」，
    /// 因果上是采在先、塌在后，所以标记落在阶段 13，塌陷落在阶段 15。
    bool overmined = false;
};

/// 灵眼：灵脉的地表出露点。
struct SpiritEye
{
    std::uint32_t vein = 0;
    std::uint32_t node = 0;
    std::uint32_t cell = 0;
    std::uint32_t flow = 0;   ///< 流量：单位时间可采灵气量
    bool depleted = false;    ///< 枯眼：已抽干，不可复生
};

/// 一处历史疤痕。land_type 是它的结果，scar_kind 是它本身。
///
/// 名字带 Site 后缀：枚举已占了 Scar，结构体不能同名。
struct ScarSite
{
    Scar kind = Scar::none;
    std::uint32_t cell = 0;    ///< 中心
    std::uint32_t radius = 0;  ///< 影响半径，格
    std::uint32_t target = 0;  ///< 关联的灵脉或灵眼编号
};

/// 洞天。设定 2.5：本质是用灵石供养的蓄电池。
struct CaveHeaven
{
    std::uint32_t cell = 0;
    std::uint32_t capacity = 0;  ///< 可锁住的灵气量
    std::uint32_t upkeep = 0;    ///< 年耗灵石
};

}  // namespace gamev2::world

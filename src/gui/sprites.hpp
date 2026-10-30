/// @file sprites.hpp
/// @brief 角色精灵册：22 个角色的静态精灵与动画图集，以及个体到角色的身份判定。
#pragma once

#include <QImage>
#include <QPointF>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamev2::gui {

/// 角色目录。顺序与 assets/sprites/characters 下的资源名一一对应：
/// 前九个是宗门职位，接着四个是四修流派，最后九个是九境。
enum class Character : std::uint8_t
{
    SectMaster = 0, ///< 宗主
    Elder,          ///< 长老
    InnerMale,      ///< 内门弟子
    InnerFemale,    ///< 内门弟子
    InnerFemale2,   ///< 内门弟子
    OuterMale,      ///< 外门弟子
    OuterMale2,     ///< 外门弟子
    OuterFemale,    ///< 外门弟子
    Rogue,          ///< 散修
    Sword,          ///< 剑修
    Alchemy,        ///< 丹修
    Talisman,       ///< 符修
    Body,           ///< 体修
    Realm1,         ///< 引气
    Realm2,         ///< 炼气
    Realm3,         ///< 筑基
    Realm4,         ///< 金丹
    Realm5,         ///< 元婴
    Realm6,         ///< 化神
    Realm7,         ///< 炼虚
    Realm8,         ///< 合体
    Realm9,         ///< 大乘
};

/// 角色总数。
inline constexpr std::size_t kCharacterCount = 22;

/// 待机段的帧数。
inline constexpr int kIdleFrames = 4;

/// 行走段的帧数。
inline constexpr int kWalkFrames = 6;

/// 图集里的帧总数：待机 4 帧加行走 6 帧。
inline constexpr int kFrameCount = kIdleFrames + kWalkFrames;

/// 角色中文名。
[[nodiscard]] QString character_name(Character character);

/// 一个个体在生态图上的显示数据。
struct CreatureSprite
{
    /// 世界坐标。
    QPointF pos;

    /// 本季适应力。绝对值只在种群内部有意义，因此身份判定只看它的分位。
    float fitness = 0.0f;

    /// 四项用于判定流派的性状。
    float body_size = 0.0f;
    float speed = 0.0f;
    float fecundity = 0.0f;
    float sense_range = 0.0f;

    /// 该个体用哪张精灵。由 assign_characters 填。
    Character character = Character::Realm1;
};

/// 给一群个体定身份，并返回每个个体的适应力分位（0..1，最强为 1）。
///
/// 两条规则：
/// - **境界**：适应力在种群里的分位决定九境，分位越高境界越高。
/// - **流派**：速度最快、体型最大、繁殖最强、感知最灵各出一人，改用流派精灵。
///
/// 四道极致按固定顺序取冠军，同一个个体命中两轴时以先命中的为准。
[[nodiscard]] std::vector<float> assign_characters(
    std::vector<CreatureSprite>& creatures);

/// 玩家的宗门职位。适应力分位越高职位越高：散修 → 宗主。
[[nodiscard]] Character office_of(float fitness_rank);

/// 精灵册：静态精灵给人群，动画图集给玩家。
///
/// 存的是 CPU 侧的 QImage 而非 QPixmap：Canvas Painter 的 addImage() 收
/// QImage，而 QPixmap 绑在 GUI 线程的绘制表面上，纹理上传那条路上用不了。
class SpriteBook
{
public:
    SpriteBook();

    /// 人群用的缩略精灵。静态精灵的半尺寸（16×16）——像素画按整数倍缩小
    /// 才不糊，32 缩到 16 正好一半。
    [[nodiscard]] const QImage& crowd(Character character) const;

    /// 动画帧：原尺寸 32×32。walking 为真取行走段，否则取待机段。
    ///
    /// 首次取用时从图集切出并留下副本。切出来的图要能反复命中 Canvas
    /// Painter 的纹理缓存（缓存按 QImage 的 cacheKey 索引），所以不能每次
    /// 现切——现切每次都是新图，缓存永远不中，纹理会一轮轮地堆起来。
    [[nodiscard]] const QImage& frame(Character character, bool walking,
                                     int index) const;

private:
    std::array<QImage, kCharacterCount> crowd_;
    std::array<QImage, kCharacterCount> atlas_;

    /// 切出来的动画帧，按需填充。只有玩家的职位会被用到。
    mutable std::array<std::array<QImage, kFrameCount>, kCharacterCount> frames_;
};

}  // namespace gamev2::gui

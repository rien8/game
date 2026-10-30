#include "sprites.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace gamev2::gui {

namespace {

/// 角色的资源名与中文名。顺序即 Character 的枚举顺序。
struct Entry
{
    const char* stem;  ///< assets/sprites/characters 下的文件名（不含扩展名）
    const char* name;  ///< 中文名
};

constexpr std::array<Entry, kCharacterCount> kEntries{{
    {"office1_sect_master", "宗主"},
    {"office2_elder", "长老"},
    {"office3_inner_m", "内门弟子"},
    {"office4_inner_f", "内门弟子"},
    {"office5_inner_f2", "内门弟子"},
    {"office6_outer_m", "外门弟子"},
    {"office7_outer_m2", "外门弟子"},
    {"office8_outer_f", "外门弟子"},
    {"office9_rogue", "散修"},
    {"path_sword", "剑修"},
    {"path_alchemy", "丹修"},
    {"path_talisman", "符修"},
    {"path_body", "体修"},
    {"realm1_yinqi", "引气"},
    {"realm2_lianqi", "炼气"},
    {"realm3_zhuji", "筑基"},
    {"realm4_jindan", "金丹"},
    {"realm5_yuanying", "元婴"},
    {"realm6_huashen", "化神"},
    {"realm7_lianxu", "炼虚"},
    {"realm8_heti", "合体"},
    {"realm9_dacheng", "大乘"},
}};

/// 图集规格。atlas.json 的 spec.grid 是 6×2：待机 4 帧占前 4 格，
/// 行走 6 帧占后 6 格——跨到了第二行。
constexpr int kCellSize = 32;
constexpr int kAtlasColumns = 6;
constexpr int kWalkOffset = kIdleFrames;

/// 人群缩略精灵的边长。静态精灵 32×32 缩到一半。
constexpr int kCrowdSize = 16;

[[nodiscard]] constexpr std::size_t index_of(const Character character) noexcept
{
    return static_cast<std::size_t>(character);
}

/// 适应力分位落到的境界。分位越高境界越高。
[[nodiscard]] Character realm_of(const float fitness_rank)
{
    const auto level = static_cast<int>(fitness_rank * 9.0f);
    const auto clamped = std::clamp(level, 0, 8);
    return static_cast<Character>(static_cast<int>(Character::Realm1) + clamped);
}

}  // namespace

QString character_name(const Character character)
{
    return QString::fromUtf8(kEntries[index_of(character)].name);
}

std::vector<float> assign_characters(std::vector<CreatureSprite>& creatures)
{
    std::vector<float> ranks(creatures.size(), 0.0f);
    if (creatures.empty())
    {
        return ranks;
    }

    // 适应力分位：排序后按落点定档，最强者为 1。适应力的绝对值随季节漂，
    // 只有种群内部的名次是稳定的，因此按名次而不是按阈值分境界。
    std::vector<float> sorted;
    sorted.reserve(creatures.size());
    for (const auto& creature : creatures)
    {
        sorted.push_back(creature.fitness);
    }
    std::sort(sorted.begin(), sorted.end());

    const float span = static_cast<float>(sorted.size() - 1);
    for (std::size_t i = 0; i < creatures.size(); ++i)
    {
        const auto slot = std::lower_bound(sorted.begin(), sorted.end(),
                                           creatures[i].fitness);
        const auto rank = span > 0.0f
                              ? static_cast<float>(slot - sorted.begin()) / span
                              : 0.0f;
        ranks[i] = rank;
        creatures[i].character = realm_of(rank);
    }

    // 四道极致：速度最快、体型最大、繁殖最强、感知最灵各出一人，改用流派
    // 精灵。他们是人群里唯一几个「看得出修行方向」的个体。
    std::size_t fastest = 0;
    std::size_t biggest = 0;
    std::size_t most_prolific = 0;
    std::size_t keenest = 0;
    for (std::size_t i = 1; i < creatures.size(); ++i)
    {
        if (creatures[i].speed > creatures[fastest].speed)
        {
            fastest = i;
        }
        if (creatures[i].body_size > creatures[biggest].body_size)
        {
            biggest = i;
        }
        if (creatures[i].fecundity > creatures[most_prolific].fecundity)
        {
            most_prolific = i;
        }
        if (creatures[i].sense_range > creatures[keenest].sense_range)
        {
            keenest = i;
        }
    }
    creatures[fastest].character = Character::Sword;
    creatures[biggest].character = Character::Body;
    creatures[most_prolific].character = Character::Alchemy;
    creatures[keenest].character = Character::Talisman;
    return ranks;
}

Character office_of(const float fitness_rank)
{
    const auto level = static_cast<int>(fitness_rank * 9.0f);
    // 枚举的前九个就是职位，从宗主排到散修。
    return static_cast<Character>(std::clamp(level, 0, 8));
}

SpriteBook::SpriteBook()
{
    for (std::size_t i = 0; i < kCharacterCount; ++i)
    {
        const QString stem = QString::fromUtf8(kEntries[i].stem);
        const QImage sprite(
            QStringLiteral(":/assets/sprites/characters/individual/") + stem +
            QStringLiteral(".png"));
        // 最近邻缩小：像素画按整数倍缩小才不糊，32 → 16 正好一半。
        crowd_[i] = sprite.scaled(kCrowdSize, kCrowdSize, Qt::KeepAspectRatio,
                                  Qt::FastTransformation);
        atlas_[i].load(QStringLiteral(":/assets/sprites/characters/anim/") +
                       stem + QStringLiteral("/atlas/atlas.png"));
    }
}

const QImage& SpriteBook::crowd(const Character character) const
{
    return crowd_[index_of(character)];
}

const QImage& SpriteBook::frame(const Character character, const bool walking,
                                 const int index) const
{
    const auto slot = index_of(character);
    const int cell = walking ? kWalkOffset + index : index;
    auto& image = frames_[slot][cell];
    if (image.isNull())
    {
        const int x = (cell % kAtlasColumns) * kCellSize;
        const int y = (cell / kAtlasColumns) * kCellSize;
        image = atlas_[slot].copy(x, y, kCellSize, kCellSize);
    }
    return image;
}

}  // namespace gamev2::gui

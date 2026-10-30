/// @file views.hpp
/// @brief 三个视图部件：生态活体图、世界地图、种群曲线。
#pragma once

#include "simulation.hpp"
#include "sprites.hpp"
#include "gamev2/world/world.hpp"

#include <QCanvasOffscreenCanvas>
#include <QCanvasPainter>
#include <QCanvasPainterWidget>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace gamev2::gui {

/// 世界地图可切换的图层。
enum class MapLayer
{
    Biome,       ///< 群系
    Elevation,   ///< 高程
    Rainfall,    ///< 降水
    Temperature, ///< 温度
    Ley,         ///< 地脉强度
    LandType,    ///< 地貌：福地、瘴域、阴地、死地、浊域、凡地
    CaveHeaven,  ///< 洞天
    Tribulation, ///< 渡劫场
    Stone,       ///< 地层岩性（z = 1）
    Deposit      ///< 矿脉（z = 1）
};

/// 图层总数。界面按它铺下拉框。
inline constexpr int kMapLayerCount = 10;

/// 图层名，界面上显示用。
[[nodiscard]] QString layer_name(MapLayer layer);

/// 群系名。
[[nodiscard]] QString biome_name(gamev2::world::Biome biome);

/// 地貌名。
[[nodiscard]] QString land_type_name(gamev2::world::LandType type);

/// 生态活体图：64×64 的格网。
///
/// 底色是承载力，越绿越肥；个体画成角色精灵，玩家画成原尺寸的动画精灵并
/// 套上黄圈。整幅图交给 Qt Canvas Painter 画到 GPU 上。
///
/// 静态场景（承载力底色与人群）缓存在一张离屏画布里，只在种群变动时重画；
/// 每帧只把这张画布合成上去，再补画玩家那几笔。Canvas Painter 没有
/// QPainter 那种脏矩形缓存——一万多个精灵按 12fps 重画会把渲染线程压垮。
class EcologyView : public QCanvasPainterWidget
{
    Q_OBJECT

public:
    explicit EcologyView(Simulation& sim, QWidget* parent = nullptr);

    /// 从模拟里取一次快照。
    void refresh();

    /// 玩家这一季打算往哪走。画成玩家身旁的箭头——移动要到季末才结算，
    /// 没有这个箭头，按键会像是没反应。
    void set_intent(int dx, int dy);

    /// 玩家当前的宗门职位。
    [[nodiscard]] Character player_character() const noexcept
    {
        return player_character_;
    }

protected:
    /// 纹理在这里上传。只在首次渲染以及图形资源失效之后调用。
    void initializeResources(QCanvasPainter* painter) override;

    /// 场景变了才重画离屏画布；玩家帧变了就补一次纹理上传。
    void prePaint(QCanvasPainter* painter) override;

    /// 每帧的合成：静态场景 + 玩家。
    void paint(QCanvasPainter* painter) override;

    /// 换了顶层窗口后纹理全没了，把句柄清空，下次 initializeResources 重传。
    void graphicsResourcesInvalidated() override;

private:
    /// 推进一帧玩家动画。
    void advance_animation();

    /// 把承载力底色与人群画进离屏画布。
    void rebuild_scene(QCanvasPainter* painter);

    /// 格网在控件里的落位。
    [[nodiscard]] QRectF grid_rect() const;

    Simulation& sim_;
    std::vector<CreatureSprite> creatures_;
    QPointF player_pos_;
    bool has_player_ = false;
    Character player_character_ = Character::SectMaster;
    int intent_dx_ = 0;
    int intent_dy_ = 0;

    SpriteBook sprites_;

    /// 人群用的静态精灵纹理。22 张 16×16，上传一次长期复用。
    std::array<QCanvasImage, kCharacterCount> crowd_images_;

    /// 玩家当前的动画帧纹理。
    QCanvasImage player_image_;

    /// 静态场景的离屏画布与它的纹理句柄。
    QCanvasOffscreenCanvas scene_;
    QCanvasImage scene_image_;

    /// 离屏画布建时的大小。控件改了尺寸，画布要跟着重建。
    QSize scene_size_;

    /// 场景需要重画：种群变了、控件改了尺寸，或者纹理刚重新上传。
    bool scene_dirty_ = true;

    /// 12fps 驱动玩家精灵的待机与行走动画。图集 manifest 的 fps 也是 12。
    QTimer anim_timer_;
    int idle_frame_ = 0;
    int walk_frame_ = 0;

    /// 还要播多少帧行走。移动到季末才结算，因此由位置变化触发。
    int walk_left_ = 0;
};

/// 世界地图：世界生成器的产物，可切图层、点格检视。
class WorldView : public QWidget
{
    Q_OBJECT

public:
    /// 世界按值收下：视图要比世界活得久，因此不能只存一个引用。
    explicit WorldView(gamev2::world::World world, QWidget* parent = nullptr);

    void set_layer(MapLayer layer);
    [[nodiscard]] MapLayer layer() const noexcept { return layer_; }

    /// 某一格的详情文字。
    [[nodiscard]] QString cell_detail(int x, int y) const;

signals:
    /// 玩家点了一格。
    void cell_clicked(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// 按当前图层重画一遍底图。
    void rebuild();

    /// 底图在控件里的落位。绘制与点击都靠它换算，两者必须一致。
    [[nodiscard]] QRectF image_rect() const;

    gamev2::world::World world_;
    MapLayer layer_ = MapLayer::Biome;
    QImage image_;
};

/// 种群曲线：种群数量随季节的变化。
class PopulationChart : public QWidget
{
    Q_OBJECT

public:
    explicit PopulationChart(QWidget* parent = nullptr);

    void push(std::uint32_t season, std::size_t population);
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<std::pair<std::uint32_t, std::size_t>> points_;
};

}  // namespace gamev2::gui

/// @file views.hpp
/// @brief 三个视图部件：生态活体图、世界地图、种群曲线。
#pragma once

#include "simulation.hpp"
#include "gamev2/world/world.hpp"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QWidget>

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
/// 底色是承载力，越绿越肥；个体画成小点，玩家画成带环的亮点。绘制只读
/// refresh() 取好的快照，不在 paintEvent 里遍历实体。
class EcologyView : public QWidget
{
    Q_OBJECT

public:
    explicit EcologyView(Simulation& sim, QWidget* parent = nullptr);

    /// 从模拟里取一次快照。
    void refresh();

    /// 玩家这一季打算往哪走。画成玩家身旁的箭头——移动要到季末才结算，
    /// 没有这个箭头，按键会像是没反应。
    void set_intent(int dx, int dy);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Simulation& sim_;
    std::vector<QPointF> creatures_;
    QPointF player_pos_;
    bool has_player_ = false;
    int intent_dx_ = 0;
    int intent_dy_ = 0;
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

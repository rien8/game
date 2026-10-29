#include "views.hpp"

#include "gamev2/environment/climate.hpp"
#include "gamev2/world/enums.hpp"
#include "gamev2/world/grid.hpp"
#include "gamev2/world/tables.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gamev2::gui {

namespace {

using gamev2::ecs::EntityId;
using gamev2::ecs::adapt::Position;

/// 群系配色。与 worldgen_demo 导出的 biome.ppm 同一张表——两张图放在一起
/// 看才认得出是同一个世界。
constexpr std::array<std::array<std::uint8_t, 3>, gamev2::world::kBiomeCount>
    kBiomeColor{{
        {20, 40, 120}, {30, 90, 160}, {130, 130, 130}, {90, 110, 70},
        {70, 100, 60}, {40, 80, 60}, {30, 100, 40}, {20, 120, 50},
        {150, 130, 60}, {20, 140, 40}, {120, 170, 70}, {150, 190, 90},
        {190, 180, 90}, {160, 150, 90}, {60, 100, 60}, {220, 200, 130},
        {150, 130, 110}, {170, 110, 90}, {200, 210, 210}, {235, 240, 245},
    }};

constexpr std::array<const char*, gamev2::world::kBiomeCount> kBiomeNames{{
    "海洋", "湖泊", "山地", "沼泽湿地", "湿原",
    "温带针叶林", "温带阔叶林", "热带针叶林",
    "热带干旱阔叶林", "热带湿润阔叶林", "草原",
    "草甸", "稀树草原", "灌丛", "松林荒原", "沙漠",
    "岩漠", "恶地", "苔原", "冰川",
}};

constexpr std::array<std::array<std::uint8_t, 3>, gamev2::world::kLandTypeCount>
    kLandTypeColor{{
        {240, 220, 120},  // blessed 福地
        {120, 180, 90},   // miasma 瘴域
        {90, 70, 120},    // yin 阴地
        {30, 30, 30},     // dead 死地
        {110, 150, 200},  // turbid 浊域
        {205, 205, 205},  // mundane 凡地
    }};

constexpr std::array<const char*, gamev2::world::kLandTypeCount>
    kLandTypeNames{{"福地", "瘴域", "阴地", "死地", "浊域", "凡地"}};

/// 深蓝到白的五段渐变，中间过渡蓝、绿、黄三色。够看出趋势即可。
[[nodiscard]] QRgb ramp(const int value)
{
    constexpr std::array<std::array<int, 3>, 5> kRamp{{
        {0, 0, 40}, {0, 90, 160}, {40, 150, 60}, {210, 200, 60}, {255, 255, 255},
    }};
    const double t = std::clamp(value, 0, 255) / 255.0 *
                     static_cast<double>(kRamp.size() - 1);
    const auto lo = static_cast<std::size_t>(t);
    const auto hi = std::min(lo + 1, kRamp.size() - 1);
    const double f = t - static_cast<double>(lo);

    std::array<int, 3> rgb{};
    for (std::size_t c = 0; c < 3; ++c)
    {
        rgb[c] = static_cast<int>(kRamp[lo][c] * (1.0 - f) +
                                   kRamp[hi][c] * f);
    }
    return qRgb(rgb[0], rgb[1], rgb[2]);
}

/// 标量层按本图最大值归一化：elevation 的上限是 100 而别的层能到 255，
/// 固定刻度会让其中一张全黑。
[[nodiscard]] std::uint8_t grid_peak(const gamev2::world::Grid<std::uint8_t>& grid)
{
    std::uint32_t peak = 1;
    for (std::uint32_t i = 0; i < grid.size(); ++i)
    {
        peak = std::max(peak, static_cast<std::uint32_t>(grid.at(i)));
    }
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(peak, 255));
}

/// 承载力的配色。贫瘠的土 → 中等的草 → 肥沃的亮绿。
[[nodiscard]] QColor capacity_color(const double t)
{
    constexpr std::array<std::array<int, 3>, 3> kStops{{
        {44, 36, 28}, {92, 104, 52}, {104, 178, 74},
    }};
    const auto segment = t < 0.5 ? 0u : 1u;
    const double f = segment == 0 ? t * 2.0 : (t - 0.5) * 2.0;
    std::array<int, 3> rgb{};
    for (std::size_t c = 0; c < 3; ++c)
    {
        rgb[c] = static_cast<int>(kStops[segment][c] * (1.0 - f) +
                                   kStops[segment + 1][c] * f);
    }
    return QColor(rgb[0], rgb[1], rgb[2]);
}

}  // namespace

QString layer_name(const MapLayer layer)
{
    switch (layer)
    {
    case MapLayer::Biome:
        return QStringLiteral("群系");
    case MapLayer::Elevation:
        return QStringLiteral("高程");
    case MapLayer::Rainfall:
        return QStringLiteral("降水");
    case MapLayer::Temperature:
        return QStringLiteral("温度");
    case MapLayer::Ley:
        return QStringLiteral("地脉强度");
    case MapLayer::LandType:
        return QStringLiteral("地貌");
    case MapLayer::CaveHeaven:
        return QStringLiteral("洞天");
    case MapLayer::Tribulation:
        return QStringLiteral("渡劫场");
    case MapLayer::Stone:
        return QStringLiteral("地层岩性");
    case MapLayer::Deposit:
        return QStringLiteral("矿脉");
    }
    return QStringLiteral("群系");
}

QString biome_name(const gamev2::world::Biome biome)
{
    const auto index = static_cast<std::size_t>(biome);
    if (index >= kBiomeNames.size())
    {
        return QStringLiteral("未知");
    }
    return QString::fromUtf8(kBiomeNames[index]);
}

QString land_type_name(const gamev2::world::LandType type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= kLandTypeNames.size())
    {
        return QStringLiteral("未知");
    }
    return QString::fromUtf8(kLandTypeNames[index]);
}

EcologyView::EcologyView(Simulation& sim, QWidget* parent)
    : QWidget(parent), sim_{sim}
{
    setMinimumSize(360, 360);
    refresh();
}

void EcologyView::refresh()
{
    creatures_.clear();
    auto& entities = sim_.entities();
    entities.for_each_entity(
        [&](const EntityId e)
        {
            const auto* position = entities.get_if<Position>(e);
            if (position == nullptr)
            {
                return;
            }
            creatures_.emplace_back(position->x, position->y);
        });

    const auto view = sim_.player_view();
    has_player_ = view.id != EntityId{};
    player_pos_ = QPointF(view.x, view.y);
    update();
}

void EcologyView::set_intent(const int dx, const int dy)
{
    intent_dx_ = dx;
    intent_dy_ = dy;
    update();
}

void EcologyView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const auto side = static_cast<double>(std::min(width(), height()));
    const double cell = side / static_cast<double>(gamev2::environment::kGridSide);
    const double ox = (width() - side) / 2.0;
    const double oy = (height() - side) / 2.0;

    // 底色：承载力。承载力是这局的地形——哪里肥沃，哪里是死地。
    const auto& resources = sim_.environment().resources();
    p.setPen(Qt::NoPen);
    for (std::size_t c = 0; c < gamev2::environment::kCellCount; ++c)
    {
        const auto index =
            gamev2::environment::CellIndex{static_cast<std::uint16_t>(c)};
        const double t = std::clamp(
            static_cast<double>(resources.capacity(index)) / 3.9, 0.0, 1.0);
        const auto x = static_cast<int>(
            ox + static_cast<double>(c % gamev2::environment::kGridSide) * cell);
        const auto y = static_cast<int>(
            oy + static_cast<double>(c / gamev2::environment::kGridSide) * cell);
        p.setBrush(capacity_color(t));
        p.drawRect(x, y, static_cast<int>(cell) + 1, static_cast<int>(cell) + 1);
    }

    // 个体：一次 drawPoints 画完。逐个 drawEllipse 在一万个个体上会卡住。
    // drawPoints 走的是笔而不是刷——笔要是 NoPen，一个点都画不出来。
    p.setPen(QPen(QColor(235, 235, 235, 210), 1.0));
    p.setBrush(Qt::NoBrush);
    QPolygonF dots;
    for (const auto& creature : creatures_)
    {
        dots << QPointF(ox + creature.x() * cell, oy + creature.y() * cell);
    }
    p.drawPoints(dots);

    // 玩家：带白环的亮点，盖在同格的其他个体上面。
    if (has_player_)
    {
        const double px = ox + player_pos_.x() * cell;
        const double py = oy + player_pos_.y() * cell;
        p.setPen(QPen(QColor(255, 255, 255), 1.5));
        p.setBrush(QColor(255, 208, 64));
        p.drawEllipse(QPointF(px, py), cell * 0.6, cell * 0.6);

        // 意图箭头。移动要到季末才结算，没有它按键像是没反应。
        if (intent_dx_ != 0 || intent_dy_ != 0)
        {
            const QPointF tip(px + intent_dx_ * cell * 1.4,
                             py + intent_dy_ * cell * 1.4);
            p.setPen(QPen(QColor(255, 238, 150), 2.0));
            p.setBrush(QColor(255, 238, 150));
            p.drawLine(QPointF(px, py), tip);
            p.drawEllipse(tip, cell * 0.22, cell * 0.22);
        }
    }
}

WorldView::WorldView(gamev2::world::World world, QWidget* parent)
    : QWidget(parent), world_{std::move(world)}
{
    setMinimumSize(360, 360);
    rebuild();
}

void WorldView::set_layer(const MapLayer layer)
{
    if (layer == layer_)
    {
        return;
    }
    layer_ = layer;
    rebuild();
}

void WorldView::rebuild()
{
    const auto width = static_cast<int>(world_.params().width);
    const auto height = static_cast<int>(world_.params().height);
    image_ = QImage(width, height, QImage::Format_RGB32);

    const auto elevation_peak = grid_peak(world_.elevation);
    const auto rainfall_peak = grid_peak(world_.rainfall);
    const auto temperature_peak = grid_peak(world_.temperature);
    // 岩性与矿脉是枚举，取值 0..11 与 0..12，直接喂 ramp 会全落在最低一段
    // 看不出差别，因此放大到整个渐变范围。
    constexpr int kEnumScale = 20;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto i = world_.elevation.index_of(
                static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            QRgb color = qRgb(20, 20, 24);
            switch (layer_)
            {
            case MapLayer::Biome:
            {
                const auto& c =
                    kBiomeColor[static_cast<std::size_t>(world_.biome.at(i))];
                color = qRgb(c[0], c[1], c[2]);
                break;
            }
            case MapLayer::Elevation:
                color = ramp(static_cast<int>(world_.elevation.at(i)) * 255 /
                             elevation_peak);
                break;
            case MapLayer::Rainfall:
                color = ramp(static_cast<int>(world_.rainfall.at(i)) * 255 /
                             rainfall_peak);
                break;
            case MapLayer::Temperature:
                color = ramp(static_cast<int>(world_.temperature.at(i)) * 255 /
                             temperature_peak);
                break;
            case MapLayer::Ley:
                // 灵气层本身就是 0..255 的字节，浓度分档（8/24/60/120）有意义，
                // 按最大值归一化会把潮位差与阈值关系一起抹掉。
                color = ramp(world_.ley_strength.at(i));
                break;
            case MapLayer::LandType:
            {
                const auto& c = kLandTypeColor[static_cast<std::size_t>(
                    world_.land_type.at(i))];
                color = qRgb(c[0], c[1], c[2]);
                break;
            }
            case MapLayer::CaveHeaven:
                color = world_.cave_heaven.at(i) ? qRgb(240, 210, 110)
                                                 : qRgb(28, 28, 34);
                break;
            case MapLayer::Tribulation:
                color = world_.tribulation_site.at(i) ? qRgb(230, 70, 45)
                                                       : qRgb(45, 45, 55);
                break;
            case MapLayer::Stone:
                if (world_.stone.size() > 1)
                {
                    color = ramp(static_cast<int>(world_.stone[1].at(i)) *
                                 kEnumScale);
                }
                break;
            case MapLayer::Deposit:
                if (world_.deposit.size() > 1)
                {
                    color = ramp(static_cast<int>(world_.deposit[1].at(i)) *
                                 kEnumScale);
                }
                break;
            }
            image_.setPixel(x, y, color);
        }
    }
    update();
}

QRectF WorldView::image_rect() const
{
    if (image_.isNull())
    {
        return {};
    }
    const double scale =
        std::min(static_cast<double>(width()) / image_.width(),
                 static_cast<double>(height()) / image_.height());
    const double w = image_.width() * scale;
    const double h = image_.height() * scale;
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

void WorldView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 22));
    if (image_.isNull())
    {
        return;
    }
    const auto area = image_rect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(area, image_);
    p.setPen(QPen(QColor(90, 90, 104), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area);
}

void WorldView::mousePressEvent(QMouseEvent* event)
{
    const auto area = image_rect();
    const auto pos = event->position();
    if (area.isEmpty() || !area.contains(pos))
    {
        return;
    }
    const auto x = static_cast<int>((pos.x() - area.left()) * image_.width() /
                                    area.width());
    const auto y = static_cast<int>((pos.y() - area.top()) * image_.height() /
                                    area.height());
    if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height())
    {
        return;
    }
    emit cell_clicked(x, y);
}

QString WorldView::cell_detail(const int x, const int y) const
{
    if (x < 0 || y < 0 || x >= static_cast<int>(world_.params().width) ||
        y >= static_cast<int>(world_.params().height))
    {
        return QStringLiteral("越界。");
    }
    const auto i = world_.elevation.index_of(
        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return QStringLiteral("(%1, %2)\n群系：%3\n地貌：%4\n"
                          "高程 %5　降水 %6　温度 %7\n地脉强度 %8")
        .arg(x)
        .arg(y)
        .arg(biome_name(world_.biome.at(i)))
        .arg(land_type_name(world_.land_type.at(i)))
        .arg(world_.elevation.at(i))
        .arg(world_.rainfall.at(i))
        .arg(world_.temperature.at(i))
        .arg(world_.ley_strength.at(i));
}

PopulationChart::PopulationChart(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(130);
}

void PopulationChart::push(const std::uint32_t season,
                           const std::size_t population)
{
    points_.emplace_back(season, population);
    update();
}

void PopulationChart::reset()
{
    points_.clear();
    update();
}

void PopulationChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 26, 30));
    if (points_.size() < 2)
    {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("还没有数据"));
        return;
    }

    std::size_t peak = 1;
    std::size_t valley = points_.front().second;
    for (const auto& point : points_)
    {
        peak = std::max(peak, point.second);
        valley = std::min(valley, point.second);
    }

    const double left = 8.0;
    const double right = width() - 8.0;
    const double top = 10.0;
    const double bottom = height() - 20.0;

    QPolygonF line;
    for (std::size_t i = 0; i < points_.size(); ++i)
    {
        const double x =
            left + (right - left) * static_cast<double>(i) /
                       static_cast<double>(points_.size() - 1);
        const double y = bottom - (bottom - top) *
                                   static_cast<double>(points_[i].second) /
                                   static_cast<double>(peak);
        line << QPointF(x, y);
    }
    p.setPen(QPen(QColor(120, 200, 130), 1.6));
    p.drawPolyline(line);

    p.setPen(QColor(205, 205, 205));
    const QString caption =
        QStringLiteral("峰值 %1  谷值 %2  当前 %3  共 %4 季")
            .arg(peak)
            .arg(valley)
            .arg(points_.back().second)
            .arg(points_.size());
    p.drawText(QRectF(left, bottom + 2.0, right - left, 16.0),
               Qt::AlignLeft | Qt::AlignVCenter, caption);
}

}  // namespace gamev2::gui

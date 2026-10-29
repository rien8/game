/// @file main.cpp
/// @brief 演示程序入口：Qt 界面把两个世界摆在同一扇窗里。
///
/// 左页是生态活体图——64×64 的格网上跑着一整季生态，方向键接管其中一个
/// 个体，死了可以重选；右页是世界地图，世界生成器的产物。
///
/// 界面只做两件事：把模拟层的数据画出来，把玩家的按键变成一次移动。其余
/// 全部由 ecology 模块自己算——demo_gui 与 sim 跑的是同一份场景配置
/// （gamev2/demo/scenario.hpp），同一个种子下两边的数字必须对得上。
#include "simulation.hpp"
#include "views.hpp"

#include "gamev2/world/generate.hpp"
#include "gamev2/world/params.hpp"
#include "gamev2/world/world.hpp"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstdint>
#include <exception>
#include <utility>

namespace {

constexpr std::uint64_t kDefaultSeed = 20261016;

/// 界面的默认预设。构造 Simulation 与填下拉框都用它——两边各写一份默认值，
/// 迟早会出现「下拉框写着 persist、跑的却是 evolve」这种事。
constexpr gamev2::demo::Preset kDefaultPreset = gamev2::demo::Preset::Persist;

/// 预设与下拉框下标的对应。两边靠它换算。
[[nodiscard]] constexpr int preset_index(const gamev2::demo::Preset preset) noexcept
{
    switch (preset)
    {
    case gamev2::demo::Preset::Evolve:
        return 0;
    case gamev2::demo::Preset::Persist:
        return 1;
    case gamev2::demo::Preset::Fragile:
        return 2;
    }
    return 0;
}

/// 候选列表最多列这么多个。一万个个体全塞进 QTableWidget 会卡住建表，
/// 而玩家的选择空间本来也用不着那么大。
constexpr std::size_t kMaxCandidates = 300;

}  // namespace

/// 主窗口。不用 Q_OBJECT：所有连接都走 lambda，槽函数一个也不需要。
class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent),
          sim_{kDefaultSeed, kDefaultPreset}
    {
        build_ui();
        connect_signals();
        // 速度下拉在连接信号之前就设好了初值，那次 currentIndexChanged
        // 没人接。定时器得在这里起一次，否则世界是静止的。
        apply_speed();
        refresh_panel();

        // 世界生成要几秒。放在事件循环起来之后再做，窗口先亮出来，
        // 而不是让用户对着一个还没画完的窗等。
        QTimer::singleShot(0, this,
                           [this]
                           {
                               generate_map();
                               // 先走一季：个体的适应力与存活率要过完一季
                               // 才有真实值，开局那张表全是默认值 1.0。
                               sim_.step();
                               eco_view_->refresh();
                               chart_->push(sim_.season(), sim_.population());
                               refresh_panel();
                               pick_creature(
                                   QStringLiteral("选一个个体开始 —— 它替你活下去"));
                           });
    }

private:
    void build_ui()
    {
        setWindowTitle(QStringLiteral("寰宇世界 · 生态活体演示"));
        resize(1320, 840);

        eco_view_ = new gamev2::gui::EcologyView(sim_, this);
        chart_ = new gamev2::gui::PopulationChart(this);

        tabs_ = new QTabWidget(this);
        tabs_->addTab(eco_view_, QStringLiteral("生态活体"));

        map_placeholder_ = new QWidget(tabs_);
        auto* placeholder_layout = new QVBoxLayout(map_placeholder_);
        auto* placeholder_label =
            new QLabel(QStringLiteral("正在生成世界……"), map_placeholder_);
        placeholder_label->setAlignment(Qt::AlignCenter);
        placeholder_layout->addWidget(placeholder_label);
        tabs_->addTab(map_placeholder_, QStringLiteral("世界地图"));

        auto* panel = new QWidget(this);
        auto* panel_layout = new QVBoxLayout(panel);

        auto* world_box = new QGroupBox(QStringLiteral("世界"), panel);
        auto* world_form = new QFormLayout(world_box);
        preset_box_ = new QComboBox(world_box);
        preset_box_->addItem(QStringLiteral("evolve（短命，快换代）"));
        preset_box_->addItem(QStringLiteral("persist（长命，稳生态）"));
        preset_box_->addItem(QStringLiteral("fragile（近交衰退）"));
        preset_box_->setCurrentIndex(preset_index(kDefaultPreset));
        seed_box_ = new QSpinBox(world_box);
        seed_box_->setRange(0, 2147483647);
        seed_box_->setValue(static_cast<int>(kDefaultSeed));
        speed_box_ = new QComboBox(world_box);
        speed_box_->addItem(QStringLiteral("暂停"));
        speed_box_->addItem(QStringLiteral("慢"));
        speed_box_->addItem(QStringLiteral("中"));
        speed_box_->addItem(QStringLiteral("快"));
        speed_box_->setCurrentIndex(1);
        world_form->addRow(QStringLiteral("预设"), preset_box_);
        world_form->addRow(QStringLiteral("种子"), seed_box_);
        world_form->addRow(QStringLiteral("速度"), speed_box_);

        auto* buttons = new QWidget(world_box);
        auto* button_layout = new QHBoxLayout(buttons);
        button_layout->setContentsMargins(0, 0, 0, 0);
        auto* restart_button = new QPushButton(QStringLiteral("重开"), buttons);
        auto* repick_button = new QPushButton(QStringLiteral("换个体"), buttons);
        button_layout->addWidget(restart_button);
        button_layout->addWidget(repick_button);
        world_form->addRow(buttons);
        panel_layout->addWidget(world_box);

        auto* player_box = new QGroupBox(QStringLiteral("玩家"), panel);
        auto* player_layout = new QVBoxLayout(player_box);
        player_label_ = new QLabel(player_box);
        player_label_->setWordWrap(true);
        player_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        player_layout->addWidget(player_label_);
        panel_layout->addWidget(player_box);

        auto* population_box = new QGroupBox(QStringLiteral("种群"), panel);
        auto* population_layout = new QVBoxLayout(population_box);
        population_label_ = new QLabel(population_box);
        population_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        population_layout->addWidget(population_label_);
        chart_ = new gamev2::gui::PopulationChart(population_box);
        population_layout->addWidget(chart_);
        panel_layout->addWidget(population_box);

        auto* cell_box = new QGroupBox(QStringLiteral("地图选格"), panel);
        auto* cell_layout = new QVBoxLayout(cell_box);
        map_layer_box_ = new QComboBox(cell_box);
        for (int index = 0;
             index < static_cast<int>(gamev2::gui::kMapLayerCount); ++index)
        {
            map_layer_box_->addItem(
                gamev2::gui::layer_name(
                    static_cast<gamev2::gui::MapLayer>(index)));
        }
        cell_layout->addWidget(map_layer_box_);
        cell_label_ = new QLabel(QStringLiteral("在右页点一格看它的详情。"), cell_box);
        cell_label_->setWordWrap(true);
        cell_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cell_layout->addWidget(cell_label_);
        panel_layout->addWidget(cell_box);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(tabs_);
        splitter->addWidget(panel);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({900, 380});
        setCentralWidget(splitter);

        statusBar()->showMessage(
            QStringLiteral("方向键或 WASD 让玩家走一步（每季结算时生效）；"
                           "个体死亡后可重选。"));

        connect(restart_button, &QPushButton::clicked, this,
                [this] { restart(); });
        connect(repick_button, &QPushButton::clicked, this,
                [this] { pick_creature(QStringLiteral("换一个个体接管")); });
        connect(speed_box_, &QComboBox::currentIndexChanged, this,
                [this] { apply_speed(); });
        connect(map_layer_box_, &QComboBox::currentIndexChanged, this,
                [this](const int index)
                {
                    map_layer_ = static_cast<gamev2::gui::MapLayer>(index);
                    if (map_view_ != nullptr)
                    {
                        map_view_->set_layer(map_layer_);
                    }
                });
    }

    void connect_signals()
    {
        connect(&timer_, &QTimer::timeout, this, [this] { on_tick(); });

        const auto bind = [this](const QKeySequence& sequence, const int dx,
                                 const int dy)
        {
            auto* shortcut = new QShortcut(sequence, this);
            connect(shortcut, &QShortcut::activated, this,
                    [this, dx, dy] { apply_intent(dx, dy); });
        };
        bind(QKeySequence(Qt::Key_Up), 0, -1);
        bind(QKeySequence(Qt::Key_Down), 0, 1);
        bind(QKeySequence(Qt::Key_Left), -1, 0);
        bind(QKeySequence(Qt::Key_Right), 1, 0);
        bind(QKeySequence(QStringLiteral("W")), 0, -1);
        bind(QKeySequence(QStringLiteral("S")), 0, 1);
        bind(QKeySequence(QStringLiteral("A")), -1, 0);
        bind(QKeySequence(QStringLiteral("D")), 1, 0);
    }

    /// 重新生成世界地图。地图与生态用同一个种子，两边才对得上。
    void generate_map()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        gamev2::world::WorldParams params;
        params.seed = current_seed();
        auto generated = gamev2::world::generate_world(params);
        QApplication::restoreOverrideCursor();

        if (!generated)
        {
            QMessageBox::warning(this, QStringLiteral("生成失败"),
                                 QStringLiteral("世界参数非法。"));
            return;
        }

        auto* view = new gamev2::gui::WorldView(std::move(*generated), this);
        connect(view, &gamev2::gui::WorldView::cell_clicked, this,
                [this, view](const int x, const int y)
                { cell_label_->setText(view->cell_detail(x, y)); });

        if (map_view_ != nullptr)
        {
            tabs_->removeTab(tabs_->indexOf(map_view_));
            delete map_view_;
        }
        else if (map_placeholder_ != nullptr)
        {
            tabs_->removeTab(tabs_->indexOf(map_placeholder_));
            delete map_placeholder_;
            map_placeholder_ = nullptr;
        }
        map_view_ = view;
        map_view_->set_layer(map_layer_);
        tabs_->insertTab(1, map_view_, QStringLiteral("世界地图"));
    }

    void on_tick()
    {
        sim_.step();
        eco_view_->refresh();
        chart_->push(sim_.season(), sim_.population());
        refresh_panel();

        if (sim_.player() && !sim_.player_alive())
        {
            // 先把控制权收回来，再问要不要换人。否则每走一季都会弹一次窗。
            sim_.release_control();
            eco_view_->set_intent(0, 0);
            pick_creature(QStringLiteral("你的个体死了 —— 选一个新的接管"));
        }
    }

    void apply_speed()
    {
        const auto index = speed_box_->currentIndex();
        if (index <= 0)
        {
            timer_.stop();
            return;
        }
        // 一季 800 毫秒不是随便定的：evolve 预设里个体寿命只有 8 季，
        // 再快的话玩家一辈子只剩两三秒。
        static constexpr int kIntervals[] = {0, 800, 400, 150};
        timer_.setInterval(kIntervals[index]);
        timer_.start();
    }

    void apply_intent(const int dx, const int dy)
    {
        // 焦点在会吃掉方向键的控件里时，方向键归那个控件。少了这一条，
        // 在下拉框上按方向键会既改下拉框又让玩家走一步。
        auto* focus = QApplication::focusWidget();
        if (focus != nullptr &&
            (qobject_cast<QComboBox*>(focus) != nullptr ||
             qobject_cast<QAbstractSpinBox*>(focus) != nullptr ||
             qobject_cast<QLineEdit*>(focus) != nullptr ||
             qobject_cast<QAbstractItemView*>(focus) != nullptr))
        {
            return;
        }
        if (!sim_.player() || !sim_.player_alive())
        {
            return;
        }
        sim_.nudge(dx, dy);
        eco_view_->set_intent(dx, dy);
    }

    /// 选一个个体接管。返回是否真的选定了。
    bool pick_creature(const QString& title)
    {
        const auto candidates = sim_.candidates(kMaxCandidates);
        if (candidates.empty())
        {
            QMessageBox::warning(this, title,
                                 QStringLiteral("没有活过一季的个体可选。"));
            return false;
        }

        // 选人的时候世界得停下来：模态框有自己的事件循环，定时器照样会在
        // 里面响——世界在玩家犹豫的时候继续跑，列表里的数字就全过期了。
        const bool was_running = timer_.isActive();
        timer_.stop();

        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.resize(820, 460);
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(
            QStringLiteral("双击一行接管那个个体。适应力与存活率来自它刚过去的"
                           "那个季节；年龄越大说明它已经扛过几轮生死了。"),
            &dialog));

        auto* table = new QTableWidget(static_cast<int>(candidates.size()), 10,
                                       &dialog);
        table->setHorizontalHeaderLabels(
            {QStringLiteral("年龄"), QStringLiteral("剩余寿命"),
             QStringLiteral("适应力"), QStringLiteral("存活率"),
             QStringLiteral("x"), QStringLiteral("y"),
             QStringLiteral("体型"), QStringLiteral("速度"),
             QStringLiteral("寿命"), QStringLiteral("韧性")});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setStretchLastSection(true);
        for (std::size_t row = 0; row < candidates.size(); ++row)
        {
            const auto& creature = candidates[row];
            const float remaining =
                std::max(0.0f, creature.lifespan - static_cast<float>(creature.age));
            const std::array<QString, 10> values{
                QString::number(creature.age),
                QString::number(remaining, 'f', 1),
                QString::number(creature.fitness, 'f', 3),
                QString::number(creature.survival_rate, 'f', 3),
                QString::number(creature.x, 'f', 1),
                QString::number(creature.y, 'f', 1),
                QString::number(creature.body_size, 'f', 2),
                QString::number(creature.speed, 'f', 2),
                QString::number(creature.lifespan, 'f', 1),
                QString::number(creature.resilience, 'f', 2)};
            for (int column = 0; column < table->columnCount(); ++column)
            {
                auto* item = new QTableWidgetItem(values[column]);
                // 实体句柄挂在数据位上：排序之后行的位置会变，靠下标回取就
                // 取错人了。
                item->setData(Qt::UserRole,
                              static_cast<qulonglong>(creature.id.value));
                table->setItem(static_cast<int>(row), column, item);
            }
        }
        // 填完再开排序：开着排序插三千个单元格，每插一个都要重排一次。
        table->setSortingEnabled(true);
        layout->addWidget(table);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("接管"));
        buttons->button(QDialogButtonBox::Cancel)
            ->setText(QStringLiteral("再看看"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(table, &QTableWidget::cellDoubleClicked, &dialog,
                [&dialog](int, int) { dialog.accept(); });

        const bool accepted = dialog.exec() == QDialog::Accepted;
        const auto row = table->currentRow();
        bool chosen = false;
        if (accepted && row >= 0)
        {
            const auto* item = table->item(row, 0);
            if (item != nullptr)
            {
                const auto raw = item->data(Qt::UserRole).toULongLong();
                sim_.take_control(
                    gamev2::ecs::EntityId{static_cast<std::uint64_t>(raw)});
                eco_view_->refresh();
                refresh_panel();
                chosen = true;
            }
        }
        if (was_running)
        {
            timer_.start();
        }
        return chosen;
    }

    void restart()
    {
        timer_.stop();
        sim_.reset(current_seed(), current_preset());
        chart_->reset();
        eco_view_->refresh();
        eco_view_->set_intent(0, 0);
        refresh_panel();
        generate_map();
        pick_creature(QStringLiteral("重开了 —— 选一个个体"));
        apply_speed();
    }

    void refresh_panel()
    {
        population_label_->setText(
            QStringLiteral("第 %1 季　种群 %2　杂合度 %3　近交系数 %4")
                .arg(sim_.season())
                .arg(sim_.population())
                .arg(sim_.heterozygosity(), 0, 'f', 4)
                .arg(sim_.inbreeding(), 0, 'f', 4));
        update_player_panel();
    }

    void update_player_panel()
    {
        if (!sim_.player() || !sim_.player_alive())
        {
            player_label_->setText(
                QStringLiteral("没有人接管。点「换个体」挑一个。"));
            return;
        }
        const auto view = sim_.player_view();
        player_label_->setText(
            QStringLiteral("个体 %1　年龄 %2 季\n坐标 (%3, %4)\n"
                           "适应力 %5　本季存活率 %6\n体型 %7　速度 %8　"
                           "寿命 %9　韧性 %10")
                .arg(view.id.index())
                .arg(view.age)
                .arg(view.x, 0, 'f', 1)
                .arg(view.y, 0, 'f', 1)
                .arg(view.fitness, 0, 'f', 3)
                .arg(view.survival_rate, 0, 'f', 3)
                .arg(view.body_size, 0, 'f', 2)
                .arg(view.speed, 0, 'f', 2)
                .arg(view.lifespan, 0, 'f', 1)
                .arg(view.resilience, 0, 'f', 2));
    }

    [[nodiscard]] std::uint64_t current_seed() const
    {
        return static_cast<std::uint64_t>(seed_box_->value());
    }

    [[nodiscard]] gamev2::demo::Preset current_preset() const
    {
        switch (preset_box_->currentIndex())
        {
        case 0:
            return gamev2::demo::Preset::Evolve;
        case 1:
            return gamev2::demo::Preset::Persist;
        case 2:
            return gamev2::demo::Preset::Fragile;
        default:
            return kDefaultPreset;
        }
    }

    gamev2::gui::Simulation sim_;
    gamev2::gui::EcologyView* eco_view_ = nullptr;
    gamev2::gui::WorldView* map_view_ = nullptr;
    gamev2::gui::PopulationChart* chart_ = nullptr;
    gamev2::gui::MapLayer map_layer_ = gamev2::gui::MapLayer::Biome;
    QTabWidget* tabs_ = nullptr;
    QWidget* map_placeholder_ = nullptr;
    QLabel* population_label_ = nullptr;
    QLabel* player_label_ = nullptr;
    QLabel* cell_label_ = nullptr;
    QComboBox* preset_box_ = nullptr;
    QSpinBox* seed_box_ = nullptr;
    QComboBox* speed_box_ = nullptr;
    QComboBox* map_layer_box_ = nullptr;
    QTimer timer_;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        MainWindow window;
        window.show();
        return app.exec();
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                              QString::fromUtf8(error.what()));
        return 1;
    }
}

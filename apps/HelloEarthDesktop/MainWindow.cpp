#include "EarthViewWidget.h"
#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTreeWidget>
#include <QWidget>
#include <QString>
#include <QTreeWidgetItem>
#include <QVariant>

namespace
{
    // QTreeWidgetItem 除了显示文字，还可以保存程序内部数据。
    //
    // Qt::UserRole 是 Qt 专门预留给应用程序使用的数据位置。
    // 我们从这里开始，分别保存节点类型、Map UID 和 Layer UID。
    constexpr int ItemTypeRole =
        Qt::UserRole;

    constexpr int MapUidRole =
        Qt::UserRole + 1;

    constexpr int LayerUidRole =
        Qt::UserRole + 2;

    // 表示 Layers Dock 中一个树节点的业务类型。
    //
    // 界面显示文字将来可能修改或翻译，
    // 因此程序不应该依赖 "Imagery Layers" 等文字判断节点用途，
    // 而应该读取节点内部保存的类型。
    enum class LayerTreeItemType : int
    {
        // 代表一个真实 osgEarth Map。
        Map,

        // 影像图层分类节点，本身不是真实 osgEarth Layer。
        ImageryGroup,

        // 高程图层分类节点，本身不是真实 osgEarth Layer。
        ElevationGroup,

        // 对应一个真实 osgEarth ImageLayer。
        ImageryLayer,

        // 对应一个真实 osgEarth ElevationLayer。
        ElevationLayer
    };
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent){
    resize(1200, 800);
    setWindowTitle("HelloEarth Desktop");

    // 创建菜单栏。
    auto* fileMenu = menuBar()->addMenu("File");

    auto* openImageryAction =
        fileMenu->addAction("Open Imagery...");

    auto* openElevationAction =
        fileMenu->addAction("Open DEM...");

    fileMenu->addSeparator();

    auto* exitAction =
        fileMenu->addAction("Exit");

    connect(
        exitAction,
        &QAction::triggered,
        this,
        &QWidget::close
    );

    // 创建中央 OpenGL 三维显示控件。
    //
    // EarthViewWidget 本身属于 QWidget 体系，
    // 因此可以直接安装为 MainWindow 的中央控件。
    earthViewWidget_ = new EarthViewWidget(this);

    // 允许用户点击三维区域后，将键盘焦点交给它。
    // 后续键盘和快捷键事件才能传入三维视图。
    earthViewWidget_->setFocusPolicy(
        Qt::StrongFocus
    );

    // MainWindow 接管 EarthViewWidget 的界面布局和生命周期。
    setCentralWidget(earthViewWidget_);

    // 创建左侧图层面板。
    auto* layerDock = new QDockWidget("Layers", this);
    layerDock->setObjectName("LayerDock");

    layerTree_ = new QTreeWidget(layerDock);
    layerTree_->setHeaderLabel("Layers");

    // 当 EarthViewWidget 成功创建 osgEarth Map 后，
    // 在 Layers Dock 中创建与该真实 Map 对应的树结构。
    connect(
        earthViewWidget_,
        &EarthViewWidget::mapCreated,
        this,
        [this](
            int mapUid,
            const QString& mapDisplayName
        )
        {
            // 根据 EarthViewWidget 发送的真实 Map UID 和显示名称，
            // 创建 Map、Imagery Layers 和 Elevation Layers 三层结构。
            defaultMapTreeItems_ =
                createMapTreeItems(
                    mapUid,
                    mapDisplayName
                );
        }
    );

    // 当 EarthViewWidget 成功把影像加入 osgEarth Map 后，
    // 在对应 Map 的 Imagery Layers 分类下创建界面叶子节点。
    connect(
        earthViewWidget_,
        &EarthViewWidget::imageryLayerAdded,
        this,
        [this](
            int mapUid,
            int layerUid,
            const QString& layerDisplayName
        )
        {
            addImageryLayerTreeItem(
                mapUid,
                layerUid,
                layerDisplayName
            );
        }
    );

    // 当图层树中某个节点的数据发生变化时，
    // 判断它是否为真实图层叶子，并同步其显隐状态到 osgEarth。
    connect(
        layerTree_,
        &QTreeWidget::itemChanged,
        this,
        [this](
            QTreeWidgetItem* item,
            int column
        )
        {
            // 防止收到空节点，且当前只处理图层名称所在的第 0 列。
            if (item == nullptr || column != 0)
            {
                return;
            }

            // itemChanged 不只会由复选框触发。
            //
            // 节点刚创建、设置名称或写入隐藏数据时，
            // 也可能触发该信号，因此首先检查节点类型是否已经存在。
            const QVariant itemTypeData =
                item->data(
                    0,
                    ItemTypeRole
                );

            if (!itemTypeData.isValid())
            {
                return;
            }

            const auto itemType =
                static_cast<LayerTreeItemType>(
                    itemTypeData.toInt()
                );

            // 当前只允许真实影像图层和真实高程图层
            // 通过复选框控制显隐。
            //
            // Map 和分类节点的数据变化不应操作 osgEarth Layer。
            if (
                itemType != LayerTreeItemType::ImageryLayer &&
                itemType != LayerTreeItemType::ElevationLayer
            )
            {
                return;
            }

            // 读取该叶子节点内部保存的 Map UID、Layer UID
            // 以及 Qt 复选框状态。
            const QVariant mapUidData =
                item->data(
                    0,
                    MapUidRole
                );

            const QVariant layerUidData =
                item->data(
                    0,
                    LayerUidRole
                );

            const QVariant checkStateData =
                item->data(
                    0,
                    Qt::CheckStateRole
                );

            // 创建叶子节点时，各项数据是逐步写入的。
            //
            // 只有身份信息和复选状态都写入完成后，
            // 才允许调用真实 osgEarth 图层接口。
            if (
                !mapUidData.isValid() ||
                !layerUidData.isValid() ||
                !checkStateData.isValid()
            )
            {
                return;
            }

            const int mapUid =
                mapUidData.toInt();

            const int layerUid =
                layerUidData.toInt();

            // 已勾选表示显示图层；
            // 未勾选表示隐藏图层。
            const bool visible =
                item->checkState(0) ==
                Qt::Checked;

            // 将 Qt 图层树中的显隐状态，
            // 同步给对应的真实 osgEarth Layer。
            earthViewWidget_->setLayerVisible(
                mapUid,
                layerUid,
                visible
            );
        }
    );

    layerDock->setWidget(layerTree_);

    // layerDock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        layerDock
    );

    // 创建状态栏。
    statusBar()->showMessage("Ready");
}

MainWindow::MapTreeItems
MainWindow::createMapTreeItems(
    int mapUid,
    const QString& mapDisplayName
)
{
    // 创建一个临时结构，用于收集本次创建的三个树节点。
    MapTreeItems items;

    // 保存这个树结构对应的真实 osgEarth Map UID。
    items.mapUid = mapUid;

    // 创建 Map 顶层节点。
    //
    // 以 layerTree_ 为父对象，表示该节点直接显示在
    // Layers Dock 图层树的最外层。
    items.mapItem =
        new QTreeWidgetItem(layerTree_);

    items.mapItem->setText(
        0,
        mapDisplayName
    );

    // 记录该节点属于 Map 类型。
    //
    // enum class 不能直接当作普通整数使用，
    // 因此通过 static_cast<int>() 转换后存入 QVariant。
    items.mapItem->setData(
        0,
        ItemTypeRole,
        static_cast<int>(
            LayerTreeItemType::Map
        )
    );

    // 记录该节点对应的真实 osgEarth Map UID。
    items.mapItem->setData(
        0,
        MapUidRole,
        mapUid
    );

    // 创建影像图层分类节点。
    //
    // 以 mapItem 为父节点，因此它会显示在 Map 节点下面。
    items.imageryGroupItem =
        new QTreeWidgetItem(items.mapItem);

    items.imageryGroupItem->setText(
        0,
        QStringLiteral("Imagery Layers")
    );

    // 记录该节点是影像图层分类，而不是真实影像图层。
    items.imageryGroupItem->setData(
        0,
        ItemTypeRole,
        static_cast<int>(
            LayerTreeItemType::ImageryGroup
        )
    );

    // 记录该分类属于哪个真实 Map。
    //
    // 未来存在多个 Map 时，程序可以通过这个 UID
    // 判断拖入的影像应当添加到哪个 Map。
    items.imageryGroupItem->setData(
        0,
        MapUidRole,
        mapUid
    );

    // 创建高程图层分类节点。
    items.elevationGroupItem =
        new QTreeWidgetItem(items.mapItem);

    items.elevationGroupItem->setText(
        0,
        QStringLiteral("Elevation Layers")
    );

    // 记录该节点是高程图层分类，而不是真实 DEM 图层。
    items.elevationGroupItem->setData(
        0,
        ItemTypeRole,
        static_cast<int>(
            LayerTreeItemType::ElevationGroup
        )
    );

    // 记录该高程分类属于哪个真实 Map。
    items.elevationGroupItem->setData(
        0,
        MapUidRole,
        mapUid
    );

    // 默认展开 Map 及两个分类节点。
    items.mapItem->setExpanded(true);
    items.imageryGroupItem->setExpanded(true);
    items.elevationGroupItem->setExpanded(true);

    // 把 Map UID 和三个节点指针一起返回给调用者。
    return items;
}

void MainWindow::addImageryLayerTreeItem(
    int mapUid,
    int layerUid,
    const QString& layerDisplayName
)
{
    // 当前版本只有一个默认 Map。
    //
    // 如果信号中的 Map UID 与当前图层树记录的 UID 不一致，
    // 说明这个影像不属于当前 Map，因此暂时不创建节点。
    //
    // 未来支持多 Map 时，这里会改成根据 mapUid
    // 从多个 MapTreeItems 中查找正确的那一组。
    if (defaultMapTreeItems_.mapUid != mapUid)
    {
        return;
    }

    // 在当前 Map 的 Imagery Layers 分类下创建叶子节点。
    //
    // imageryGroupItem 被指定为父节点，
    // 因此新节点会自动出现在影像分类下面。
    auto* imageryLayerItem =
        new QTreeWidgetItem(
            defaultMapTreeItems_.imageryGroupItem
        );

    // 设置用户在 Layers Dock 中看到的图层名称。
    imageryLayerItem->setText(
        0,
        layerDisplayName
    );

    // 记录该节点代表一个真实影像图层，
    // 而不是 Map 或影像分类节点。
    imageryLayerItem->setData(
        0,
        ItemTypeRole,
        static_cast<int>(
            LayerTreeItemType::ImageryLayer
        )
    );

    // 保存该影像所属的真实 Map UID。
    imageryLayerItem->setData(
        0,
        MapUidRole,
        mapUid
    );

    // 保存该节点对应的真实 osgEarth Layer UID。
    //
    // 后续显隐、删除和调整顺序时，
    // 可以通过这个 UID 找到 osgEarth 中的实际图层。
    imageryLayerItem->setData(
        0,
        LayerUidRole,
        layerUid
    );

    // 允许用户勾选或取消勾选这个真实影像图层节点。
    //
    // flags() 会取得节点原本已有的能力，例如可选择、可启用；
    // 使用 | 添加 ItemIsUserCheckable，不会覆盖原有能力。
    imageryLayerItem->setFlags(
        imageryLayerItem->flags() |
        Qt::ItemIsUserCheckable
    );

    // 新加载的 osgEarth 图层默认处于显示状态，
    // 因此对应的界面复选框也应当默认勾选。
    imageryLayerItem->setCheckState(
        0,
        Qt::Checked
    );
}
#include "EarthViewWidget.h"
#include "MainWindow.h"
#include "LayerTreeWidget.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QWidget>
#include <QString>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractItemView>

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

    // 当用户点击 File -> Open Imagery... 时，
    // 打开文件选择窗口并将选中的影像交给 EarthViewWidget 加载。
    connect(
        openImageryAction,
        &QAction::triggered,
        this,
        [this]()
        {
            // 打开系统文件选择窗口。
            //
            // 第一个参数 this：
            // 让文件窗口属于当前 MainWindow。
            //
            // 第二个参数：
            // 文件选择窗口的标题。
            //
            // 第三个参数 QString()：
            // 当前暂不指定固定的初始文件夹。
            //
            // 第四个参数：
            // 当前阶段只允许选择 GeoTIFF 文件。
            const QString imagePath =
                QFileDialog::getOpenFileName(
                    this,
                    QStringLiteral("Open Imagery"),
                    QString(),
                    QStringLiteral(
                        "GeoTIFF Images (*.tif *.tiff);;"
                        "All Files (*.*)"
                    )
                );

            // 用户点击“取消”时，QFileDialog 返回空字符串。
            // 这不属于加载失败，只表示用户放弃了本次操作。
            if (imagePath.isEmpty())
            {
                return;
            }

            // 调用统一影像加载入口。
            //
            // addImageLayer() 会完成金字塔预处理、
            // TIFF/VRT 实际路径选择、GDALImageLayer 创建、
            // osgEarth Map 添加以及 Layers Dock 通知。
            earthViewWidget_->addImageLayer(
                imagePath
            );
        }
    );

    // 当用户点击 File -> Open DEM... 时，
    // 打开文件选择窗口，并将选中的高程数据交给 EarthViewWidget 加载。
    connect(
        openElevationAction,
        &QAction::triggered,
        this,
        [this]()
        {
            // 打开系统文件选择窗口。
            //
            // 当前阶段只选择 GeoTIFF 格式的 DEM。
            // 以后如果支持 IMG、VRT 等高程格式，
            // 可以继续扩展这里的文件过滤条件。
            const QString elevationPath =
                QFileDialog::getOpenFileName(
                    this,
                    QStringLiteral("Open DEM"),
                    QString(),
                    QStringLiteral(
                        "GeoTIFF Elevation Data (*.tif *.tiff);;"
                        "All Files (*.*)"
                    )
                );

            // 用户点击“取消”时，返回的是空字符串。
            // 这属于正常取消操作，不需要报告加载失败。
            if (elevationPath.isEmpty())
            {
                return;
            }

            // 调用统一的高程图层加载入口。
            //
            // addElevationLayer() 内部负责：
            // 1. 检查并预处理 DEM；
            // 2. 检查或构建金字塔；
            // 3. 创建 GDALElevationLayer；
            // 4. 将高程图层加入 osgEarth Map；
            // 5. 计算 DEM 对应的初始视点；
            // 6. 发出 elevationLayerAdded 信号。
            earthViewWidget_->addElevationLayer(
                elevationPath
            );
        }
    );

    // 创建左侧图层面板。
    auto* layerDock = new QDockWidget("Layers", this);
    layerDock->setObjectName("LayerDock");

    layerTree_ = new LayerTreeWidget(layerDock);
    layerTree_->setHeaderLabel("Layers");

    // 开启图层树内部的拖放操作。
    //
    // InternalMove 表示当前阶段只允许移动
    // layerTree_ 自己内部已有的节点，
    // 暂时还不处理从 Windows 文件夹拖进来的外部文件。
    layerTree_->setDragDropMode(
        QAbstractItemView::InternalMove
    );

    // 明确允许用户从图层树中拖起节点。
    layerTree_->setDragEnabled(true);

    // 允许图层树接收内部拖动的节点。
    layerTree_->setAcceptDrops(true);

    // 拖动过程中显示插入位置提示线，
    // 让用户知道节点松开后将会放到哪里。
    layerTree_->setDropIndicatorShown(true);

    // 拖放操作的默认行为是移动节点，
    // 而不是复制出一个新的树节点。
    layerTree_->setDefaultDropAction(
        Qt::MoveAction
    );

    // 当前只允许一次选择一个节点，
    // 避免同时拖动多个图层使排序逻辑复杂化。
    layerTree_->setSelectionMode(
        QAbstractItemView::SingleSelection
    );

    // 使用自定义右键菜单。
    //
    // 默认情况下，QTreeWidget 不会自动提供符合我们业务需求的菜单。
    // 设置为 CustomContextMenu 后，用户右键点击图层树时，
    // layerTree_ 会发出 customContextMenuRequested 信号。
    layerTree_->setContextMenuPolicy(
        Qt::CustomContextMenu
    );

    // 当用户在 Layers Dock 中点击鼠标右键时，
    // 根据点击位置判断目标节点，并为真实图层提供删除操作。
    connect(
        layerTree_,
        &QTreeWidget::customContextMenuRequested,
        this,
        [this, openImageryAction, openElevationAction](
            const QPoint& position
        )
        {
            // position 是相对于 layerTree_ 内容区域的局部坐标。
            //
            // itemAt() 根据这个坐标取得用户真正右键点击的树节点，
            // 而不是依赖之前可能选中的 currentItem()。
            QTreeWidgetItem* item =
                layerTree_->itemAt(position);

            // 用户可能点击在树节点之间的空白区域。
            if (item == nullptr)
            {
                return;
            }

            // 读取节点内部保存的业务类型。
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

            // 创建本次右键操作使用的临时菜单。
            QMenu contextMenu(this);

            // 分别记录菜单中可能创建的操作。
            //
            // 某种操作没有出现在当前菜单中时，
            // 对应指针将继续保持 nullptr。
            QAction* addImageryAction = nullptr;
            QAction* addElevationAction = nullptr;
            QAction* removeAction = nullptr;

            // 根据被点击节点的业务类型，
            // 决定当前右键菜单应该提供什么操作。
            if (itemType == LayerTreeItemType::ImageryGroup)
            {
                // 用户右键点击 Imagery Layers 分类节点。
                addImageryAction =
                    contextMenu.addAction(
                        QStringLiteral("Add Imagery Layer...")
                    );
            }
            else if (itemType == LayerTreeItemType::ElevationGroup)
            {
                // 用户右键点击 Elevation Layers 分类节点。
                addElevationAction =
                    contextMenu.addAction(
                        QStringLiteral("Add DEM Layer...")
                    );
            }
            else if (
                itemType == LayerTreeItemType::ImageryLayer ||
                itemType == LayerTreeItemType::ElevationLayer
            )
            {
                // 用户右键点击真实影像或高程图层。
                removeAction =
                    contextMenu.addAction(
                        QStringLiteral("Remove Layer")
                    );
            }
            else
            {
                // 当前不为 Map 节点提供右键操作。
                return;
            }

            // 在鼠标右键点击的位置显示菜单。
            QAction* selectedAction =
                contextMenu.exec(
                    layerTree_->viewport()->mapToGlobal(
                        position
                    )
                );
            
            // 用户点击菜单外部、按下 Esc，或者以其他方式关闭菜单时，
            // exec() 会返回 nullptr。
            //
            // 由于当前菜单中没有创建的 QAction 指针本身也可能是 nullptr，
            // 所以必须在比较各个 QAction 之前单独处理取消操作。
            if (selectedAction == nullptr)
            {
                return;
            }

            // 用户可能直接点击菜单外部关闭菜单。
            // 此时 selectedAction 为 nullptr，下面所有判断都不会成立。

            if (selectedAction == addImageryAction)
            {
                // 复用 File -> Open Imagery... 的 QAction。
                //
                // trigger() 会让该 QAction 发出 triggered 信号，
                // 从而执行我们之前连接好的文件选择和影像加载流程。
                openImageryAction->trigger();
            }
            else if (selectedAction == addElevationAction)
            {
                // 复用 File -> Open DEM... 的完整加载流程。
                openElevationAction->trigger();
            }
            else if (selectedAction == removeAction)
            {
                // 删除 osgEarth 中的真实图层，
                // 并在成功后删除对应的 Qt 树节点。
                removeLayerTreeItem(item);
            }
        }
    );

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

    // 当 EarthViewWidget 成功把 DEM 加入 osgEarth Map 后，
    // elevationLayerAdded 信号会携带 Map UID、Layer UID 和显示名称。
    //
    // MainWindow 收到信号后，在对应 Map 的
    // Elevation Layers 分类下创建一个高程图层树节点。
    connect(
        earthViewWidget_,
        &EarthViewWidget::elevationLayerAdded,
        this,
        [this](
            int mapUid,
            int layerUid,
            const QString& layerDisplayName
        )
        {
            addElevationLayerTreeItem(
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

MainWindow::MapTreeItems MainWindow::createMapTreeItems(int mapUid, const QString& mapDisplayName)
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

    // Map 是图层树的固定结构节点，不允许用户把它拖走。
    items.mapItem->setFlags(
        items.mapItem->flags() &
        ~Qt::ItemIsDragEnabled
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

    // Imagery Layers 是固定分类节点，
    // 可以作为图层的容器，但不允许它自身被拖动。
    items.imageryGroupItem->setFlags(
        items.imageryGroupItem->flags() &
        ~Qt::ItemIsDragEnabled
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

    // Elevation Layers 是固定分类节点，
    // 可以作为图层的容器，但不允许它自身被拖动。
    items.elevationGroupItem->setFlags(
        items.elevationGroupItem->flags() &
        ~Qt::ItemIsDragEnabled
    );

    // 默认展开 Map 及两个分类节点。
    items.mapItem->setExpanded(true);
    items.imageryGroupItem->setExpanded(true);
    items.elevationGroupItem->setExpanded(true);

    // 把 Map UID 和三个节点指针一起返回给调用者。
    return items;
}

void MainWindow::addImageryLayerTreeItem(int mapUid, int layerUid, const QString& layerDisplayName)
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

    // 先创建一个尚未加入图层树的独立叶子节点。
    auto* imageryLayerItem =
        new QTreeWidgetItem();

    // 将新加载的影像插入 Imagery Layers 的第 0 个位置。
    //
    // Qt 的子节点索引从 0 开始，因此第 0 个位置就是界面最上方。
    // 这样 Layers Dock 的显示习惯与 ArcGIS 类似：
    // 后加载的影像位于上方，先加载的全球底图位于下方。
    defaultMapTreeItems_
        .imageryGroupItem
        ->insertChild(
            0,
            imageryLayerItem
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
    // 真实影像图层既可以通过复选框控制显隐，
    // 也可以在图层树中被用户拖动。
    imageryLayerItem->setFlags(
        imageryLayerItem->flags() |
        Qt::ItemIsUserCheckable |
        Qt::ItemIsDragEnabled
    );

    // 新加载的 osgEarth 图层默认处于显示状态，
    // 因此对应的界面复选框也应当默认勾选。
    imageryLayerItem->setCheckState(
        0,
        Qt::Checked
    );
}

void MainWindow::addElevationLayerTreeItem(int mapUid, int layerUid, const QString& layerDisplayName)
{
    // 当前版本只管理一个默认 Map。
    //
    // 如果信号携带的 Map UID 与图层树记录的 Map UID 不一致，
    // 说明这个高程图层不属于当前 Map，因此暂时不创建树节点。
    if (defaultMapTreeItems_.mapUid != mapUid)
    {
        return;
    }

    // 创建一个暂时还没有加入图层树的独立叶子节点。
    auto* elevationLayerItem =
        new QTreeWidgetItem();

    // 将新加载的 DEM 插入 Elevation Layers 分类的最上方。
    //
    // 索引 0 代表第一个子节点，也就是界面中最上方的位置。
    defaultMapTreeItems_
        .elevationGroupItem
        ->insertChild(
            0,
            elevationLayerItem
        );

    // 设置用户在 Layers Dock 中看到的 DEM 文件名。
    elevationLayerItem->setText(
        0,
        layerDisplayName
    );

    // 标记该节点代表一个真实的 osgEarth 高程图层，
    // 而不是 Map 节点或 Elevation Layers 分类节点。
    elevationLayerItem->setData(
        0,
        ItemTypeRole,
        static_cast<int>(
            LayerTreeItemType::ElevationLayer
        )
    );

    // 保存该高程图层所属 Map 的 UID。
    //
    // 以后程序支持多个 Map 时，可以通过这个 UID
    // 确定需要操作的是哪个 Map 中的高程图层。
    elevationLayerItem->setData(
        0,
        MapUidRole,
        mapUid
    );

    // 保存真实 osgEarth ElevationLayer 的 UID。
    //
    // 后续控制显示、隐藏、删除等操作时，
    // 可以通过这个 UID 找到 Map 中真正的高程图层。
    elevationLayerItem->setData(
        0,
        LayerUidRole,
        layerUid
    );

    // 在保留节点原有能力的基础上，
    // 增加允许用户勾选或取消勾选的能力。
    // 真实高程图层既可以通过复选框控制显隐，
    // 也可以在图层树中被用户拖动。
    elevationLayerItem->setFlags(
        elevationLayerItem->flags() |
        Qt::ItemIsUserCheckable |
        Qt::ItemIsDragEnabled
    );

    // 新加入 Map 的高程图层默认处于启用状态，
    // 所以图层树中的复选框也默认勾选。
    elevationLayerItem->setCheckState(
        0,
        Qt::Checked
    );
}

void MainWindow::removeLayerTreeItem(
    QTreeWidgetItem* item
)
{
    // 防止传入空指针。
    //
    // 例如当前图层树没有选中任何节点时，
    // currentItem() 就可能返回 nullptr。
    if (item == nullptr)
    {
        return;
    }

    // 读取节点内部保存的业务类型。
    //
    // 图层树中除了真实图层，还有 Map 节点和分类节点。
    // 它们都不应该通过当前函数删除。
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

    // 目前只允许删除真实影像图层和真实高程图层。
    //
    // Map、Imagery Layers 和 Elevation Layers
    // 都属于程序组织结构，不能作为普通图层删除。
    if (
        itemType != LayerTreeItemType::ImageryLayer &&
        itemType != LayerTreeItemType::ElevationLayer
    )
    {
        return;
    }

    // 从树节点中读取它所关联的 Map UID 和 Layer UID。
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

    // 如果身份数据不完整，就无法确定应该删除哪个真实图层。
    if (
        !mapUidData.isValid() ||
        !layerUidData.isValid()
    )
    {
        return;
    }

    const int mapUid =
        mapUidData.toInt();

    const int layerUid =
        layerUidData.toInt();

    // 先删除 osgEarth Map 中的真实图层。
    //
    // 只有真实图层删除成功后，才允许删除界面树节点，
    // 从而避免界面状态与 osgEarth Map 状态不一致。
    const bool removed =
        earthViewWidget_->removeLayer(
            mapUid,
            layerUid
        );

    if (!removed)
    {
        // 删除失败时保留界面节点，方便发现和排查问题。
        statusBar()->showMessage(
            QStringLiteral("Failed to remove layer."),
            3000
        );

        return;
    }

    // 保存显示名称。
    //
    // delete item 以后不能再访问 item，
    // 所以需要使用的信息必须提前取出来。
    const QString layerDisplayName =
        item->text(0);

    // 删除图层树中的叶子节点。
    //
    // QTreeWidgetItem 不继承 QObject，
    // 因此不能使用 deleteLater()。
    // delete 会自动把它从父节点的子节点列表中移除。
    delete item;

    // 到这一行以后，item 已经失效，
    // 绝对不能再调用 item->text()、item->data() 等函数。

    statusBar()->showMessage(
        QStringLiteral("Removed layer: %1")
            .arg(layerDisplayName),
        3000
    );
}
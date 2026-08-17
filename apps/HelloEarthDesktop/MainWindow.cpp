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
#include <QFileInfo>
#include <QAbstractButton>
#include <QPushButton>

#include <algorithm>
#include <vector>

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

    // 当用户把本地 TIFF 文件拖入三维窗口后，
    // EarthViewWidget 会发出 rasterFilesDropped 信号。
    //
    // EarthViewWidget 只负责提取文件路径；
    // MainWindow 负责询问用户希望作为影像还是 DEM 加载。
    connect(
        earthViewWidget_,
        &EarthViewWidget::rasterFilesDropped,
        this,
        [this](
            const QStringList& filePaths
        )
        {
            // 当前阶段一次只支持加载一个拖入文件。
            //
            // EarthViewWidget 已经做过初筛，
            // 这里再次验证是为了让槽函数自身保持可靠，
            // 也方便未来其他代码主动发出这个信号。
            if (filePaths.size() != 1)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Please drop exactly one raster file."
                    ),
                    5000
                );

                return;
            }

            const QString filePath =
                filePaths.first();

            if (filePath.isEmpty())
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the file path is empty."
                    ),
                    5000
                );

                return;
            }

            const QFileInfo fileInfo(
                filePath
            );

            // 创建本次拖入操作使用的类型选择窗口。
            QMessageBox typeDialog(this);

            typeDialog.setWindowTitle(
                QStringLiteral(
                    "Choose Raster Layer Type"
                )
            );

            typeDialog.setIcon(
                QMessageBox::Question
            );

            typeDialog.setText(
                QStringLiteral(
                    "How should this raster be loaded?"
                )
            );

            // 显示具体文件名，让用户确认当前选择的是哪份数据。
            typeDialog.setInformativeText(
                QStringLiteral(
                    "File: %1"
                ).arg(
                    fileInfo.fileName()
                )
            );

            // TIFF 既可能是颜色影像，也可能是 DEM。
            //
            // 这里不尝试根据波段数或数据类型自动猜测，
            // 而是让用户明确决定其业务用途。
            auto* imageryButton =
                typeDialog.addButton(
                    QStringLiteral(
                        "Load as Imagery"
                    ),
                    QMessageBox::AcceptRole
                );

            auto* elevationButton =
                typeDialog.addButton(
                    QStringLiteral(
                        "Load as DEM"
                    ),
                    QMessageBox::AcceptRole
                );

            // Cancel 使用 Qt 的标准取消按钮。
            //
            // 用户取消时不会产生任何加载请求。
            typeDialog.addButton(
                QMessageBox::Cancel
            );

            // exec() 会显示模态窗口并等待用户完成选择。
            typeDialog.exec();

            QAbstractButton* selectedButton =
                typeDialog.clickedButton();

            if (selectedButton == imageryButton)
            {
                // 统一入口会再次检查路径和后缀，
                // 然后调用 EarthViewWidget::addImageLayer()。
                requestRasterLayerLoading(
                    filePath,
                    RasterLayerType::Imagery
                );

                return;
            }

            if (selectedButton == elevationButton)
            {
                // 使用同一个文件路径，
                // 但将其作为高程数据交给 DEM 加载流程。
                requestRasterLayerLoading(
                    filePath,
                    RasterLayerType::Elevation
                );

                return;
            }

            // selectedButton 为 Cancel 按钮、nullptr，
            // 或其他未识别按钮时，都视为用户取消。
            statusBar()->showMessage(
                QStringLiteral(
                    "Raster loading canceled."
                ),
                3000
            );
        }
    );

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

            // 文件选择窗口只负责取得路径。
            //
            // 路径验证、格式检查以及具体加载分流，
            // 统一交给 requestRasterLayerLoading()。
            requestRasterLayerLoading(
                imagePath,
                RasterLayerType::Imagery
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

            requestRasterLayerLoading(
                elevationPath,
                RasterLayerType::Elevation
            );
        }
    );

    // 创建左侧图层面板。
    auto* layerDock = new QDockWidget("Layers", this);
    layerDock->setObjectName("LayerDock");

    layerTree_ = new LayerTreeWidget(layerDock);
    layerTree_->setHeaderLabel("Layers");

    // 同时允许：
    // 1. 图层树内部的真实图层顺序调整；
    // 2. 从 Windows 资源管理器拖入外部栅格文件。
    //
    // LayerTreeWidget 会根据 event->source() 区分两种拖放来源。
    layerTree_->setDragDropMode(
        QAbstractItemView::DragDrop
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

    // 连接图层移动完成信号。
    connect(
        layerTree_,
        &LayerTreeWidget::layerItemMoved,
        this,
        &MainWindow::handleLayerItemMoved
    );

    // 当用户从 Windows 资源管理器把单个 TIFF
    // 拖到图层树的合法位置后，处理对应的加载请求。
    connect(
        layerTree_,
        &LayerTreeWidget::externalRasterFileDropped,
        this,
        [this](
            const QString& filePath,
            QTreeWidgetItem* targetGroupItem,
            int insertionIndex
        )
        {
            // 信号必须提供一个有效的目标分类节点。
            if (targetGroupItem == nullptr)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target layer group is invalid."
                    ),
                    5000
                );

                return;
            }

            // 当前只允许同时进行一个栅格图层加载请求。
            //
            // 这样可以避免后一次拖放覆盖前一次尚未完成的
            // 目标分类和插入位置。
            if (pendingRasterTreeInsertion_.active)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Another dropped raster is still being loaded."
                    ),
                    5000
                );

                return;
            }

            bool itemTypeIsValid = false;

            const int itemTypeValue =
                targetGroupItem
                    ->data(
                        0,
                        ItemTypeRole
                    )
                    .toInt(
                        &itemTypeIsValid
                    );

            if (!itemTypeIsValid)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target group type is invalid."
                    ),
                    5000
                );

                return;
            }

            const LayerTreeItemType targetItemType =
                static_cast<LayerTreeItemType>(
                    itemTypeValue
                );

            RasterLayerType rasterLayerType;

            if (
                targetItemType ==
                LayerTreeItemType::ImageryGroup
            )
            {
                rasterLayerType =
                    RasterLayerType::Imagery;
            }
            else if (
                targetItemType ==
                LayerTreeItemType::ElevationGroup
            )
            {
                rasterLayerType =
                    RasterLayerType::Elevation;
            }
            else
            {
                // resolveExternalDropTarget() 主要根据树层级判断目标位置，
                // MainWindow 在这里再根据 ItemTypeRole 做业务级验证。
                //
                // Map 节点、真实图层节点或未来增加的其他分类
                // 都不能进入当前 TIFF 加载流程。
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target is not a raster layer group."
                    ),
                    5000
                );

                return;
            }

            bool mapUidIsValid = false;

            const int mapUid =
                targetGroupItem
                    ->data(
                        0,
                        MapUidRole
                    )
                    .toInt(
                        &mapUidIsValid
                    );

            if (
                !mapUidIsValid ||
                mapUid < 0
            )
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target map is invalid."
                    ),
                    5000
                );

                return;
            }

            // 当前版本只管理一个默认 Map。
            //
            // 检查 UID 可以防止把文件加载到界面上已经失效、
            // 或者并不属于当前 Map 的分类节点中。
            if (
                mapUid !=
                defaultMapTreeItems_.mapUid
            )
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target map is not available."
                    ),
                    5000
                );

                return;
            }

            // 再检查目标分类节点是否确实是当前 Map
            // 记录的 Imagery Layers 或 Elevation Layers。
            if (
                rasterLayerType ==
                RasterLayerType::Imagery &&
                targetGroupItem !=
                    defaultMapTreeItems_.imageryGroupItem
            )
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the imagery group is invalid."
                    ),
                    5000
                );

                return;
            }

            if (
                rasterLayerType ==
                RasterLayerType::Elevation &&
                targetGroupItem !=
                    defaultMapTreeItems_.elevationGroupItem
            )
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the elevation group is invalid."
                    ),
                    5000
                );

                return;
            }

            // 合法插入索引的范围是：
            //
            // 0 到 childCount()，两端都包含。
            //
            // 例如当前有 3 个图层：
            // 0 表示最上方；
            // 3 表示最后一个图层的下方。
            if (
                insertionIndex < 0 ||
                insertionIndex >
                    targetGroupItem->childCount()
            )
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot load dropped raster: "
                        "the target insertion position is invalid."
                    ),
                    5000
                );

                return;
            }

            // 在启动异步加载前保存用户选择的目标位置。
            //
            // 必须先保存再调用 requestRasterLayerLoading()，
            // 以保证后续即使加载流程很快完成，
            // imageryLayerAdded 或 elevationLayerAdded
            // 也能读到正确的插入请求。
            pendingRasterTreeInsertion_.active =
                true;

            pendingRasterTreeInsertion_.layerType =
                rasterLayerType;

            pendingRasterTreeInsertion_.mapUid =
                mapUid;

            pendingRasterTreeInsertion_.insertionIndex =
                insertionIndex;

            // 复用菜单、右键加载和三维窗口拖入
            // 已经使用的统一栅格加载入口。
            const bool loadingStarted =
                requestRasterLayerLoading(
                    filePath,
                    rasterLayerType
                );

            if (!loadingStarted)
            {
                // 如果请求在路径检查、格式检查或底层入口处被拒绝，
                // 就立即清除尚未真正生效的插入请求。
                pendingRasterTreeInsertion_ =
                    PendingRasterTreeInsertion{};
            }
        }
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

            // 只有右键点击真实图层叶子时，
            // 才会创建“移动到图层范围”操作。
            QAction* zoomToLayerAction = nullptr;

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
                // 影像和 DEM 都继承自 osgEarth::TileLayer，
                // 因此都可以根据自身 DataExtent 计算 Viewpoint。
                zoomToLayerAction =
                    contextMenu.addAction(
                        QStringLiteral("Zoom to Layer")
                    );

                // 将导航操作和具有破坏性的删除操作分隔开，
                // 减少用户误点 Remove Layer 的可能。
                contextMenu.addSeparator();

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
            else if (selectedAction == zoomToLayerAction)
            {
                // 从当前 Tree 叶子节点中读取它所属的 Map UID，
                // 以及它对应的真实 osgEarth Layer UID。
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

                // 真实图层节点必须同时具有 Map UID 和 Layer UID。
                if (
                    !mapUidData.isValid() ||
                    !layerUidData.isValid()
                )
                {
                    statusBar()->showMessage(
                        QStringLiteral(
                            "Cannot zoom to layer: "
                            "the tree item does not contain valid UIDs."
                        ),
                        5000
                    );

                    return;
                }

                const int mapUid =
                    mapUidData.toInt();

                const int layerUid =
                    layerUidData.toInt();

                if (
                    mapUid < 0 ||
                    layerUid < 0
                )
                {
                    statusBar()->showMessage(
                        QStringLiteral(
                            "Cannot zoom to layer: "
                            "invalid Map or Layer UID."
                        ),
                        5000
                    );

                    return;
                }

                // 将相机控制交给 EarthViewWidget。
                //
                // MainWindow 只负责读取用户选择的 Tree 节点，
                // 不直接访问 osgEarth Map、TileLayer 或 EarthManipulator。
                const bool movementStarted =
                    earthViewWidget_ != nullptr &&
                    earthViewWidget_->moveToLayer(
                        mapUid,
                        layerUid
                    );

                if (!movementStarted)
                {
                    statusBar()->showMessage(
                        QStringLiteral(
                            "Unable to calculate or start "
                            "the selected layer viewpoint."
                        ),
                        5000
                    );

                    return;
                }

                // item->text(0) 是 Layers Dock 中显示的图层名称。
                statusBar()->showMessage(
                    QStringLiteral("Moving to layer: %1")
                        .arg(
                            item->text(0)
                        ),
                    3000
                );
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
            // 默认加载入口仍然把新影像放到最上方。
            int insertionIndex = 0;

            // 判断当前成功加入 Map 的影像，
            // 是否对应一次从图层树拖入的待处理请求。
            const bool usesPendingInsertion =
                pendingRasterTreeInsertion_.active &&
                pendingRasterTreeInsertion_.layerType ==
                    RasterLayerType::Imagery &&
                pendingRasterTreeInsertion_.mapUid ==
                    mapUid;

            if (usesPendingInsertion)
            {
                insertionIndex =
                    pendingRasterTreeInsertion_
                        .insertionIndex;
            }

            // 真实 osgEarth 图层已经创建成功，
            // 现在才创建与其 UID 关联的树节点。
            QTreeWidgetItem* addedItem =
                addImageryLayerTreeItem(
                    mapUid,
                    layerUid,
                    layerDisplayName,
                    insertionIndex
                );

            // 普通菜单加载、右键加载和三维窗口拖入
            // 不需要处理指定位置。
            if (!usesPendingInsertion)
            {
                return;
            }

            // 这次待处理请求已经被对应的 layerAdded 信号消费。
            //
            // 无论后面的树节点创建或顺序同步是否成功，
            // 都不能让 active 一直保持为 true。
            pendingRasterTreeInsertion_ =
                PendingRasterTreeInsertion{};

            if (addedItem == nullptr)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The imagery layer was added, "
                        "but its tree item could not be created."
                    ),
                    5000
                );

                return;
            }

            QTreeWidgetItem* groupItem =
                addedItem->parent();

            if (groupItem == nullptr)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The imagery layer was added, "
                        "but its target group is invalid."
                    ),
                    5000
                );

                return;
            }

            // addImageryLayerTreeItem() 内部可能通过 std::clamp()
            // 修正原始 insertionIndex，
            // 所以这里重新读取节点最终实际所在的位置。
            const int actualInsertionIndex =
                groupItem->indexOfChild(
                    addedItem
                );

            if (actualInsertionIndex < 0)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The imagery tree item position is invalid."
                    ),
                    5000
                );

                return;
            }

            // osgEarth 中的新图层刚加入时已经处于最上层，
            // Qt 节点也在索引 0 时，两边顺序天然一致。
            if (actualInsertionIndex == 0)
            {
                return;
            }

            // 对 osgEarth 来说，新图层的初始位置对应 Qt 索引 0。
            //
            // 当前 Qt 节点已经位于用户指定的新索引，
            // 因此复用已有的图层顺序同步和失败回滚逻辑。
            handleLayerItemMoved(
                addedItem,
                0,
                actualInsertionIndex
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
            // 普通 DEM 加载默认插入最上方。
            int insertionIndex = 0;

            const bool usesPendingInsertion =
                pendingRasterTreeInsertion_.active &&
                pendingRasterTreeInsertion_.layerType ==
                    RasterLayerType::Elevation &&
                pendingRasterTreeInsertion_.mapUid ==
                    mapUid;

            if (usesPendingInsertion)
            {
                insertionIndex =
                    pendingRasterTreeInsertion_
                        .insertionIndex;
            }

            QTreeWidgetItem* addedItem =
                addElevationLayerTreeItem(
                    mapUid,
                    layerUid,
                    layerDisplayName,
                    insertionIndex
                );

            if (!usesPendingInsertion)
            {
                return;
            }

            // 当前拖入请求已经完成对应关系匹配，
            // 及时恢复为空闲状态。
            pendingRasterTreeInsertion_ =
                PendingRasterTreeInsertion{};

            if (addedItem == nullptr)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The DEM layer was added, "
                        "but its tree item could not be created."
                    ),
                    5000
                );

                return;
            }

            QTreeWidgetItem* groupItem =
                addedItem->parent();

            if (groupItem == nullptr)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The DEM layer was added, "
                        "but its target group is invalid."
                    ),
                    5000
                );

                return;
            }

            const int actualInsertionIndex =
                groupItem->indexOfChild(
                    addedItem
                );

            if (actualInsertionIndex < 0)
            {
                statusBar()->showMessage(
                    QStringLiteral(
                        "The DEM tree item position is invalid."
                    ),
                    5000
                );

                return;
            }

            // 目标本来就是最上方时，无须额外移动 osgEarth 图层。
            if (actualInsertionIndex == 0)
            {
                return;
            }

            // 新 DEM 在 osgEarth 中的初始同类位置视为最上方，
            // 将它同步到用户在 Qt 树中指定的位置。
            handleLayerItemMoved(
                addedItem,
                0,
                actualInsertionIndex
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

bool MainWindow::requestRasterLayerLoading(
    const QString& filePath,
    RasterLayerType layerType
)
{
    // 去掉路径首尾可能存在的空白字符。
    //
    // 正常的 QFileDialog 路径通常不会包含这些空白，
    // 但拖放、复制粘贴或未来其他输入方式可能产生这种情况。
    const QString trimmedFilePath =
        filePath.trimmed();

    if (trimmedFilePath.isEmpty())
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot load raster: the file path is empty."
            ),
            5000
        );

        return false;
    }

    // QFileInfo 只负责检查文件系统信息，
    // 不会在这里真正打开或读取栅格数据。
    const QFileInfo fileInfo(
        trimmedFilePath
    );

    // 路径必须真实存在。
    if (!fileInfo.exists())
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot load raster: the file does not exist."
            ),
            5000
        );

        return false;
    }

    // 当前只接受单个普通文件。
    //
    // 如果用户未来把文件夹拖进程序，
    // 这里会拒绝，而不会错误地把目录当作栅格数据。
    if (!fileInfo.isFile())
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot load raster: the path is not a file."
            ),
            5000
        );

        return false;
    }

    // 当前进程必须具有读取该文件的权限。
    if (!fileInfo.isReadable())
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot load raster: the file is not readable."
            ),
            5000
        );

        return false;
    }

    // suffix() 返回不包含点号的后缀。
    //
    // 例如：
    // image.tif  -> tif
    // image.TIFF -> TIFF
    //
    // 转换成小写后，判断就不再区分大小写。
    const QString suffix =
        fileInfo
            .suffix()
            .toLower();

    // 当前阶段只正式支持 GeoTIFF 文件。
    //
    // 注意：后缀检查只是快速初筛。
    // 一个文件名为 .tif 并不代表其内容一定有效，
    // 后续仍然会经过栅格预处理模块和 GDAL 的严格检查。
    const bool isSupportedRaster =
        suffix == QStringLiteral("tif") ||
        suffix == QStringLiteral("tiff");

    if (!isSupportedRaster)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Unsupported raster format: .%1"
            ).arg(suffix),
            5000
        );

        return false;
    }

    // 三维控件负责真正创建 osgEarth Layer、
    // 计算 Viewpoint 以及在相机飞行后加入 Map。
    if (earthViewWidget_ == nullptr)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot load raster: "
                "the Earth view is not available."
            ),
            5000
        );

        return false;
    }

    // 使用规范的绝对路径交给底层加载逻辑。
    //
    // 这样加载结果不依赖程序当前工作目录，
    // 也方便未来从文件拖放事件中接收路径。
    const QString absoluteFilePath =
        fileInfo.absoluteFilePath();

    bool loadingRequested = false;
    QString layerTypeDisplayName;

    // 根据用户指定的用途，
    // 把同一份 TIFF 分发给不同的 osgEarth 图层加载流程。
    switch (layerType)
    {
        case RasterLayerType::Imagery:
        {
            layerTypeDisplayName =
                QStringLiteral("imagery");

            loadingRequested =
                earthViewWidget_->addImageLayer(
                    absoluteFilePath
                );

            break;
        }

        case RasterLayerType::Elevation:
        {
            layerTypeDisplayName =
                QStringLiteral("DEM");

            loadingRequested =
                earthViewWidget_->addElevationLayer(
                    absoluteFilePath
                );

            break;
        }

        default:
        {
            // 当前枚举理论上只存在 Imagery 和 Elevation。
            // 保留 default 可以防止未来增加枚举值后，
            // 忘记在这里增加对应加载逻辑。
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot load raster: "
                    "unsupported layer type."
                ),
                5000
            );

            return false;
        }
    }

    if (!loadingRequested)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Failed to start loading %1: %2"
            )
                .arg(layerTypeDisplayName)
                .arg(fileInfo.fileName()),
            5000
        );

        return false;
    }

    // 当前 addImageLayer() 和 addElevationLayer()
    // 采用“先计算并移动视点，飞行结束后再加入 Map”的流程。
    //
    // 因此这里提示的是请求已经建立，
    // 不表示图层已经立即完成全部加载和渲染。
    statusBar()->showMessage(
        QStringLiteral(
            "Loading %1: %2"
        )
            .arg(layerTypeDisplayName)
            .arg(fileInfo.fileName()),
        3000
    );

    return true;
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

QTreeWidgetItem* MainWindow::addImageryLayerTreeItem(
    int mapUid,
    int layerUid,
    const QString& layerDisplayName,
    int insertionIndex
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
        return nullptr;;
    }

    // 先创建一个尚未加入图层树的独立叶子节点。
    auto* imageryLayerItem =
        new QTreeWidgetItem();

    // 将新加载的影像插入 Imagery Layers 的第 0 个位置。
    //
    QTreeWidgetItem* imageryGroupItem =
        defaultMapTreeItems_.imageryGroupItem;

    if (imageryGroupItem == nullptr)
    {
        delete imageryLayerItem;
        return nullptr;;
    }

    // 用户松开鼠标到真实图层加入 Map 之间存在一段异步时间。
    //
    // 在这段时间里，树结构理论上可能发生变化，
    // 所以不能完全相信之前保存的 insertionIndex。
    //
    // std::clamp() 会把索引限制在：
    // 0 到当前 childCount() 之间。
    const int safeInsertionIndex =
        std::clamp(
            insertionIndex,
            0,
            imageryGroupItem->childCount()
        );

    // 按指定位置插入新的影像图层节点。
    //
    // 如果调用者没有传入 insertionIndex，
    // 头文件中的默认值 0 会让它继续插入最上方。
    imageryGroupItem->insertChild(
        safeInsertionIndex,
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

    // 节点已经交给 imageryGroupItem 管理。
    //
    // 返回的是非拥有型指针，
    // 调用者不能手动释放它。
    return imageryLayerItem;
}

QTreeWidgetItem* MainWindow::addElevationLayerTreeItem(
    int mapUid,
    int layerUid,
    const QString& layerDisplayName,
    int insertionIndex
)
{
    // 当前版本只管理一个默认 Map。
    //
    // 如果信号携带的 Map UID 与图层树记录的 Map UID 不一致，
    // 说明这个高程图层不属于当前 Map，因此暂时不创建树节点。
    if (defaultMapTreeItems_.mapUid != mapUid)
    {
        return nullptr;;
    }

    // 创建一个暂时还没有加入图层树的独立叶子节点。
    auto* elevationLayerItem =
        new QTreeWidgetItem();

    // 将新加载的 DEM 插入 Elevation Layers 分类的最上方。
    //
    // 索引 0 代表第一个子节点，也就是界面中最上方的位置。
    QTreeWidgetItem* elevationGroupItem =
        defaultMapTreeItems_.elevationGroupItem;

    if (elevationGroupItem == nullptr)
    {
        delete elevationLayerItem;
        return nullptr;;
    }

    // 把之前记录的目标位置限制在当前合法索引范围内。
    //
    // 如果当前有 3 个 DEM 节点，合法范围就是 0～3：
    // 0 表示最上方，3 表示最下方。
    const int safeInsertionIndex =
        std::clamp(
            insertionIndex,
            0,
            elevationGroupItem->childCount()
        );

    // 按指定位置插入新的 DEM 图层节点。
    elevationGroupItem->insertChild(
        safeInsertionIndex,
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

    // 节点现在由 elevationGroupItem 所在的树结构管理。
    return elevationLayerItem;
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

void MainWindow::handleLayerItemMoved(
    QTreeWidgetItem* item,
    int oldIndex,
    int newIndex
)
{
    // 防止收到空节点。
    if (item == nullptr)
    {
        return;
    }

    // oldIndex 和 newIndex 都必须是有效的非负索引。
    if (oldIndex < 0 || newIndex < 0)
    {
        return;
    }

    // 如果新旧索引相同，说明节点实际没有发生位置变化。
    //
    // LayerTreeWidget 正常情况下已经过滤过这种情况，
    // 这里再次检查是为了让函数自身保持可靠。
    if (oldIndex == newIndex)
    {
        return;
    }

    // 取得节点移动完成后所属的父分类节点。
    //
    // 当前合法结构应当是：
    //
    // ImageryLayer   -> ImageryGroup
    // ElevationLayer -> ElevationGroup
    QTreeWidgetItem* groupItem =
        item->parent();

    if (groupItem == nullptr)
    {
        return;
    }

    // 取得移动完成后分类中的实际子节点数量。
    const int childCount =
        groupItem->childCount();

    // oldIndex 和 newIndex 都应当位于同一个分类
    // 当前有效的子节点索引范围内。
    //
    // 节点移动不会改变分类中的图层总数，
    // 所以旧索引和新索引都应小于 childCount。
    if (
        oldIndex >= childCount ||
        newIndex >= childCount
    )
    {
        return;
    }

    // 再次确认节点当前实际所在的位置，
    // 与 LayerTreeWidget 报告的 newIndex 一致。
    //
    // 如果不一致，说明树结构可能在信号传递期间
    // 又发生了其他变化，此时不能继续同步 osgEarth。
    const int actualNewIndex =
        groupItem->indexOfChild(
            item
        );

    if (actualNewIndex != newIndex)
    {
        return;
    }

    // 读取被移动节点和父分类节点的业务类型。
    const QVariant itemTypeData =
        item->data(
            0,
            ItemTypeRole
        );

    const QVariant groupTypeData =
        groupItem->data(
            0,
            ItemTypeRole
        );

    if (
        !itemTypeData.isValid() ||
        !groupTypeData.isValid()
    )
    {
        return;
    }

    const auto itemType =
        static_cast<LayerTreeItemType>(
            itemTypeData.toInt()
        );

    const auto groupType =
        static_cast<LayerTreeItemType>(
            groupTypeData.toInt()
        );

    // 验证真实图层与分类节点是否正确匹配。
    //
    // 不能出现：
    // ImageryLayer   -> ElevationGroup
    // ElevationLayer -> ImageryGroup
    const bool isValidImageryRelationship =
        itemType ==
            LayerTreeItemType::ImageryLayer &&
        groupType ==
            LayerTreeItemType::ImageryGroup;

    const bool isValidElevationRelationship =
        itemType ==
            LayerTreeItemType::ElevationLayer &&
        groupType ==
            LayerTreeItemType::ElevationGroup;

    if (
        !isValidImageryRelationship &&
        !isValidElevationRelationship
    )
    {
        return;
    }

    // 从分类节点和图层节点中分别读取 Map UID。
    //
    // 两者应该完全一致，表示该叶子确实属于
    // 这个分类所管理的真实 osgEarth Map。
    const QVariant groupMapUidData =
        groupItem->data(
            0,
            MapUidRole
        );

    const QVariant itemMapUidData =
        item->data(
            0,
            MapUidRole
        );

    if (
        !groupMapUidData.isValid() ||
        !itemMapUidData.isValid()
    )
    {
        return;
    }

    const int mapUid =
        groupMapUidData.toInt();

    if (
        mapUid < 0 ||
        itemMapUidData.toInt() != mapUid
    )
    {
        return;
    }

    // 被移动的叶子节点还必须保存有效的真实 Layer UID。
    const QVariant movedLayerUidData =
        item->data(
            0,
            LayerUidRole
        );

    if (
        !movedLayerUidData.isValid() ||
        movedLayerUidData.toInt() < 0
    )
    {
        return;
    }

    // 到这里已经确认：
    //
    // 1. 移动节点存在；
    // 2. oldIndex 和 newIndex 合法且确实发生变化；
    // 3. 节点仍然位于正确的分类中；
    // 4. 图层类型与分类类型匹配；
    // 5. 分类与叶子属于同一个 Map；
    // 6. 被移动节点保存了有效 Layer UID。
    //
    // 下一步将遍历 groupItem 的所有子节点，
    // 按照 Qt 从上到下的顺序收集完整 Layer UID 列表。
    // 保存当前分类中，按照 Qt 界面从上到下排列的
    // 全部真实 Layer UID。
    std::vector<int> layerUidsTopToBottom;

    layerUidsTopToBottom.reserve(
        static_cast<std::size_t>(
            childCount
        )
    );

    // 根据当前父分类的类型，确定其中每个叶子
    // 应当具有的真实图层节点类型。
    const LayerTreeItemType expectedLayerType =
        groupType ==
            LayerTreeItemType::ImageryGroup
        ?
            LayerTreeItemType::ImageryLayer
        :
            LayerTreeItemType::ElevationLayer;

    // 记录遍历过程中是否确实找到了本次被移动的节点。
    //
    // 正常情况下它一定属于 groupItem，
    // 这里再次记录是为了验证整个分类结构。
    bool movedItemFound = false;

    // 按照 Qt 图层树从上到下的顺序遍历分类中的所有叶子。
    for (
        int childIndex = 0;
        childIndex < childCount;
        ++childIndex
    )
    {
        QTreeWidgetItem* childItem =
            groupItem->child(
                childIndex
            );

        if (childItem == nullptr)
        {
            return;
        }

        if (childItem == item)
        {
            movedItemFound = true;
        }

        // 分类下的每个子节点都必须保存有效的业务类型。
        const QVariant childTypeData =
            childItem->data(
                0,
                ItemTypeRole
            );

        if (!childTypeData.isValid())
        {
            return;
        }

        const auto childType =
            static_cast<LayerTreeItemType>(
                childTypeData.toInt()
            );

        // ImageryGroup 中只能出现 ImageryLayer；
        // ElevationGroup 中只能出现 ElevationLayer。
        if (childType != expectedLayerType)
        {
            return;
        }

        // 读取该叶子所属的 Map UID。
        const QVariant childMapUidData =
            childItem->data(
                0,
                MapUidRole
            );

        if (
            !childMapUidData.isValid() ||
            childMapUidData.toInt() != mapUid
        )
        {
            // 分类中的所有真实图层都必须属于同一个 Map。
            return;
        }

        // 读取该叶子关联的真实 osgEarth Layer UID。
        const QVariant childLayerUidData =
            childItem->data(
                0,
                LayerUidRole
            );

        if (
            !childLayerUidData.isValid() ||
            childLayerUidData.toInt() < 0
        )
        {
            return;
        }

        // childIndex 从 0 开始向后遍历，
        // 所以写入 vector 的顺序就是 Qt 界面从上到下的顺序。
        layerUidsTopToBottom.emplace_back(
            childLayerUidData.toInt()
        );
    }

    // 被移动节点必须确实存在于刚才遍历的父分类中。
    if (!movedItemFound)
    {
        return;
    }

    // 收集到的 UID 数量必须和分类子节点数量完全一致。
    if (
        layerUidsTopToBottom.size() !=
        static_cast<std::size_t>(
            childCount
        )
    )
    {
        return;
    }

    // 到这里，layerUidsTopToBottom 已经表示
    // 当前分类移动完成后的完整目标顺序。
    //
    // 例如界面显示：
    // Local B
    // Local A
    // Global
    //
    // 那么 vector 中就是：
    // [Local B UID, Local A UID, Global UID]
    //
    // 请求 EarthViewWidget 根据 Qt 分类中的完整目标顺序，
    // 调整真实 osgEarth Map 中的 Layer 顺序。
    const bool synchronized =
        earthViewWidget_ != nullptr &&
        earthViewWidget_->synchronizeLayerOrder(
            mapUid,
            layerUidsTopToBottom
        );

    // 同步成功时，Qt 图层树和 osgEarth Map
    // 已经同时处于用户要求的新顺序。
    if (synchronized)
    {
        statusBar()->showMessage(
            QStringLiteral("Layer order updated."),
            3000
        );

        return;
    }

    // 同步失败后，Qt 图层树已经完成移动，
    // 但 osgEarth Map 没有可靠地达到目标状态。
    //
    // 为避免界面长期显示一个并未真正生效的顺序，
    // 需要把 Qt 节点恢复到 oldIndex。
    const int currentIndex =
        groupItem->indexOfChild(
            item
        );

    if (currentIndex < 0)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Layer order synchronization failed, "
                "and the tree item could not be restored."
            ),
            5000
        );

        return;
    }

    // 回滚过程中临时阻止 layerTree_ 发出其他 Qt 信号。
    //
    // 当前 layerItemMoved 信号只由我们的 dropEvent() 主动发出，
    // 正常来说程序插回节点不会再次触发它。
    // 这里仍然使用 QSignalBlocker，避免 itemChanged 等父类信号
    // 在树节点被取出和插回时产生额外业务操作。
    const QSignalBlocker signalBlocker(
        layerTree_
    );

    // 从当前新位置取出被移动节点。
    //
    // takeChild() 只解除节点与父分类的关系，
    // 不会删除 QTreeWidgetItem，因此节点可以继续插回旧位置。
    QTreeWidgetItem* itemToRestore =
        groupItem->takeChild(
            currentIndex
        );

    if (itemToRestore == nullptr)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Layer order synchronization failed, "
                "and the tree item could not be taken for rollback."
            ),
            5000
        );

        return;
    }

    // 安全确认：取出的节点必须就是本次移动的节点。
    if (itemToRestore != item)
    {
        // 如果发生异常，先把误取出的节点放回原位置，
        // 避免进一步破坏树结构。
        groupItem->insertChild(
            currentIndex,
            itemToRestore
        );

        statusBar()->showMessage(
            QStringLiteral(
                "Layer order synchronization failed because "
                "the moved tree item could not be identified."
            ),
            5000
        );

        return;
    }

    // 将节点插回移动前的位置。
    groupItem->insertChild(
        oldIndex,
        itemToRestore
    );

    // 构造拖动发生前的分类 UID 顺序，
    // 用于尝试恢复可能已经被部分修改的 osgEarth Map。
    //
    // layerUidsTopToBottom 当前保存的是移动后的顺序。
    // 将 newIndex 上的 UID 取出，再插回 oldIndex，
    // 就能重建移动之前的完整分类顺序。
    std::vector<int> originalLayerUidsTopToBottom =
        layerUidsTopToBottom;

    const int movedLayerUid =
        originalLayerUidsTopToBottom[
            static_cast<std::size_t>(
                newIndex
            )
        ];

    originalLayerUidsTopToBottom.erase(
        originalLayerUidsTopToBottom.begin() +
        newIndex
    );

    originalLayerUidsTopToBottom.insert(
        originalLayerUidsTopToBottom.begin() +
        oldIndex,
        movedLayerUid
    );

    // 第一次同步可能在完成部分 moveLayer() 后才发现异常。
    //
    // 因此在恢复 Qt 树顺序后，再按照原始 UID 顺序
    // 尝试恢复真实 osgEarth Map。
    const bool mapRestored =
        earthViewWidget_ != nullptr &&
        earthViewWidget_->synchronizeLayerOrder(
            mapUid,
            originalLayerUidsTopToBottom
        );

    if (mapRestored)
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Layer order update failed. "
                "The previous order was restored."
            ),
            5000
        );
    }
    else
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Layer order update failed, and the osgEarth "
                "layer order could not be fully restored."
            ),
            7000
        );
    }
}
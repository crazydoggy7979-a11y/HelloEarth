#pragma once

#include <QMainWindow>

class EarthViewWidget;
class LayerTreeWidget;

class QString;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    // 表示用户希望把一份栅格数据作为哪种 osgEarth 图层加载。
    //
    // 这个类型描述的是“加载用途”，而不是文件格式。
    //
    // 例如同一个 GeoTIFF：
    // 1. 可以作为 Imagery 加载，为地球表面提供颜色；
    // 2. 也可以作为 Elevation 加载，为地形提供高度。
    //
    // 后续无论加载请求来自菜单、右键操作还是文件拖入，
    // 都使用这个枚举明确表达最终要创建的图层类型。
    enum class RasterLayerType
    {
        // 作为 osgEarth 影像图层加载。
        Imagery,

        // 作为 osgEarth 高程图层加载。
        Elevation
    };

    // 保存一次“从外部文件拖入图层树”的待完成插入请求。
    //
    // 用户松开鼠标时，真实 osgEarth Layer 尚未创建，
    // 因此不能立即创建最终的树叶子节点。
    //
    // MainWindow 会先记录目标 Map、图层类型和插入位置。
    // 等 EarthViewWidget 发出 imageryLayerAdded 或
    // elevationLayerAdded 信号后，再使用这些信息创建树节点。
    struct PendingRasterTreeInsertion
    {
        // true 表示当前存在一个等待真实图层加载完成的插入请求。
        //
        // 普通菜单加载和拖入三维窗口时不会启用这个状态，
        // 它们仍然默认把新图层插入分类最上方。
        bool active = false;

        // 记录本次文件要作为影像还是 DEM 加载。
        RasterLayerType layerType =
            RasterLayerType::Imagery;

        // 记录目标分类所属的真实 osgEarth Map UID。
        //
        // 当前虽然只有一个 Map，
        // 但保留这个字段可以为未来多 Map 提供基础。
        int mapUid = -1;

        // 记录新节点在分类中的目标索引。
        //
        // 索引采用 Qt 图层树从上到下的顺序：
        // 0 表示最上方，1 表示第二个位置。
        int insertionIndex = 0;
    };

    // 统一处理一份本地栅格数据的加载请求。
    //
    // filePath：
    // 用户选择或拖入的原始文件路径。
    //
    // layerType：
    // 用户希望把该栅格作为影像还是高程图层加载。
    //
    // 当前函数将负责：
    // 1. 检查文件路径是否为空；
    // 2. 检查文件是否真实存在且为普通文件；
    // 3. 根据后缀判断是否为当前支持的栅格格式；
    // 4. 根据 RasterLayerType 调用 EarthViewWidget 中
    //    对应的影像或 DEM 加载函数；
    // 5. 在状态栏中反馈加载请求是否成功建立。
    //
    // 返回 true 表示加载请求已经成功提交。
    //
    // 由于当前图层采用“先飞行、后加入 Map”的异步流程，
    // true 不代表图层已经立即出现在 Map 中，
    // 而是表示预处理、图层打开和视点计算等请求已经成功启动。
    //
    // 返回 false 表示路径无效、格式不受支持，
    // EarthViewWidget 尚未创建，或者底层拒绝了加载请求。
    bool requestRasterLayerLoading(
        const QString& filePath,
        RasterLayerType layerType
    );

    // 记录一个 Map 在 Layers Dock 中对应的整组树节点。
    //
    // 目前程序只创建一组；未来支持多个 Map 时，
    // 可以为每个 Map 分别创建一个 MapTreeItems。
    struct MapTreeItems
    {
        // osgEarth 为真实 Map 分配的运行时唯一编号。
        //
        // 后续收到图层信号时，可以通过这个编号判断
        // 图层应当加入哪个 Map 的分类节点。
        int mapUid = -1;

        // 代表一个 Map 的顶层节点。
        QTreeWidgetItem* mapItem = nullptr;

        // 该 Map 下的影像图层分类节点。
        QTreeWidgetItem* imageryGroupItem = nullptr;

        // 该 Map 下的高程图层分类节点。
        QTreeWidgetItem* elevationGroupItem = nullptr;
    };

    // 根据真实 Map 的 UID 和显示名称，
    // 创建该 Map 在 Layers Dock 中对应的树结构。
    MapTreeItems createMapTreeItems(
        int mapUid,
        const QString& mapDisplayName
    );

    // insertionIndex 表示新节点在 Imagery Layers 中
    // 从上到下的目标索引。
    //
    // 默认值 0 表示：
    // 如果调用者没有指定位置，新影像仍然插入分类最上方。
    QTreeWidgetItem* addImageryLayerTreeItem(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName,
        int insertionIndex = 0
    );

    // 在指定 Map 的 Elevation Layers 分类下，
    // 创建一个与真实 osgEarth 高程图层关联的叶子节点。
    //
    // mapUid：高程图层所属 Map 的唯一编号。
    // layerUid：真实 osgEarth ElevationLayer 的唯一编号。
    // layerDisplayName：显示在 Layers Dock 中的图层名称。
    QTreeWidgetItem* addElevationLayerTreeItem(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName,
        int insertionIndex = 0
    );

    // 删除一个图层树叶子及其关联的真实 osgEarth 图层。
    //
    // item 必须是 ImageryLayer 或 ElevationLayer 类型的叶子节点。
    // 函数会从节点内部读取 Map UID 和 Layer UID，
    // 先请求 EarthViewWidget 删除真实图层；
    // 真实图层删除成功后，才会删除界面中的树节点。
    //
    // Map、Imagery Layers 和 Elevation Layers 等结构节点
    // 不允许通过这个函数删除。
    void removeLayerTreeItem(
        QTreeWidgetItem* item
    );

    // 处理 Layers Dock 中真实图层节点完成拖动后的顺序同步。
    //
    // item：
    // 本次被移动的图层树节点。
    //
    // oldIndex：
    // 节点移动前在所属分类中的 Qt 索引。
    //
    // newIndex：
    // 节点移动后在所属分类中的 Qt 索引。
    //
    // 函数将负责：
    // 1. 验证被移动节点及其父分类；
    // 2. 读取父分类所属的 Map UID；
    // 3. 按界面从上到下收集该分类的全部 Layer UID；
    // 4. 调用 EarthViewWidget::synchronizeLayerOrder()；
    // 5. 同步失败时，将 Qt 节点恢复到 oldIndex。
    void handleLayerItemMoved(
        QTreeWidgetItem* item,
        int oldIndex,
        int newIndex
    );

    // 中央三维显示控件。
    EarthViewWidget* earthViewWidget_ = nullptr;

    // Layers Dock 中的图层树。
    LayerTreeWidget* layerTree_ = nullptr;

    // 当前默认 Map 对应的树节点集合。
    //
    // 未来支持多个 Map 时，可以将其升级为：
    // std::vector<MapTreeItems>。
    MapTreeItems defaultMapTreeItems_;

    // 当前等待真实图层加载完成的树插入请求。
    //
    // 由于 EarthViewWidget 已经限制同一时间只能进行一次
    // 异步栅格图层加载，因此这里暂时只需要保存一个请求。
    PendingRasterTreeInsertion
        pendingRasterTreeInsertion_;
};
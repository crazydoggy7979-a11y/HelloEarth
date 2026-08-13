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

    // 在指定 Map 的 Imagery Layers 分类下，
    // 创建一个与真实 osgEarth 影像图层关联的叶子节点。
    void addImageryLayerTreeItem(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName
    );

    // 在指定 Map 的 Elevation Layers 分类下，
    // 创建一个与真实 osgEarth 高程图层关联的叶子节点。
    //
    // mapUid：高程图层所属 Map 的唯一编号。
    // layerUid：真实 osgEarth ElevationLayer 的唯一编号。
    // layerDisplayName：显示在 Layers Dock 中的图层名称。
    void addElevationLayerTreeItem(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName
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

    // 中央三维显示控件。
    EarthViewWidget* earthViewWidget_ = nullptr;

    // Layers Dock 中的图层树。
    LayerTreeWidget* layerTree_ = nullptr;

    // 当前默认 Map 对应的树节点集合。
    //
    // 未来支持多个 Map 时，可以将其升级为：
    // std::vector<MapTreeItems>。
    MapTreeItems defaultMapTreeItems_;
};
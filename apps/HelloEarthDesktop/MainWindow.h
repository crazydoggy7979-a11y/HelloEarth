#pragma once

#include <QMainWindow>

class EarthViewWidget;
class QTreeWidget;

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

    // 中央三维显示控件。
    EarthViewWidget* earthViewWidget_ = nullptr;

    // Layers Dock 中的图层树。
    QTreeWidget* layerTree_ = nullptr;

    // 当前默认 Map 对应的树节点集合。
    //
    // 未来支持多个 Map 时，可以将其升级为：
    // std::vector<MapTreeItems>。
    MapTreeItems defaultMapTreeItems_;
};
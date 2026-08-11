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
        // 代表一个 Map 的顶层节点。
        QTreeWidgetItem* mapItem = nullptr;

        // 该 Map 下的影像图层分类节点。
        QTreeWidgetItem* imageryGroupItem = nullptr;

        // 该 Map 下的高程图层分类节点。
        QTreeWidgetItem* elevationGroupItem = nullptr;
    };

    // 为一个 Map 创建完整的图层树分类结构。
    MapTreeItems createMapTreeItems(
        const QString& mapDisplayName
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
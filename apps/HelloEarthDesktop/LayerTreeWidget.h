#pragma once

#include <QTreeWidget>
#include <QPoint>
#include <QString>

// 前置声明拖放事件类型。
//
// 这里只使用 QDragMoveEvent 和 QDropEvent 的指针，
// 所以头文件中暂时不需要包含它们的完整定义。
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

class QDragLeaveEvent;
class QPaintEvent;

// HelloEarth 专用的图层树控件。
//
// QTreeWidget 本身已经具有树节点显示、选择、展开、
// 复选框以及内部拖放等基础能力。
//
// LayerTreeWidget 在这些能力之上增加业务限制：
// 1. 影像图层只能在 Imagery Layers 分类内部移动；
// 2. 高程图层只能在 Elevation Layers 分类内部移动；
// 3. 不允许图层被拖入另一种分类；
// 4. 不允许真实图层成为其他图层的子节点。
class LayerTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    // explicit 可以避免 QWidget* 被意外地
    // 隐式转换成 LayerTreeWidget 对象。
    explicit LayerTreeWidget(
        QWidget* parent = nullptr
    );

signals:
    // 当一个真实图层节点在分类内部完成移动后发出。
    //
    // item：
    // 本次被移动的 QTreeWidgetItem。
    // MainWindow 可以从中读取 MapUidRole 和 LayerUidRole，
    // 从而找到对应的真实 osgEarth Layer。
    //
    // oldIndex：
    // 节点移动前在父分类中的索引。
    //
    // newIndex：
    // 节点移动后在父分类中的索引。
    //
    // 这里的索引都是 Qt 图层树中从上到下的索引，
    // 尚未转换成 osgEarth Map 的全局 Layer 索引。
    //
    // 保留 oldIndex 的主要原因是：
    // 如果后续 osgEarth 图层顺序同步失败，
    // MainWindow 可以把 Qt 节点恢复到原来的位置。
    void layerItemMoved(
        QTreeWidgetItem* item,
        int oldIndex,
        int newIndex
    );

    // 当用户把一个受支持的本地栅格文件拖到图层树的合法位置后发出。
    //
    // filePath：
    // 被拖入的本地文件绝对路径。
    //
    // targetGroupItem：
    // 文件最终要进入的分类节点。
    // 当前只能是 Imagery Layers 或 Elevation Layers。
    //
    // insertionIndex：
    // 新图层在分类节点中的目标位置，采用 Qt 从上到下的索引。
    // 例如 0 表示插入到该分类的最上方。
    //
    // 注意：
    // 这个信号只表达“用户提出了加载请求”，
    // LayerTreeWidget 不会立即创建图层树叶子。
    // 只有真实 osgEarth 图层加载成功后，MainWindow 才会创建叶子节点。
    void externalRasterFileDropped(
        const QString& filePath,
        QTreeWidgetItem* targetGroupItem,
        int insertionIndex
    );

protected:
    // 外部文件第一次进入图层树区域时触发。
    //
    // 后续将在这里检查：
    // 1. 拖入内容是否为本地文件；
    // 2. 是否只有一个文件；
    // 3. 文件后缀是否为 tif 或 tiff。
    void dragEnterEvent(
        QDragEnterEvent* event
    ) override;

    // 用户开始拖动图层节点时触发。
    //
    // QTreeWidget::startDrag() 返回时，
    // Qt 的内部移动、目标节点创建以及原节点删除才全部完成。
    //
    // 因此我们将在这里记录移动前的位置，
    // 并在父类完成整个拖放流程后发送 layerItemMoved 信号。
    void startDrag(
        Qt::DropActions supportedActions
    ) override;

    // 拖动尚未松开鼠标时触发。
    //
    // 我们将在这里检查当前悬停位置是否允许放置，
    // 并决定是否显示有效的拖放提示。
    void dragMoveEvent(
        QDragMoveEvent* event
    ) override;

    // 用户松开鼠标、正式执行放置时触发。
    //
    // 即使 dragMoveEvent 已经检查过，
    // 这里仍然会进行最终验证，避免非法树结构真正生效。
    void dropEvent(
        QDropEvent* event
    ) override;

    // 外部文件离开图层树区域时触发。
    //
    // 用于清除自定义的外部文件插入提示线。
    void dragLeaveEvent(
        QDragLeaveEvent* event
    ) override;

    // 绘制图层树内容。
    //
    // 先让 QTreeWidget 绘制正常的树节点，
    // 再在需要时绘制外部文件的插入位置提示线。
    void paintEvent(
        QPaintEvent* event
    ) override;

private:
    // 外部文件插入提示线当前是否需要显示。
    bool externalDropIndicatorVisible_ = false;

    // 提示线在 viewport 中的纵向坐标。
    int externalDropIndicatorY_ = 0;

    // 提示线左端的横向坐标。
    //
    // 根据目标节点的树层级设置，
    // 让提示线从真实图层所在的缩进位置开始。
    int externalDropIndicatorLeft_ = 0;

    // 清除外部文件插入提示线并刷新图层树。
    void clearExternalDropIndicator();

    // LayerTreeWidget 内部使用的临时拖动标识。
    //
    // Qt 内部移动 QTreeWidgetItem 时可能复制节点再删除原节点，
    // 因此不能依靠原来的节点指针寻找移动后的节点。
    //
    // 这个 Role 用于把临时编号跟随节点数据一起复制到新位置。
    static constexpr int DragTokenRole =
        Qt::UserRole + 1000;

    // 为每次拖动生成不同的临时编号。
    qulonglong nextDragToken_ = 0;

    // 判断当前内部拖动是否允许放到指定位置。
    //
    // 这个函数只负责验证树结构，不真正移动节点。
    bool canDropCurrentItem(
        const QPoint& position
    ) const;

    // 检查外部拖动数据中是否只包含一个受支持的本地 TIFF 文件。
    //
    // 该函数只负责检查文件本身，
    // 不负责判断鼠标当前所在的树节点是否合法。
    bool containsSingleSupportedRasterFile(
        const QMimeData* mimeData
    ) const;

    // 根据鼠标位置计算外部文件最终应当进入的分类节点和插入索引。
    //
    // 成功时：
    // targetGroupItem 指向 Imagery Layers 或 Elevation Layers；
    // insertionIndex 表示新图层在该分类中的目标位置。
    //
    // 失败时返回 false，表示当前位置不允许放置文件。
    bool resolveExternalDropTarget(
        const QPoint& position,
        QTreeWidgetItem*& targetGroupItem,
        int& insertionIndex
    ) const;
};
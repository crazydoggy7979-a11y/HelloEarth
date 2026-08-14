#pragma once

#include <QTreeWidget>
#include <QPoint>

// 前置声明拖放事件类型。
//
// 这里只使用 QDragMoveEvent 和 QDropEvent 的指针，
// 所以头文件中暂时不需要包含它们的完整定义。
class QDragMoveEvent;
class QDropEvent;

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

protected:
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

private:
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
};
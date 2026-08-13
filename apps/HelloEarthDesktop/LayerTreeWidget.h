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
public:
    // explicit 可以避免 QWidget* 被意外地
    // 隐式转换成 LayerTreeWidget 对象。
    explicit LayerTreeWidget(
        QWidget* parent = nullptr
    );

protected:
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
    // 判断当前内部拖动是否允许放到指定位置。
    //
    // 这个函数只负责验证树结构，不真正移动节点。
    bool canDropCurrentItem(
        const QPoint& position
    ) const;
};
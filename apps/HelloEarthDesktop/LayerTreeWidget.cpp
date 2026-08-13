#include "LayerTreeWidget.h"

#include <QAbstractItemView>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTreeWidgetItem>

LayerTreeWidget::LayerTreeWidget(
    QWidget* parent
)
    : QTreeWidget(parent)
{
    // 当前构造函数只负责把 parent 交给父类 QTreeWidget。
    //
    // 拖放模式、选择模式等设置目前仍由 MainWindow 统一配置。
}

bool LayerTreeWidget::canDropCurrentItem(
    const QPoint& position
) const
{
    // 当前程序使用单选模式。
    //
    // 在内部拖动期间，currentItem() 就是用户正在拖动的节点。
    QTreeWidgetItem* draggedItem =
        currentItem();

    if (draggedItem == nullptr)
    {
        return false;
    }

    // 真实影像或高程图层都有一个分类父节点：
    //
    // ImageryLayer   -> Imagery Layers
    // ElevationLayer -> Elevation Layers
    //
    // Map 和分类节点本身不属于这种叶子结构。
    QTreeWidgetItem* sourceGroup =
        draggedItem->parent();

    if (sourceGroup == nullptr)
    {
        return false;
    }

    // 根据鼠标当前位置，找到它下面的树节点。
    QTreeWidgetItem* targetItem =
        itemAt(position);

    // 当前阶段不允许把图层拖到树的空白区域。
    if (targetItem == nullptr)
    {
        return false;
    }

    // dropIndicatorPosition() 表示 Qt 准备如何放置节点。
    //
    // 它可能返回：
    // OnItem：放到目标节点内部；
    // AboveItem：放到目标节点上方；
    // BelowItem：放到目标节点下方；
    // OnViewport：放到整个树的空白区域。
    switch (dropIndicatorPosition())
    {
        case QAbstractItemView::OnItem:
        {
            // 只有直接放到原来的分类节点上才允许。
            //
            // 例如：
            // ref.tif 可以放到 Imagery Layers 节点上，
            // 但不能放到某个其他 TIFF 上，
            // 否则一个真实图层可能成为另一个图层的子节点。
            return targetItem == sourceGroup;
        }

        case QAbstractItemView::AboveItem:
        case QAbstractItemView::BelowItem:
        {
            // 放到某个节点上方或下方时，
            // 目标节点必须和被拖节点拥有同一个父分类。
            //
            // 这样会自然形成以下限制：
            //
            // 影像 -> 只能放到其他影像的上方或下方；
            // DEM  -> 只能放到其他 DEM 的上方或下方。
            //
            // 因为影像和 DEM 的父分类不同，
            // 所以它们无法被拖入对方的分类。
            return targetItem->parent() == sourceGroup;
        }

        case QAbstractItemView::OnViewport:
        default:
        {
            // 当前阶段不允许放到图层树空白区域，
            // 也不接受无法识别的放置方式。
            return false;
        }
    }
}

void LayerTreeWidget::dragMoveEvent(
    QDragMoveEvent* event
)
{
    // 先让父类计算当前落点位置。
    //
    // canDropCurrentItem() 需要读取
    // dropIndicatorPosition() 的最新结果，
    // 所以这一步仍然需要先执行。
    QTreeWidget::dragMoveEvent(event);

    const bool canDrop =
        canDropCurrentItem(
            event->position().toPoint()
        );

    if (!canDrop)
    {
        // 非法落点不显示插入提示线，
        // 避免给用户造成“这里可以放置”的错觉。
        setDropIndicatorShown(false);

        // 拒绝当前拖放位置。
        //
        // Qt 的拖放系统收到 ignore() 后，
        // 通常会自动把拖动光标显示为禁止标志。
        event->ignore();

        // 请求立即刷新视口，清除上一位置可能残留的提示线。
        viewport()->update();
        return;
    }

    // 回到合法位置时，重新开启落点提示线。
    setDropIndicatorShown(true);

    // 接受当前内部移动操作。
    event->acceptProposedAction();

    // 立即刷新，让合法插入位置的提示线显示出来。
    viewport()->update();
}

void LayerTreeWidget::dropEvent(
    QDropEvent* event
)
{
    // 一次拖动即将结束。
    // 恢复默认的落点提示设置，为下一次拖动做好准备。
    setDropIndicatorShown(true);

    if (!canDropCurrentItem(
            event->position().toPoint()))
    {
        event->ignore();
        viewport()->update();
        return;
    }

    QTreeWidget::dropEvent(event);

    // 节点移动完成后刷新图层树。
    viewport()->update();
}
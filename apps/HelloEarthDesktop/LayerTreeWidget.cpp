#include "LayerTreeWidget.h"

#include <QAbstractItemView>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTreeWidgetItem>
#include <QSignalBlocker>
#include <QVariant>

LayerTreeWidget::LayerTreeWidget(
    QWidget* parent
)
    : QTreeWidget(parent)
{
    // 当前构造函数只负责把 parent 交给父类 QTreeWidget。
    //
    // 拖放模式、选择模式等设置目前仍由 MainWindow 统一配置。
}

void LayerTreeWidget::startDrag(
    Qt::DropActions supportedActions
)
{
    // 当前程序使用单选模式，
    // 因此 currentItem() 就是用户准备拖动的节点。
    QTreeWidgetItem* draggedItem =
        currentItem();

    if (draggedItem == nullptr)
    {
        return;
    }

    // 保存拖动开始前所属的分类节点。
    //
    // 当前允许的结构是：
    // ImageryLayer   -> ImageryGroup
    // ElevationLayer -> ElevationGroup
    QTreeWidgetItem* sourceGroup =
        draggedItem->parent();

    if (sourceGroup == nullptr)
    {
        return;
    }

    // 在 Qt 改变树结构之前，记录原来的分类内部索引。
    const int oldIndex =
        sourceGroup->indexOfChild(
            draggedItem
        );

    if (oldIndex < 0)
    {
        return;
    }

    // 为本次拖动生成唯一的临时编号。
    //
    // 编号从 1 开始不断增加，因此无效 QVariant 转换得到的 0
    // 不会和正常拖动编号发生冲突。
    const qulonglong dragToken =
        ++nextDragToken_;

    {
        // 设置临时 Role 时，QTreeWidget 可能发出 itemChanged 信号。
        //
        // 这个编号只服务于内部拖放识别，
        // 不代表图层显示状态或其他业务属性发生变化，
        // 因此临时阻止信号向外传播。
        const QSignalBlocker signalBlocker(
            this
        );

        draggedItem->setData(
            0,
            DragTokenRole,
            QVariant::fromValue(
                dragToken
            )
        );
    }

    // 交给 QTreeWidget 完整执行拖放流程。
    //
    // 这一行内部会经历：
    // 1. 创建拖动数据；
    // 2. 持续处理 dragMoveEvent()；
    // 3. 用户松开鼠标后调用 dropEvent()；
    // 4. 在新位置创建节点；
    // 5. 删除原位置的旧节点。
    //
    // 只有这个函数返回后，整个内部移动才真正完成。
    QTreeWidget::startDrag(
        supportedActions
    );

    // Qt 的内部移动可能采用“复制新节点，再删除旧节点”的方式，
    // 因此原来的 draggedItem 指针现在可能已经失效，
    // 绝对不能继续访问它。
    //
    // 我们改为在原分类中查找携带本次 dragToken 的最终节点。
    QTreeWidgetItem* movedItem = nullptr;
    int newIndex = -1;

    for (
        int childIndex = 0;
        childIndex < sourceGroup->childCount();
        ++childIndex
    )
    {
        QTreeWidgetItem* childItem =
            sourceGroup->child(
                childIndex
            );

        if (childItem == nullptr)
        {
            continue;
        }

        const qulonglong childDragToken =
            childItem
                ->data(
                    0,
                    DragTokenRole
                )
                .toULongLong();

        if (childDragToken == dragToken)
        {
            movedItem = childItem;
            newIndex = childIndex;
            break;
        }
    }

    // 没找到最终节点，说明本次拖动没有形成可识别的合法结果。
    if (
        movedItem == nullptr ||
        newIndex < 0
    )
    {
        viewport()->update();
        return;
    }

    {
        // 临时编号已经完成使命，将它从最终节点中清除。
        //
        // 同样阻止这次内部数据清理触发 itemChanged 业务逻辑。
        const QSignalBlocker signalBlocker(
            this
        );

        movedItem->setData(
            0,
            DragTokenRole,
            QVariant()
        );
    }

    // 如果用户取消拖动，或者最终仍落在原位置，
    // Qt 树与 osgEarth Map 都不需要调整。
    if (newIndex == oldIndex)
    {
        viewport()->update();
        return;
    }

    // 此时 Qt 的内部移动已经彻底完成：
    //
    // movedItem 是最终有效节点；
    // oldIndex 是拖动前的位置；
    // newIndex 是拖动后的真实位置。
    //
    // 现在才通知 MainWindow 同步 osgEarth 图层顺序。
    emit layerItemMoved(
        movedItem,
        oldIndex,
        newIndex
    );

    viewport()->update();
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
    // 上一次鼠标所在位置如果是非法落点，
    // 程序可能临时关闭了落点指示器。
    //
    // 因此每次处理新的鼠标位置前，都要先重新开启，
    // 让 QTreeWidget 能重新计算当前属于：
    // AboveItem、BelowItem、OnItem 还是 OnViewport。
    setDropIndicatorShown(true);

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
    // 一次拖动即将结束，
    // 恢复落点提示器，为下一次拖动做好准备。
    setDropIndicatorShown(true);

    // 在真正修改树结构前，再进行一次最终验证。
    if (!canDropCurrentItem(
            event->position().toPoint()))
    {
        event->ignore();
        viewport()->update();
        return;
    }

    // 这里只负责让 QTreeWidget 执行放置操作。
    //
    // 不在这里读取 currentItem()、计算 newIndex 或发送信号，
    // 因为这个函数执行期间，Qt 的内部移动可能尚未全部结束。
    QTreeWidget::dropEvent(
        event
    );

    viewport()->update();
}
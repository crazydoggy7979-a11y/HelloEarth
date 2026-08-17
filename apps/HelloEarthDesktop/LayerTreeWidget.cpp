#include "LayerTreeWidget.h"

#include <QAbstractItemView>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTreeWidgetItem>
#include <QSignalBlocker>
#include <QVariant>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

#include <QDragLeaveEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>

LayerTreeWidget::LayerTreeWidget(
    QWidget* parent
)
    : QTreeWidget(parent)
{
    // 当前构造函数只负责把 parent 交给父类 QTreeWidget。
    //
    // 拖放模式、选择模式等设置目前仍由 MainWindow 统一配置。
}

void LayerTreeWidget::clearExternalDropIndicator()
{
    // 如果提示线本来就没有显示，
    // 不需要重复刷新图层树。
    if (!externalDropIndicatorVisible_)
    {
        return;
    }

    // 关闭提示线显示状态。
    externalDropIndicatorVisible_ = false;

    // 清理上一次记录的绘制坐标。
    externalDropIndicatorY_ = 0;
    externalDropIndicatorLeft_ = 0;

    // 请求重新绘制图层树显示区域。
    //
    // paintEvent() 再次执行时发现 visible 为 false，
    // 就不会继续绘制外部文件插入提示线。
    viewport()->update();
}

bool LayerTreeWidget::containsSingleSupportedRasterFile(
    const QMimeData* mimeData
) const
{
    // 拖放事件必须携带有效的 MIME 数据。
    if (mimeData == nullptr)
    {
        return false;
    }

    // Windows 资源管理器拖入的文件通常以 URL 列表形式保存。
    //
    // 如果没有 URL，拖入内容可能只是普通文本或其他数据，
    // 当前图层树不接受这些内容。
    if (!mimeData->hasUrls())
    {
        return false;
    }

    const QList<QUrl> urls =
        mimeData->urls();

    // 当前阶段一次只允许拖入一个文件。
    //
    // 这样可以让一个文件明确对应一个目标位置，
    // 避免多个文件的插入顺序和加载失败处理变得复杂。
    if (urls.size() != 1)
    {
        return false;
    }

    const QUrl fileUrl =
        urls.first();

    // 拒绝网络 URL。
    //
    // 当前只处理 Windows 文件系统中的本地数据，
    // 例如：
    // file:///D:/data/image.tif
    if (!fileUrl.isLocalFile())
    {
        return false;
    }

    const QFileInfo fileInfo(
        fileUrl.toLocalFile()
    );

    // 文件必须真实存在。
    if (!fileInfo.exists())
    {
        return false;
    }

    // 拒绝文件夹，只允许普通文件。
    if (!fileInfo.isFile())
    {
        return false;
    }

    // 当前进程必须能够读取该文件。
    if (!fileInfo.isReadable())
    {
        return false;
    }

    // suffix() 返回不带点号的文件后缀。
    //
    // 转成小写后，可以同时兼容：
    // image.tif
    // image.TIF
    // image.tiff
    // image.TIFF
    const QString suffix =
        fileInfo
            .suffix()
            .toLower();

    return
        suffix == QStringLiteral("tif") ||
        suffix == QStringLiteral("tiff");
}

bool LayerTreeWidget::resolveExternalDropTarget(
    const QPoint& position,
    QTreeWidgetItem*& targetGroupItem,
    int& insertionIndex
) const
{
    // 先把输出参数初始化为无效状态。
    //
    // 这样即使函数在中途返回 false，
    // 调用者也不会误用上一次遗留的指针或索引。
    targetGroupItem = nullptr;
    insertionIndex = -1;

    // 根据鼠标在图层树中的局部坐标，
    // 查找鼠标当前位置对应的树节点。
    QTreeWidgetItem* targetItem =
        itemAt(position);

    // 当前不接受把文件放到图层树的空白区域。
    //
    // 未来支持多个 Map 时，
    // 可以再考虑在空白区域弹出 Map 选择窗口。
    if (targetItem == nullptr)
    {
        return false;
    }

    // dropIndicatorPosition() 是 QAbstractItemView 提供的结果，
    // 表示 Qt 当前准备怎样放置被拖动的数据。
    const QAbstractItemView::DropIndicatorPosition
        indicatorPosition =
            dropIndicatorPosition();

    switch (indicatorPosition)
    {
        case QAbstractItemView::OnItem:
        {
            // 外部文件不能直接放到任何节点表面。
            //
            // 分类节点只是一个逻辑容器，
            // 真实图层节点也不能拥有子节点。
            //
            // 只有出现明确的上方或下方插入线时，
            // 用户才能松开鼠标完成加载。
            return false;
        }

        case QAbstractItemView::AboveItem:
        case QAbstractItemView::BelowItem:
        {
            QTreeWidgetItem* targetParentItem =
                targetItem->parent();

            // 判断 targetItem 本身是不是分类节点。
            //
            // 当前树结构：
            //
            // Map                         parent == nullptr
            // ├── Imagery Layers         parent 是 Map
            // │   └── image.tif          parent 是分类
            // └── Elevation Layers
            //
            // 所以分类节点满足：
            // 1. 自己有父节点；
            // 2. 父节点 Map 没有父节点。
            const bool targetIsGroup =
                targetParentItem != nullptr &&
                targetParentItem->parent() == nullptr;

            if (targetIsGroup)
            {
                // 分类节点只接受它下方的插入线。
                //
                // 这条线表示把新图层放到该分类的第一个位置。
                // 分类节点上方的线属于 Map 层级，不接受。
                if (
                    indicatorPosition !=
                    QAbstractItemView::BelowItem
                )
                {
                    return false;
                }

                targetGroupItem = targetItem;
                insertionIndex = 0;

                return true;
            }

            // 如果目标不是分类节点，
            // 那么它应当是分类下面的真实图层叶子。
            QTreeWidgetItem* groupItem =
                targetParentItem;

            if (groupItem == nullptr)
            {
                return false;
            }

            // 真实图层所属分类的父节点必须是 Map。
            QTreeWidgetItem* mapItem =
                groupItem->parent();

            if (
                mapItem == nullptr ||
                mapItem->parent() != nullptr
            )
            {
                return false;
            }

            const int targetIndex =
                groupItem->indexOfChild(
                    targetItem
                );

            if (targetIndex < 0)
            {
                return false;
            }

            targetGroupItem = groupItem;

            if (
                indicatorPosition ==
                QAbstractItemView::AboveItem
            )
            {
                // 放到目标图层上方。
                insertionIndex =
                    targetIndex;
            }
            else
            {
                // 放到目标图层下方。
                insertionIndex =
                    targetIndex + 1;
            }

            return true;
        }

        case QAbstractItemView::OnViewport:
        default:
        {
            // 禁止放到空白区域或其他无法识别的位置。
            return false;
        }
    }
}

void LayerTreeWidget::dragEnterEvent(
    QDragEnterEvent* event
)
{
    if (event == nullptr)
    {
        return;
    }

    // 如果拖动来源就是当前 LayerTreeWidget，
    // 说明用户正在调整已有图层的内部顺序。
    //
    // 内部拖动仍然交给 QTreeWidget 原有机制处理，
    // 不能拿外部 TIFF 文件的规则去检查它。
    if (event->source() == this)
    {
        QTreeWidget::dragEnterEvent(
            event
        );

        return;
    }

    // 如果拖动来源不是 this，
    // 就认为它是来自 Windows 资源管理器等外部程序的数据。
    //
    // 只有一个可读取的本地 TIFF 文件才允许进入图层树。
    if (!containsSingleSupportedRasterFile(
            event->mimeData()))
    {
        event->ignore();
        return;
    }

    // 当前只表示“文件允许进入图层树区域”。
    //
    // 文件最终能不能放在鼠标所在的位置，
    // 还需要由后续 dragMoveEvent() 判断。
    event->acceptProposedAction();
}

void LayerTreeWidget::dragLeaveEvent(
    QDragLeaveEvent* event
)
{
    // 鼠标带着外部文件离开图层树后，
    // 上一次合法位置的提示线必须立即消失。
    clearExternalDropIndicator();

    // 继续让 QTreeWidget 处理它自身的离开逻辑。
    QTreeWidget::dragLeaveEvent(
        event
    );
}

void LayerTreeWidget::paintEvent(
    QPaintEvent* event
)
{
    // 首先让 QTreeWidget 完成正常绘制：
    //
    // 1. 节点文字；
    // 2. 展开和折叠箭头；
    // 3. 复选框；
    // 4. 选中背景；
    // 5. Qt 内部拖动提示。
    QTreeWidget::paintEvent(
        event
    );

    // 没有外部文件合法落点时，
    // 不绘制额外的提示线。
    if (!externalDropIndicatorVisible_)
    {
        return;
    }

    QWidget* treeViewport =
        viewport();

    if (treeViewport == nullptr)
    {
        return;
    }

    // 提示线必须位于当前可见区域内。
    if (
        externalDropIndicatorY_ < 0 ||
        externalDropIndicatorY_ >=
            treeViewport->height()
    )
    {
        return;
    }

    // 在右侧保留少量空白，
    // 避免提示线紧贴滚动区域边缘。
    const int rightPosition =
        treeViewport->width() - 4;

    if (
        externalDropIndicatorLeft_ >=
        rightPosition
    )
    {
        return;
    }

    // QPainter 用于在 Qt 控件表面执行二维绘制。
    //
    // 这里必须画在 viewport() 上，
    // 因为 QTreeWidget 的树节点实际显示在 viewport 中。
    QPainter painter(
        treeViewport
    );

    // 使用当前 Qt 主题的高亮颜色。
    //
    // 这样在不同 Windows 主题下，
    // 提示线颜色仍然能与选中颜色保持协调。
    QPen indicatorPen(
        palette().color(
            QPalette::Highlight
        )
    );

    // 两像素宽度比普通单像素线更容易识别。
    indicatorPen.setWidth(2);

    painter.setPen(
        indicatorPen
    );

    painter.drawLine(
        externalDropIndicatorLeft_,
        externalDropIndicatorY_,
        rightPosition,
        externalDropIndicatorY_
    );
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
    if (event == nullptr)
    {
        return;
    }

    const bool isInternalDrag =
        event->source() == this;

    // 如果用户正在拖动树内已有节点，
    // 就清除外部文件专用提示线。
    //
    // 内部拖动继续使用 Qt 自带的提示线。
    if (isInternalDrag)
    {
        clearExternalDropIndicator();
    }

    // 先开启 Qt 提示器，让父类计算当前属于
    // OnItem、AboveItem、BelowItem 还是 OnViewport。
    setDropIndicatorShown(true);

    QTreeWidget::dragMoveEvent(
        event
    );

    bool canDrop = false;

    QTreeWidgetItem* externalTargetGroupItem =
        nullptr;

    int externalInsertionIndex = -1;

    if (isInternalDrag)
    {
        // 图层树内部排序仍然采用原有验证规则。
        canDrop =
            canDropCurrentItem(
                event->position().toPoint()
            );
    }
    else
    {
        // 外部文件首先必须通过 TIFF 文件检查。
        if (containsSingleSupportedRasterFile(
                event->mimeData()))
        {
            // 计算目标分类和插入索引。
            canDrop =
                resolveExternalDropTarget(
                    event->position().toPoint(),
                    externalTargetGroupItem,
                    externalInsertionIndex
                );
        }

        if (canDrop)
        {
            QTreeWidgetItem* targetItem =
                itemAt(
                    event->position().toPoint()
                );

            if (targetItem == nullptr)
            {
                canDrop = false;
            }
            else
            {
                // 获取目标节点当前在 viewport 中占据的矩形区域。
                //
                // top() 是节点上边缘；
                // bottom() 是节点下边缘；
                // left() 包含当前树层级的缩进信息。
                const QRect targetRectangle =
                    visualItemRect(
                        targetItem
                    );

                if (!targetRectangle.isValid())
                {
                    canDrop = false;
                }
                else
                {
                    const auto indicatorPosition =
                        dropIndicatorPosition();

                    if (
                        indicatorPosition ==
                        QAbstractItemView::AboveItem
                    )
                    {
                        // 拖到真实图层上方时，
                        // 提示线画在该节点的上边缘。
                        externalDropIndicatorY_ =
                            targetRectangle.top();
                    }
                    else if (
                        indicatorPosition ==
                        QAbstractItemView::BelowItem
                    )
                    {
                        // 拖到分类节点或真实图层下方时，
                        // 提示线画在该节点的下边缘。
                        externalDropIndicatorY_ =
                            targetRectangle.bottom();
                    }
                    else
                    {
                        // resolveExternalDropTarget() 正常情况下
                        // 已经拒绝 OnItem 和 OnViewport。
                        //
                        // 这里再次防御性验证。
                        canDrop = false;
                    }

                    if (canDrop)
                    {
                        // 如果 targetItem 就是目标分类节点，
                        // 说明当前提示线是“分类下方、第一图层上方”。
                        //
                        // 将横向起点增加一个树缩进，
                        // 让线看起来属于分类内部，而不是 Map 层级。
                        if (
                            targetItem ==
                            externalTargetGroupItem
                        )
                        {
                            externalDropIndicatorLeft_ =
                                targetRectangle.left() +
                                indentation();
                        }
                        else
                        {
                            // 目标是真实图层时，
                            // 直接使用该图层矩形自身的左侧缩进。
                            externalDropIndicatorLeft_ =
                                targetRectangle.left();
                        }

                        // 避免异常样式计算出负数横坐标。
                        if (
                            externalDropIndicatorLeft_ < 4
                        )
                        {
                            externalDropIndicatorLeft_ = 4;
                        }

                        externalDropIndicatorVisible_ =
                            true;
                    }
                }
            }
        }

        if (!canDrop)
        {
            // 外部文件当前位置不合法，
            // 清除上一个合法位置留下的自定义提示线。
            clearExternalDropIndicator();
        }
    }

    if (!canDrop)
    {
        // 非法位置不显示 Qt 原生提示。
        setDropIndicatorShown(false);

        event->ignore();
        viewport()->update();

        return;
    }

    if (isInternalDrag)
    {
        // 内部图层排序继续使用 Qt 原生插入线。
        setDropIndicatorShown(true);
    }
    else
    {
        // 外部 TIFF 使用我们刚刚计算的自定义提示线。
        //
        // 关闭 Qt 原生提示，避免出现两条线或错误的节点高亮。
        setDropIndicatorShown(false);
    }

    event->acceptProposedAction();

    // 触发 paintEvent()，
    // 绘制最新位置的外部文件提示线。
    viewport()->update();
}

void LayerTreeWidget::dropEvent(
    QDropEvent* event
)
{
    if (event == nullptr)
    {
        return;
    }

    // 如果拖动来源就是当前 LayerTreeWidget，
    // 说明用户正在调整已有图层节点的顺序。
    if (event->source() == this)
    {
        // 内部节点移动不使用外部文件专用提示线。
        clearExternalDropIndicator();

        // 内部移动继续使用 Qt 原生落点提示线。
        setDropIndicatorShown(true);

        // 在真正修改树结构前，
        // 再进行一次内部图层落点验证。
        if (!canDropCurrentItem(
                event->position().toPoint()))
        {
            event->ignore();
            viewport()->update();
            return;
        }

        // 内部拖动必须交给 QTreeWidget 处理，
        // 因为它需要真正改变树节点的位置。
        //
        // 父类完成节点移动后，
        // startDrag() 才会通过 DragToken 找到移动后的节点，
        // 并发出 layerItemMoved 信号。
        QTreeWidget::dropEvent(
            event
        );

        viewport()->update();
        return;
    }

    // 运行到这里，说明当前是从 Windows 资源管理器等
    // 外部程序拖入的文件。
    //
    // 鼠标已经松开，外部文件提示线应当立即消失。
    clearExternalDropIndicator();

    // 外部文件不使用 Qt 原生提示线。
    //
    // 如果这里继续保持 true，
    // Qt 自身残留的落点状态可能继续绘制一条提示线。
    setDropIndicatorShown(false);

    // 即使 dragEnterEvent() 和 dragMoveEvent()
    // 已经验证过，正式放下时仍然再次检查文件。
    if (!containsSingleSupportedRasterFile(
            event->mimeData()))
    {
        event->ignore();
        viewport()->update();
        return;
    }

    QTreeWidgetItem* targetGroupItem =
        nullptr;

    int insertionIndex = -1;

    // 根据用户松开鼠标时的最终位置，
    // 计算目标分类和分类内部插入索引。
    if (!resolveExternalDropTarget(
            event->position().toPoint(),
            targetGroupItem,
            insertionIndex))
    {
        event->ignore();
        viewport()->update();
        return;
    }

    const QList<QUrl> urls =
        event->mimeData()->urls();

    // 当前一次只允许拖入一个文件。
    //
    // containsSingleSupportedRasterFile() 已经检查过，
    // 这里再次验证是为了保证当前函数自身可靠。
    if (urls.size() != 1)
    {
        event->ignore();
        viewport()->update();
        return;
    }

    const QUrl fileUrl =
        urls.first();

    const QFileInfo fileInfo(
        fileUrl.toLocalFile()
    );

    // 使用绝对路径，
    // 避免后续加载结果依赖程序当前工作目录。
    const QString absoluteFilePath =
        fileInfo.absoluteFilePath();

    if (absoluteFilePath.isEmpty())
    {
        event->ignore();
        viewport()->update();
        return;
    }

    // 外部文件拖入采用复制语义。
    //
    // 这只表示程序读取原始文件，
    // 不会移动、复制或删除磁盘上的 TIFF。
    event->setDropAction(
        Qt::CopyAction
    );

    event->accept();

    // 在发出加载信号前，再确保两种提示线都处于关闭状态。
    clearExternalDropIndicator();
    setDropIndicatorShown(false);

    // repaint() 会立即执行一次重绘。
    //
    // 这里不能只依赖 update()：
    // update() 只是把重绘请求放入 Qt 事件队列。
    //
    // externalRasterFileDropped 信号使用同线程直接连接，
    // 发出信号后会立即进入金字塔检查、GDAL 打开和视点计算，
    // 这些操作可能需要一些时间。
    //
    // 使用 repaint() 可以确保提示线先从界面上消失，
    // 然后再开始实际加载。
    viewport()->repaint();

    // 通知 MainWindow：
    //
    // 1. 用户拖入了哪个文件；
    // 2. 文件要进入哪个分类；
    // 3. 加载成功后应当插入哪个位置。
    emit externalRasterFileDropped(
        absoluteFilePath,
        targetGroupItem,
        insertionIndex
    );

    // 信号处理完成后再次保证提示器关闭，
    // 并请求后续正常重绘。
    clearExternalDropIndicator();
    setDropIndicatorShown(false);
    viewport()->update();
}
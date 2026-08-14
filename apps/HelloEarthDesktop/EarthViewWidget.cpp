#include "EarthViewWidget.h"
#include <unordered_set>
#include <cmath>

#include <algorithm>
#include <iostream>

#include <QSurfaceFormat>
#include <QTimer>
#include <QMouseEvent>
#include <osgGA/GUIEventAdapter>
#include <QWheelEvent>
#include <QFileInfo>

#include <osg/Camera>
#include <osgEarth/GDAL>
#include <osgEarth/GLUtils>
#include <osgEarth/VisibleLayer>

#include <HelloEarth/raster/RasterPreprocessor.h>
#include <HelloEarth/navigation/ViewpointCalculator.h>

namespace
{
    // 将 Qt 的鼠标按键枚举转换为 OSG 使用的按键编号。
    //
    // OSG 约定：
    // 1 表示左键，2 表示中键，3 表示右键。
    unsigned int convertMouseButtonToOsg(
        Qt::MouseButton qtButton
    )
    {
        switch (qtButton)
        {
        case Qt::LeftButton:
            return 1;

        case Qt::MiddleButton:
            return 2;

        case Qt::RightButton:
            return 3;

        default:
            // 0 表示当前按键不进行转发。
            return 0;
        }
    }
}

EarthViewWidget::EarthViewWidget(QWidget* parent)
    : QOpenGLWidget(parent){
    // 设置希望 Qt 为该控件创建的 OpenGL Context 格式。
    QSurfaceFormat format;

    // 为三维场景申请 24 位深度缓冲区。
    // 后续 osgEarth 会利用深度缓冲判断物体的前后遮挡关系。
    format.setDepthBufferSize(24);

    // 为三维场景申请 8 位模板缓冲区。
    // 当前暂时不用，但部分复杂渲染效果可能需要它。
    format.setStencilBufferSize(8);

    // 必须在控件的 OpenGL Context 创建之前设置格式。
    setFormat(format);

    // 每次更新时重绘整个三维区域。
    //
    // QOpenGLWidget 始终使用 Qt 管理的 Framebuffer，
    // NoPartialUpdate 表示不要求保留上一帧未重绘部分的内容。
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    // 创建三维画面的刷新定时器。
    //
    // this 被设置为父对象，因此 EarthViewWidget 销毁时，
    // Qt 会自动销毁这个定时器。
    renderTimer_ = new QTimer(this);

    // 使用精度较高的定时器类型，尽量减少刷新间隔的波动。
    renderTimer_->setTimerType(Qt::PreciseTimer);

    // 每次定时器到期时，请求 Qt 更新三维控件。
    //
    // update() 不会立即调用 paintGL()，
    // 它只是向 Qt 事件循环提交一次重绘请求。
    // Qt 会在 OpenGL Context 正确激活后调用 paintGL()。
    connect(
        renderTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            update();
        }
    );

    // 大约每 16 毫秒请求一次刷新，接近每秒 60 帧。
    renderTimer_->start(16);
}

EarthViewWidget::~EarthViewWidget()
{
    // 停止产生新的三维窗口刷新请求，
    // 避免在释放 Viewer 的过程中继续请求绘制。
    if (renderTimer_)
    {
        renderTimer_->stop();
    }
    // 如果 Qt 已经为这个控件创建了 OpenGL Context，
    // 就在释放 OSG 图形资源前重新激活它。
    //
    // 部分 OSG 对象在析构时可能需要释放显卡资源，
    // 因此最好保证此时存在有效的 OpenGL Context。
    const bool hasOpenGLContext = context() != nullptr;

    if (hasOpenGLContext)
    {
        makeCurrent();
    }

    if (viewer_)
    {
        // 停止后续渲染。
        viewer_->setDone(true);

        // 解除 Viewer 对操纵器和地图场景的引用。
        viewer_->setCameraManipulator(nullptr);
        viewer_->setSceneData(nullptr);
    }

    // 在 OpenGL Context 仍然有效时释放 osgEarth 对象。
    manipulator_ = nullptr;
    mapNode_ = nullptr;

    viewer_ = nullptr;
    graphicsWindow_ = nullptr;

    if (hasOpenGLContext)
    {
        doneCurrent();
    }
}

bool EarthViewWidget::addImageLayer(const QString& sourcePath)
{
    // 空路径通常表示用户没有选择文件，
    // 或者文件选择窗口被取消。
    if (sourcePath.isEmpty())
    {
        std::cerr
            << "Cannot add imagery: source path is empty."
            << std::endl;

        return false;
    }

    // MapNode 必须已经初始化完成。
    //
    // 如果在 initializeGL() 创建 MapNode 之前调用该函数，
    // 当前还不存在能够接收图层的 osgEarth Map。
    if (!mapNode_ || !mapNode_->getMap())
    {
        std::cerr
            << "Cannot add imagery: map is not initialized."
            << std::endl;

        return false;
    }

    // 将 Qt 字符串转换为 UTF-8 编码的 std::string。
    //
    // Qt 的文件选择窗口和拖放事件提供 QString；
    // 当前栅格预处理模块接收的是 std::string。
    const std::string sourcePathString =
        sourcePath.toUtf8().toStdString();

    // 从用户选择的原始路径中提取文件名。
    //
    // 即使预处理最终让 osgEarth 读取一个隔离 VRT，
    // Layers Dock 仍然显示用户最初选择的数据文件名。
    const QString layerDisplayName =
        QFileInfo(sourcePath).fileName();

    // 接收预处理后真正应该交给 osgEarth 的路径。
    //
    // 它可能仍然是原始 TIFF，
    // 也可能是程序为内部金字塔问题生成的 VRT。
    std::string preparedImagePath;

    // 执行已有的栅格预处理闭环：
    //
    // 1. 检查栅格基本信息；
    // 2. 检查内部或外部金字塔；
    // 3. 必要时构建外部 OVR；
    // 4. 必要时创建隔离 VRT；
    // 5. 返回最终允许 osgEarth 加载的路径。
    const bool imagePrepared =
        HelloEarth::Raster::prepareRasterForLoading(
            sourcePathString,
            preparedImagePath
        );

    if (!imagePrepared)
    {
        std::cerr
            << "Image preprocessing failed: "
            << sourcePathString
            << std::endl;

        return false;
    }

    // 使用 osg::ref_ptr 暂时持有新图层。
    //
    // Map 添加成功后也会持有该图层；
    // 如果中途失败，ref_ptr 可以安全管理对象生命周期。
    osg::ref_ptr<osgEarth::GDALImageLayer> imageryLayer =
        new osgEarth::GDALImageLayer();

    // osgEarth 内部图层名称与 Layers Dock 显示名称保持一致。
    //
    // 这里使用用户最初选择的文件名，而不是自动生成的 VRT 文件名。
    imageryLayer->setName(
        layerDisplayName
            .toUtf8()
            .toStdString()
    );

    // 真正的数据地址必须使用预处理结果。
    imageryLayer->setURL(
        preparedImagePath
    );

    // 当前 Map 已经在函数前面验证过存在。
    osgEarth::Map* map =
        mapNode_->getMap();

    // 在图层独立打开前，先使用当前 Map 的读取选项。
    //
    // 这样图层使用的插件、缓存和资源读取环境，
    // 与之后正式加入 Map 时保持一致。
    imageryLayer->setReadOptions(
        map->getReadOptions()
    );

    // 主动打开图层，但暂时不把它加入 Map。
    //
    // open() 会让 GDALImageLayer 读取数据源、空间参考和数据范围，
    // 因此后面可以在图层尚未参与渲染时计算 Viewpoint。
    const osgEarth::Status openStatus =
        imageryLayer->open();

    if (openStatus.isError())
    {
        std::cerr
            << "Failed to open prepared image before adding it to Map: "
            << openStatus.toString()
            << std::endl;

        return false;
    }

    std::cout
        << "Image opened successfully before adding to Map: "
        << preparedImagePath
        << std::endl;

    // 图层虽然尚未加入 Map，但已经成功打开，
    // 因此现在可以读取它的 DataExtent 并计算目标视点。
    const auto layerViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *imageryLayer
        );

    // 把“正式加入 Map”的操作组织成一个局部函数。
    //
    // 默认全球影像可以立即调用它；
    // 用户添加的局部影像则在相机飞行结束后调用它。
    const auto attachImageryLayerToMap =
        [this, imageryLayer, layerDisplayName]() -> bool
        {
            if (
                !mapNode_ ||
                !mapNode_->getMap()
            )
            {
                std::cerr
                    << "Cannot attach imagery layer: "
                    << "Map is no longer available."
                    << std::endl;

                return false;
            }

            osgEarth::Map* currentMap =
                mapNode_->getMap();

            // 到达这一行时，局部影像才真正进入 Map，
            // 并开始参与 Terrain Engine 和影像瓦片渲染。
            currentMap->addLayer(
                imageryLayer.get()
            );

            if (imageryLayer->getStatus().isError())
            {
                std::cerr
                    << "Failed to attach opened image layer to Map: "
                    << imageryLayer->getStatus().toString()
                    << std::endl;

                currentMap->removeLayer(
                    imageryLayer.get()
                );

                return false;
            }

            std::cout
                << "Image added to Map: "
                << imageryLayer->getName()
                << std::endl;

            // 只有图层真正加入 Map 后，
            // 才通知 MainWindow 创建对应的 Tree 节点。
            emit this->imageryLayerAdded(
                currentMap->getUID(),
                imageryLayer->getUID(),
                layerDisplayName
            );

            update();

            return true;
        };

    // 无法计算视点时，不能执行“先飞行再加载”。
    // 这种情况下保留容错能力，直接把图层加入 Map。
    if (!layerViewpoint)
    {
        std::cerr
            << "Image opened, but its viewpoint "
            << "could not be calculated: "
            << preparedImagePath
            << std::endl;

        return attachImageryLayerToMap();
    }

    std::cout
        << "Calculated image viewpoint before adding to Map: "
        << layerViewpoint->toString()
        << std::endl;

    if (!layerViewpoint)
    {
        std::cerr
            << "Image opened, but its viewpoint "
            << "could not be calculated: "
            << preparedImagePath
            << std::endl;

        return attachImageryLayerToMap();
    }

    std::cout
        << "Calculated image viewpoint before adding to Map: "
        << layerViewpoint->toString()
        << std::endl;

    // 根据该影像的推荐观察距离，
    // 设置影像允许显示的最大相机距离。
    //
    // 局部影像在全球视角下会自动退出渲染；
    // 回到影像附近后会自动重新显示。
    if (layerViewpoint->range().isSet())
    {
        const double viewpointRangeMeters =
            layerViewpoint
                ->range()
                ->as(
                    osgEarth::Units::METERS
                );

        // 学习阶段先使用推荐观察距离的 4 倍。
        //
        // 以后可以把这个倍率放到程序设置中，
        // 或者根据影像分辨率和范围进一步计算。
        const double maximumVisibleRangeMeters =
            viewpointRangeMeters * 4.0;

        if (
            std::isfinite(maximumVisibleRangeMeters) &&
            maximumVisibleRangeMeters > 0.0
        )
        {
            imageryLayer->setMaxVisibleRange(
                static_cast<float>(
                    maximumVisibleRangeMeters
                )
            );

            std::cout
                << "Image maximum visible range: "
                << maximumVisibleRangeMeters
                << " meters"
                << std::endl;
        }
    }

    // 程序启动加载默认全球影像时，
    // EarthManipulator 可能还没有创建。
    //
    // 此时没有“从当前视角飞向数据”的需求，直接加入 Map。
    if (!manipulator_)
    {
        return attachImageryLayerToMap();
    }

    // 此时局部影像已经打开并计算出 Viewpoint，
    // 但仍然没有加入 Map，也不会参与飞行过程中的图层调度。
    manipulator_->setViewpoint(
        *layerViewpoint,

        // 相机飞行动画时间，单位为秒。
        0.5
    );

    // 创建一个临时定时器，定期检查相机飞行动画是否完成。
    //
    // 这个定时器不会调用 viewer_->frame()。
    // 现有 renderTimer_ 会继续通过 paintGL() 驱动 Viewer 渲染。
    auto* viewpointWaitTimer =
        new QTimer(this);

    viewpointWaitTimer->setInterval(
        16
    );

    viewpointWaitTimer->setTimerType(
        Qt::PreciseTimer
    );

    connect(
        viewpointWaitTimer,
        &QTimer::timeout,
        this,
        [
            this,
            viewpointWaitTimer,
            attachImageryLayerToMap
        ]()
        {
            // true 表示 EarthManipulator 仍在执行
            // setViewpoint() 创建的视点过渡动画。
            if (
                manipulator_ &&
                manipulator_->isSettingViewpoint()
            )
            {
                return;
            }

            // 相机飞行已经结束，停止检查。
            viewpointWaitTimer->stop();

            // 现在才把局部影像真正加入 Map。
            const bool layerAdded =
                attachImageryLayerToMap();

            if (!layerAdded)
            {
                qWarning()
                    << "The camera transition finished, "
                    << "but the image could not be added to Map.";
            }

            // 定时器使命已经完成。
            //
            // 使用 deleteLater()，让 Qt 在当前事件处理结束后
            // 安全销毁对象。
            viewpointWaitTimer->deleteLater();
        }
    );

    viewpointWaitTimer->start();

    // 请求 Qt 尽快开始绘制相机飞行动画。
    update();

    // 此处的 true 表示加载请求已经成功建立：
    // 图层已打开、Viewpoint 已计算、相机飞行已经启动。
    //
    // 图层真正加入 Map 的动作将在飞行结束后发生。
    return true;
}

bool EarthViewWidget::addElevationLayer(const QString& sourcePath)
{
    // 1. 路径为空时无法加载数据。
    if (sourcePath.isEmpty())
    {
        qWarning() << "Cannot add elevation layer: the source path is empty.";
        return false;
    }

    // 2. 高程图层最终必须加入 Map。
    //    如果 MapNode 尚未创建，说明 osgEarth 场景还没有初始化完成。
    if (!mapNode_ || !mapNode_->getMap())
    {
        qWarning() << "Cannot add elevation layer: MapNode is not ready.";
        return false;
    }

    // 3. osgEarth 和底层 GDAL 使用 std::string 路径。
    //    通过 UTF-8 转换，尽量兼容包含中文的文件路径。
    const std::string sourcePathString =
        sourcePath.toUtf8().toStdString();

    // 4. 图层树中显示原始 DEM 文件名。
    //    即使预处理后实际加载的是 VRT，也不让用户看到内部代理文件名。
    const QString layerDisplayName =
        QFileInfo(sourcePath).fileName();

    // 5. 在交给 osgEarth 之前，先执行我们已有的栅格预处理流程。
    //    这里可能检查或构建金字塔，也可能返回一个隔离用的 VRT 路径。
    std::string preparedElevationPath;

    if (!HelloEarth::Raster::prepareRasterForLoading(
            sourcePathString,
            preparedElevationPath))
    {
        qWarning() << "Elevation preprocessing failed:"
                   << sourcePath;
        return false;
    }

    // 6. 创建 osgEarth 的 GDAL 高程图层。
    //    ImageLayer 提供影像颜色，而 ElevationLayer 提供地表高度。
    osg::ref_ptr<osgEarth::GDALElevationLayer> elevationLayer =
        new osgEarth::GDALElevationLayer();

    // 对外仍使用原始 DEM 文件名作为图层名称。
    elevationLayer->setName(
        layerDisplayName.toUtf8().toStdString());

    // 实际读取预处理后确定的 TIFF 或 VRT。
    elevationLayer->setURL(preparedElevationPath);

    // 当前 Map 已经在函数开头验证过存在。
    osgEarth::Map* map =
        mapNode_->getMap();

    // 在 DEM 独立打开之前，使用当前 Map 的读取选项。
    //
    // 这样插件、缓存以及资源读取环境，
    // 与 DEM 后续正式加入 Map 时保持一致。
    elevationLayer->setReadOptions(
        map->getReadOptions()
    );

    // 主动打开 DEM，但暂时不把它加入 Map。
    //
    // open() 会读取高程数据源、空间参考和数据范围，
    // 因此后面能够提前计算 Viewpoint。
    //
    // 此时 DEM 还不会参与 Terrain Engine 的地形构建。
    const osgEarth::Status openStatus =
        elevationLayer->open();

    if (openStatus.isError())
    {
        std::cerr
            << "Failed to open prepared elevation before "
            << "adding it to Map: "
            << openStatus.toString()
            << std::endl;

        return false;
    }

    std::cout
        << "Elevation opened successfully before adding to Map: "
        << preparedElevationPath
        << std::endl;

    // DEM 已经成功打开，所以即使它尚未进入 Map，
    // 也可以从 DataExtent 中计算目标 Viewpoint。
    const auto layerViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *elevationLayer
        );

    // 把 DEM 正式加入 Map 的操作组织成局部函数。
    //
    // 无法计算 Viewpoint 时可以直接调用；
    // 正常情况下在相机飞行结束后调用。
    const auto attachElevationLayerToMap =
        [this, elevationLayer, layerDisplayName]() -> bool
        {
            if (
                !mapNode_ ||
                !mapNode_->getMap()
            )
            {
                std::cerr
                    << "Cannot attach elevation layer: "
                    << "Map is no longer available."
                    << std::endl;

                return false;
            }

            osgEarth::Map* currentMap =
                mapNode_->getMap();

            // 到达这里时，DEM 才真正进入 Map。
            //
            // 从这一刻开始，Terrain Engine 才会把该 DEM
            // 纳入地形高程采样和地表网格构建过程。
            currentMap->addLayer(
                elevationLayer.get()
            );

            if (elevationLayer->getStatus().isError())
            {
                std::cerr
                    << "Failed to attach opened elevation "
                    << "layer to Map: "
                    << elevationLayer
                        ->getStatus()
                        .toString()
                    << std::endl;

                currentMap->removeLayer(
                    elevationLayer.get()
                );

                return false;
            }

            std::cout
                << "Elevation added to Map: "
                << elevationLayer->getName()
                << std::endl;

            // 只有 DEM 真正加入 Map 后，
            // 才通知 MainWindow 创建 Elevation Layers 下的树节点。
            emit this->elevationLayerAdded(
                currentMap->getUID(),
                elevationLayer->getUID(),
                layerDisplayName
            );

            // DEM 加入后地形发生变化，请求重新绘制。
            update();

            return true;
        };

    // 如果无法计算 DEM 的 Viewpoint，
    // 就无法执行“先飞行、后加入”的正常流程。
    //
    // 为了保留容错能力，此时直接把 DEM 加入 Map。
    if (!layerViewpoint)
    {
        std::cerr
            << "Elevation opened, but its viewpoint "
            << "could not be calculated: "
            << preparedElevationPath
            << std::endl;

        return attachElevationLayerToMap();
    }

    std::cout
        << "Calculated elevation viewpoint before adding to Map: "
        << layerViewpoint->toString()
        << std::endl;

    if (!layerViewpoint)
    {
        std::cerr
            << "Elevation opened, but its viewpoint "
            << "could not be calculated: "
            << preparedElevationPath
            << std::endl;

        return attachElevationLayerToMap();
    }

    std::cout
        << "Calculated elevation viewpoint before adding to Map: "
        << layerViewpoint->toString()
        << std::endl;

    // 根据 DEM 的推荐观察距离，
    // 设置它参与地形计算的最大相机距离。
    //
    // 当相机远离 DEM 所在区域、进入大范围视角后，
    // osgEarth 会停止使用这个局部 DEM；
    // 当相机重新回到局部范围后，DEM 会自动恢复参与地形计算。
    if (layerViewpoint->range().isSet())
    {
        const double viewpointRangeMeters =
            layerViewpoint
                ->range()
                ->as(
                    osgEarth::Units::METERS
                );

        // 学习阶段先使用推荐观察距离的 4 倍。
        //
        // 例如推荐观察距离为 30 km，
        // 那么相机距离超过约 120 km 时，
        // 这个局部 DEM 将停止参与当前地形构建。
        const double maximumVisibleRangeMeters =
            viewpointRangeMeters * 4.0;

        if (
            std::isfinite(maximumVisibleRangeMeters) &&
            maximumVisibleRangeMeters > 0.0
        )
        {
            elevationLayer->setMaxVisibleRange(
                static_cast<float>(
                    maximumVisibleRangeMeters
                )
            );

            std::cout
                << "Elevation maximum visible range: "
                << maximumVisibleRangeMeters
                << " meters"
                << std::endl;
        }
    }

    // 如果 EarthManipulator 尚未创建，
    // 当前没有可以执行视点飞行的相机控制器，
    // 因此直接加入 DEM。
    if (!manipulator_)
    {
        return attachElevationLayerToMap();
    }

    // DEM 已经打开且 Viewpoint 已经计算完成，
    // 但此时仍未加入 Map，因此不会参与飞行过程中的地形重建。
    manipulator_->setViewpoint(
        *layerViewpoint,

        // 相机飞行动画时间，单位为秒。
        0.5
    );

    // 创建临时定时器，持续检查视点飞行动画是否结束。
    //
    // 它不直接执行渲染；
    // 原来的 renderTimer_ 和 paintGL() 仍负责逐帧渲染。
    auto* viewpointWaitTimer =
        new QTimer(this);

    viewpointWaitTimer->setInterval(
        16
    );

    viewpointWaitTimer->setTimerType(
        Qt::PreciseTimer
    );

    connect(
        viewpointWaitTimer,
        &QTimer::timeout,
        this,
        [
            this,
            viewpointWaitTimer,
            attachElevationLayerToMap
        ]()
        {
            // EarthManipulator 仍在执行视点过渡时继续等待。
            if (
                manipulator_ &&
                manipulator_->isSettingViewpoint()
            )
            {
                return;
            }

            // 相机已经到达目标区域，停止继续检查。
            viewpointWaitTimer->stop();

            // 现在才把 DEM 真正加入 Map，
            // 让 Terrain Engine 在目标区域开始构建地形。
            const bool layerAdded =
                attachElevationLayerToMap();

            if (!layerAdded)
            {
                qWarning()
                    << "The camera transition finished, "
                    << "but the elevation layer could not "
                    << "be added to Map.";
            }

            // 当前临时定时器已经完成使命。
            viewpointWaitTimer->deleteLater();
        }
    );

    viewpointWaitTimer->start();

    // 请求 Qt 尽快开始绘制相机飞行动画。
    update();

    // 这里的 true 表示：
    // 1. DEM 已经成功打开；
    // 2. Viewpoint 已成功计算；
    // 3. 相机飞行已经开始；
    // 4. 飞行结束后将自动把 DEM 加入 Map。
    return true;
}

bool EarthViewWidget::moveToLayer(
    int mapUid,
    int layerUid,
    double durationSeconds
)
{
    // 相机移动必须依赖已经创建完成的 EarthManipulator。
    //
    // 如果三维窗口尚未完成初始化，
    // 当前还没有可以接收 Viewpoint 的相机控制器。
    if (!manipulator_)
    {
        qWarning()
            << "Cannot move to layer: "
            << "EarthManipulator is not initialized.";

        return false;
    }

    // MapNode 和真实 Map 必须处于有效状态。
    if (
        !mapNode_ ||
        !mapNode_->getMap()
    )
    {
        qWarning()
            << "Cannot move to layer: "
            << "Map is not available.";

        return false;
    }

    // 飞行动画时间必须是有限的非负数。
    //
    // durationSeconds == 0.0：
    // 立即跳转到目标视点。
    //
    // durationSeconds > 0.0：
    // 使用指定秒数播放视点过渡动画。
    if (
        !std::isfinite(durationSeconds) ||
        durationSeconds < 0.0
    )
    {
        qWarning()
            << "Cannot move to layer: "
            << "invalid transition duration:"
            << durationSeconds;

        return false;
    }

    // 当前程序使用负数表示无效 UID。
    if (
        mapUid < 0 ||
        layerUid < 0
    )
    {
        qWarning()
            << "Cannot move to layer: "
            << "invalid Map or Layer UID.";

        return false;
    }

    osgEarth::Map* map =
        mapNode_->getMap();

    // 防止根据另一个 Map 的 UID，
    // 错误操作当前 EarthViewWidget 管理的 Map。
    if (
        static_cast<int>(
            map->getUID()
        ) != mapUid
    )
    {
        qWarning()
            << "Cannot move to layer: "
            << "Map UID does not match.";

        return false;
    }

    // 根据 Tree 节点保存的 Layer UID，
    // 查找 Map 中对应的真实 osgEarth Layer。
    osgEarth::Layer* layer =
        map->getLayerByUID(
            layerUid
        );

    if (layer == nullptr)
    {
        qWarning()
            << "Cannot move to layer: "
            << "Layer was not found. UID:"
            << layerUid;

        return false;
    }

    // 当前 ViewpointCalculator 接收 TileLayer。
    //
    // ImageLayer 和 ElevationLayer 都继承自 TileLayer，
    // 因此影像和 DEM 都能使用同一套视点计算逻辑。
    //
    // 如果未来传入 ModelLayer、AnnotationLayer 等其他类型，
    // dynamic_cast 将返回 nullptr，当前函数会安全地报告不支持。
    auto* tileLayer =
        dynamic_cast<osgEarth::TileLayer*>(
            layer
        );

    if (tileLayer == nullptr)
    {
        qWarning()
            << "Cannot move to layer: "
            << "the selected layer is not a TileLayer.";

        return false;
    }

    // 根据该图层自己的 DataExtent，
    // 重新计算适合完整观察它的 Viewpoint。
    const auto layerViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *tileLayer
        );

    if (!layerViewpoint)
    {
        qWarning()
            << "Cannot move to layer: "
            << "unable to calculate a valid Viewpoint.";

        return false;
    }

    qDebug()
        << "Moving camera to layer:"
        << QString::fromStdString(
               layer->getName()
           )
        << QString::fromStdString(
               layerViewpoint->toString()
           );

    // 将目标 Viewpoint 交给 EarthManipulator。
    //
    // 这里只控制相机，不修改图层顺序，
    // 也不会重新创建或重新加入图层。
    manipulator_->setViewpoint(
        *layerViewpoint,
        durationSeconds
    );

    // 请求 Qt 尽快开始绘制视点过渡动画。
    //
    // 真正的逐帧渲染仍然由 renderTimer_、
    // paintGL() 和 viewer_->frame() 共同完成。
    update();

    return true;
}

bool EarthViewWidget::setLayerVisible(int mapUid, int layerUid, bool visible)
{
    // MapNode 可能尚未完成创建，或者已经开始释放。
    //
    // 在这两种状态下都无法安全访问真实 Map。
    if (!mapNode_ || !mapNode_->getMap())
    {
        return false;
    }

    osgEarth::Map* map =
        mapNode_->getMap();

    // 当前 EarthViewWidget 暂时只管理一个 Map。
    //
    // 先比较调用者传来的 Map UID，
    // 避免把属于其他 Map 的图层操作错误地应用到当前 Map。
    if (map->getUID() != mapUid)
    {
        return false;
    }

    // 根据 Layer UID 从真实 osgEarth Map 中查找图层。
    //
    // getLayerByUID() 返回的是通用 Layer 指针，
    // 因为 Map 中可能同时存在影像、高程、模型等多种图层。
    osgEarth::Layer* layer =
        map->getLayerByUID(layerUid);

    if (layer == nullptr)
    {
        // 没有找到对应 UID 的真实图层。
        return false;
    }

    // 将通用 Layer 尝试转换为支持显隐控制的 VisibleLayer。
    //
    // dynamic_cast 会在运行时检查真实对象的类型：
    // 如果该图层继承自 VisibleLayer，就返回有效指针；
    // 如果不支持该类型，就返回 nullptr。
    auto* visibleLayer =
        dynamic_cast<osgEarth::VisibleLayer*>(
            layer
        );

    if (visibleLayer == nullptr)
    {
        // 找到的图层不支持 VisibleLayer 提供的显隐接口。
        return false;
    }

    // 修改真实 osgEarth 图层的显示状态。
    visibleLayer->setVisible(visible);

    // 请求 Qt 在合适的时机重新绘制三维窗口。
    //
    // 当前虽然已经有持续渲染定时器，
    // 这里主动请求一次更新，可以明确表达：
    // 图层状态修改后，画面需要刷新。
    update();

    return true;
}

bool EarthViewWidget::removeLayer(
    int mapUid,
    int layerUid
)
{
    // MapNode 可能尚未初始化完成，也可能正在被释放。
    // 这时不能访问真实 osgEarth Map。
    if (!mapNode_ || !mapNode_->getMap())
    {
        return false;
    }

    osgEarth::Map* map =
        mapNode_->getMap();

    // 当前 EarthViewWidget 只管理一个 Map。
    //
    // 先检查删除请求中的 Map UID，
    // 防止误删另一个 Map 中具有相同 Layer UID 的图层。
    if (map->getUID() != mapUid)
    {
        return false;
    }

    // 根据 Layer UID，从 Map 中查找真正的 osgEarth 图层。
    //
    // 这里获得的是通用 Layer 指针，
    // 因此影像图层、高程图层以及未来的其他图层
    // 都可以使用这套删除逻辑。
    osgEarth::Layer* layer =
        map->getLayerByUID(layerUid);

    if (layer == nullptr)
    {
        // 图层可能已经被删除，或者传入的 UID 不正确。
        return false;
    }

    // 从 osgEarth Map 中移除真实图层。
    //
    // Map 使用引用计数管理其中的 Layer。
    // removeLayer() 会解除 Map 对该图层的持有关系，
    // 所以这里不需要手动调用 delete。
    map->removeLayer(layer);

    // 图层结构发生变化后，请求 Qt 重新绘制三维窗口。
    update();

    return true;
}

bool EarthViewWidget::synchronizeLayerOrder(
    int mapUid,
    const std::vector<int>& layerUidsTopToBottom
)
{
    // MapNode 可能尚未初始化完成，也可能正在释放。
    // 当前状态下无法访问真实 osgEarth Map。
    if (!mapNode_ || !mapNode_->getMap())
    {
        return false;
    }

    osgEarth::Map* map =
        mapNode_->getMap();

    // 当前 EarthViewWidget 暂时只管理一个 Map。
    //
    // 如果调用者传入的 Map UID 与当前 Map 不一致，
    // 就不能继续调整图层，以免操作错误的地图。
    if (map->getUID() != mapUid)
    {
        return false;
    }

    // 分类中不存在图层，或者只有一个图层时，
    // 不可能产生相对顺序变化。
    //
    // 这种情况虽然不需要调用 moveLayer()，
    // 但从同步结果来看已经满足目标顺序，因此返回 true。
    if (layerUidsTopToBottom.size() < 2)
    {
        return true;
    }

    // 保存已经检查过的 UID，用于检测重复图层。
    //
    // 如果同一个 Layer UID 在列表中出现两次，
    // 后面的顺序计算将无法形成有效的一一对应关系。
    std::unordered_set<int> checkedLayerUids;

    // 按照 Qt 界面从上到下的顺序，
    // 保存经过验证的真实 osgEarth Layer。
    //
    // 使用 osg::ref_ptr 暂时持有图层，
    // 可以确保整个排序过程中这些对象保持有效。
    std::vector<
        osg::ref_ptr<osgEarth::Layer>
    > layersTopToBottom;

    layersTopToBottom.reserve(
        layerUidsTopToBottom.size()
    );

    // 逐个验证 MainWindow 提供的 Layer UID。
    for (const int layerUid : layerUidsTopToBottom)
    {
        // 当前程序使用 -1 表示无效 UID。
        if (layerUid < 0)
        {
            return false;
        }

        // insert() 返回一个 pair。
        //
        // second 为 true：
        // 该 UID 第一次出现，已经成功写入集合。
        //
        // second 为 false：
        // 该 UID 之前已经出现，说明列表中存在重复项。
        const bool inserted =
            checkedLayerUids
                .insert(layerUid)
                .second;

        if (!inserted)
        {
            return false;
        }

        // 根据 UID 查找 Map 中真实存在的 Layer。
        osgEarth::Layer* layer =
            map->getLayerByUID(layerUid);

        if (layer == nullptr)
        {
            // Qt 图层树中记录了该 UID，
            // 但真实 osgEarth Map 已经找不到对应图层。
            //
            // 此时界面与 Map 状态可能已经不一致，
            // 不能继续执行排序。
            return false;
        }

        // 按照界面从上到下的顺序保存真实图层引用。
        layersTopToBottom.emplace_back(
            layer
        );
    }

    // 读取 osgEarth Map 当前保存的完整 Layer 顺序。
    //
    // 这里取得的不只是当前分类中的影像或 DEM，
    // 而是 Map 中所有类型的 Layer，例如：
    //
    // Image、Elevation、Model、Annotation 等。
    osgEarth::LayerVector currentMapLayers;

    map->getLayers(
        currentMapLayers
    );

    // 理论上前面已经通过 getLayerByUID() 验证了所有目标图层。
    //
    // 如果完整 Map 列表为空，说明 Map 状态在验证过程中
    // 发生了异常变化，因此不能继续执行排序。
    if (currentMapLayers.empty())
    {
        return false;
    }

    // 记录待排序图层当前占据的 Map 全局索引。
    //
    // 例如当前完整 Map 顺序是：
    //
    // 0：Image A
    // 1：DEM X
    // 2：Image B
    // 3：Model Y
    // 4：Image C
    //
    // 如果本次调整的是影像，那么得到的槽位就是：
    // [0, 2, 4]
    std::vector<unsigned> occupiedMapIndices;

    occupiedMapIndices.reserve(
        layersTopToBottom.size()
    );

    // 遍历 osgEarth Map 的完整 Layer 列表。
    for (
        unsigned mapIndex = 0;
        mapIndex < currentMapLayers.size();
        ++mapIndex
    )
    {
        osgEarth::Layer* mapLayer =
            currentMapLayers[mapIndex].get();

        if (mapLayer == nullptr)
        {
            // 正常情况下 Map 的 Layer 列表中不应存在空对象。
            // 如果出现空对象，说明当前 Map 状态不可靠。
            return false;
        }

        const int currentLayerUid =
            static_cast<int>(
                mapLayer->getUID()
            );

        // checkedLayerUids 中保存的是本次需要调整顺序的全部 UID。
        //
        // 如果当前 Map Layer 的 UID 存在于集合中，
        // 就记录它当前占据的全局 Map 索引。
        if (
            checkedLayerUids.find(currentLayerUid) !=
            checkedLayerUids.end()
        )
        {
            occupiedMapIndices.push_back(
                mapIndex
            );
        }
    }

    // 找到的全局槽位数量必须与传入的目标图层数量一致。
    //
    // 如果数量不同，说明验证图层之后 Map 又发生了变化，
    // 或者 Qt 图层树与真实 Map 已经出现状态不一致。
    if (
        occupiedMapIndices.size() !=
        layersTopToBottom.size()
    )
    {
        return false;
    }

    // Qt 图层树采用“顶部优先”的显示顺序，
    // osgEarth Map 中则按照“底层到上层”的方向排列。
    //
    // 因此需要把 Qt 的从上到下顺序反转，
    // 得到 osgEarth 最终需要的从底到顶顺序。
    std::vector<
        osg::ref_ptr<osgEarth::Layer>
    > layersBottomToTop(
        layersTopToBottom.rbegin(),
        layersTopToBottom.rend()
    );

    // 复制当前完整 Map 顺序，作为目标顺序模板。
    //
    // 接下来只替换本次目标图层占据的槽位，
    // 其他类型图层仍然保留在原来的位置。
    osgEarth::LayerVector desiredMapLayers =
        currentMapLayers;

    // 按照目标底到顶顺序，重新填充同类图层原来占据的槽位。
    for (
        std::size_t layerIndex = 0;
        layerIndex < occupiedMapIndices.size();
        ++layerIndex
    )
    {
        const unsigned mapIndex =
            occupiedMapIndices[layerIndex];

        desiredMapLayers[mapIndex] =
            layersBottomToTop[layerIndex];
    }

    // 从 Map 的第一个全局位置开始，逐个检查真实顺序
    // 是否已经和 desiredMapLayers 中的目标顺序一致。
    for (
        std::size_t desiredIndex = 0;
        desiredIndex < desiredMapLayers.size();
        ++desiredIndex
    )
    {
        // osgEarth 的图层索引接口使用 unsigned，
        // 而 std::vector::size() 和索引通常使用 std::size_t。
        //
        // 当前图层数量远小于 unsigned 的表示范围，
        // 因此这里显式转换为 osgEarth 接口需要的类型。
        const unsigned mapIndex =
            static_cast<unsigned>(
                desiredIndex
            );

        // 取得目标顺序中，该位置应当出现的 Layer。
        osgEarth::Layer* desiredLayer =
            desiredMapLayers[desiredIndex].get();

        if (desiredLayer == nullptr)
        {
            // 前面正常计算得到的目标列表中不应该出现空 Layer。
            return false;
        }

        // 读取真实 osgEarth Map 在该位置上
        // 当前实际保存的 Layer。
        //
        // 注意：这里直接从 map 重新读取，
        // 而不是读取之前的 currentMapLayers 快照。
        //
        // 因为每次调用 moveLayer() 后，真实 Map 顺序会变化，
        // 但 currentMapLayers 这个本地快照不会自动更新。
        osgEarth::Layer* currentLayer =
            map->getLayerAt(
                mapIndex
            );

        if (currentLayer == nullptr)
        {
            return false;
        }

        // 如果当前位置已经是目标 Layer，
        // 就不需要执行任何移动。
        if (currentLayer == desiredLayer)
        {
            continue;
        }

        // 找到目标 Layer 当前在真实 Map 中的位置。
        //
        // getIndexOfLayer() 找不到 Layer 时，
        // 会返回 map->getNumLayers()。
        const unsigned currentIndex =
            map->getIndexOfLayer(
                desiredLayer
            );

        if (currentIndex >= map->getNumLayers())
        {
            // 目标 Layer 已经不在当前 Map 中，
            // 说明排序过程中 Map 状态发生了异常变化。
            return false;
        }

        // 将目标 Layer 移动到当前正在处理的目标位置。
        //
        // moveLayer() 操作的是已经存在于 Map 中的真实 Layer，
        // 不会重新创建图层，也不会重新分配 Layer UID。
        map->moveLayer(
            desiredLayer,
            mapIndex
        );
    }

    // 所有移动操作结束后，重新读取一次真实 Map 顺序，
    // 用于确认最终结果确实与 desiredMapLayers 完全一致。
    osgEarth::LayerVector verifiedMapLayers;

    map->getLayers(
        verifiedMapLayers
    );

    // 图层数量也必须完全一致。
    if (
        verifiedMapLayers.size() !=
        desiredMapLayers.size()
    )
    {
        return false;
    }

    // 逐个位置验证真实 Map 和目标顺序是否一致。
    for (
        std::size_t mapIndex = 0;
        mapIndex < desiredMapLayers.size();
        ++mapIndex
    )
    {
        osgEarth::Layer* verifiedLayer =
            verifiedMapLayers[mapIndex].get();

        osgEarth::Layer* desiredLayer =
            desiredMapLayers[mapIndex].get();

        if (verifiedLayer != desiredLayer)
        {
            // 只要有一个位置不一致，
            // 就不能向 MainWindow 报告同步成功。
            return false;
        }
    }

    // osgEarth Map 的真实图层顺序已经调整完成，
    // 请求 Qt 安排下一次三维画面刷新。
    update();

    return true;
}

void EarthViewWidget::initializeGL()
{
    // 初始化当前 OpenGL Context 对应的函数入口。
    initializeOpenGLFunctions();

    // QOpenGLWidget 的宽高属于 Qt 的逻辑像素；
    // OpenGL Framebuffer 使用的可能是高 DPI 缩放后的物理像素。
    const double devicePixelRatio = devicePixelRatioF();

    const int framebufferWidth = std::max(
        1,
        qRound(width() * devicePixelRatio)
    );

    const int framebufferHeight = std::max(
        1,
        qRound(height() * devicePixelRatio)
    );

    // 创建 OSG Viewer。
    //
    // 在独立的 OSG 程序中，通常由 viewer.run() 管理渲染循环；
    // 在 Qt 程序中，渲染循环由 Qt 管理，所以后面会在
    // paintGL() 中调用 viewer_->frame()。
    viewer_ = new osgViewer::Viewer();

    // 安装 osgEarth 提供的 OpenGL 初始化操作。
    //
    // 它会在 Viewer realize 时配置 osgEarth 所需的
    // OpenGL 状态及渲染能力。
    viewer_->setRealizeOperation(
        new osgEarth::GL3RealizeOperation()
    );

    // Qt 的 OpenGL Context 工作在 GUI 主线程中，
    // 因此目前使用 OSG 的单线程渲染模式最稳妥。
    viewer_->setThreadingModel(
        osgViewer::Viewer::SingleThreaded
    );

    // OpenGL Context 的创建、激活和释放由 QOpenGLWidget 管理。
    // 因此不让 OSG 在每一帧结束后主动释放 Context。
    viewer_->setReleaseContextAtEndOfFrameHint(false);

    // 告诉 OSG：它将被嵌入一个已经存在的窗口。
    //
    // GraphicsWindowEmbedded 不会创建新的 Windows 窗口，
    // 而是作为 OSG 与 QOpenGLWidget 之间的适配对象。
    graphicsWindow_ =
        viewer_->setUpViewerAsEmbeddedInWindow(
            0,
            0,
            framebufferWidth,
            framebufferHeight
        );

    // Qt 的窗口坐标原点位于左上角，Y 坐标向下增加。
    //
    // 告诉 OSG 使用相同的鼠标 Y 轴方向，
    // 后续就可以直接转发 Qt 的鼠标坐标，
    // 不需要手动执行 height - y 的上下翻转。
    graphicsWindow_
        ->getEventQueue()
        ->getCurrentEventState()
        ->setMouseYOrientation(
            osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS
        );

    // QOpenGLWidget 不会直接在系统默认的 0 号 Framebuffer 上绘制，
    // 它会创建并管理自己的 Framebuffer。
    //
    // 因此必须把 Qt 当前使用的 FBO 编号告诉 OSG，
    // 否则 OSG 可能把画面绘制到错误的位置。
    graphicsWindow_->setDefaultFboId(
        defaultFramebufferObject()
    );

    osg::Camera* camera = viewer_->getCamera();

    // QOpenGLWidget 使用的是 FBO。
    // FBO 的颜色缓冲区是 GL_COLOR_ATTACHMENT0，
    // 而不是普通桌面窗口常用的 GL_BACK。
    camera->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    camera->setReadBuffer(GL_COLOR_ATTACHMENT0);

    // 设置 OSG 相机每帧开始时使用的清屏颜色。
    // 从这一步开始，背景颜色由 OSG Camera 管理。
    camera->setClearColor(
        osg::Vec4(
            0.0F,
            0.0F,
            0.0F,
            1.0F
        )
    );

    // 每帧开始前清理上一帧的颜色和深度信息。
    camera->setClearMask(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    // 创建 osgEarth 地图场景的根节点。
    mapNode_ = new osgEarth::MapNode();

    // MapNode 创建完成后，它内部管理的 Map 也已经存在。
    //
    // getUID() 返回 osgEarth 为这个 Map 分配的运行时唯一编号。
    // MainWindow 后续会使用这个编号，将图层树节点与真实 Map 对应起来。
    const int mapUid =
        mapNode_->getMap()->getUID();

    // 发出“Map 已创建”信号。
    //
    // emit 不会创建新的 Map，也不会修改图层树；
    // 它只负责把 Map UID 和显示名称通知给已经连接该信号的对象。
    emit mapCreated(
        mapUid,
        QStringLiteral("Map 1")
    );

    // 当前程序启动时默认加载的本地全球影像。
    //
    // 默认底图与后续菜单选择、文件拖入的影像，
    // 都统一经过 addImageLayer() 完成预处理、创建图层、
    // 状态检查以及 Layers Dock 通知。
    const QString defaultGlobalImageryPath =
        QStringLiteral(
            "D:/work/projects/HelloEarthWorkspace/testdata/"
            "NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif"
        );

    // 通过统一影像加载接口添加默认全球底图。
    //
    // 如果加载失败，addImageLayer() 会返回 false；
    // 当前先保留空 Map，让桌面程序仍然可以正常启动。
    if (!addImageLayer(defaultGlobalImageryPath))
    {
        std::cerr
            << "Failed to load the default global imagery."
            << std::endl;
    }

    // 将 MapNode 设置为 Viewer 要渲染的场景数据。
    //
    // 从这里开始，Viewer 渲染的不再是普通的空 OSG 场景，
    // 而是由 osgEarth 管理的地图场景。
    viewer_->setSceneData(mapNode_);

    // 创建地球专用的相机操纵器。
    manipulator_ =
        new osgEarth::Util::EarthManipulator();

    // 把操纵器交给 Viewer 管理。
    //
    // 目前还没有把 Qt 鼠标事件传给 OSG，
    // 所以此时操纵器已经安装，但鼠标暂时还不能控制它。
    viewer_->setCameraManipulator(manipulator_);

    // 初始化 Viewer 的图形资源。
    //
    // 此时 initializeGL() 已经确保 Qt 的 OpenGL Context
    // 正处于当前线程，所以 OSG 可以安全完成初始化。
    viewer_->realize();
}

void EarthViewWidget::resizeGL(int width, int height)
{
    // resizeGL() 有可能在 OSG Viewer 完成创建前被调用，
    // 因此首先检查嵌入窗口是否已经存在。
    if (!graphicsWindow_)
    {
        return;
    }

    const double devicePixelRatio = devicePixelRatioF();

    const int framebufferWidth = std::max(
        1,
        qRound(width * devicePixelRatio)
    );

    const int framebufferHeight = std::max(
        1,
        qRound(height * devicePixelRatio)
    );

    // 通知 OSG 嵌入窗口尺寸发生了变化。
    //
    // resized() 会同步 GraphicsContext、Camera Viewport
    // 以及相机投影矩阵的宽高比。
    graphicsWindow_->resized(
        0,
        0,
        framebufferWidth,
        framebufferHeight
    );

    // 同时向 OSG 的事件队列发送窗口尺寸变化事件。
    //
    // 后续接入 EarthManipulator 和鼠标事件时，
    // 操纵器会依赖正确的窗口尺寸计算鼠标坐标。
    graphicsWindow_->getEventQueue()->windowResize(
        0,
        0,
        framebufferWidth,
        framebufferHeight
    );
}

void EarthViewWidget::paintGL()
{
    if (!viewer_ || !graphicsWindow_)
    {
        return;
    }

    // QOpenGLWidget 的 Framebuffer 可能会在窗口缩放、
    // 高 DPI 状态变化等情况下重新创建。
    //
    // 因此不能只在 initializeGL() 中记录一次 FBO，
    // 每一帧渲染前都重新同步更加稳妥。
    graphicsWindow_->setDefaultFboId(
        defaultFramebufferObject()
    );

    // 让 OSG 执行一帧完整的渲染流程：
    //
    // 1. 处理事件
    // 2. 更新场景
    // 3. 遍历场景图
    // 4. 使用 Camera 绘制当前画面
    //
    // 这里不使用 viewer_->run()，
    // 因为 Qt 才是整个桌面程序的主事件循环。
    viewer_->frame();
}

void EarthViewWidget::mousePressEvent(
    QMouseEvent* event
)
{
    if (!graphicsWindow_)
    {
        // OSG 还没有完成初始化时，
        // 将事件交回 QOpenGLWidget 的默认处理流程。
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    const unsigned int osgButton =
        convertMouseButtonToOsg(event->button());

    if (osgButton == 0)
    {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    // 用户点击三维窗口后，让它获得键盘焦点。
    // 后续接入键盘事件时会使用这个焦点。
    setFocus(Qt::MouseFocusReason);

    // Qt 提供的是逻辑像素坐标，
    // OSG 嵌入窗口使用的是 Framebuffer 物理像素坐标。
    const double pixelRatio = devicePixelRatioF();

    const float osgX = static_cast<float>(
        event->position().x() * pixelRatio
    );

    const float osgY = static_cast<float>(
        event->position().y() * pixelRatio
    );

    // 把鼠标按下事件加入 OSG 的事件队列。
    graphicsWindow_
        ->getEventQueue()
        ->mouseButtonPress(
            osgX,
            osgY,
            osgButton
        );

    // 告诉 Qt，这次鼠标事件已经由当前控件处理。
    event->accept();
}

void EarthViewWidget::mouseMoveEvent(
    QMouseEvent* event
)
{
    if (!graphicsWindow_)
    {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const double pixelRatio = devicePixelRatioF();

    const float osgX = static_cast<float>(
        event->position().x() * pixelRatio
    );

    const float osgY = static_cast<float>(
        event->position().y() * pixelRatio
    );

    // OSG 的 EventQueue 会保留当前哪些按键处于按下状态。
    //
    // 因此这里不需要再次判断左键还是右键；
    // EarthManipulator 会结合之前的 mouseButtonPress()
    // 判断当前属于左键拖动、右键拖动还是中键拖动。
    graphicsWindow_
        ->getEventQueue()
        ->mouseMotion(
            osgX,
            osgY
        );

    event->accept();
}

void EarthViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!graphicsWindow_)
    {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    const unsigned int osgButton =
        convertMouseButtonToOsg(event->button());

    if (osgButton == 0)
    {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    const double pixelRatio = devicePixelRatioF();

    const float osgX = static_cast<float>(
        event->position().x() * pixelRatio
    );

    const float osgY = static_cast<float>(
        event->position().y() * pixelRatio
    );

    // 把鼠标释放事件加入 OSG 的事件队列。
    //
    // EarthManipulator 收到后就知道本次拖动已经结束。
    graphicsWindow_
        ->getEventQueue()
        ->mouseButtonRelease(
            osgX,
            osgY,
            osgButton
        );

    event->accept();
}

void EarthViewWidget::wheelEvent(
    QWheelEvent* event
)
{
    if (!graphicsWindow_)
    {
        // OSG 尚未完成初始化时，
        // 将滚轮事件交给 QOpenGLWidget 默认处理。
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    // 获取滚轮在垂直方向上的滚动量。
    //
    // 通常：
    // 大于 0 表示滚轮向上；
    // 小于 0 表示滚轮向下；
    // 等于 0 表示这次可能只有水平方向滚动。
    const int verticalDelta =
        event->angleDelta().y();

    if (verticalDelta == 0)
    {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    // 先把滚轮发生时的鼠标位置同步给 OSG。
    //
    // EarthManipulator 可以利用当前鼠标位置，
    // 判断缩放操作所对应的屏幕位置。
    const double pixelRatio = devicePixelRatioF();

    const float osgX = static_cast<float>(
        event->position().x() * pixelRatio
    );

    const float osgY = static_cast<float>(
        event->position().y() * pixelRatio
    );

    graphicsWindow_
        ->getEventQueue()
        ->mouseMotion(
            osgX,
            osgY
        );

    if (verticalDelta > 0)
    {
        // Qt 滚轮向上，转换成 OSG 的向上滚动事件。
        graphicsWindow_
            ->getEventQueue()
            ->mouseScroll(
                osgGA::GUIEventAdapter::SCROLL_UP
            );
    }
    else
    {
        // Qt 滚轮向下，转换成 OSG 的向下滚动事件。
        graphicsWindow_
            ->getEventQueue()
            ->mouseScroll(
                osgGA::GUIEventAdapter::SCROLL_DOWN
            );
    }

    // 事件已经成功传递给 OSG，不再交给其他 Qt 控件处理。
    event->accept();
}
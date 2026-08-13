#include "EarthViewWidget.h"

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

    // 将图层加入当前真实 osgEarth Map。
    //
    // addLayer() 会触发图层打开和数据源初始化。
    osgEarth::Map* map =
        mapNode_->getMap();

    map->addLayer(
        imageryLayer.get()
    );

    // 检查 osgEarth 是否成功打开最终数据源。
    if (imageryLayer->getStatus().isError())
    {
        std::cerr
            << "Failed to open prepared image: "
            << imageryLayer->getStatus().toString()
            << std::endl;

        // 从 Map 中移除打开失败的图层，
        // 避免无效 Layer 留在地图的图层集合中。
        map->removeLayer(
            imageryLayer.get()
        );

        return false;
    }

    std::cout
        << "Image opened successfully: "
        << preparedImagePath
        << std::endl;

    // 根据已经成功打开的影像图层数据范围，
    // 计算能够完整观察该图层的合适 Viewpoint。
    //
    // GDALImageLayer 继承自 TileLayer，
    // 因此可以直接交给公共视点计算模块。
    const auto layerViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *imageryLayer
        );

    if (!layerViewpoint)
    {
        // 影像本身已经成功加入 Map；
        // 这里只是无法根据图层范围计算有效视点，
        // 因此不把整个图层加载过程判定为失败。
        std::cerr
            << "Image opened, but its viewpoint "
            << "could not be calculated: "
            << sourcePathString
            << std::endl;
    }
    else
    {
        std::cout
            << "Calculated image viewpoint: "
            << layerViewpoint->toString()
            << std::endl;

        // EarthManipulator 只有在 Viewer 初始化阶段后半段
        // 才会被创建并保存到 manipulator_。
        //
        // 程序启动时加载默认全球影像，manipulator_ 仍为空，
        // 因此不会在这里强制修改初始相机。
        //
        // 用户后续通过菜单加载影像时，manipulator_ 已经存在，
        // 此时让相机平滑移动到新影像的数据范围。
        if (manipulator_)
        {
            manipulator_->setViewpoint(
                *layerViewpoint,

                // 从当前观察状态移动到目标 Viewpoint
                // 所使用的动画时间，单位为秒。
                0.5
            );
        }
    }

    // 只有真实图层成功加入 Map 后，
    // 才通知 MainWindow 创建 Layers Dock 叶子节点。
    emit imageryLayerAdded(
        map->getUID(),
        imageryLayer->getUID(),
        layerDisplayName
    );

    // 明确请求刷新三维窗口。
    //
    // 当前定时器本来也会持续刷新，
    // 这里用于表达“地图内容发生变化，需要显示新结果”。
    update();

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

    osgEarth::Map* map = mapNode_->getMap();

    // 7. 将高程图层加入 Map。
    //    加入之后，地形引擎才会开始读取 DEM 并构建有起伏的地表。
    map->addLayer(elevationLayer.get());

    // 8. addLayer 后检查状态。
    //    某些错误只有图层加入 Map、开始初始化后才会暴露。
    if (elevationLayer->getStatus().isError())
    {
        qWarning() << "Failed to add elevation layer:"
                   << sourcePath
                   << QString::fromStdString(
                          elevationLayer->getStatus().message());

        // 加载失败时，把无效图层从 Map 中移除。
        map->removeLayer(elevationLayer.get());
        return false;
    }

    qDebug() << "Elevation layer opened successfully:"
             << sourcePath;

    // 9. 根据 DEM 的地理范围计算适合观察该区域的视点。
    const auto layerViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *elevationLayer);

    if (!layerViewpoint)
    {
        // 视点计算失败不代表高程图层加载失败，
        // 所以这里只报告警告，不移除图层。
        qWarning() << "Unable to calculate elevation viewpoint:"
                   << sourcePath;
    }
    else if (manipulator_)
    {
        // 用 1.5 秒平滑飞行到 DEM 所在区域。
        manipulator_->setViewpoint(
            *layerViewpoint,
            0.5
        );
    }

    // 10. 通知 MainWindow：
    //     一个新的高程图层已经成功加入 osgEarth Map。
    //     下一阶段 Layers Dock 会订阅该信号并创建对应树节点。
    emit elevationLayerAdded(
        map->getUID(),
        elevationLayer->getUID(),
        layerDisplayName
    );

    // 11. 请求 Qt 安排下一次 OpenGL 绘制。
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
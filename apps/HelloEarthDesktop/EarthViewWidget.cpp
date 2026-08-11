#include "EarthViewWidget.h"

#include <algorithm>
#include <iostream>

#include <QSurfaceFormat>
#include <QTimer>
#include <QMouseEvent>
#include <osgGA/GUIEventAdapter>
#include <QWheelEvent>

#include <osg/Camera>
#include <osgEarth/GDAL>
#include <osgEarth/GLUtils>

#include <HelloEarth/raster/RasterPreprocessor.h>

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
    : QOpenGLWidget(parent)
{
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

    // 用户选择或程序预设的原始 TIFF 路径。
    const std::string globalImagerySourcePath =
        "D:/work/projects/HelloEarthWorkspace/testdata/"
        "NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif";

    // 用于接收栅格预处理结束后真正应该加载的路径。
    //
    // 正常情况下可能仍然是原始 TIFF；
    // 如果内部金字塔检查失败，则也可能返回隔离后的 VRT 路径。
    std::string preparedGlobalImageryPath;

    // 在创建 GDALImageLayer 之前完成：
    // 1. TIFF 基础有效性检查；
    // 2. 外部或内部金字塔检查；
    // 3. 必要时构建外部 OVR；
    // 4. 必要时创建隔离 VRT；
    // 5. 返回最终允许 osgEarth 加载的路径。
    const bool globalImageryPrepared =
        HelloEarth::Raster::prepareRasterForLoading(
            globalImagerySourcePath,
            preparedGlobalImageryPath
        );

    if (!globalImageryPrepared)
    {
        std::cerr
            << "Global imagery preprocessing failed: "
            << globalImagerySourcePath
            << std::endl;
    }
    else
    {
        // 只有影像及其金字塔预处理成功后，
        // 才创建并添加 osgEarth 影像图层。
        auto* globalImageryLayer =
            new osgEarth::GDALImageLayer();

        globalImageryLayer->setName(
            "Global Imagery"
        );

        // 必须使用预处理函数返回的最终路径，
        // 不能继续固定使用原始 TIFF 路径。
        globalImageryLayer->setURL(
            preparedGlobalImageryPath
        );

        mapNode_->getMap()->addLayer(
            globalImageryLayer
        );

        if (globalImageryLayer->getStatus().isError())
        {
            std::cerr
                << "Failed to open prepared global imagery: "
                << globalImageryLayer->getStatus().toString()
                << std::endl;

            mapNode_->getMap()->removeLayer(
                globalImageryLayer
            );
        }
        else
        {
            std::cout
                << "Global imagery opened successfully: "
                << preparedGlobalImageryPath
                << std::endl;
        }
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

void EarthViewWidget::mouseReleaseEvent(
    QMouseEvent* event
)
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
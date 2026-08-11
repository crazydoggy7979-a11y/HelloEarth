#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/EarthManipulator>
#include <osgEarth/MapNode>

class QTimer;

class QMouseEvent;

class QWheelEvent;

// HelloEarth 的三维显示控件。
//
// Qt 负责 QWidget、OpenGL Context 和 Framebuffer；
// OSG Viewer 负责相机、场景图和每一帧的三维渲染。
class EarthViewWidget
    : public QOpenGLWidget,
      protected QOpenGLFunctions
{
public:
    // 创建三维显示控件。
    explicit EarthViewWidget(QWidget* parent = nullptr);

    // 释放 OSG Viewer 及其占用的 OpenGL 资源。
    ~EarthViewWidget() override;

protected:
    // OpenGL Context 创建完成后，由 Qt 调用。
    //
    // 后续将在这里创建和配置 OSG Viewer，
    // 让 OSG 使用 Qt 当前的 OpenGL Context。
    void initializeGL() override;

    // 三维控件尺寸变化时，由 Qt 调用。
    //
    // 后续将在这里同步 OSG 相机和事件窗口的尺寸。
    void resizeGL(int width, int height) override;

    // 三维控件需要绘制新的一帧时，由 Qt 调用。
    //
    // 后续将在这里调用 viewer_->frame()。
    void paintGL() override;

    // Qt 检测到鼠标按键按下时调用。
    void mousePressEvent(QMouseEvent* event) override;

    // 鼠标在三维控件中移动时调用。
    void mouseMoveEvent(QMouseEvent* event) override;

    // Qt 检测到鼠标按键释放时调用。
    void mouseReleaseEvent(QMouseEvent* event) override;

    // Qt 检测到鼠标滚轮操作时调用。
    void wheelEvent(QWheelEvent* event) override;

private:
    // 定期请求 Qt 重绘三维窗口。
    //
    // QTimer 不直接执行 OSG 渲染，
    // 它只负责触发 QOpenGLWidget::update()。
    // Qt 随后会在合适的时机调用 paintGL()，
    // 最终由 paintGL() 中的 viewer_->frame() 完成一帧渲染。
    QTimer* renderTimer_ = nullptr;

    // OSG Viewer 是三维场景的核心调度对象。
    //
    // 它负责管理相机、场景数据、事件处理器和每帧渲染流程。
    // Qt 程序中不调用 viewer.run()，而是在 paintGL() 中
    // 逐帧调用 viewer_->frame()。
    osg::ref_ptr<osgViewer::Viewer> viewer_;

    // OSG 对“外部窗口”的抽象适配对象。
    //
    // 真正的窗口和 OpenGL Context 由 QOpenGLWidget 创建；
    // GraphicsWindowEmbedded 不会再创建独立系统窗口，
    // 它主要向 OSG 提供窗口尺寸、GraphicsContext 状态
    // 以及后续传递鼠标键盘事件所需的 EventQueue。
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> graphicsWindow_;

    // osgEarth 地图场景的根节点。
    //
    // 后续加载的影像图层、DEM 图层等数据，
    // 都会添加到 MapNode 内部管理的 Map 中。
    osg::ref_ptr<osgEarth::MapNode> mapNode_;

    // osgEarth 专用的地球相机操纵器。
    //
    // 后续 Qt 的鼠标和键盘事件会传递给它，
    // 由它计算新的相机位置和观察方向。
    osg::ref_ptr<osgEarth::Util::EarthManipulator> manipulator_;
};
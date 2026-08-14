#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QString>

#include <vector>

#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/EarthManipulator>
#include <osgEarth/MapNode>
#include <osgEarth/TileLayer>

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
    Q_OBJECT

public:
    // 创建三维显示控件。
    explicit EarthViewWidget(QWidget* parent = nullptr);

    // 释放 OSG Viewer 及其占用的 OpenGL 资源。
    ~EarthViewWidget() override;

    // 根据 Map UID 和 Layer UID 设置真实 osgEarth 图层的显隐状态。
    //
    // mapUid：
    // 用于确认本次操作针对的是当前 EarthViewWidget 管理的 Map。
    //
    // layerUid：
    // 用于从 osgEarth::Map 中找到对应的真实 Layer。
    //
    // visible：
    // true 表示显示图层，false 表示隐藏图层。
    //
    // 返回 true 表示成功找到并修改了图层；
    // 返回 false 表示 Map 不匹配、图层不存在，
    // 或者该图层不支持 VisibleLayer 的显隐接口。
    bool setLayerVisible(
        int mapUid,
        int layerUid,
        bool visible
    );

    // 根据 Map UID 和 Layer UID，从当前 osgEarth Map 中
    // 删除一个真实图层。
    //
    // 这个接口不区分影像图层和高程图层：
    // 只要目标对象属于 osgEarth::Layer，都可以使用同一套删除流程。
    //
    // mapUid：
    // 用于确认删除请求针对的是当前 EarthViewWidget 管理的 Map。
    //
    // layerUid：
    // 用于从 Map 中找到需要删除的真实 osgEarth Layer。
    //
    // 返回 true 表示成功找到并删除图层；
    // 返回 false 表示 Map 尚未初始化、Map UID 不匹配，
    // 或者目标 Layer 已经不存在。
    bool removeLayer(
        int mapUid,
        int layerUid
    );

    // 将相机移动到指定 osgEarth 图层的数据范围。
    //
    // mapUid：
    // 目标图层所属 Map 的 UID。
    //
    // layerUid：
    // 需要定位的真实 osgEarth Layer UID。
    //
    // durationSeconds：
    // 从当前视点移动到目标视点所使用的动画时间，单位为秒。
    // 0.0 表示立即跳转，不播放飞行动画。
    //
    // 当前实现主要支持继承自 osgEarth::TileLayer 的图层，
    // 包括 ImageLayer 和 ElevationLayer。
    //
    // 返回 true 表示：
    // 1. Map 和 Layer 均成功找到；
    // 2. 图层能够转换为 TileLayer；
    // 3. 成功计算出 Viewpoint；
    // 4. 已经向 EarthManipulator 提交视点移动请求。
    //
    // 返回 false 表示目标不存在、类型不支持、
    // Viewpoint 计算失败，或者 EarthManipulator 尚未初始化。
    bool moveToLayer(
        int mapUid,
        int layerUid,
        double durationSeconds = 0.5
    );

    // 根据 Layers Dock 提供的同类图层完整顺序，
    // 同步调整当前 osgEarth Map 中真实 Layer 的相对位置。
    //
    // mapUid：
    // 用于确认本次排序针对的是当前 EarthViewWidget 管理的 Map。
    //
    // layerUidsTopToBottom：
    // 同一个图层分类中，从界面顶部到底部排列的全部 Layer UID。
    //
    // 例如 Layers Dock 中显示：
    // local_high.tif
    // local_low.tif
    // global.tif
    //
    // 那么传入顺序就是：
    // [local_high UID, local_low UID, global UID]。
    //
    // 函数内部会把 Qt 的“顶部优先”顺序转换成
    // osgEarth Map 所使用的“底层到上层”顺序。
    //
    // 该函数只会调整传入 UID 对应图层之间的相对顺序，
    // 不主动改变影像、DEM、模型等其他类型图层之间的关系。
    //
    // 返回 true 表示图层顺序同步成功；
    // 返回 false 表示 Map 不存在、Map UID 不匹配、
    // UID 重复、目标图层不存在，或传入的图层集合不合法。
    bool synchronizeLayerOrder(
        int mapUid,
        const std::vector<int>& layerUidsTopToBottom
    );

    // 将一份本地栅格数据作为影像图层加入当前 osgEarth Map。
    //
    // sourcePath 是用户指定的原始数据路径，
    // 例如通过文件选择窗口获得的 TIFF 路径。
    //
    // 函数内部将统一负责：
    // 1. 检查当前 Map 是否已经初始化；
    // 2. 执行栅格和金字塔预处理；
    // 3. 确定真正加载的 TIFF 或 VRT 路径；
    // 4. 创建并添加 GDALImageLayer；
    // 5. 检查图层是否成功打开；
    // 6. 发出 imageryLayerAdded 信号，通知 Layers Dock。
    //
    // 返回 true 表示影像成功加入 Map；
    // 返回 false 表示路径无效、预处理失败、
    // Map 尚未初始化或者 osgEarth 图层打开失败。
    bool addImageLayer(
        const QString& sourcePath
    );

    // 将一份本地栅格数据作为高程图层加入当前 osgEarth Map。
    //
    // sourcePath 是用户选择的原始 DEM 数据路径。
    //
    // 函数内部将统一负责：
    // 1. 检查当前 Map 是否已经初始化；
    // 2. 执行栅格和金字塔预处理；
    // 3. 确定真正加载的 TIFF 或 VRT 路径；
    // 4. 创建并添加 GDALElevationLayer；
    // 5. 检查高程图层是否成功打开；
    // 6. 计算图层范围对应的 Viewpoint；
    // 7. 发出 elevationLayerAdded 信号，通知 Layers Dock。
    //
    // 返回 true 表示 DEM 成功加入 Map；
    // 返回 false 表示路径无效、预处理失败、
    // Map 尚未初始化或者 osgEarth 图层打开失败。
    bool addElevationLayer(
        const QString& sourcePath
    );

signals:
    // osgEarth Map 创建成功后发出该信号。
    //
    // mapUid 是 osgEarth 为 Map 分配的运行时唯一编号；
    // mapDisplayName 是该 Map 在 Layers Dock 中显示的名称。
    void mapCreated(
        int mapUid,
        const QString& mapDisplayName
    );

    // 影像图层成功加入 osgEarth Map 后发出该信号。
    //
    // mapUid：
    // 表示该影像属于哪个真实 Map。
    //
    // layerUid：
    // osgEarth 为该图层分配的运行时唯一编号。
    // 后续图层树叶子将通过它找到对应的真实 Layer。
    //
    // layerDisplayName：
    // 图层在 Layers Dock 中显示的名称。
    void imageryLayerAdded(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName
    );

    // 高程图层成功加入 osgEarth Map 后发出该信号。
    //
    // mapUid：
    // 表示该 DEM 属于哪个真实 Map。
    //
    // layerUid：
    // osgEarth 为该高程图层分配的运行时唯一编号。
    // 后续图层树叶子将通过它找到真实 ElevationLayer。
    //
    // layerDisplayName：
    // 高程图层在 Layers Dock 中显示的名称。
    void elevationLayerAdded(
        int mapUid,
        int layerUid,
        const QString& layerDisplayName
    );

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
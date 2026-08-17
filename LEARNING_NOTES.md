# HelloEarth 学习笔记

> 更新日期：2026-08-17

## 1. 项目定位与学习目标

HelloEarth 是一个基于 osgEarth、OpenSceneGraph、GDAL 和 Qt 的三维 GIS 学习工程。

学习路线不是直接堆叠完整软件功能，而是逐层建立理解：

```text
本地遥感数据
    ↓
GDAL 数据检查与金字塔处理
    ↓
osgEarth Map / Layer 数据组织
    ↓
OSG Viewer 与相机交互
    ↓
Qt 桌面窗口和图层管理
    ↓
可继续扩展的三维 GIS 原型
```

当前已经从最初的独立 Viewer 示例，发展到包含公共静态库、独立 Example 和 Qt 桌面端的多 Target 工程。

## 2. 当前工程架构

Workspace 采用源码、构建结果和测试数据分离的结构：

```text
HelloEarthWorkspace/
├── HelloEarth/       # Git 源码仓库
├── build/            # CMake 构建结果
├── install/          # 安装结果
└── testdata/         # 本地影像和 DEM，不进入 Git
```

源码仓库内部主要分为：

```text
HelloEarth/
├── include/HelloEarth/   # 公共头文件
├── src/                  # 可复用底层静态库
├── examples/             # 独立学习 Target
└── apps/                  # Qt 桌面应用
```

CMake 组织关系：

```text
顶层 CMakeLists.txt
├── src/CMakeLists.txt
│   ├── HelloEarth::Raster
│   └── HelloEarth::Navigation
├── examples/CMakeLists.txt
│   ├── SingleLocalTIF
│   └── SingleLocalDEM
└── apps/CMakeLists.txt
    └── HelloEarthDesktop
```

静态库不会要求手动编译两次。构建 `HelloEarthDesktop` 或 Example 时，CMake 会先检查依赖库；源文件没有变化就直接复用已有结果，有变化才重新编译。

## 3. osgEarth 核心对象模型

当前对 osgEarth 程序结构的理解：

```text
osgViewer::Viewer
    ↓ 管理逐帧渲染、Camera 和事件
OSG Scene Graph
    ↓
osgEarth::MapNode
    ↓ 把 Map 转换为可渲染场景
osgEarth::Map
    ↓ 保存真实、有顺序的 Layer 集合
osgEarth::Layer
```

主要职责：

- `Viewer`：逐帧更新和渲染场景。
- `Camera`：决定观察位置、方向、投影和清屏颜色。
- `EarthManipulator`：根据地理 Viewpoint 和鼠标输入控制 Camera。
- `MapNode`：连接 osgEarth 地图数据和 OSG 场景图。
- `Map`：保存 Image、Elevation、Model 等真实图层。
- `Layer`：描述具体数据源和图层行为。

当前程序只使用一个 `MapNode` 和一个 `Map`，但界面树节点保留了 Map UID，便于未来扩展多 Map。

## 4. 本地 GeoTIFF 的加载语义

加载本地影像使用：

```cpp
osgEarth::GDALImageLayer
```

DEM 使用：

```cpp
osgEarth::GDALElevationLayer
```

调用 `open()` 或把图层加入 Map 后，表示数据源已经完成打开和元数据初始化，不表示整幅影像已经全部读进内存或显存。

后续渲染阶段会根据 Camera、LOD 和 TileKey 按需读取像素窗口并生成地形瓦片。

因此：

```text
Layer 打开成功
    ≠ 所有像素已经加载
    ≠ 所有层级一定能高效显示
```

检查图层状态可以使用：

```cpp
layer->getStatus().isError()
```

## 5. DataExtent 与 Viewpoint

打开 TileLayer 后，可以通过数据范围获得观察中心和地面跨度：

```cpp
const osgEarth::DataExtent& extent =
    layer.getDataExtentsUnion();
```

`const T&` 的含义：

- `&`：引用已有对象，不额外复制。
- `const`：只读，不能通过该引用修改原对象。

`isValid()` 主要确认范围和 SRS 对象具备最低限度的有效结构，不等于完成完整的数据质量、经纬度范围或业务正确性检查。

公共模块 `HelloEarth::Navigation` 中的：

```cpp
calculateInitialViewpoint(layer, options)
```

执行的核心流程是：

1. 读取图层总体 DataExtent。
2. 计算中心 `GeoPoint`。
3. 把宽、高换算成米。
4. 取最大跨度并乘 `rangeScale`。
5. 设置 heading、pitch、range 和中心高程。
6. 返回 `std::optional<osgEarth::Viewpoint>`。

`std::optional` 表示计算可能失败：成功时保存 Viewpoint，失败时返回 `std::nullopt`。

Viewpoint 的主要参数：

- `focalPoint`：相机观察的地理目标点。
- `heading`：绕观察目标的水平方向。
- `pitch`：俯仰角，`-90°` 接近垂直俯视。
- `range`：相机到目标点的三维直线距离，不是窗口显示宽度。

EarthManipulator 会根据这些参数自动计算 Camera 的实际三维位置和姿态。

## 6. Overview 金字塔与 RasterPreprocessor

大型 GeoTIFF 如果没有可用 Overview，在低分辨率视角下仍可能需要读取高分辨率原图并实时降采样，造成读取和瓦片生成开销过大。

当前学习阶段约定：只有宽或高超过 `1024` 且没有可用金字塔时，才需要新建 Overview；但只要已经存在 Overview，无论影像尺寸大小，都要检查其可信度。

当前 `HelloEarth::Raster::prepareRasterForLoading()` 已形成以下处理闭环：

```text
输入 TIFF
    ↓
检查单一 TIFF 的尺寸、波段等基本信息
    ↓
是否存在外部同名 .ovr？
    ├── 有：严格检查波段数、层数和各层尺寸
    │       ├── 可信：直接使用
    │       └── 不可信：备份旧 OVR，重建并再次验证
    └── 无：检查内部 Overview
            ├── 不存在：根据尺寸决定是否建立外部 OVR
            ├── 可信：直接加载原 TIFF
            └── 不可信：创建隔离 VRT，为 VRT 建外部 OVR并验证
```

关键设计结论：

- 外部 `.ovr` 与内部 Overview 必须分开处理。
- 对不完整或结构不一致的 Overview，不采用“局部相信、局部补全”，而是倾向于完整重建，牺牲少量性能换取稳定性。
- 不直接破坏带有不可信内部 Overview 的原 TIFF，而是使用 VRT 隔离数据访问和外部 Overview。
- 预处理函数返回最终加载路径，因此 osgEarth 可能加载原 TIFF，也可能加载生成的 VRT。
- Overview 处理目前仍同步执行；真正产品化时应迁移到后台线程，并支持进度、取消和失败恢复。

## 7. 影像与 DEM 的三维组合

影像和 DEM 在 osgEarth 中承担不同职责：

- ImageLayer 提供地表颜色纹理。
- ElevationLayer 提供地形高度。
- MapNode/Terrain Engine 组合二者，生成有纹理的三维地形。

它们不需要简单地按照同一个二维图层列表互相覆盖。当前 Layers Dock 把影像和 DEM 分组管理，同类数据维护覆盖顺序，osgEarth 根据图层类型和渲染机制组合不同类别。

## 8. Qt 嵌入 osgEarth Viewer

桌面端使用：

```text
QMainWindow
└── EarthViewWidget : QOpenGLWidget
```

`EarthViewWidget` 内部持有 Viewer、MapNode、EarthManipulator 和 `GraphicsWindowEmbedded`。

可以把 `GraphicsWindowEmbedded` 理解为 osgViewer 使用的嵌入式图形窗口接口。Qt 显示真实控件，OSG 不再自己创建顶层窗口；Qt 收到的鼠标事件被转换后交给 OSG 事件队列。

渲染循环：

```text
QTimer timeout
    ↓
EarthViewWidget::update()
    ↓
Qt 安排 paintGL()
    ↓
viewer_->frame()
```

`update()` 不会重新构造 EarthViewWidget，只是请求重绘；`QOpenGLWidget` 的机制会在合适时机调用 `paintGL()`。

鼠标交互流程：

```text
Qt mousePress/mouseMove/mouseRelease/wheel
    ↓
GraphicsWindowEmbedded 的事件队列
    ↓
Viewer 事件遍历
    ↓
EarthManipulator 更新相机状态
    ↓
下一帧按新 Camera 渲染
```

## 9. 稳定的图层加载顺序

曾经出现过全球和局部高分辨率影像组合后，在全球/局部视点之间快速切换导致 NVIDIA OpenGL 驱动线程异常的问题。

当前采用的稳定流程为：

```text
栅格预处理
    ↓
创建并 open() osgEarth Layer，但暂不加入 Map
    ↓
根据 Layer 的 DataExtent 计算 Viewpoint
    ↓
EarthManipulator 飞向目标范围
    ↓
轮询 isSettingViewpoint()，等待飞行结束
    ↓
把 Layer 加入 Map
    ↓
发出 layerAdded 信号
```

同时为局部 Image/Elevation Layer 设置与其 Viewpoint range 相关的最大可视距离，避免在远超数据适用尺度的全球视角下继续调度局部高分辨率栅格。

当前还通过 `rasterLayerLoadPending_` 限制同一时间只启动一个栅格异步加载请求，防止多个飞行和延迟 addLayer 回调互相覆盖。

## 10. Layers Dock 与真实 osgEarth Layer 的关联

当前树结构：

```text
Global Map
├── Imagery Layers
│   ├── local_high.tif
│   └── global.tif
└── Elevation Layers
    └── dem.tif
```

每个节点通过 `Qt::UserRole` 及其偏移保存业务数据：

- `ItemTypeRole`：Map、分类、影像叶子或 DEM 叶子。
- `MapUidRole`：对应 osgEarth Map UID。
- `LayerUidRole`：对应真实 osgEarth Layer UID。

界面显示文字可能被修改或翻译，所以业务逻辑不能依赖 `item->text()` 判断节点类型，而应读取 Role。

Qt 信号/槽负责连接界面与三维数据：

```text
EarthViewWidget 创建 Map/Layer
    ↓ emit mapCreated / imageryLayerAdded / elevationLayerAdded
MainWindow 收到信号
    ↓
创建并关联树节点
```

已有操作包括：

- 复选框显隐：根据 UID 找到真实 Layer，修改其 enabled 状态。
- 删除：先删除真实 osgEarth Layer，成功后再删除树节点。
- Zoom to Layer：读取真实 TileLayer，计算 Viewpoint 并驱动 EarthManipulator。

## 11. 图层树内部拖动排序

内部拖动是一组事件组成的状态机：

```text
startDrag()
    ↓ 记录 oldIndex 和临时 DragToken
QTreeWidget::startDrag()
    ├── dragEnterEvent()
    ├── dragMoveEvent() 持续验证落点
    └── dropEvent() 让 Qt 真正移动节点
父类 startDrag 返回
    ↓ 根据 DragToken 找到移动后的有效节点
    ↓ 得到 newIndex
emit layerItemMoved(item, oldIndex, newIndex)
    ↓
MainWindow::handleLayerItemMoved()
    ↓
EarthViewWidget::synchronizeLayerOrder()
```

为什么需要 DragToken：Qt 内部移动 QTreeWidgetItem 时可能复制新节点再删除旧节点，因此拖动前保存的原指针在结束后不一定仍然有效。临时 Token 保存在节点的 `Qt::UserRole + 1000` 中，会跟随节点数据一起复制，拖动结束后可以据此找到最终节点。

`QSignalBlocker` 用于防止写入和清除 Token 时触发无关的 `itemChanged` 业务逻辑。

当前内部规则：

- 影像只能在 Imagery Layers 内调整。
- DEM 只能在 Elevation Layers 内调整。
- 真实图层不能成为其他真实图层的子节点。
- 一次只移动一个节点。

MainWindow 按 Qt 从上到下收集同类 Layer UID。osgEarth Map 使用底层到上层顺序，因此同步函数会反转列表，并只替换这一类图层当前占据的全局槽位，其他类型图层保持原位置。

同步失败时，Qt 节点会回滚到 `oldIndex`，并再次尝试恢复 osgEarth 原始顺序。

## 12. 从资源管理器拖入 TIFF

外部文件拖入和内部节点排序共用 `dragEnterEvent()`、`dragMoveEvent()` 和 `dropEvent()`，通过：

```cpp
event->source() == this
```

区分来源。内部节点来源是当前 LayerTreeWidget；资源管理器文件通常不是当前 Qt 对象。

文件先以 `QMimeData` 的 URL 列表进入程序。当前要求：

- 只有一个 URL。
- 必须是本地普通文件。
- 文件存在并可读。
- 后缀为 `.tif` 或 `.tiff`。

外部树落点规则：

- 分类节点表面和真实图层表面禁止。
- 分类节点下方的提示线表示插入分类顶部。
- 真实图层上方/下方的提示线表示对应索引。
- Map 节点和空白区域禁止。

Qt 原生 Model 不理解外部 TIFF URL 应如何转换为业务图层，所以外部拖入使用自定义提示线：`dragMoveEvent()` 计算坐标，`paintEvent()` 在树正常绘制完成后用 `QPainter` 画线。

松开鼠标时使用 `repaint()` 立即清除提示线，再发出加载信号；如果只使用 `update()`，同步执行的 GDAL 预处理可能阻塞事件循环，使提示线延迟消失。

外部文件的目标位置通过 `PendingRasterTreeInsertion` 保存，因为文件放下和真实图层加入 Map 不在同一时刻。等 layerAdded 信号到达后，程序在指定索引创建树节点，并复用已有排序同步逻辑调整 osgEarth 顺序。

三维视图也支持外部 TIFF 拖入，但由于三维区域本身无法表达影像/DEM 分类，会弹出窗口让用户明确选择加载类型，新图层默认进入相应分类顶部。

## 13. C++ 与 Qt 语法复习

### `const`

表示对象或引用只读，帮助编译器阻止意外修改。

### 引用 `&`

引用已有对象，不创建另一份副本。作为输出参数时，也可以让函数修改调用者传入的变量。

### 继承

`EarthViewWidget : public QOpenGLWidget` 和 `LayerTreeWidget : public QTreeWidget` 表示在 Qt 父类已有能力上增加自己的状态、事件处理和业务接口。

### `public / protected / private`

- `public`：外部调用者可以使用。
- `protected`：类自身及派生类使用，适合 Qt 事件重写。
- `private`：只允许类自身实现访问。

### 析构函数

例如：

```cpp
~EarthViewWidget() override;
```

对象销毁时自动执行，用于停止定时器、释放 Viewer 关系和 OpenGL 相关资源。`override` 表示重写父类虚析构函数。

## 14. 已处理和仍存在的问题

已经处理：

- GDAL API 链接错误：显式链接 `GDAL::GDAL`。
- MSVC 中文源码警告：使用 UTF-8 保存，并设置 `/utf-8`。
- Terrain Engine 创建失败：在调试环境中配置 OSG 插件路径。
- Qt Windows 光标位图断言：采用当前稳定的 QOpenGLWidget 嵌入方案。
- 全球/局部图层视点切换崩溃：先飞行后 addLayer，并限制局部层最大可视距离。
- Qt 图层树与 osgEarth 顺序不一致：建立 UID 关联、完整顺序同步和失败回滚。
- 外部拖放提示不清晰：增加自定义合法落点提示线。

仍需继续处理：

- `Ctrl+F5` 非调试启动可能出现 Qt 平台插件初始化错误。
- Fontconfig 和 Windows DPI 可能输出警告。
- 栅格预处理在 UI 主线程同步运行，大数据时会阻塞界面。
- 测试数据和全球底图路径仍有硬编码。
- 当前没有形成可直接分发的 Qt/osgEarth 运行时目录。
- 公共模块和 UI 中仍存在体量较大的函数，需要逐步拆分职责。

## 15. 当前里程碑

截至 2026-08-17 已完成：

- osgEarth、GDAL、Qt、CMake 和 vcpkg 开发环境。
- 源码、构建、安装和测试数据分离的 Workspace。
- 三级 CMake Example 结构和公共静态库结构。
- 本地 GeoTIFF 影像与 DEM 三维显示。
- Overview 检查、备份、重建和 VRT 隔离闭环。
- 通用初始 Viewpoint 计算模块。
- Qt 中嵌入 osgEarth Viewer 并实现鼠标交互。
- Layers Dock 与 Map/Layer UID 关联。
- 图层显隐、删除、定位和分类内排序。
- 菜单、右键、三维视图拖入和图层树指定位置拖入。
- 外部文件拖放校验、插入提示和异步目标位置保存。

## 16. 下一阶段建议

优先级较高的工作：

1. 把栅格预处理移到后台任务，状态栏显示进度并允许取消。
2. 清理硬编码路径，增加配置和最近使用数据记录。
3. 完善 Debug、非调试和安装后的 Qt/osgEarth 插件部署。
4. 拆分 MainWindow、LayerTreeWidget 和 RasterPreprocessor 中的大函数。
5. 建立更通用的 Layer Model，为多 Map 和更多数据类型做准备。
6. 继续学习矢量、模型、点云和 BIM 数据在 osgEarth/OSG 中的组织方式。
7. 逐步增加图层透明度、重命名、属性信息和数据源管理。

当前最重要的认识是：界面树、业务数据模型和真实渲染对象是三套不同层次。稳定的软件需要用 UID、信号和明确的同步/回滚规则把它们连接起来，而不能只修改界面或只修改 osgEarth Map。

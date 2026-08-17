# HelloEarth

HelloEarth 是一个面向 osgEarth 初学者的 C++ 学习与原型工程。项目从独立数据加载示例起步，目前已经扩展为包含公共底层模块、影像与 DEM 示例，以及 Qt 桌面端三维 GIS 原型的多 Target CMake 工程。

当前阶段重点不是提供完整 GIS 产品，而是通过可运行、可调试、带有详细注释的代码，逐步理解 osgEarth、OpenSceneGraph、GDAL、Qt 和 CMake 如何协同工作。

## 当前已实现

### 独立学习示例

| Target | 内容 | 状态 |
| --- | --- | --- |
| `SingleLocalTIF` | 加载本地 GeoTIFF 影像，进行栅格预处理并显示在 osgEarth 地球上 | 已完成 |
| `SingleLocalDEM` | 加载本地 DEM，叠加局部影像并显示三维地形 | 已完成 |
| `HelloEarthDesktop` | Qt + osgEarth 桌面端原型，提供三维视图和图层管理 | 持续开发 |

### 公共底层模块

- `HelloEarth::Raster`
  - 检查单一 TIFF 数据集的基本有效性。
  - 检查外部 `.ovr` 和内部 Overview。
  - 对不可信的外部 Overview 进行备份和重建。
  - 为缺少金字塔的大型栅格建立外部 Overview。
  - 在内部 Overview 不可信时创建隔离 VRT，并为 VRT 建立外部 Overview。
  - 向调用者返回最终应交给 osgEarth 加载的 TIFF 或 VRT 路径。
- `HelloEarth::Navigation`
  - 根据已经打开的 `osgEarth::TileLayer` 数据范围计算观察中心。
  - 将图层范围换算为米，并估算相机 `range`。
  - 返回可供 `EarthManipulator` 使用的 `osgEarth::Viewpoint`。

### Qt 桌面端

- 使用 `QMainWindow`、`QDockWidget` 和状态栏搭建桌面端框架。
- 使用 `QOpenGLWidget` 嵌入 osgEarth/OSG Viewer。
- 通过定时刷新驱动 Viewer 持续逐帧渲染。
- 将 Qt 鼠标按键、移动和滚轮事件转发给 OSG 事件队列，实现地球旋转、平移和缩放。
- 启动时加载本地全球 GeoTIFF 底图。
- 通过菜单、分类节点右键菜单和文件拖放加载本地影像或 DEM。
- 加载前复用 `HelloEarth::Raster` 执行金字塔预处理。
- 先打开图层并计算 Viewpoint，在相机飞行结束后再把图层加入 Map，降低大范围视点切换时的渲染压力。
- 使用最大可视距离限制局部栅格在全球尺度下的调度范围。
- Layers Dock 按 `Map / Imagery Layers / Elevation Layers / Layer` 组织数据。
- 图层树叶子与真实 osgEarth Map UID、Layer UID 建立关联。
- 支持图层显隐、删除和“Zoom to Layer”。
- 支持影像和 DEM 分类内部拖动排序，并同步真实 osgEarth 图层顺序。
- 支持把单个 `.tif/.tiff` 从资源管理器拖入三维视图，由用户选择影像或 DEM。
- 支持把单个 TIFF 拖到图层树的指定分类和具体插入位置。
- 为外部文件拖放提供合法落点检查、禁止光标和自定义插入提示线。

## 工程结构

```text
HelloEarth/
├── apps/
│   ├── CMakeLists.txt
│   └── HelloEarthDesktop/
│       ├── CMakeLists.txt
│       ├── main.cpp
│       ├── MainWindow.h/.cpp
│       ├── EarthViewWidget.h/.cpp
│       └── LayerTreeWidget.h/.cpp
├── examples/
│   ├── CMakeLists.txt
│   ├── imagery/
│   │   ├── CMakeLists.txt
│   │   └── SingleLocalTIF.cpp
│   └── elevation/
│       ├── CMakeLists.txt
│       └── SingleLocalDEM.cpp
├── include/HelloEarth/
│   ├── raster/RasterPreprocessor.h
│   └── navigation/ViewpointCalculator.h
├── src/
│   ├── CMakeLists.txt
│   ├── raster/
│   │   ├── CMakeLists.txt
│   │   └── RasterPreprocessor.cpp
│   └── navigation/
│       ├── CMakeLists.txt
│       └── ViewpointCalculator.cpp
├── .vscode/launch.json
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── LEARNING_NOTES.md
└── README.md
```

CMake 的组织关系为：

```text
顶层 CMakeLists.txt
├── src/       → 构建可复用静态库
├── examples/  → 构建独立学习程序
└── apps/      → 构建 Qt 桌面应用
```

## 推荐 Workspace 结构

源码、构建结果、安装结果和测试数据相互分离：

```text
HelloEarthWorkspace/
├── HelloEarth/       # Git 源码仓库
├── build/            # CMake 构建结果
├── install/          # CMake 安装结果
└── testdata/         # 本地测试数据，不进入 Git
```

`CMakePresets.json` 会把构建和安装结果输出到源码仓库外部。

## 开发环境

当前项目在以下环境中开发和验证：

- Windows 10/11 x64
- Visual Studio 2022 / MSVC x64
- Visual Studio Code
- CMake 3.20+
- vcpkg Manifest Mode
- Qt 6 Widgets、OpenGL、OpenGLWidgets
- OpenSceneGraph 3.6.5
- osgEarth 3.8
- GDAL 3.12.4
- C++17

## 构建前准备

安装 Visual Studio 2022 C++ 桌面开发工具、CMake、Git、vcpkg，以及需要时使用的 VS Code CMake Tools/C++ 扩展，并设置：

```text
VCPKG_ROOT=<本机 vcpkg 根目录>
```

例如：

```text
VCPKG_ROOT=D:\work\tool\vcpkg
```

依赖由 `vcpkg.json` 声明，包括 `osgearth`、`gdal` 和带 Widgets/OpenGL 功能的 `qtbase`。

## 配置与构建

在仓库根目录执行：

```powershell
cmake --preset windows-msvc
cmake --build --preset debug --target HelloEarthDesktop SingleLocalTIF SingleLocalDEM
```

也可以只构建一个 Target：

```powershell
cmake --build --preset debug --target HelloEarthDesktop
```

构建结果位于仓库外部的 `../build` 目录。

## 运行与调试

使用 VS Code 打开 `HelloEarthWorkspace/HelloEarth`，在“运行和调试”中选择：

- `SingleLocalTIF Debug`
- `SingleLocalDEM Debug`
- `HelloEarthDesktop Debug`

然后按 `F5`。`launch.json` 负责设置 Debug DLL、Qt 插件和 OSG 插件搜索路径。

> 当前已验证 F5 调试启动。`Ctrl+F5` 非调试启动仍可能出现 Qt Windows 平台插件初始化错误，后续需要继续整理统一的运行时部署方案。

## 测试数据

测试数据不提交到 GitHub，统一放在：

```text
HelloEarthWorkspace/testdata/
```

当前部分 Example 和桌面端全球底图仍使用源码中的本地绝对路径。首次运行前，需要把这些路径改为本机数据位置：

- `examples/imagery/SingleLocalTIF.cpp`
- `examples/elevation/SingleLocalDEM.cpp`
- `apps/HelloEarthDesktop/EarthViewWidget.cpp`

桌面端运行后，可以通过菜单、右键分类节点或拖放继续加载其他本地 TIFF。

## 当前边界与已知问题

- 当前正式支持的外部拖入格式为单个 `.tif/.tiff`。
- TIFF 是作为影像还是 DEM，由菜单入口、目标分类或用户选择决定，不根据像素内容自动猜测。
- 当前只管理一个 osgEarth `Map`；数据结构已经保留 Map UID，为未来多 Map 扩展做准备。
- 栅格预处理目前是同步执行，处理大型金字塔时可能阻塞 UI；后续应迁移到后台任务并提供进度、取消和错误反馈。
- `RasterPreprocessor.cpp` 功能闭环已经形成，但函数体量仍较大，后续应进一步拆分检查、构建、备份和 VRT 管理职责。
- 启动时可能出现 Fontconfig 或 Windows DPI 警告，目前不影响核心影像、DEM 和交互功能。
- osgEarth、Qt 和 OSG 插件路径目前依赖开发环境配置，尚未形成完整的可分发安装包。

## 后续方向

- 将栅格预处理改造成后台任务，并在状态栏显示进度。
- 把硬编码测试路径改为配置或用户设置。
- 完善 Qt/OSG/osgEarth 运行时部署。
- 支持多 Map 管理与切换。
- 增加矢量、三维模型、点云和 BIM 数据加载。
- 增加图层属性、重命名、透明度和数据源信息。
- 研究二维视图与三维视图联动。
- 在功能逐步稳定后补充自动化测试和更细的公共模块划分。

## 学习记录

更详细的概念、设计原因、问题排查和阶段总结见 [LEARNING_NOTES.md](LEARNING_NOTES.md)。

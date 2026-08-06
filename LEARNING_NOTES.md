# HelloEarth 学习笔记

## 项目简介

HelloEarth 是一个基于 osgEarth 的三维 GIS 学习工程。

本项目主要用于学习：

- C++ 工程开发流程
- CMake 项目管理
- vcpkg 依赖管理
- OpenSceneGraph（OSG）三维渲染框架
- osgEarth 三维地球引擎
- 遥感数据与三维 GIS 的结合应用


## 我的学习背景

本人具有以下专业背景：

- 测绘工程
- 摄影测量与遥感
- 多模态遥感影像处理
- 无人机遥感
- GIS 数据处理

当前学习目标：

从遥感算法研究方向，进一步拓展到：

```
遥感数据
    ↓
GIS数据组织
    ↓
三维可视化引擎
    ↓
工程数字化平台
```

最终希望掌握面向工程应用的：

- 三维 GIS 平台开发
- 数字孪生场景构建
- 遥感数据三维可视化


---

# 一、开发环境配置记录

## 1. 当前开发环境

操作系统：

- Windows


开发工具：

- Visual Studio Code
- C++ Extension
- CMake Tools
- Codex


编译环境：

- Visual Studio 2022 Community
- MSVC x64


构建工具：

- CMake


依赖管理：

- vcpkg manifest 模式


三维 GIS 引擎：

- OpenSceneGraph 3.6.5
- osgEarth 3.8


---

# 二、工程目录结构

当前工程结构：

```
HelloEarth
│
├── CMakeLists.txt          # CMake工程配置
│
├── vcpkg.json              # 项目依赖管理
│
├── main.cpp                # 主程序
│
├── build/                  # 编译生成目录
│
├── .vscode/
│      └── launch.json      # VS Code调试配置
│
├── AGENTS.md               # AI助手行为规范
│
└── LEARNING_NOTES.md       # 学习笔记
```


---

# 三、osgEarth环境搭建过程

## 1. 使用vcpkg安装osgEarth

采用官方推荐方式：

```bash
vcpkg install osgearth:x64-windows
```

安装工具：

```bash
vcpkg install osgearth[tools]:x64-windows --recurse
```


安装完成后获得：

- osgEarth库
- OpenSceneGraph库
- GDAL
- PROJ
- CURL
- 图像处理相关依赖


---

# 四、遇到的问题及解决过程


## 问题1：CMake无法找到osgEarth


### 错误现象

```
Could not find a package configuration file provided by "osgEarth"
```


### 原因

CMake没有正确连接vcpkg工具链。


### 解决方法

使用vcpkg toolchain：

```
CMAKE_TOOLCHAIN_FILE

D:/work/tool/vcpkg/scripts/buildsystems/vcpkg.cmake
```


之后通过：

- VS Code CMake Tools
- Visual Studio MSVC x64 Kit

成功完成配置。


---

# 问题2：官方osgearth_viewer可以运行，但是自己的程序运行失败


### 现象

出现：

```
FAILED to create a terrain engine for this map
```


### 原因分析

osgEarth不仅依赖动态库，还依赖运行时插件。


主要包括：

```
osgEarth.dll

osgPlugins/

osgdb_earth.dll

osgdb_osgearth_engine_rex.dll
```


编译成功 ≠ 运行环境完整。


---

# 问题3：VS Code调试时无法创建地形


### 原因

VS Code启动程序时没有继承osgEarth插件路径。


### 解决方法

通过：

```
.vscode/launch.json
```

配置运行环境：


包括：

- PATH
- OSG_LIBRARY_PATH


使Debug运行时能够找到：

- osgEarth动态库
- OSG插件
- Terrain Engine插件


---

# 五、VS Code开发环境配置


目前已经实现：

## 编译流程

```
修改代码

↓

CMake生成

↓

Debug编译

↓

生成HelloEarth.exe
```


## 调试流程

```
F5

↓

启动cppvsdbg

↓

进入断点

↓

查看变量

↓

单步调试
```


---

# 六、调试功能验证


## 断点测试


测试代码：

```cpp
osgEarth::initialize();
```


测试结果：

成功进入断点。


说明：

- VS Code调试器正常
- Debug符号正常
- MSVC环境正常


---

## 调试过程中遇到的问题


使用F11（逐语句）时进入：

```
new_scalar.cpp

MSVC Runtime Library
```


原因：

F11会进入函数内部，包括：

- C++标准库
- MSVC运行库
- new/delete实现


这是正常现象。


日常调试建议：

使用：

```
F10 逐过程
```

避免进入第三方库。


---

# 七、目前对osgEarth架构的初步理解


当前理解：

```
osgViewer::Viewer

        ↓

OpenSceneGraph Scene Graph

        ↓

osgEarth::MapNode

        ↓

osgEarth::Map

        ↓

Layer数据层
```


---

## 1. Viewer

负责：

- 创建窗口
- 渲染循环
- 相机控制


类似：

三维场景的显示管理器。


---

## 2. MapNode

osgEarth核心节点。

负责连接：

- 地图
- 图层
- 地形引擎


可以理解为：

三维地球场景的入口。


---

## 3. Layer


负责管理不同类型的数据。


例如：

影像：

```
ImageLayer
```


高程：

```
ElevationLayer
```


模型：

```
ModelLayer
```


矢量：

```
FeatureLayer
```


---

# 八、当前学习状态


已经完成：

✅ vcpkg安装osgEarth

✅ CMake工程配置

✅ VS Code开发环境搭建

✅ C++代码提示

✅ Debug断点调试

✅ osgEarth基础地球显示


目前已经从：

```
环境搭建阶段
```

进入：

```
osgEarth应用开发学习阶段
```


---

# 九、下一阶段学习计划


## 第一阶段：理解osgEarth基础架构

学习：

- Viewer
- Scene Graph
- MapNode
- Map
- Layer


目标：

理解一个三维地球程序如何运行。


---

## 第二阶段：遥感数据加载


重点学习：

### 影像

```
GeoTIFF
GDALImageLayer
ImageLayer
```


### 高程

```
DEM

ElevationLayer

TerrainEngine
```


目标：

实现：

```
遥感影像
+
DEM

↓

三维地形展示
```


---

## 第三阶段：GIS数据融合


学习：

- Shapefile
- GeoJSON
- 矢量图层
- 空间查询


---

## 第四阶段：工程应用开发


学习：

- 鼠标拾取
- 坐标转换
- 属性查询
- 三维模型加载
- 数字孪生场景


---

# 十、2026-07-31 总结


今天完成了HelloEarth项目最重要的第一步：

建立完整的osgEarth C++开发环境。


解决的问题：

1. vcpkg安装osgEarth
2. CMake查找osgEarth
3. VS Code配置CMake Tools
4. Debug运行环境配置
5. osgEarth插件加载
6. C++断点调试


最终实现：

```
VS Code

+

CMake

+

vcpkg

+

osgEarth

+

MSVC Debug

↓

成功运行三维地球
```


后续继续学习osgEarth内部架构，
逐步实现遥感影像、DEM、三维模型以及工程GIS数据的加载与可视化。


---

# 十一、2026-08-05 本地 RGB GeoTIFF 加载学习记录


## 1. osgEarth 程序的基本运行骨架


今天进一步理解了一个独立 osgEarth 程序的完整流程：

```
main()
    ↓
osgEarth::initialize()
    ↓
创建 ArgumentParser 和 Viewer
    ↓
登记 GL3RealizeOperation
    ↓
创建 MapNode、Map 和 Layer
    ↓
将 MapNode 设置为 Viewer 的场景数据
    ↓
安装 EarthManipulator
    ↓
viewer.run() 进入事件与渲染循环
```


各对象的主要职责：

- `Viewer`：管理窗口、Camera、事件和逐帧渲染。
- `Map`：地图的数据模型，保存有顺序的 Layer 集合。
- `MapNode`：一个可渲染的 OSG 场景节点，负责把 Map 转换成三维地图场景。
- `Layer`：为地图提供影像、高程、矢量等数据。
- `EarthManipulator`：根据地理视点和鼠标输入控制 Camera。


一个 `MapNode` 在同一时刻持有并渲染一个 `Map`；一个 `Map` 可以包含多个有顺序的 Layer。

影像图层的顺序比较接近传统 GIS 的上下叠放关系；高程、矢量和三维模型还会受到数据组合方式、深度测试、海拔和渲染状态影响，不能全部简单理解为二维图层覆盖。


---

## 2. 本地 RGB GeoTIFF 的加载链路


本地 RGB GeoTIFF 使用：

```cpp
osgEarth::GDALImageLayer
```


基本数据链路：

```
本地 GeoTIFF 路径
    ↓ setURL()
GDALImageLayer
    ↓ Map::addLayer()
GDAL 打开数据集并读取元数据
    ↓
MapNode 按当前视野请求地图瓦片
    ↓
GDAL 按需读取像素窗口
    ↓
生成纹理并渲染到地球表面
```


重要结论：

```cpp
mapNode->getMap()->addLayer(imagery);
```

执行后，图层已经完成数据源的打开和初始化，可以读取状态、空间参考和数据范围；但整幅影像并没有一次性全部加载到内存或显存。像素主要在后续渲染过程中，根据 Camera 和 `TileKey` 按需读取。


使用以下接口检查图层是否打开成功：

```cpp
imagery->getStatus().isError()
```


`getStatus()` 成功表示数据集成功打开，不代表所有分辨率层级的瓦片都一定能快速生成或成功显示。


---

## 3. DataExtent 与数据范围


图层打开成功后，通过：

```cpp
const osgEarth::DataExtent& imageExtent =
    imagery->getDataExtentsUnion();
```


获取图层数据覆盖范围。

这里使用 `const T&` 的含义是：

- `&`：引用 osgEarth 内部已经存在的对象，不额外复制一份。
- `const`：只能读取，不能通过该引用修改原对象。


使用：

```cpp
imageExtent.isValid()
```


进行最低限度的结构检查。它主要确认：

- 存在有效的 SRS 对象。
- 范围宽度非负。
- 范围高度非负。


它不负责判断投影声明在业务上是否正确，也不负责进行完整的经纬度合法性和数据质量检查。


`DataExtentList` 不是 Shapefile 中每个 Feature 的包围框列表，而是表示一个瓦片图层在哪些空间范围和 LOD 上能够提供数据。

例如一个稀疏 TMS 图层可能同时报告：

```
DataExtent 1：北京，LOD 10～18
DataExtent 2：上海，LOD 10～18
```


`getDataExtentsUnion()` 会返回能够包住所有有效范围的总外接范围。对于当前一个 `GDALImageLayer` 对应一个普通 GeoTIFF 的情况，通常只有一个 `DataExtent`，所以 Union 就是该 TIFF 自身的四至。


---

## 4. 从影像范围计算观察视点


首先取得范围中心：

```cpp
const osgEarth::GeoPoint imageCenter =
    imageExtent.getCentroid();
```


`GeoPoint` 不只是 XYZ 数值，它还携带对应的 `SpatialReference`，因此不需要提前手动把投影坐标转换成经纬度。


然后把范围宽高统一换算成米：

```cpp
const double widthMeters =
    imageExtent.width(osgEarth::Units::METERS);

const double heightMeters =
    imageExtent.height(osgEarth::Units::METERS);
```


不能简单使用 `xMax - xMin` 作为 Camera 距离，因为原始 SRS 单位可能是度、米或英尺。


当前使用的初始距离估算：

```cpp
const double maxSpanMeters =
    std::max(widthMeters, heightMeters);

const double cameraRangeMeters =
    maxSpanMeters * 2.0;
```


`2.0` 是适合当前实验的经验系数，不是严格保证完整显示的通用常数。精确距离还与 Camera 的视场角、窗口宽高比、影像长宽比和 pitch 有关。


---

## 5. Viewpoint 的含义


一个 `Viewpoint` 主要包含：

```
focalPoint
heading
pitch
range
```


含义：

- `focalPoint`：Camera 看向的地理目标点。
- `heading`：Camera 围绕目标点的水平方向。
- `pitch`：Camera 相对目标点的俯仰方向。
- `range`：Camera 到目标点的三维直线距离。


当：

```text
pitch = -90°
```


Camera 垂直俯视目标点，此时 `range` 可以近似理解为 Camera 与目标点之间的高差。

当相机倾斜观察时，`range` 是斜距，不能再简单理解为垂直高度。


`EarthManipulator` 会根据：

```
focalPoint + heading + pitch + range
```


自动计算 Camera 的三维位置、方向和观察矩阵，不需要直接设置 Camera 的 XYZ。


---

## 6. setHomeViewpoint 与 setViewpoint


两个接口的语义不同：

```cpp
manipulator->setHomeViewpoint(viewpoint);
```


用于设置：

- 程序启动时采用的 Home 位置。
- 用户触发 Home 操作时返回的位置。


```cpp
manipulator->setViewpoint(viewpoint, durationSeconds);
```


用于主动改变当前视角，适合：

- “缩放至图层”。
- 点击目标后飞行定位。
- 2D/3D 视图同步。
- 程序运行期间切换观察目标。


当前独立 Viewer 实验中，只在 `viewer.run()` 前调用 `setViewpoint()`，初始视角会受到 Viewer 首次 Camera/Home 初始化的影响；设置 `setHomeViewpoint()` 后，Viewer 启动时能够正确采用影像视点。

当前程序只保留 `setHomeViewpoint()`，也可以在启动时正确定位到影像范围。


---

## 7. Overview 金字塔问题及实验结论


测试数据：

```text
ref.tif
尺寸：16021 × 15842
文件大小：约 875 MB
```


第一次测试时没有内部 Overview，也没有外部 `.ovr`：

```
Home Viewpoint 已经定位成功
    ↓
可以看到带光照变化的地形表面
    ↓
影像纹理没有及时出现
```


把对应的外部金字塔放到 TIFF 同级目录：

```text
ref.tif
ref.tif.ovr
```


再次运行后，影像能够正常显示。


原因：

```
没有 Overview
    ↓
低分辨率显示仍需从完整分辨率数据读取并实时降采样
    ↓
瓦片生成代价很高
    ↓
影像纹理迟迟不能出现

存在 Overview
    ↓
GDAL 选择接近当前显示分辨率的概览层
    ↓
快速生成 osgEarth 所需瓦片
    ↓
影像正常显示
```


重要认识：

- `.ovr` 不改变 GeoTIFF 的地理位置和投影。
- `.ovr` 主要改善多尺度读取性能。
- 不能只通过是否存在同名 `.ovr` 文件判断金字塔，因为 GeoTIFF 也可能具有内部 Overview。
- 正式程序应该使用 GDAL 波段的 `GetOverviewCount()` 检查 GDAL 实际可用的 Overview。
- 小影像不一定必须构建 Overview；大型影像应根据尺寸、访问方式和显示需求决定。
- 构建 Overview 不宜在 UI/渲染线程中同步进行，也不宜与 osgEarth 同时读写同一数据集。


推荐的后续加载流程：

```
用户选择 TIFF
    ↓
GDAL 只读检查数据与 Overview
    ↓
判断是否需要构建
    ├── 不需要 → 创建 GDALImageLayer
    └── 需要   → 后台构建并关闭 GDALDataset
                         ↓
                  创建 GDALImageLayer
    ↓
Map::addLayer()
    ↓
设置 Home/Viewpoint
```


---

## 8. 直接使用 GDAL API 的工程配置


由于 HelloEarth 后续会直接调用 GDAL API，因此在 `vcpkg.json` 中显式声明：

```json
"dependencies": [
    "osgearth",
    "gdal"
]
```


在 CMake 中查找并链接：

```cmake
find_package(osgEarth CONFIG REQUIRED)
find_package(GDAL CONFIG REQUIRED)
```


```cmake
target_link_libraries(
    HelloEarth
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
)
```


只包含 `gdal_priv.h` 而不链接 `GDAL::GDAL` 时，编译可以通过，但链接阶段会出现：

```text
LNK2019：无法解析的外部符号 GDALVersionInfo
LNK2019：无法解析的外部符号 GDALAllRegister
```


原因是编译器看到了函数声明，但链接器没有得到 GDAL 函数实现所在的导入库。


当前已经成功调用：

```cpp
GDALAllRegister();
GDALVersionInfo("RELEASE_NAME");
```


并输出：

```text
GDAL version: 3.12.4
```


---

## 9. 源码 UTF-8 配置


为了让 VS Code 与 MSVC 正确处理中文注释：

1. 在 VS Code 中使用 UTF-8 保存 `main.cpp`。
2. 在 CMake 中为 MSVC 添加 `/utf-8`：

```cmake
if(MSVC)
    target_compile_options(
        HelloEarth
        PRIVATE
        /utf-8
    )
endif()
```


这可以避免：

```text
warning C4819
```


已有的乱码注释如果内容本身已经损坏，需要重新输入；仅改变文件编码不能自动恢复已经损坏的文字。


---

## 10. 尚未处理的问题


运行时仍出现：

```text
Fontconfig error: Cannot load default config file
```


该问题当前主要影响字体配置，与本地 GeoTIFF 影像纹理加载不是同一个问题，后续学习文字、标注或 UI 时再单独处理。


---

## 11. 下一次学习起点


下一次从 GDAL Overview 检查开始：

```
显式使用 GDAL C++ API
    ↓
只读打开 TIFF
    ↓
读取 RasterBand
    ↓
GetOverviewCount()
    ↓
输出每一级 Overview 尺寸
    ↓
计算需要的金字塔层级
    ↓
GDALDataset::BuildOverviews()
    ↓
添加错误处理、进度和取消机制
```


今天最终实现：

```
本地 RGB GeoTIFF
    +
外部 .ovr 金字塔
    +
自动计算初始 Home Viewpoint
    ↓
在 osgEarth Viewer 中正确定位并显示
```

# HelloEarth GIS 处理功能开发指南

## 1. 文档目的

本文面向即将参与 HelloEarth GIS 处理功能开发的成员，帮助你快速了解：

- HelloEarth 当前是什么、已经实现了什么；
- GIS 处理模块与三维显示模块如何分工；
- 为什么现阶段不把 QGIS 直接链接进主程序；
- 第一阶段应该完成哪些调查、原型和工程成果；
- 主程序与 GIS 处理程序之间如何交换任务、进度和结果；
- 提交代码前应达到哪些基本质量要求。

本文描述的是当前阶段的开发方向。接口草案会在原型验证后继续调整，不应把尚未实现的示例结构误认为现有正式 API。

## 2. 项目简介

HelloEarth 是一个基于 C++、Qt、OpenSceneGraph 和 osgEarth 的三维 GIS 学习与原型工程。目前项目正在从独立的数据加载示例，逐步演进为具有图层管理、空间数据加载和三维交互能力的桌面端 GIS 原型。

当前主要技术环境：

- Windows 10/11 x64；
- Visual Studio 2022 / MSVC x64；
- CMake 3.20+；
- vcpkg Manifest Mode；
- C++17；
- Qt 6 Widgets、OpenGL、OpenGLWidgets；
- OpenSceneGraph 3.6.5；
- osgEarth 3.8；
- GDAL 3.12.4。

当前仓库主要结构：

```text
HelloEarth/
├── apps/HelloEarthDesktop/       # Qt + osgEarth 桌面端原型
├── examples/                     # 独立数据加载学习示例
│   ├── imagery/                  # 本地影像示例
│   └── elevation/                # 本地 DEM 示例
├── include/HelloEarth/           # 公共模块头文件
├── src/                          # 公共模块实现
│   ├── raster/                   # 栅格预处理
│   └── navigation/               # 视点计算
├── CMakeLists.txt
├── CMakePresets.json
└── vcpkg.json
```

## 3. 当前已经实现的能力

### 3.1 公共模块

`HelloEarth::Raster` 当前能够：

- 检查单个 TIFF 数据集的基本有效性；
- 检查外部 `.ovr` 和内部 Overview；
- 备份并重建不可信的外部 Overview；
- 为缺少金字塔的大型栅格建立外部 Overview；
- 在内部 Overview 不可信时创建隔离 VRT，并为其建立外部 Overview；
- 返回最终应交给 osgEarth 加载的 TIFF 或 VRT 路径。

公共入口为：

```cpp
HelloEarth::Raster::prepareRasterForLoading(...)
```

`HelloEarth::Navigation` 当前能够根据已经打开的 osgEarth `TileLayer` 范围，计算适合相机飞行和观察图层的 `osgEarth::Viewpoint`。

### 3.2 桌面端

`HelloEarthDesktop` 当前已经具备：

- 在 `QOpenGLWidget` 中嵌入 osgEarth/OSG Viewer；
- 地球旋转、平移、缩放等鼠标交互；
- 加载本地 GeoTIFF 影像和 DEM；
- 加载前执行现有栅格预处理；
- 图层树与 osgEarth `Map/Layer` 的 UID 关联；
- 图层显示、隐藏、删除和定位；
- 影像、DEM 分类内的拖动排序及真实渲染顺序同步；
- 从菜单、右键菜单和文件拖放入口加载 TIFF；
- 根据拖放位置决定图层插入位置。

主要类的职责如下：

| 类 | 当前职责 |
| --- | --- |
| `MainWindow` | 菜单、Dock、图层树、用户操作入口和界面协调 |
| `EarthViewWidget` | osgEarth Viewer、Map、Layer、相机和三维交互 |
| `LayerTreeWidget` | 图层树内部拖动和外部文件拖放行为 |

## 4. GIS 处理开发者的职责

你的主要方向是建立 HelloEarth 的 GIS 数据处理能力，而不是负责 osgEarth 的场景渲染细节。

职责范围包括：

1. 调研 QGIS Processing、`qgis_process`、PyQGIS，以及 GDAL/OGR、GEOS、PROJ 等能力边界；
2. 整理平台需要的 GIS 算法目录和参数；
3. 建立可被主程序调用的独立 GIS 处理程序或服务；
4. 定义并实现任务输入、结果输出、进度、取消、日志和错误报告；
5. 对输入数据、坐标参考系、几何类型和输出结果进行验证；
6. 为处理功能编写独立测试和使用文档；
7. 记录所使用组件、算法 Provider、部署依赖和许可证；
8. 将成功处理出的普通数据成果交给主程序统一注册和显示。

第一阶段暂不要求直接修改：

- osgEarth 场景图和渲染流程；
- `EarthManipulator` 相机交互；
- `LayerTreeWidget` 的拖放实现；
- `EarthViewWidget` 内部的 Map/Layer 管理；
- 三维窗口中的要素拾取和编辑逻辑。

确实需要修改这些模块时，应先讨论接口和职责边界，避免每增加一个算法就在 `MainWindow` 或 `EarthViewWidget` 中堆积一套专用逻辑。

## 5. 总体分工与数据流

```text
三维显示与空间编辑
    负责人：osgEarth/OSG 方向
    内容：数据显示、图层生命周期、相机、拾取、选择、编辑

GIS 处理引擎
    负责人：GIS 处理方向
    内容：算法、参数校验、处理任务、进度、取消、日志、结果

业务流程编排
    负责人：后续业务方向
    内容：组合多个算法，形成具体业务工作流

公共任务协议
    共同维护
    内容：数据描述、任务请求、任务状态、结果和错误格式
```

一个典型的数据流应当是：

```text
用户在 HelloEarth 中选择数据和操作
                ↓
主程序构造 Processing Job
                ↓
独立 GIS 处理进程执行算法
                ↓
生成结果文件并返回结果描述
                ↓
主程序验证结果并加入图层树和 osgEarth Map
```

关键原则：GIS 处理模块负责“计算出结果”，主程序负责“如何把结果显示和管理起来”。

## 6. 为什么第一阶段采用独立处理进程

QGIS 是完整的 GIS 平台和 Processing 框架，不是一个轻量、无依赖的算法 DLL。当前不计划把 QGIS C++ API 直接链接进 `HelloEarthDesktop`。

推荐的第一阶段结构：

```text
HelloEarthDesktop.exe
    └── ProcessingService（主程序侧调用接口）
            ↓ 启动进程并交换参数/JSON/文件路径
        qgis_process 或独立 GIS Worker
            ↓
        QGIS Processing / GDAL / GEOS 等处理能力
```

这样设计主要为了：

- 隔离双方 Qt、GDAL、PROJ 和插件运行环境；
- 避免 QGIS 处理错误直接导致三维主程序崩溃；
- 允许 GIS 处理模块独立开发、测试和升级；
- 让处理后端未来可以在 QGIS、GDAL/OGR、GEOS 或自研算法之间调整；
- 为商业发布保留相对清晰的程序边界。

第一阶段进程之间只交换普通数据：

- 算法标识；
- 输入输出文件路径；
- 字符串、数字、布尔值、数组等普通参数；
- JSON 消息；
- 进度、日志和错误文本。

不要跨进程传递 `QgsVectorLayer*`、`QgsGeometry*`、osgEarth `Layer*` 等框架内部对象。

## 7. QGIS 与底层库的使用原则

不要默认所有 GIS 功能都必须通过 QGIS 完成。选择实现方式时，应记录选择理由。

| 需求类型 | 可优先评估的能力 |
| --- | --- |
| 栅格读写、裁剪、重投影、金字塔 | GDAL |
| 矢量数据读写 | OGR |
| 矢量几何运算 | GEOS / QGIS Processing |
| 坐标转换 | PROJ |
| 丰富的算法目录和模型编排 | QGIS Processing |
| 点云处理 | 后续评估 PDAL 等方案 |

同一个算法应尽量只有一个明确的生产实现，避免主程序、QGIS 脚本和 GDAL 工具各自保存一套行为不一致的版本。

## 8. 处理任务协议草案

以下结构用于指导原型，不是已经实现的正式接口。验证后由双方共同确定字段和版本策略。

### 8.1 请求示例

```json
{
  "protocolVersion": 1,
  "jobId": "20260818-0001",
  "algorithm": "native:buffer",
  "inputs": {
    "INPUT": "D:/data/roads.gpkg|layername=roads"
  },
  "parameters": {
    "DISTANCE": 100.0,
    "SEGMENTS": 8,
    "DISSOLVE": false
  },
  "outputs": {
    "OUTPUT": "D:/results/roads_buffer.gpkg"
  }
}
```

### 8.2 成功结果示例

```json
{
  "protocolVersion": 1,
  "jobId": "20260818-0001",
  "status": "succeeded",
  "progress": 100,
  "outputs": [
    {
      "name": "OUTPUT",
      "path": "D:/results/roads_buffer.gpkg",
      "dataType": "vector"
    }
  ],
  "warnings": [],
  "error": null
}
```

### 8.3 失败结果示例

```json
{
  "protocolVersion": 1,
  "jobId": "20260818-0001",
  "status": "failed",
  "progress": 35,
  "outputs": [],
  "warnings": [],
  "error": {
    "code": "INVALID_CRS",
    "message": "输入数据缺少有效坐标参考系",
    "details": "..."
  }
}
```

### 8.4 协议设计要求

- 使用稳定的算法标识，不依赖中文界面名称；
- 请求和结果都包含协议版本与任务 ID；
- 明确区分失败、取消和成功；
- 错误同时包含机器可判断的错误码和用户可读说明；
- 输出明确数据类型和实际路径；
- 路径处理支持中文、空格和较长路径；
- 算法运行不得静默覆盖原始输入数据；
- 临时文件和正式成果使用不同目录；
- 只有处理成功并通过结果检查后，主程序才能加载成果。

## 9. 第一阶段工作计划

### 阶段 A：需求与环境调查

建议先整理首批最常用的 10 个 GIS 操作，例如：

- 缓冲区；
- 矢量裁剪；
- 相交；
- 融合；
- 重投影；
- 栅格裁剪；
- 栅格重采样；
- 栅格拼接；
- 等高线；
- 山体阴影。

每个操作至少记录：用户问题、输入输出类型、参数、坐标系要求、候选实现方式、Provider、依赖和许可证。

### 阶段 B：命令行原型

先独立验证 `qgis_process`，不要立即修改 HelloEarth 主程序：

1. 能否列出当前环境可用的 Provider 和算法；
2. 能否查询指定算法的参数说明；
3. 能否使用固定测试数据执行算法；
4. 能否可靠解析返回状态和结果路径；
5. 能否获得有意义的日志和错误信息；
6. 如何取消长时间运行的任务；
7. 中文路径、空格路径和大数据是否正常；
8. 环境缺失或 Provider 不可用时如何诊断。

至少跑通三个具有代表性的原型：

- 一个矢量缓冲区；
- 一个矢量裁剪或相交；
- 一个栅格裁剪、重投影或山体阴影。

### 阶段 C：独立 Worker

在命令行原型稳定后，再封装独立处理入口。第一版应优先实现：

- 接收一个任务请求；
- 校验必要字段和输入路径；
- 调用对应处理算法；
- 输出结构化进度或至少阶段日志；
- 返回统一结果 JSON；
- 失败时返回非零退出码；
- 不把调试信息混入结构化结果通道；
- 能被独立测试，而不需要启动 HelloEarthDesktop。

### 阶段 D：与主程序进行最小集成

第一条完整链路建议只选择一个简单算法，例如矢量缓冲区：

```text
Qt 参数窗口
    ↓
创建处理任务
    ↓
QProcess 启动独立 Worker
    ↓
显示日志和完成状态
    ↓
检查输出文件
    ↓
交给统一图层加载入口
```

不要在第一条链路中同时开发算法目录、批处理、模型编排和复杂 UI。先证明边界和数据流可靠，再扩展能力。

## 10. 原型验收标准

首个 GIS 处理原型至少应满足：

- 正确输入能够稳定生成正确结果；
- 缺少输入、错误参数和无效路径会明确失败；
- 输入数据不会被意外覆盖；
- 输出文件在报告成功前已经完整关闭并可重新打开；
- 中文和空格路径经过验证；
- 坐标系与几何有效性问题有可理解的错误信息；
- 进程退出码和结果状态一致；
- 算法失败不会导致 HelloEarthDesktop 崩溃；
- 同一测试数据可以重复运行；
- 有最小使用说明和测试记录；
- 使用到的 Provider、版本和许可证已经记录。

性能测试至少记录 Worker 启动时间、算法处理时间、数据规模、内存情况，以及取消后是否残留进程或临时文件。

## 11. 商业化与许可证注意事项

QGIS 允许商业使用和收费服务，但采用 GNU GPL 许可证。当前技术方案不把 QGIS 直接链接进 HelloEarth 主程序，而是通过独立进程和普通参数、JSON、文件进行通信。

开发过程中必须记录：

- QGIS 的准确版本和来源；
- 使用到的 Processing Provider；
- 第三方插件及其许可证；
- 是否修改了 QGIS 或插件代码；
- 最终是否会随产品安装包分发相关组件；
- 对应源码、许可证文本和版权声明的提供方式。

进程隔离有利于形成清晰的技术边界，但不能代替正式的许可证审核。产品对外发布前，应根据最终代码、通信方式和安装包内容进行法务确认。

不要在产品命名和宣传中让用户误认为 HelloEarth 是 QGIS 官方产品。

## 12. 协作与代码提交建议

### 12.1 分支与提交

- 从最新 `main` 创建自己的功能分支；
- 一个分支尽量只解决一个明确问题；
- 提交前同步 `main` 并处理冲突；
- 不直接向受保护的 `main` 推送；
- 通过 Pull Request 说明设计、测试结果和已知限制。

分支名称示例：

```text
feature/processing-qgis-prototype
feature/processing-job-protocol
feature/vector-buffer-worker
```

### 12.2 Pull Request 至少说明

- 本次解决的问题；
- 采用的实现方式及原因；
- 修改或新增的接口；
- 运行和测试步骤；
- 测试数据及预期结果；
- 新增的运行时依赖；
- 许可证影响；
- 已知问题和后续计划。

### 12.3 模块边界

- GIS Worker 不直接操作 osgEarth `Map` 或 `Layer`；
- 桌面端不依赖某个算法的内部实现细节；
- 公共协议变化需要双方评审；
- 算法输出不能绕开统一图层加载和验证入口；
- 不提交大型测试数据、构建目录、安装目录和本机绝对路径；
- 新依赖应先说明用途、版本、许可证和部署影响。

## 13. 加入项目后的建议阅读顺序

1. 阅读仓库根目录 `README.md`，了解工程能力、构建方式和已知边界；
2. 阅读 `AGENTS.md`，了解项目目标和协作原则；
3. 浏览顶层、`src`、`examples` 和 `apps` 的 CMake 组织方式；
4. 阅读 `RasterPreprocessor.h/.cpp`，了解现有 GDAL 公共能力；
5. 阅读 `ViewpointCalculator.h/.cpp`，理解公共模块如何被桌面端复用；
6. 阅读 `MainWindow::requestRasterLayerLoading()`，了解桌面端统一加载入口；
7. 阅读 `EarthViewWidget::addImageLayer()` 和 `addElevationLayer()`；
8. 独立运行 `SingleLocalTIF`、`SingleLocalDEM` 和 `HelloEarthDesktop`；
9. 再开始 QGIS Processing 环境与算法原型调查。

## 14. 第一次技术评审需要回答的问题

完成第一阶段调查后，请整理一份简短报告，至少回答：

1. 首批算法分别适合使用 QGIS Processing 还是底层库直接实现？
2. 推荐使用哪个 QGIS 版本，为什么？
3. `qgis_process` 的启动、进度、取消和错误报告能力是否满足要求？
4. 是否需要从一次一进程升级为常驻 Worker？
5. Worker 使用什么语言和运行环境最容易稳定部署？
6. 输入输出数据格式应优先选择哪些？
7. 如何处理临时目录、同名成果和失败清理？
8. 如何验证结果坐标系、几何和数据完整性？
9. 最终安装包需要携带哪些 QGIS、Provider 或 Python 组件？
10. 当前方案有哪些许可证和商业发布风险？

这些问题得到验证后，再共同确定正式的目录结构、任务协议和主程序接口。

## 15. 当前阶段的核心目标

当前目标不是一次性复制 QGIS 的全部功能，而是先建立一条可复用、可诊断、可取消、可测试的 GIS 处理通道。

第一条通道稳定后，后续增加算法应主要变成：

1. 注册算法及参数描述；
2. 实现或映射算法执行逻辑；
3. 增加输入输出验证；
4. 增加测试；
5. 由主程序使用统一流程调用并加载成果。

如果新增一个算法仍然必须同时大量修改 Worker、`MainWindow`、`EarthViewWidget` 和图层树，说明模块边界还不够稳定，应先调整公共接口，而不是继续堆叠功能。

## 16. 官方参考资料

- [QGIS Processing 框架介绍](https://docs.qgis.org/4.2/en/docs/user_manual/processing/intro.html)
- [`qgis_process` 独立运行说明](https://docs.qgis.org/4.2/en/docs/user_manual/processing/standalone.html)
- [PyQGIS 开发者手册](https://docs.qgis.org/3.44/en/docs/pyqgis_developer_cookbook/)
- [QGIS 第三方 Processing Provider 说明](https://docs.qgis.org/3.44/en/docs/user_manual/processing/3rdParty.html)
- [QGIS 官方许可证](https://www.qgis.org/license/)
- [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html)

版本号和接口可能变化。正式实现前应根据项目锁定的 QGIS 版本重新核对相应版本文档。

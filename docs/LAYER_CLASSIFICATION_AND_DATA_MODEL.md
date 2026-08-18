# HelloEarth 图层分类与数据模型设计指南

## 1. 文档目的

本文用于说明 HelloEarth 在逐步支持影像、高程、专题栅格、矢量、三维模型、点云和 BIM 等数据时，应如何组织图层分类和内部数据描述。

这份设计的目标是：

- 让图层树的分类符合 GIS 用户的理解；
- 让新增文件格式时不必重新设计整个图层树；
- 区分数据用途、渲染方式和存储格式；
- 为 GIS 处理成果自动进入软件展示建立稳定入口；
- 为未来的图层属性、项目保存、要素编辑和业务追踪预留结构；
- 避免让 `QTreeWidgetItem` 永久承担全部项目数据职责。

本文包含当前已确定的原则，也包含需要随功能开发逐步验证的演进建议。尚未实现的类名和结构示例不代表已经存在的正式 API。

## 2. 当前图层结构

HelloEarth 当前只正式支持一个 osgEarth `Map`，图层树为其建立以下分类：

```text
Global
├── Imagery Layers
│   ├── global.tif
│   └── local_ortho.tif
└── Elevation Layers
    └── local_dem.tif
```

当前已经实现：

- 影像和 DEM 加载；
- 图层树节点与 osgEarth Map UID、Layer UID 的关联；
- 图层显隐、删除、定位和分类内部排序；
- 从菜单、分类节点右键菜单和文件拖放入口加载 TIFF；
- 根据目标分类判断 TIFF 应作为影像还是高程加载。

目前这种分类是正确的，因为影像和 DEM 虽然都可能使用 `.tif`，但在 osgEarth 中承担不同角色：

- 影像通过 `ImageLayer` 为地表提供颜色；
- DEM 通过 `ElevationLayer` 为地形提供高度。

## 3. 核心分类原则

### 3.1 图层树按数据角色分类

图层树的主要分类应回答：

> 这份数据在地图和应用中承担什么角色？

推荐的角色包括：

```text
Imagery
Elevation
ThematicRaster
Vector
Model
PointCloud
BIM
Annotation
Table
```

这些名称面向用户和产品功能，相对稳定。

### 3.2 不按文件扩展名建立顶层分类

不要建立这样的图层树：

```text
TIF Files
SHP Files
GPKG Files
JSON Files
```

原因是扩展名不能可靠表示数据用途：

- TIFF 可以是真彩色影像、DEM、NDVI、坡度或土地利用分类；
- GeoPackage 可以同时包含多个矢量层、普通表和栅格数据；
- JSON 可以是 GeoJSON，也可能是 3D Tiles 的 `tileset.json`；
- CSV 可以是普通表，也可能包含经纬度字段；
- NetCDF/HDF 可能包含多个变量、时间步和子数据集。

文件格式应作为图层元数据保存，而不是决定顶层分类。

### 3.3 不按本地、网络或数据库建立主要分类

以下内容属于数据来源：

```text
本地文件
远程服务
空间数据库
内存对象
临时处理成果
```

一幅影像无论来自本地 GeoTIFF、远程 WMS 还是数据库，用户仍然会把它理解为影像图层。因此来源也不应替代数据角色分类。

## 4. 每个图层需要同时描述的三个维度

图层分类不能只依赖一个枚举。每个图层至少应同时记录以下三个相互独立的维度。

### 4.1 数据角色

数据角色决定图层树位置、用户可以进行的操作和默认属性界面。

示例：

```text
Imagery
Elevation
ThematicRaster
Vector
Model
PointCloud
Annotation
Table
```

### 4.2 渲染方式

渲染方式决定程序内部如何把数据送入 osgEarth/OSG。

示例：

```text
GDALImageLayer
GDALElevationLayer
FeatureModelLayer
FeatureImageLayer
TiledFeatureModelLayer
ModelLayer
ThreeDTilesLayer
AnnotationLayer
```

渲染方式是内部实现细节，不必直接作为普通用户看到的分类名称。

### 4.3 数据来源和格式

数据来源决定程序如何打开、验证和枚举数据。

示例：

```text
GeoTIFF
VRT
GeoPackage
Shapefile
GeoJSON
LAS/LAZ
glTF/GLB
3D Tiles
本地文件
PostGIS
WMS/TMS/XYZ
内存数据
```

同一个数据角色可以有多个来源，同一个来源格式也可能对应多个数据角色。

## 5. 数据角色、渲染方式和格式示例

| 数据 | 数据角色 | 可能的渲染方式 | 数据格式或来源 |
| --- | --- | --- | --- |
| `ortho.tif` | Imagery | `GDALImageLayer` | GeoTIFF |
| `dem.tif` | Elevation | `GDALElevationLayer` | GeoTIFF |
| `landuse.tif` | ThematicRaster | 着色后的 ImageLayer | GeoTIFF |
| `roads.gpkg` 中的 roads | Vector/Line | `FeatureModelLayer` + OGR | GeoPackage 子图层 |
| `villages.geojson` | Vector/Point | `FeatureModelLayer` + OGR | GeoJSON |
| `building.glb` | Model | `ModelLayer` 或 `GeoTransform` | glTF Binary |
| `tileset.json` | 3D 场景或点云 | `ThreeDTilesLayer` | 3D Tiles |
| 内存量测线 | Annotation | `AnnotationLayer` 或临时 OSG 节点 | 内存数据 |
| `statistics.csv` | Table | Qt 表格模型 | CSV |

## 6. 推荐的长期图层树

```text
Global Map
├── Imagery Layers
│   ├── global.tif
│   └── local_ortho.tif
│
├── Elevation Layers
│   ├── global_dem.tif
│   └── local_dem.tif
│
├── Thematic Raster Layers
│   ├── ndvi.tif
│   ├── slope.tif
│   └── landuse.tif
│
├── Vector Layers
│   ├── villages.gpkg        [点]
│   ├── roads.gpkg           [线]
│   └── parcels.gpkg         [面]
│
├── 3D Layers
│   ├── building.glb         [普通模型]
│   ├── city/tileset.json    [3D Tiles]
│   ├── survey_pointcloud    [点云]
│   └── building_bim         [BIM]
│
├── Annotation Layers
│   ├── Measurement Graphics
│   ├── Editing Graphics
│   └── Labels
│
└── Tables
    ├── statistics.csv
    └── population_table
```

这个结构是长期方向，不要求现在一次性创建所有空分类。应当随着真实能力实现逐步增加分类。

## 7. 各分类的职责

### 7.1 Imagery Layers

用于提供地表颜色，例如：

- 卫星影像；
- 航空影像；
- UAV 正射影像；
- 全球底图；
- RGB/RGBA 渲染结果。

影像顺序通常具有明确的上下覆盖意义，分类内部应支持排序、透明度、显隐和最大可视范围等属性。

### 7.2 Elevation Layers

用于提供地形高度，例如：

- DEM；
- DSM；
- DTM；
- DTED；
- 地形修正或高程偏移数据。

高程图层不能按照普通影像的方式解释。高程之间可能存在替代、组合、偏移、分辨率优先和垂直基准问题。

### 7.3 Thematic Raster Layers

用于表示像素值具有分析意义、需要经过颜色映射才能显示的数据，例如：

- NDVI；
- 坡度和坡向；
- 土地利用分类；
- 风险等级；
- 可见性分析；
- 单波段连续值栅格；
- 带调色板的分类栅格。

专题栅格后续通常需要：

- 最小值和最大值；
- 连续色带；
- 离散分类颜色；
- NoData；
- 透明度；
- 单位；
- 图例。

虽然专题栅格底层可能仍通过 ImageLayer 显示，但从用户操作和图层属性角度，它与普通 RGB 影像不同，因此建议单独分类。

### 7.4 Vector Layers

用于点、线、面及其属性数据，例如：

- GeoPackage；
- Shapefile；
- GeoJSON；
- KML/GML；
- 带坐标字段的 CSV；
- 空间数据库图层。

矢量图层通常需要：

- 点、线、面样式；
- 属性表；
- 查询、筛选和选择；
- Feature ID；
- 标注；
- 编辑和保存；
- 空间索引；
- 坐标系与几何有效性检查。

#### 是否按点、线、面再分组

初期不建议固定成：

```text
Vector Layers
├── Point
├── Line
└── Polygon
```

更建议将所有矢量图层放在同一个分类中，使用图标表示几何类型：

```text
Vector Layers
├── villages.gpkg    [点图标]
├── roads.gpkg       [线图标]
└── parcels.gpkg     [面图标]
```

原因：

- 点、线、面共享要素、属性、选择和编辑体系；
- 用户可能需要在不同几何类型之间调整绘制优先级；
- 可能出现 MultiPoint、MultiLineString、MultiPolygon、GeometryCollection 或未知几何类型；
- 过多固定分类会增加图层树深度。

几何类型仍然必须作为元数据保存，因为样式、编辑工具和合法操作依赖它。

### 7.5 3D Layers

初期可建立一个统一的 `3D Layers` 分类，容纳：

- 普通三维模型；
- 3D Tiles；
- 倾斜摄影；
- 点云；
- BIM。

当数据量和功能增加后，再细分为：

```text
3D Layers
├── Models
├── 3D Tiles
├── Point Clouds
└── BIM
```

用户数据角色与底层渲染方式不能混为一谈。例如建筑和点云都可能使用 3D Tiles 承载，但它们仍具有不同的业务语义和属性界面。

普通模型还可能需要额外记录：

- 地理位置；
- 高程；
- Heading/Pitch/Roll；
- 缩放比例；
- 模型原点；
- 高程贴合模式。

点云和 BIM 不应只通过增加扩展名实现。点云需要空间索引、分块和 LOD；BIM 还需要保留构件属性与模型几何之间的关联。

### 7.6 Annotation Layers

用于尚未成为正式数据源的交互和临时图形，例如：

- 鼠标绘制点、线、面；
- 距离和面积量测；
- 选中高亮；
- 编辑控制点；
- 临时分析范围；
- 文字标注；
- 业务提示图形。

典型转换流程：

```text
用户临时绘制多边形
        ↓
Editing Graphics
        ↓ 用户确认保存
写入 GeoPackage 等正式数据源
        ↓
加入 Vector Layers
```

### 7.7 Tables

用于没有空间几何的数据，例如：

- CSV 统计表；
- 数据库普通表；
- GeoPackage 中无几何字段的表；
- GIS 处理生成的汇总结果。

表格不能直接加入 osgEarth Map，但仍属于项目数据。后续可通过字段连接与空间图层关联。

## 8. 图层顺序与拖放规则

不同类别的上下顺序具有不同意义，不能完全使用同一套规则。

| 分类 | 顺序的主要含义 |
| --- | --- |
| Imagery | 决定地表影像覆盖顺序 |
| Elevation | 决定高程组合、替代或偏移关系 |
| ThematicRaster | 类似影像覆盖，同时涉及透明度和图例 |
| Vector | 影响绘制优先级，也受高程模式、样式和深度测试影响 |
| 3D | 主要由真实空间位置和深度测试决定 |
| Annotation | 通常需要保持醒目或位于专门的渲染阶段 |
| Table | 没有三维渲染顺序 |

推荐的拖放原则：

1. 默认只允许真实图层叶子移动；
2. 分类内部允许排序时，必须同步真实渲染顺序；
3. 默认禁止把图层拖到不兼容分类；
4. 不应把 DEM 通过拖动变成影像或矢量；
5. 数据角色转换应使用明确的转换命令，而不是普通排序操作；
6. 不支持排序的分类可以在界面中禁用拖动；
7. 图层树排序失败时，应恢复原顺序并报告原因。

## 9. 数据导入不应永久依赖扩展名

当前根据 `.tif/.tiff` 判断拖入文件，在功能初期是合理的。但随着格式增多，应逐渐形成统一的数据探测流程：

```text
用户选择或拖入数据
        ↓
Data Source Probe
        ↓
识别实际驱动和数据能力
        ↓
枚举内部图层、子数据集或场景入口
        ↓
推断可用的数据角色
        ↓
必要时由用户确认
        ↓
验证坐标系、范围、NoData、几何等信息
        ↓
预处理或优化
        ↓
创建相应 osgEarth/OSG 渲染对象
        ↓
注册到项目图层模型和图层树
```

### 9.1 程序可以自动判断的内容

- GDAL/OGR 实际驱动；
- 数据集是否包含栅格或矢量；
- 栅格尺寸、波段数、数据类型、NoData 和颜色表；
- 矢量内部图层数量；
- 几何类型和要素数量；
- 坐标系和空间范围；
- 是否存在子数据集；
- 是否为合法的 3D Tiles 入口；
- 当前运行环境是否具备所需驱动或 osgDB 插件。

### 9.2 可能仍需用户确认的内容

- TIFF 是普通影像、DEM 还是专题栅格；
- 单波段连续值应使用什么色带；
- GeoPackage 中加载哪些内部图层；
- CSV 的坐标字段和坐标系；
- 普通三维模型的地理位置、姿态和比例；
- 未携带垂直基准信息的 DEM 使用什么垂直基准；
- 多维数据选择哪个变量、时间或高度层。

## 10. 建议的内部图层描述

随着图层类型增加，仅在树节点中保存 Map UID、Layer UID 和 Item Type 将逐渐不够。每个图层概念上还需要描述：

```text
项目内唯一 ID
所属 Map ID
显示名称
数据角色
数据子类型
源 URI 或文件路径
数据驱动名称
容器内部图层或子数据集名称
坐标参考系
空间范围
几何类型
栅格波段与数据类型
对应渲染对象 ID
是否可见
透明度
排序位置
加载状态
错误信息
是否为处理成果
来源处理任务 ID
创建和修改时间
```

示例：

```text
显示名称：roads
数据角色：Vector
数据子类型：Line
数据源：D:/results/result.gpkg
内部图层：roads
驱动：GPKG
渲染方式：FeatureModelLayer
来源任务：buffer-job-001
```

## 11. 未来的 LayerRegistry / ProjectLayerModel

当前图层树可以继续承担界面展示和用户操作入口，但长期不建议让 `QTreeWidgetItem` 成为图层信息的唯一真实来源。

推荐逐步形成：

```text
ProjectLayerModel / LayerRegistry
        ├── 保存所有图层描述和项目顺序
        ├── 驱动 LayerTreeWidget 的显示
        ├── 关联 osgEarth/OSG 渲染对象
        ├── 记录 GIS 处理成果来源
        └── 为项目保存、恢复和多 Map 管理提供数据
```

各部分职责建议为：

| 组件 | 长期职责 |
| --- | --- |
| `LayerTreeWidget` | 显示、选择、拖放和用户界面事件 |
| `MainWindow` | 协调命令、对话框和模块交互 |
| `ProjectLayerModel` | 保存项目层面的图层结构和元数据 |
| `EarthViewWidget` | 管理 osgEarth/OSG 渲染对象和三维交互 |
| `DataSourceProbe` | 识别数据源、驱动、子图层和可用角色 |
| `LayerFactory` | 根据图层描述创建正确的渲染对象 |
| `ProcessingService` | 调用 GIS Worker 并返回处理成果 |

理想的数据流：

```text
用户导入或 GIS Worker 返回成果
             ↓
DataSourceProbe 识别数据
             ↓
形成统一 LayerDescriptor
             ↓
ProjectLayerModel 注册图层
             ↓
LayerFactory 创建渲染对象
             ↓
EarthViewWidget 加入 Map
             ↓
LayerTreeWidget 显示对应节点
```

这个结构不要求当前立即重构完成，但在增加矢量、专题栅格等类型时，应避免继续让所有判断无限堆积到 `MainWindow` 和 `EarthViewWidget`。

## 12. GIS 处理成果的推荐格式

为了让 GIS Worker 与 HelloEarth 展示端形成稳定闭环，建议优先约定以下交换格式：

| 成果类型 | 推荐格式 | 说明 |
| --- | --- | --- |
| 普通影像 | GeoTIFF/COG 或 VRT | 保留坐标系、范围和 NoData |
| DEM | GeoTIFF 或 VRT | 额外记录单位和垂直基准 |
| 专题栅格 | GeoTIFF | 记录数据类型、NoData、单位和分类信息 |
| 常规矢量 | GeoPackage | 支持多个图层、属性和空间索引 |
| 轻量矢量交换 | GeoJSON | 适合小型数据和调试 |
| 兼容性矢量 | Shapefile | 需要完整管理伴随文件和编码 |
| 普通三维模型 | glTF/GLB 或经验证的 OSG 格式 | 另行记录地理定位信息 |
| 海量三维场景 | 3D Tiles | 支持空间分块和 LOD |
| 非空间结果 | CSV 或 GeoPackage 普通表 | 进入 Tables，不直接加入 Map |

处理成果返回时，除了路径，还应尽量携带：

- 数据角色建议；
- 容器内部图层名称；
- 坐标系；
- 数据范围；
- 结果类型；
- 来源任务 ID；
- 建议显示名称；
- 可能需要的渲染提示。

HelloEarth 仍应自行验证结果，不能无条件信任外部任务返回的元数据。

## 13. 推荐的渐进实施顺序

### 当前阶段

保持：

```text
Global
├── Imagery Layers
└── Elevation Layers
```

### 矢量加载阶段

增加：

```text
├── Vector Layers
```

依次完成：

1. Shapefile 点、线、面加载；
2. Feature 样式；
3. GeoPackage 子图层枚举；
4. GeoJSON；
5. 图层树显隐、删除、定位和排序；
6. 属性查看；
7. 为选择和编辑保留稳定 Feature ID。

### 专题栅格阶段

增加：

```text
├── Thematic Raster Layers
```

依次完成连续值、分类值、NoData、透明度、色带和图例。

### 三维数据阶段

增加：

```text
├── 3D Layers
```

依次完成普通模型定位、glTF/GLB 或 OSGB、3D Tiles、倾斜摄影、点云和 BIM。

### 编辑与业务阶段

增加：

```text
├── Annotation Layers
└── Tables
```

用于量测、绘制、编辑、临时分析图形、属性结果和业务统计表。

## 14. 新增数据类型时的检查清单

每增加一种数据类型，应先回答：

1. 它在地图中承担什么数据角色？
2. 它应放入哪个现有分类，是否真的需要新分类？
3. 它的容器格式和内部子资源是什么？
4. 使用哪个 GDAL/OGR 驱动、osgDB 插件或 osgEarth Layer？
5. 当前运行环境是否真正启用了该驱动或插件？
6. 是否需要用户选择子图层、变量、波段或时间？
7. 如何读取坐标系、范围、高程单位和垂直基准？
8. 是否需要金字塔、空间索引、切片或 LOD？
9. 它的显隐、定位、透明度和排序分别如何实现？
10. 它能否被拾取、查询、编辑和保存？
11. GIS 处理模块应返回什么元数据？
12. 加载失败时如何清理部分创建的对象和树节点？
13. 项目保存后如何恢复该数据源？
14. 格式、插件和数据本身有哪些许可证要求？

## 15. 当前结论

HelloEarth 图层系统应遵循以下长期原则：

> 图层树按用户理解的数据角色分类；程序内部另外记录格式、来源、几何类型和渲染实现。

短期内只在真实功能实现时增加分类。下一步最合理的是在现有 `Imagery Layers` 和 `Elevation Layers` 之后增加 `Vector Layers`，建立本地矢量数据从读取、样式、渲染到图层树管理的完整闭环。

当矢量和专题栅格通道完成后，GIS Worker 产生的大部分常规分析成果就能够进入 HelloEarth 展示；随后再扩展普通模型、3D Tiles、点云和 BIM，不需要推翻已有分类原则。

## 16. 官方参考资料

- [osgEarth 3.8 Layer 类型总览](https://docs.osgearth.org/en/latest/layers.html)
- [osgEarth GDAL 影像与高程图层](https://docs.osgearth.org/en/latest/gdal.html)
- [osgEarth 数据准备与矢量要素说明](https://docs.osgearth.org/en/latest/data.html)
- [osgEarth Earth File 与 Feature 示例](https://docs.osgearth.org/en/latest/earthfile.html)
- [GDAL 矢量驱动列表](https://gdal.org/en/stable/drivers/vector/index.html)
- [GDAL GeoPackage 驱动](https://gdal.org/en/stable/drivers/vector/gpkg.html)
- [GDAL Shapefile 驱动](https://gdal.org/en/stable/drivers/vector/shapefile.html)
- [GDAL GeoJSON 驱动](https://gdal.org/en/stable/drivers/vector/geojson.html)

接口和格式支持能力可能随 osgEarth、GDAL/OGR 和 osgDB 版本变化。实现前应以项目锁定版本和当前构建中实际注册的驱动、插件为准。

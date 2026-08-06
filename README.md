# HelloEarth

HelloEarth 是一个面向 osgEarth 初学者的 C++ 学习仓库。

本仓库通过多个相互独立、可以单独构建和调试的 Example，逐步学习本地遥感影像、DEM、矢量数据、三维模型及其他地理空间数据在 osgEarth 中的加载与显示流程。

## 当前示例

| 示例 | 说明 | 状态 |
| --- | --- | --- |
| `SingleLocalTIF` | 加载并显示单张本地 GeoTIFF 遥感影像 | 已完成 |
| `SingleLocalDEM` | 加载单个本地 DEM 并构建地形 | 计划中 |

## 当前学习内容

`SingleLocalTIF` 示例包含：

- 使用 `GDALImageLayer` 加载本地 GeoTIFF
- 检查图层加载状态
- 获取影像地理范围和中心点
- 计算影像地面尺寸
- 根据影像范围估算相机观察距离
- 使用 `EarthManipulator` 设置初始视点
- 使用外部 `.ovr` 金字塔提高大影像显示效率

## 工程结构

```text
HelloEarth/
├── .vscode/
│   └── launch.json
├── examples/
│   ├── CMakeLists.txt
│   ├── imagery/
│   │   ├── CMakeLists.txt
│   │   └── SingleLocalTIF.cpp
│   └── elevation/
│       ├── CMakeLists.txt
│       └── SingleLocalDEM.cpp
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── LEARNING_NOTES.md
└── README.md
```

CMake 按照以下层次组织：

```text
顶层 CMakeLists.txt
    ↓
examples/CMakeLists.txt
    ↓
具体数据类别的 CMakeLists.txt
    ↓
独立的 Example Target
```

## 本地 Workspace 结构

源码、构建结果、安装结果和测试数据相互分离：

```text
HelloEarthWorkspace/
├── HelloEarth/       # Git 源码仓库
├── build/            # CMake 构建结果
├── install/          # CMake 安装结果
└── testdata/         # 本地测试数据，不进入 Git
```

## 开发环境

当前开发环境：

- Windows 10/11 x64
- Visual Studio 2022 MSVC
- Visual Studio Code
- CMake
- vcpkg Manifest Mode
- OpenSceneGraph 3.6.5
- osgEarth 3.8
- GDAL 3.12.4

## 构建前准备

安装并配置：

- Visual Studio 2022 C++ 开发工具
- CMake
- Git
- vcpkg
- Visual Studio Code CMake Tools
- Visual Studio Code C/C++ 扩展

设置环境变量：

```text
VCPKG_ROOT=<本机 vcpkg 所在目录>
```

例如：

```text
VCPKG_ROOT=D:\work\tool\vcpkg
```

## 配置与构建

在仓库根目录执行：

```powershell
cmake --preset windows-msvc
cmake --build --preset debug --target SingleLocalTIF
```

构建结果位于外部 `build` 目录。

Debug 版本通常生成在：

```text
../build/examples/imagery/Debug/SingleLocalTIF.exe
```

## 调试运行

使用 VS Code 打开仓库根目录：

```text
HelloEarthWorkspace/HelloEarth
```

在“运行和调试”面板中选择：

```text
SingleLocalTIF Debug
```

然后按 `F5`。

`launch.json` 会设置：

- 当前 Example 的可执行文件路径
- 当前工作目录
- vcpkg Debug DLL 搜索路径
- OSG 插件搜索路径

## 测试数据

遥感影像和 `.ovr` 金字塔体积较大，因此不上传 GitHub。

本地测试数据放在：

```text
HelloEarthWorkspace/testdata/
```

当前 `SingleLocalTIF.cpp` 仍处于学习阶段，影像路径暂时在源码中设置。运行前需要确认 `imagePath` 指向本机有效的 GeoTIFF。

后续将改为通过命令行参数传入影像路径。

## 新增 Example 的基本流程

在已有类别中新增示例：

1. 在对应类别目录中创建新的 `.cpp`
2. 保证该程序拥有自己的 `main()`
3. 在该类别的 `CMakeLists.txt` 中添加新的 `add_executable`
4. 配置 C++ 标准、链接库和安装规则
5. 重新执行 CMake Configure
6. 构建并调试新的 Target

增加全新数据类别时，还需要在：

```text
examples/CMakeLists.txt
```

中通过 `add_subdirectory()` 接入新目录。

## 学习记录

更详细的学习过程和概念说明见：

[LEARNING_NOTES.md](LEARNING_NOTES.md)

## 后续计划

- 检查并构建 GeoTIFF Overview
- 加载本地 DEM
- 加载矢量数据
- 加载三维模型
- 加载点云与 BIM 数据
- 集成 Qt 桌面窗口
- 实现二维与三维视图联动
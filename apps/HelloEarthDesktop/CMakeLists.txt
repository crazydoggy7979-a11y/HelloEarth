# 查找 Qt 6 所需模块。
find_package(
    Qt6
    REQUIRED
    COMPONENTS
    Widgets
    OpenGL
    OpenGLWidgets
)

# 启用 Qt 项目的常用自动化配置。
qt_standard_project_setup()

# 创建桌面应用程序 Target。
qt_add_executable(
    HelloEarthDesktop

    main.cpp

    MainWindow.h
    MainWindow.cpp

    EarthViewWidget.h
    EarthViewWidget.cpp

    LayerTreeWidget.h
    LayerTreeWidget.cpp
)

# 声明 HelloEarthDesktop 使用哪些 Qt 模块。
target_link_libraries(
    HelloEarthDesktop
    PRIVATE

    # Qt 桌面界面与 OpenGL 控件。
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets

    # osgEarth 三维 GIS 引擎。
    #
    # 该 Target 也会传递应用所需的 OSG 核心依赖，
    # 后续 EarthViewWidget 才能使用 osgViewer::Viewer、
    # osg::Camera 和 osgEarth::MapNode 等类型。
    osgEarth::osgEarth

    # HelloEarth 的公共栅格预处理模块，
    # 用于影像检查、金字塔检查与构建、VRT 管理。
    HelloEarth::Raster

    # Desktop 启动时需要初始化 GDAL 驱动。
    GDAL::GDAL

    # HelloEarth 的公共视点计算模块。
    #
    # 用于根据影像或 DEM 图层的数据范围，
    # 计算能够完整观察该图层的 osgEarth::Viewpoint。
    HelloEarth::Navigation
)

# 让 MSVC 按 UTF-8 解析源文件。
if(MSVC)
    target_compile_options(
        HelloEarthDesktop
        PRIVATE
        /utf-8
    )
endif()
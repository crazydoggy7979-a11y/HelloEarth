# 创建名为 SingleLocalShapefile 的可执行 Target。
# 第一个参数决定最终生成 SingleLocalShapefile.exe；
# 后面的参数是参与编译的源文件。
add_executable(
    SingleLocalShapefile
    SingleLocalShapefile.cpp
)

# 要求编译器至少支持 C++17。
# PRIVATE 表示该要求只属于 SingleLocalShapefile
target_compile_features(
    SingleLocalShapefile
    PRIVATE
    cxx_std_17
)

# 使用 MSVC 编译时，按照 UTF-8 读取源码，
# 避免中文注释触发 C4819 警告。
if(MSVC)
    target_compile_options(
        SingleLocalShapefile
        PRIVATE
        /utf-8
    )
endif()

# 为 SingleLocalShapefile 链接 osgEarth 和 GDAL。
# 源码直接使用了 GDALAllRegister、GDALVersionInfo，
# 因此除了 osgEarth，还需要显式链接 GDAL。
target_link_libraries(
    SingleLocalShapefile
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
    HelloEarth::Raster
    HelloEarth::Navigation
)

# 执行 cmake --install 时，将该 EXE 安装到：
# <CMAKE_INSTALL_PREFIX>/bin
install(
    TARGETS SingleLocalShapefile
    RUNTIME DESTINATION bin
)

# 创建单个本地 GeoPackage 学习示例。
#
# 每调用一次 add_executable()，CMake 就会创建一个独立 Target。
# 这个 Target 最终生成 SingleLocalGeoPackage.exe，
# 不会影响同一 CMakeLists 中的 SingleLocalShapefile.exe。
add_executable(
    SingleLocalGeoPackage
    SingleLocalGeoPackage.cpp
)

# 要求这个 Target 使用至少 C++17 的语言标准。
#
# PRIVATE 表示该编译要求只属于 SingleLocalGeoPackage，
# 不会传递给其他 Target。
target_compile_features(
    SingleLocalGeoPackage
    PRIVATE
    cxx_std_17
)

# 使用 MSVC 编译时，按照 UTF-8 解析源文件。
#
# target_compile_options() 的第一个参数必须明确写出
# 当前需要配置的 Target 名称。
if(MSVC)
    target_compile_options(
        SingleLocalGeoPackage
        PRIVATE
        /utf-8
    )
endif()

# 为 GeoPackage 示例链接所需的库。
#
# 当前第一步只使用了标准输出，
# 但后面会使用 GDAL 枚举 GeoPackage 图层，
# 并使用 osgEarth 渲染其中的矢量数据，
# 因此现在先连接完整依赖。
target_link_libraries(
    SingleLocalGeoPackage
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
    HelloEarth::Raster
    HelloEarth::Navigation
)

# 执行 cmake --install 时，
# 将 SingleLocalGeoPackage.exe 安装到 bin 目录。
install(
    TARGETS SingleLocalGeoPackage
    RUNTIME DESTINATION bin
)

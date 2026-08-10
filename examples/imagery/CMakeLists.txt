# 创建名为 SingleLocalTIF 的可执行 Target。
# 第一个参数决定最终生成 SingleLocalTIF.exe；
# 后面的参数是参与编译的源文件。
add_executable(
    SingleLocalTIF
    SingleLocalTIF.cpp
)

# 要求编译器至少支持 C++17。
# PRIVATE 表示该要求只属于 SingleLocalTIF。
target_compile_features(
    SingleLocalTIF
    PRIVATE
    cxx_std_17
)

# 使用 MSVC 编译时，按照 UTF-8 读取源码，
# 避免中文注释触发 C4819 警告。
if(MSVC)
    target_compile_options(
        SingleLocalTIF
        PRIVATE
        /utf-8
    )
endif()

# 为 SingleLocalTIF 链接 osgEarth 和 GDAL。
# 源码直接使用了 GDALAllRegister、GDALVersionInfo，
# 因此除了 osgEarth，还需要显式链接 GDAL。
target_link_libraries(
    SingleLocalTIF
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
    HelloEarth::Raster
)

# 执行 cmake --install 时，将该 EXE 安装到：
# <CMAKE_INSTALL_PREFIX>/bin
install(
    TARGETS SingleLocalTIF
    RUNTIME DESTINATION bin
)
# 创建 HelloEarth 的通用栅格处理静态库。
add_library(
    HelloEarthRaster
    STATIC
    RasterPreprocessor.cpp
)

# 为真实 Target 提供带命名空间的别名。
# 其他模块通过 HelloEarth::Raster 链接它。
add_library(
    HelloEarth::Raster
    ALIAS
    HelloEarthRaster
)

# 允许该库以及链接它的 Target 查找：
# include/HelloEarth/raster/...
target_include_directories(
    HelloEarthRaster
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 当前栅格预处理功能要求使用 C++17。
target_compile_features(
    HelloEarthRaster
    PUBLIC
    cxx_std_17
)

# 金字塔检查、构建和 VRT 管理依赖 GDAL。
target_link_libraries(
    HelloEarthRaster
    PRIVATE
    GDAL::GDAL
)

# 要求 MSVC 按照 UTF-8 读取源码和中文注释。
if(MSVC)
    target_compile_options(
        HelloEarthRaster
        PRIVATE
        /utf-8
    )
endif()
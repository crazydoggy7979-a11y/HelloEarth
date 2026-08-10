# 声明本工程要求的最低 CMake 版本。
cmake_minimum_required(VERSION 3.20)

# 定义整个 CMake 工程的名称。
# 这是工程名，不代表一定会生成 HelloEarth.exe。
project(HelloEarth)

# 查找并载入 osgEarth 提供的 CMake 配置。
# CONFIG 表示使用 osgEarthConfig.cmake；
# REQUIRED 表示找不到时立即停止配置。
find_package(osgEarth CONFIG REQUIRED)

# 查找 GDAL，并创建后续 Target 使用的 GDAL::GDAL。
find_package(GDAL CONFIG REQUIRED)

# 先构建可供其他 Target 复用的底层公共模块。
add_subdirectory(src)

# 再接入依赖底层公共模块的学习示例。
add_subdirectory(examples)
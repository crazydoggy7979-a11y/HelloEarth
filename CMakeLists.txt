cmake_minimum_required(VERSION 3.20)

project(HelloEarth)

find_package(osgEarth CONFIG REQUIRED)
find_package(GDAL CONFIG REQUIRED)

add_executable(
    HelloEarth
    main.cpp
)

# 告诉 MSVC 使用 UTF-8 解析和编译源文件
if(MSVC)
    target_compile_options(
        HelloEarth
        PRIVATE
        /utf-8
    )
endif()

target_link_libraries(
    HelloEarth
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
)

install(
    TARGETS HelloEarth
    RUNTIME DESTINATION bin
)
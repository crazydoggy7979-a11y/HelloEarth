# 接入影像类学习示例。
# CMake 会继续读取 imagery/CMakeLists.txt
add_subdirectory(imagery)
add_subdirectory(elevation)

# 完成对应示例后，再逐步启用其他类别：
# add_subdirectory(elevation)
# add_subdirectory(vector)
# add_subdirectory(model)
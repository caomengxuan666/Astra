cmake_minimum_required(VERSION 3.16)
project(ZenCore VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出路径
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# 包含路径
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third-party
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/concurrent
    ${PROJECT_SOURCE_DIR}/datastructures
)

# 加载自定义函数
include(${PROJECT_SOURCE_DIR}/cmake/SetupTarget.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/SetupInterface.cmake)

# 添加子模块
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(concurrent)
add_subdirectory(datastructures)
add_subdirectory(third-party/mimalloc)

#link_libraries(zenutils)

# 测试
enable_testing()
add_subdirectory(tests)
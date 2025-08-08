cmake_minimum_required(VERSION 3.16)
project(Astra VERSION 0.1.0.0 LANGUAGES CXX C) # 完整版本号：主.次.补丁.修订

# 添加生成目录到包含路径，确保代码能引用
include_directories("${PROJECT_BINARY_DIR}/core")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出路径
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

include(cmake/BuildInfo.cmake)

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
include(${PROJECT_SOURCE_DIR}/cmake/AddLua.cmake)

# 为Windows平台添加_WIN32_WINNT定义
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN # 避免 windows.h 包含 WinSock.h 等冗余内容
        _WINSOCK_DEPRECATED_NO_WARNINGS # 可选：避免 WinSock 老函数警告
    )

    if(MSVC)
        # 为MSVC编译器添加UTF-8编码支持
        add_compile_options("/utf-8")
    endif()
endif()

include(cmake/FindVcpkg.cmake)

# setup_vcpkg()

# 添加子模块
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(concurrent)
add_subdirectory(datastructures)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_INSTALL_TOPLEVEL ON CACHE BOOL "" FORCE)
add_subdirectory(third-party/mimalloc)
add_subdirectory(Astra-CacheServer)

# link_libraries(zenutils)

# 测试
enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmark)
add_subdirectory(third-party/gooletest)
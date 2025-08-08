# BuildInfo.cmake - 自动收集构建信息并生成版本头文件
# 使用方法：include(cmake/BuildInfo.cmake)

# -------------------------- 检查必要变量 --------------------------
# 确保 project(VERSION) 已定义（至少 major.minor.patch）
if(NOT DEFINED PROJECT_VERSION_MAJOR)
    message(FATAL_ERROR "请先在 project 命令中定义 VERSION（如 project(xxx VERSION 0.1.0)）")
endif()

# -------------------------- 1. 收集 Git 提交信息 --------------------------
find_package(Git QUIET)
set(ASTRA_VERSION_SHA1 "unknown")  # 默认值
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE ASTRA_VERSION_SHA1_TEMP
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ASTRA_VERSION_SHA1_TEMP)
        set(ASTRA_VERSION_SHA1 "${ASTRA_VERSION_SHA1_TEMP}")
    endif()
endif()

# -------------------------- 2. 生成构建 ID 与时间戳 --------------------------
string(TIMESTAMP ASTRA_BUILD_TIMESTAMP "%Y%m%d%H%M%S")  # 格式：年日时分秒（如202507241630）
string(RANDOM LENGTH 6 ASTRA_RANDOM_SUFFIX)             # 6位随机数（避免重复）
set(ASTRA_BUILD_ID "${ASTRA_BUILD_TIMESTAMP}-${ASTRA_RANDOM_SUFFIX}")  # 唯一构建ID

# -------------------------- 3. 检测系统与架构信息 --------------------------
set(ASTRA_OS "${CMAKE_SYSTEM_NAME}")  # 操作系统（如Windows/Linux/Darwin）

# 架构位数（32/64）
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ASTRA_ARCH_BITS "64")
else()
    set(ASTRA_ARCH_BITS "32")
endif()

# 编译器信息（如MSVC 19.34.31937 / GCC 11.2.0）
set(ASTRA_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# -------------------------- 4. 生成版本头文件 --------------------------
# 模板文件路径（默认在 core 目录，可自定义）
if(NOT DEFINED BUILD_INFO_TEMPLATE)
    set(BUILD_INFO_TEMPLATE "${PROJECT_SOURCE_DIR}/core/version_info.h.in")
endif()

# 输出文件路径（默认在 build/core 目录）
if(NOT DEFINED BUILD_INFO_OUTPUT)
    set(BUILD_INFO_OUTPUT "${PROJECT_BINARY_DIR}/core/version_info.h")
endif()

# 确保输出目录存在
get_filename_component(BUILD_INFO_OUTPUT_DIR "${BUILD_INFO_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${BUILD_INFO_OUTPUT_DIR}")

# 生成头文件
configure_file(
    "${BUILD_INFO_TEMPLATE}"
    "${BUILD_INFO_OUTPUT}"
    @ONLY
)

# 添加输出目录到包含路径，方便代码引用
include_directories("${BUILD_INFO_OUTPUT_DIR}")

# 打印生成信息（可选，用于验证）
message(STATUS "Generate Version files: ${BUILD_INFO_OUTPUT}")
message(STATUS "Git SHA1: ${ASTRA_VERSION_SHA1}")
message(STATUS "Build ID: ${ASTRA_BUILD_ID}")
message(STATUS "System: ${ASTRA_OS} (${ASTRA_ARCH_BITS}位)")
message(STATUS "Compiler: ${ASTRA_COMPILER}")
cmake_minimum_required(VERSION 3.16)
project(Astra VERSION 0.1.0.2 LANGUAGES CXX C) # 完整版本号：主.次.补丁.修订

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

# add_subdirectory(tests)
add_subdirectory(benchmark)
add_subdirectory(third-party/gooletest)

# CPack配置
set(CPACK_PACKAGE_NAME "Astra-CacheServer")
set(CPACK_PACKAGE_VENDOR "Astra")
set(CPACK_PACKAGE_DESCRIPTION "A high-performance Redis-compatible caching middleware based on C++17")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/your-org/Astra")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "support@astra.example.com")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# 设置安装文件名
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")

# 设置生成器
if(WIN32)
    # Windows平台生成NSIS安装包、TAR.GZ和ZIP
    set(CPACK_GENERATOR "NSIS;TGZ;ZIP")

    # NSIS特定配置
    set(CPACK_NSIS_DISPLAY_NAME "Astra Cache Server")
    set(CPACK_NSIS_PACKAGE_NAME "Astra Cache Server")
    set(CPACK_NSIS_CONTACT "support@astra.example.com")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)

    # Windows服务安装
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        ExecWait '$INSTDIR\\\\bin\\\\Astra-CacheServer.exe install'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        ExecWait '$INSTDIR\\\\bin\\\\Astra-CacheServer.exe uninstall'
    ")
else()
    # Linux平台生成DEB、TAR.GZ和ZIP
    set(CPACK_GENERATOR "DEB;TGZ;ZIP")

    # DEB特定配置
    set(CPACK_DEBIAN_PACKAGE_NAME "astra-cacheserver")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Astra Team <support@astra.example.com>")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "A high-performance Redis-compatible caching middleware")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
endif()

# 包含CPack模块
include(CPack)

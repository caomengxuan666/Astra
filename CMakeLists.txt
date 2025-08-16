cmake_minimum_required(VERSION 3.16)
project(Astra VERSION 0.1.2.0 LANGUAGES CXX C)

# 添加生成目录到包含路径
include_directories("${PROJECT_BINARY_DIR}/core")

# 语言标准设置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Windows平台配置
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
    if(MSVC)
        add_compile_options("/utf-8")
    endif()
endif()

include(cmake/FindVcpkg.cmake)

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

# 测试与基准
enable_testing()
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
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")

# 安装前缀（优先用户指定，否则自动适配系统）
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(WIN32)
        set(CMAKE_INSTALL_PREFIX "$ENV{USERPROFILE}/Astra" CACHE PATH "安装前缀" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/opt/astra" CACHE PATH "安装前缀" FORCE)
    endif()
endif()

# 平台特定生成器
if(WIN32)
    set(CPACK_GENERATOR "NSIS;TGZ;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Astra Cache Server")
    set(CPACK_NSIS_PACKAGE_NAME "Astra Cache Server")
    set(CPACK_NSIS_CONTACT "support@astra.example.com")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        ExecWait '$INSTDIR\\\\bin\\\\Astra-CacheServer.exe install'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        ExecWait '$INSTDIR\\\\bin\\\\Astra-CacheServer.exe uninstall'
    ")
else()
    # 保留多格式输出（DEB+TGZ+ZIP）
    set(CPACK_GENERATOR "DEB;TGZ;ZIP")
    set(CPACK_DEBIAN_PACKAGE_NAME "astra-cacheserver")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "caomengxuan666 <2507560089@qq.com>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd, libfmt-dev, libasio-dev")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Astra Cache Server - High performance caching solution")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()

include(CPack)

# Linux系统服务自动配置
if(LINUX)
    # 安装脚本和服务模板
    install(FILES ${PROJECT_SOURCE_DIR}/astra_service.sh
            DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ 
                        GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)
    install(FILES ${PROJECT_SOURCE_DIR}/astra.service.template
            DESTINATION ${CMAKE_INSTALL_PREFIX}/etc
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

    # 系统级路径自动配置（带权限检查）
    if(CMAKE_INSTALL_PREFIX STREQUAL "/opt/astra")
        set(SYSTEMD_SERVICE_DIR "/etc/systemd/system")
        install(CODE "
            # 检查是否在CI/CD环境中运行
            if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
                message(STATUS \"CI/CD环境检测到，跳过系统服务自动配置\")
                return()
            endif()
            
            # 检查是否有系统目录写入权限（注意：HAS_PERMISSION为0表示有权限）
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E access \"${SYSTEMD_SERVICE_DIR}\" W_OK
                RESULT_VARIABLE HAS_PERMISSION
            )
            if(HAS_PERMISSION)
                message(STATUS \"权限不足或非root模式：跳过系统服务自动配置\")
                message(STATUS \"如需启用系统服务，请使用 sudo 执行 make install\")
                return()
            endif()

            # 复制服务文件
            message(STATUS \"自动配置系统服务...\")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E copy 
                    ${CMAKE_INSTALL_PREFIX}/etc/astra.service.template 
                    ${SYSTEMD_SERVICE_DIR}/astra.service
                RESULT_VARIABLE COPY_RESULT
                ERROR_VARIABLE COPY_ERROR
            )
            if(COPY_RESULT)
                message(WARNING \"复制服务文件失败: \${COPY_ERROR}\")
                return()
            endif()

            # 重载并启动服务
            execute_process(COMMAND systemctl daemon-reload)
            execute_process(
                COMMAND systemctl enable --now astra.service
                RESULT_VARIABLE START_RESULT
                ERROR_VARIABLE START_ERROR
            )
            if(START_RESULT)
                message(WARNING \"服务启动警告: \${START_ERROR}\")
            else()
                message(STATUS \"Astra 服务已自动启动\")
            endif()
        ")
    endif()
endif()

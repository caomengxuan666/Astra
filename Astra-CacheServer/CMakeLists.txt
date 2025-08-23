set(PROJECT_NAME "Astra-CacheServer")

# 设置相对安装路径
set(CMAKE_INSTALL_RELDIR "astra" CACHE STRING "Relative installation directory for Astra projects")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 添加构建选项
option(BUILD_STATIC "Build with static linking" OFF)
option(MI_BUILD_STATIC "Build mimalloc with static linking" OFF)

# 通用的文件
set(SERVER_SOURCE
        server.cpp
        server/ChannelManager.cpp
        server/session.cpp
        server/status_collector.cpp
        persistence/util_path.cpp
        persistence/process.cpp
        persistence/process.cpp
        proto/ProtocolParser.cpp
)

# WINDOWS 平台特有的文件
set(WIN_SOURCE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/WindowsServicePlugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/WindowsTrayPlugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/ClientWindow.cpp
)

if(WIN32)
        add_executable(Astra-CacheServer ${SERVER_SOURCE} ${WIN_SOURCE})
else()
        add_executable(Astra-CacheServer ${SERVER_SOURCE})
endif()

add_lua_support(Astra-CacheServer)
target_compile_definitions(Astra-CacheServer PRIVATE FMT_DISABLE_UTF8=1)
set(MI_BUILD_STATIC ${BUILD_STATIC} CACHE INTERNAL "Inherit static build setting from parent")
target_link_libraries(Astra-CacheServer PRIVATE
    $<$<BOOL:${MI_BUILD_STATIC}>:-static>
    ${CMAKE_THREAD_LIBS_INIT}
    leveldb
)
# target_link_libraries(Astra-CacheServer zenutils)
add_library(Astra-CacheSDK SHARED
        sdk/astra_client.cpp
        persistence/util_path.cpp
        persistence/process.cpp
        sdk/astra_client_c.cpp
        persistence/process.cpp
        sdk/astra_client_labview.h
        sdk/astra_client_labview.cpp
)

target_compile_definitions(Astra-CacheSDK PRIVATE ZEN_DLL_EXPORT)

# target_link_libraries(Astra-CacheSDK PRIVATE zenutils)
setup_target(Astra-CacheSDK)
setup_target(Astra-CacheServer)

add_subdirectory(cluster)
add_subdirectory(examples)

# install 安装Astra-CacheSDK的头文件和库文件
install(TARGETS Astra-CacheSDK
        RUNTIME DESTINATION ${CMAKE_INSTALL_RELDIR}/bin
        LIBRARY DESTINATION ${CMAKE_INSTALL_RELDIR}/lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_RELDIR}/lib
        INCLUDES DESTINATION ${CMAKE_INSTALL_RELDIR}/include
        CONFIGURATIONS Release
        COMPONENT applications
)

install(FILES sdk/astra_client_c.h sdk/astra_client_labview.h sdk/astra_resp.h DESTINATION include COMPONENT applications)
install(FILES ../core/macros.hpp DESTINATION include/core COMPONENT applications)

# install 写入
install(TARGETS Astra-CacheServer RUNTIME DESTINATION ${CMAKE_INSTALL_RELDIR}/bin COMPONENT applications)

# install 指定release
install(TARGETS Astra-CacheServer RUNTIME DESTINATION ${CMAKE_INSTALL_RELDIR}/bin CONFIGURATIONS Release COMPONENT applications)
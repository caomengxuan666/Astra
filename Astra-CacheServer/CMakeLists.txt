set(PROJECT_NAME "Astra-CacheServer")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 通用的文件
set(SERVER_SOURCE
        server.cpp
        persistence/util_path.cpp 
        persistence/process.cpp
)

# WINDOWS 平台特有的文件
set(WIN_SOURCE
        platform/windows/WindowsServicePlugin.cpp
        platform/windows/WindowsTrayPlugin.cpp
        platform/windows/ClientWindow.cpp
)



if(WIN32)
add_executable(Astra-CacheServer ${SERVER_SOURCE} ${WIN_SOURCE})
else()
add_executable(Astra-CacheServer ${SERVER_SOURCE})
endif()
target_compile_definitions(Astra-CacheServer PRIVATE FMT_DISABLE_UTF8=1)
target_link_libraries(Astra-CacheServer zenutils)

add_library(Astra-CacheSDK SHARED
        sdk/astra_client.cpp
        persistence/util_path.cpp 
        sdk/astra_client_c.cpp
        persistence/process.cpp
        sdk/astra_client_labview.h
        sdk/astra_client_labview.cpp
)

target_compile_definitions(Astra-CacheSDK PRIVATE ZEN_DLL_EXPORT)
target_link_libraries(Astra-CacheSDK PRIVATE zenutils)

add_subdirectory(examples)

# install 安装Astra-CacheSDK的头文件和库文件
install(TARGETS Astra-CacheSDK
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include
        CONFIGURATIONS Release
)

install(FILES sdk/astra_client_c.h sdk/astra_client_labview.h sdk/astra_resp.h DESTINATION include)
install(FILES ../core/macros.hpp DESTINATION include/core)

# install 写入
install(TARGETS Astra-CacheServer RUNTIME DESTINATION bin)

# install 指定release
install(TARGETS Astra-CacheServer RUNTIME DESTINATION bin CONFIGURATIONS Release)
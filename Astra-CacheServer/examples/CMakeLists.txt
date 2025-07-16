add_executable(example_client example_client.cpp)
target_link_libraries(example_client Astra-CacheSDK)
target_include_directories(example_client PRIVATE ../${CMAKE_CURRENT_SOURCE_DIR}/sdk)
target_link_libraries(example_client zenutils)


add_executable(simple_example simple_example.c)

target_link_libraries(simple_example PRIVATE Astra-CacheSDK)
target_include_directories(simple_example PRIVATE ../${CMAKE_CURRENT_SOURCE_DIR}/sdk)
target_link_libraries(simple_example PRIVATE zenutils)

# 如果是MSVC编译器
if (MSVC)
    # 设置C示例的编译选项
    target_compile_options(simple_example PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/TC>  # 确保MSVC以C模式编译
    )
endif ()

add_executable(TrayPluginExample
        TrayPluginMain.cpp
        ${CMAKE_SOURCE_DIR}/Astra-CacheServer/platform/windows/WindowsTrayPlugin.cpp
        ${CMAKE_SOURCE_DIR}/Astra-CacheServer/platform/windows/ClientWindow.cpp
)

target_include_directories(TrayPluginExample PRIVATE
        ${CMAKE_SOURCE_DIR}/Astra-CacheServer/platform/windows
)

# 设置编译选项，指定使用宽字符入口wWinMain
if (MSVC)
    # 对于MSVC编译器，添加编译定义以使用宽字符版本
    target_compile_definitions(TrayPluginExample PRIVATE
            UNICODE
            _UNICODE
    )

    # 可选：如果需要隐藏控制台窗口（GUI程序通常不需要控制台）
    set_target_properties(TrayPluginExample PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif ()


# 添加hiredis依赖
#find_package(hiredis CONFIG REQUIRED)
#add_executable(example_hiredis hiredis_client.cpp)
#target_link_libraries(example_hiredis PRIVATE hiredis::hiredis zenutils)
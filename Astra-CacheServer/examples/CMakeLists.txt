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
endif()

# 添加hiredis依赖
#find_package(hiredis CONFIG REQUIRED)
#add_executable(example_hiredis hiredis_client.cpp)
#target_link_libraries(example_hiredis PRIVATE hiredis::hiredis zenutils)
add_executable(example_client example_client.cpp)
target_link_libraries(example_client Astra-CacheSDK)
target_include_directories(example_client PRIVATE ../${CMAKE_CURRENT_SOURCE_DIR}/sdk)

# 添加hiredis依赖
find_package(hiredis CONFIG REQUIRED)
add_executable(example_hiredis hiredis_client.cpp)
target_link_libraries(example_hiredis PRIVATE hiredis::hiredis zenutils)
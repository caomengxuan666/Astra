add_executable(example_client example_client.cpp)
target_link_libraries(example_client Astra-CacheSDK)
target_include_directories(example_client PRIVATE ../${CMAKE_CURRENT_SOURCE_DIR}/sdk)
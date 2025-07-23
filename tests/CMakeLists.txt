# tests/CMakeLists.txt

# 查找 GTest（建议已在顶层找到，这里只是使用）
find_package(GTest CONFIG REQUIRED)

# 加载自定义宏
include(${PROJECT_SOURCE_DIR}/cmake/SetupTarget.cmake)

include_directories(${PROJECT_SOURCE_DIR}/Astra-CacheServer)

# 收集所有 test_xxx.cpp 文件
file(GLOB TEST_SOURCES "test_*.cpp")

# 为每个测试源文件生成一个测试可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    
    # 创建可执行目标
    add_executable(${TEST_NAME} ${TEST_SOURCE})

    # 使用宏统一设置
    setup_target(${TEST_NAME})

    # 单独链接 GTest（setup_target 可能不包含它）
    target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest GTest::gtest_main)

    add_lua_support(${TEST_NAME})
    # 注册到 CTest
    add_test(NAME ${TEST_NAME}
             COMMAND ${TEST_NAME}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
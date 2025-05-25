function(setup_interface TARGET_NAME)
    # 设置接口包含目录
    target_include_directories(${TARGET_NAME} INTERFACE ${PROJECT_SOURCE_DIR}/core)

    # 设置接口预编译头
    target_precompile_headers(${TARGET_NAME} INTERFACE "core/astra.hpp")

    set(LINKED_LIBRARIES zenutils mimalloc)
    # 如果你想让依赖它的目标也自动链接 zenutils
    target_link_libraries(${TARGET_NAME} INTERFACE ${LINKED_LIBRARIES})
    message(STATUS "Setup interface ${TARGET_NAME}")
endfunction()
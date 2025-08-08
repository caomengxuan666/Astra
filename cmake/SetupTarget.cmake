function(setup_target TARGET_NAME)
    # PCH 支持
    target_precompile_headers(${TARGET_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/core/astra.hpp")

    # 根据平台设置 mimalloc 库名
    set(LINKED_LIBRARIES zenutils)  # 先添加其他确定的库

    # 处理 mimalloc 库（跨平台兼容）
    if(WIN32)
        # Windows 下，mimalloc 静态库通常命名为 mimalloc-static.lib
        list(APPEND LINKED_LIBRARIES mimalloc-static)
    else()
        # Linux/macOS 下，库名为 mimalloc（自动对应 libmimalloc.a）
        list(APPEND LINKED_LIBRARIES mimalloc)
    endif()

    # 链接库
    target_link_libraries(${TARGET_NAME} PRIVATE ${LINKED_LIBRARIES})
    #message(STATUS "Setup target ${TARGET_NAME}")
endfunction()
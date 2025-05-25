function(setup_target TARGET_NAME)
    # pch support
    target_precompile_headers(${TARGET_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/core/astra.hpp")
    set(LINKED_LIBRARIES zenutils mimalloc)
    target_link_libraries(${TARGET_NAME} PRIVATE ${LINKED_LIBRARIES})
    message(STATUS "Setup target ${TARGET_NAME}")
endfunction()


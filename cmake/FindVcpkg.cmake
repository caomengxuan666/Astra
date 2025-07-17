# 封装vcpkg路径配置逻辑为可复用函数
function(setup_vcpkg)
    # 可选参数：允许用户指定 preferred 路径（默认D:/vcpkg）
    set(oneValueArgs PREFERRED_PATH)
    cmake_parse_arguments(ARGS "" "${oneValueArgs}" "" ${ARGN})
    
    # 设置默认首选路径
    if(NOT ARGS_PREFERRED_PATH)
        set(ARGS_PREFERRED_PATH "D:/vcpkg" CACHE STRING "默认首选vcpkg路径")
    endif()

    # 1. 读取当前环境或缓存中的VCPKG_ROOT
    set(DEFAULT_VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE STRING "当前检测到的vcpkg路径")
    message(STATUS "当前环境中的VCPKG_ROOT: ${DEFAULT_VCPKG_ROOT}")

    # 2. 定义期望的首选路径
    set(MY_VCPKG_ROOT ${ARGS_PREFERRED_PATH} CACHE STRING "首选vcpkg路径")

    # 3. 智能切换逻辑
    if(DEFAULT_VCPKG_ROOT MATCHES "^[Cc]:" OR 
       DEFAULT_VCPKG_ROOT MATCHES "Program Files" OR
       NOT EXISTS "${DEFAULT_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        
        message(WARNING "检测到C盘路径或无效路径，自动切换到首选路径: ${MY_VCPKG_ROOT}")
        set(VCPKG_ROOT ${MY_VCPKG_ROOT} CACHE STRING "强制使用首选vcpkg路径" FORCE)
    else()
        set(VCPKG_ROOT ${DEFAULT_VCPKG_ROOT} CACHE STRING "使用环境变量中的vcpkg路径" FORCE)
    endif()

    # 4. 最终验证
    if(NOT EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        message(FATAL_ERROR "vcpkg工具链文件不存在于路径: ${VCPKG_ROOT}\n"
            "请确认已安装vcpkg到 ${MY_VCPKG_ROOT} 或正确设置VCPKG_ROOT环境变量")
    endif()

    # 5. 设置工具链和前缀路径
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "" FORCE)
    set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/x64-windows/share" ${CMAKE_PREFIX_PATH} CACHE STRING "" FORCE)
    
    # 输出最终结果
    message(STATUS "成功配置vcpkg，使用路径: ${VCPKG_ROOT}")
    
    # 将变量导出到父作用域
    set(VCPKG_ROOT ${VCPKG_ROOT} PARENT_SCOPE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE} PARENT_SCOPE)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()
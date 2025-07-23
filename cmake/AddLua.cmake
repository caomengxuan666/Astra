#[=======================================================================[.rst:
AddLua
------

为目标添加Lua库和sol2绑定的链接及依赖支持

用法:
  add_lua_support(target1 [target2 ...])
#]=======================================================================]

# 确保Lua子模块已添加
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third-party/lua/CMakeLists.txt")
    message(FATAL_ERROR "未找到Lua配置文件，请确保已通过git submodule添加Lua:
    git submodule add https://github.com/lua/lua.git third-party/lua
    git submodule update --init --recursive")
endif()

# 确保sol2子模块已添加
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third-party/sol2/include/sol/sol.hpp")
    message(FATAL_ERROR "未找到sol2配置文件，请添加sol2子模块:
    git submodule add https://github.com/ThePhD/sol2.git third-party/sol2
    git submodule update --init --recursive")
endif()

# 仅在未添加过Lua子目录时才添加
if(NOT TARGET astra_lua)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third-party/lua" 
                     "${PROJECT_BINARY_DIR}/third-party/astra_lua_${CMAKE_BUILD_TYPE}" 
                     EXCLUDE_FROM_ALL)
endif()

# 定义sol2接口库（纯头文件，无需编译）
if(NOT TARGET astra_sol2)
    add_library(astra_sol2 INTERFACE)
    target_include_directories(astra_sol2 INTERFACE 
        "${PROJECT_SOURCE_DIR}/third-party/sol2/include")
    # 确保sol2知道我们使用的是系统中的Lua库
    target_link_libraries(astra_sol2 INTERFACE astra_lua)
endif()

# 为目标添加Lua和sol2支持的函数
function(add_lua_support)
    if(ARGC EQUAL 0)
        message(WARNING "add_lua_support需要至少一个目标参数")
        return()
    endif()

    foreach(target IN LISTS ARGN)
        if(NOT TARGET ${target})
            message(WARNING "目标${target}不存在，跳过Lua支持添加")
            continue()
        endif()

        # 链接Lua库和sol2
        target_link_libraries(${target} PRIVATE astra_lua astra_sol2)
        
        # 添加头文件路径
        target_include_directories(${target} PRIVATE 
            "${PROJECT_SOURCE_DIR}/third-party/lua"
            "${PROJECT_SOURCE_DIR}/third-party/sol2/include")
        
        # 定义编译宏
        if(WIN32)
            target_compile_definitions(${target} PRIVATE WITH_LUA WITH_SOL2 WIN32)
        else()
            target_compile_definitions(${target} PRIVATE WITH_LUA WITH_SOL2)
        endif()
        
        message(STATUS "已为目标${target}添加Lua和sol2支持")
    endforeach()
endfunction()
    
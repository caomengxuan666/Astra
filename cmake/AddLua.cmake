#[=======================================================================[.rst:
AddLua
------

Add Lua library and sol2 binding link and dependency support for targets

Usage:
  add_lua_support(target1 [target2 ...])
#]=======================================================================]

# Smart path finding - keep original interface but more flexible
macro(determine_project_paths)
    # Try original path
    set(TRY_LUA_DIR "${PROJECT_SOURCE_DIR}/third-party/lua")
    set(TRY_SOL2_DIR "${PROJECT_SOURCE_DIR}/third-party/sol2")

    # If it is a submodule case
    if(NOT EXISTS "${TRY_LUA_DIR}/CMakeLists.txt")
        set(TRY_LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/lua")
        set(TRY_SOL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/sol2")
    endif()

    # Finalize paths
    if(EXISTS "${TRY_LUA_DIR}/CMakeLists.txt")
        set(LUA_DIR "${TRY_LUA_DIR}")
        set(SOL2_DIR "${TRY_SOL2_DIR}")
    else()
        set(LUA_DIR "${PROJECT_SOURCE_DIR}/third-party/lua")
        set(SOL2_DIR "${PROJECT_SOURCE_DIR}/third-party/sol2")
    endif()

    # Convert to absolute paths
    get_filename_component(LUA_DIR "${LUA_DIR}" ABSOLUTE)
    get_filename_component(SOL2_DIR "${SOL2_DIR}" ABSOLUTE)

    message(STATUS "Lua Path: ${LUA_DIR}")
    message(STATUS "sol2 Path: ${SOL2_DIR}")
endmacro()

# Determine the correct paths
determine_project_paths()

# Ensure Lua submodule is added
if(NOT EXISTS "${LUA_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Lua configuration file not found, please ensure:
    1. Lua submodule is properly initialized:
       git submodule update --init --recursive
    2. Check the following paths:
       ${LUA_DIR}
    3. Project structure should be:
       PROJECT_ROOT/
         ├── third-party/
         │   ├── lua/
         │   └── sol2/
         or
         third_party/Astra-CacheServer/
           └── third-party/
               ├── lua/
               └── sol2/")
endif()

# Ensure sol2 submodule is added
if(NOT EXISTS "${SOL2_DIR}/include/sol/sol.hpp")
    message(FATAL_ERROR "sol2 configuration file not found:
    Path: ${SOL2_DIR}/include/sol/sol.hpp
    Please execute: git submodule update --init --recursive")
endif()

# Add Lua subdirectory only if not already added
if(NOT TARGET astra_lua)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third-party/lua"
        "${PROJECT_BINARY_DIR}/third-party/astra_lua_${CMAKE_BUILD_TYPE}"
        EXCLUDE_FROM_ALL)
endif()

# Define sol2 interface library (keep original interface)
if(NOT TARGET astra_sol2)
    add_library(astra_sol2 INTERFACE)
    target_include_directories(astra_sol2 INTERFACE
        "${PROJECT_SOURCE_DIR}/third-party/sol2/include")

    # 确保sol2知道我们使用的是系统中的Lua库
    target_link_libraries(astra_sol2 INTERFACE astra_lua)
endif()

# Function to add Lua and sol2 support to targets (keep original interface)
function(add_lua_support)
    if(ARGC EQUAL 0)
        message(WARNING "add_lua_support requires at least one target parameter")
        return()
    endif()

    foreach(target IN LISTS ARGN)
        if(NOT TARGET ${target})
            message(WARNING "Target ${target} does not exist, skipping Lua support addition")
            continue()
        endif()

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

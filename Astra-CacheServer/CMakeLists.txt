set(PROJECT_NAME "Astra-CacheServer")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE PRO_INC ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(Astra-CacheServer sever.cpp)

target_link_libraries(Astra-CacheServer zenutils)

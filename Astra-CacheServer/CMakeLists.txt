set(PROJECT_NAME "Astra-CacheServer")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Astra-CacheServer sever.cpp)

target_link_libraries(Astra-CacheServer zenutils)

add_library(Astra-CacheSDK SHARED sdk/astra_client.cpp)

add_subdirectory(examples)
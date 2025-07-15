set(PROJECT_NAME "Astra-CacheServer")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Astra-CacheServer server.cpp persistence/util_path.cpp persistence/process.cpp)

target_link_libraries(Astra-CacheServer zenutils)

add_library(Astra-CacheSDK SHARED sdk/astra_client.cpp persistence/util_path.cpp sdk/astra_client_c.cpp
        persistence/process.cpp)

target_link_libraries(Astra-CacheSDK PRIVATE zenutils)

add_subdirectory(examples)

# install 写入
install(TARGETS Astra-CacheServer RUNTIME DESTINATION bin)

# install 指定release
install(TARGETS Astra-CacheServer RUNTIME DESTINATION bin CONFIGURATIONS Release)
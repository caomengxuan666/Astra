# 6. 项目配置
# find_package(benchmark CONFIG REQUIRED)
# add_executable(bench_logger bench_logger.cpp)
# target_link_libraries(bench_logger PRIVATE benchmark::benchmark benchmark::benchmark_main zenutils)
# target_include_directories(bench_logger PRIVATE ${PROJECT_SOURCE_DIR}/utils)

# find_package(hiredis CONFIG REQUIRED)

# add_executable(bench_astra bench_astra.cpp)
# 修正：添加 benchmark::benchmark 和 benchmark::benchmark_main 到链接库
# target_link_libraries(bench_astra PRIVATE
# hiredis::hiredis
# zenutils
# #    Astra-CacheSDK
# benchmark::benchmark       # 新增：链接benchmark核心库
# benchmark::benchmark_main  # 新增：链接benchmark主函数库
# )
# 修正：benchmark的头文件路径无需通过target_include_directories添加（find_package已处理）
# target_include_directories(bench_astra PRIVATE
# ${PROJECT_SOURCE_DIR}/Astra-CacheServer/sdk
#
# # 移除：benchmark::benchmark benchmark::benchmark_main
# )
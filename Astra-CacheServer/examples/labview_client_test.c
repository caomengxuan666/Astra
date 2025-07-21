#include "sdk/astra_client_labview.h"
#include "sdk/astra_resp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 统一打印响应类型信息（辅助调试）
static void print_response_type(int type) {
    switch (type) {
        case RESP_SIMPLE_STRING_C: printf("(Simple String)"); break;
        case RESP_BULK_STRING_C:   printf("(Bulk String)"); break;
        case RESP_INTEGER_C:       printf("(Integer)"); break;
        case RESP_ARRAY_C:         printf("(Array)"); break;
        case RESP_ERROR_C:         printf("(Error)"); break;
        case RESP_NIL_C:           printf("(Nil)"); break;
        default:                   printf("(Unknown Type: %d)", type);
    }
}

// 毫秒级时间差计算
static long long get_time_diff_ms(clock_t start, clock_t end) {
    return (long long)((end - start) * 1000.0 / CLOCKS_PER_SEC);
}

//
// 新增性能测试函数
//
static void print_performance_metrics(const char *operation, int item_count, long long total_time_ms) {
    double duration_sec = total_time_ms / 1000.0;
    double qps = item_count / duration_sec;
    double throughput = (item_count / 1024.0) / duration_sec; // MB/s，假设每个键值对约1KB
    
    printf("  性能指标:\n");
    printf("    QPS: %.2f 次/秒\n", qps);
    printf("    吞吐量: %.2f MB/s\n", throughput);
    printf("    平均耗时: %.3f 毫秒/操作\n", total_time_ms / (double)item_count);
}

// 修改测试 MSET 批量写入函数
int test_mset(void *client, int item_count) {
    printf("\n测试 MSET 批量写入 (%d 项)...", item_count);

    // 准备键值对
    char **keys = (char **)malloc(item_count * sizeof(char *));
    char **values = (char **)malloc(item_count * sizeof(char *));
    for (int i = 0; i < item_count; i++) {
        keys[i] = (char *)malloc(64);
        values[i] = (char *)malloc(128);
        snprintf(keys[i], 64, "mset_key_%d", i);
        snprintf(values[i], 128, "mset_value_%d", i);
    }

    SimpleRespValue_C response;
    clock_t start = clock();

    // 执行 MSET
    if (astra_lv_mset(client, (const char **)keys, (const char **)values, item_count, &response) == 0) {
        if (response.type == RESP_SIMPLE_STRING_C && response.str && strcmp(response.str, "OK") == 0) {
            clock_t end = clock();
            long long time_ms = get_time_diff_ms(start, end);
            
            printf(" ✅ 成功\n");
            printf("  耗时: %lld 毫秒\n", time_ms);
            
            // 打印性能指标
            print_performance_metrics("MSET", item_count, time_ms);
            
            // 验证写入
            int success_count = 0;
            for (int i = 0; i < item_count; i++) {
                SimpleRespValue_C get_response;
                if (astra_lv_get(client, keys[i], &get_response) == 0 &&
                    get_response.type == RESP_BULK_STRING_C &&
                    get_response.str && strcmp(get_response.str, values[i]) == 0) {
                    success_count++;
                }
            }
            printf("  验证成功: %d/%d\n", success_count, item_count);

            // 清理测试数据
            for (int i = 0; i < item_count; i++) {
                const char *del_keys[] = {keys[i]};
                astra_lv_del(client, del_keys, 1, &response);
            }

            // 释放内存
            for (int i = 0; i < item_count; i++) {
                free(keys[i]);
                free(values[i]);
            }
            free(keys);
            free(values);

            return 0;
        } else {
            printf(" ❌ 响应异常: ");
            print_response_type(response.type);
            if (response.str) printf(" 内容: %s\n", response.str);
            else printf("\n");
        }
    } else {
        fprintf(stderr, " ❌ 失败: %s\n", astra_lv_last_error());
    }

    // 释放内存
    for (int i = 0; i < item_count; i++) {
        free(keys[i]);
        free(values[i]);
    }
    free(keys);
    free(values);
    return -1;
}

// 修改测试 MGET 批量读取函数
int test_mget(void *client, int item_count) {
    printf("\n测试 MGET 批量读取 (%d 项)...", item_count);

    // 准备键值对
    char **keys = (char **)malloc(item_count * sizeof(char *));
    char **values = (char **)malloc(item_count * sizeof(char *));
    for (int i = 0; i < item_count; i++) {
        keys[i] = (char *)malloc(64);
        values[i] = (char *)malloc(128);
        snprintf(keys[i], 64, "mget_key_%d", i);
        snprintf(values[i], 128, "mget_value_%d", i);

        // 预先写入数据
        SimpleRespValue_C set_response;
        astra_lv_set(client, keys[i], values[i], &set_response);
    }

    SimpleRespValue_C response;
    clock_t start = clock();

    // 执行 MGET
    if (astra_lv_mget(client, (const char **)keys, item_count, &response) == 0) {
        if (response.type == RESP_ARRAY_C) {
            clock_t end = clock();
            long long time_ms = get_time_diff_ms(start, end);
            
            printf(" ✅ 成功\n");
            printf("  耗时: %lld 毫秒\n", time_ms);
            
            // 打印性能指标
            print_performance_metrics("MGET", item_count, time_ms);
            
            // 验证结果：通过 ArrayContext_C 访问数组元素
            int success_count = 0;
            if (response.array_size == item_count) {
                ArrayContext_C array_ctx;
                // 1. 初始化数组访问上下文
                if (astra_lv_begin_array_access(client, &response, &array_ctx) == 0) {
                    for (int i = 0; i < item_count; i++) {
                        SimpleRespValue_C element;
                        // 2. 使用上下文访问指定索引的元素
                        if (astra_lv_get_array_element(client, &array_ctx, i, &element) == 0 &&
                            element.type == RESP_BULK_STRING_C &&
                            element.str && strcmp(element.str, values[i]) == 0) {
                            success_count++;
                        }
                    }
                    // 3. 结束数组访问，释放上下文资源
                    astra_lv_end_array_access(&array_ctx);
                } else {
                    printf("  ❌ 数组上下文初始化失败\n");
                }
            }
            printf("  验证成功: %d/%d\n", success_count, item_count);

            // 清理测试数据
            for (int i = 0; i < item_count; i++) {
                const char *del_keys[] = {keys[i]};
                astra_lv_del(client, del_keys, 1, &response);
            }

            // 释放内存
            for (int i = 0; i < item_count; i++) {
                free(keys[i]);
                free(values[i]);
            }
            free(keys);
            free(values);

            return 0;
        } else {
            printf(" ❌ 响应类型错误: ");
            print_response_type(response.type);
            printf("\n");
        }
    } else {
        fprintf(stderr, " ❌ 失败: %s\n", astra_lv_last_error());
    }

    // 释放内存
    for (int i = 0; i < item_count; i++) {
        free(keys[i]);
        free(values[i]);
    }
    free(keys);
    free(values);
    return -1;
}

// 修改主测试函数
int main() {
    // 测试基础接口 - 创建客户端
    void *client = astra_lv_client_create("127.0.0.1", 6380);
    if (!client) {
        fprintf(stderr, "❌ 客户端创建失败: %s\n", astra_lv_last_error());
        return 1;
    }
    printf("✅ 客户端创建成功\n");

    SimpleRespValue_C response;

    // 测试 PING 命令
    printf("\n测试 PING 命令...\n");
    if (astra_lv_ping(client, &response) == 0) {
        if (response.type == RESP_SIMPLE_STRING_C) {
            printf("✅ PING 成功: %s %s\n", response.str,
                   (response.str ? "" : "(空响应)"));
        } else {
            printf("❌ PING 响应类型错误: ");
            print_response_type(response.type);
            printf("\n");
        }
    } else {
        fprintf(stderr, "❌ PING 失败: %s\n", astra_lv_last_error());
    }

    // 测试 1000 次 SET 写入
    printf("\n测试 1000 次 SET 写入...\n");
    const int WRITE_COUNT = 1000;
    int success_count = 0;
    int fail_count = 0;
    clock_t write_start = clock();

    for (int i = 0; i < WRITE_COUNT; i++) {
        // 生成唯一键值对（格式: test_key_0 到 test_key_999）
        char key[64];
        char value[128];
        snprintf(key, sizeof(key), "test_key_%d", i);
        snprintf(value, sizeof(value), "test_value_%d", i);

        // 执行SET命令
        if (astra_lv_set(client, key, value, &response) == 0) {
            if (response.type == RESP_SIMPLE_STRING_C &&
                response.str && strcmp(response.str, "OK") == 0) {
                success_count++;
            } else {
                fail_count++;
                if (fail_count <= 5) {  // 只打印前5个失败案例
                    printf("❌ 第 %d 次SET响应异常: ", i);
                    print_response_type(response.type);
                    if (response.str) printf(" 内容: %s\n", response.str);
                    else printf("\n");
                }
            }
        } else {
            fail_count++;
            if (fail_count <= 5) {
                fprintf(stderr, "❌ 第 %d 次SET失败: %s\n", i, astra_lv_last_error());
            }
        }

        // 进度提示
        if ((i + 1) % 100 == 0) {
            printf("  已完成 %d/%d 次写入...\n", i + 1, WRITE_COUNT);
        }
    }

    clock_t write_end = clock();
    long long write_time_ms = get_time_diff_ms(write_start, write_end);

    // 打印写入统计结果
    printf("\n1000次写入测试结果:\n");
    printf("  成功: %d 次\n", success_count);
    printf("  失败: %d 次\n", fail_count);
    printf("  总耗时: %lld 毫秒\n", write_time_ms);
    if (success_count > 0) {
        printf("  平均每次写入耗时: %.2f 毫秒\n",
               (double)write_time_ms / success_count);
    }

    // 随机验证10个写入结果
    printf("\n随机验证10个写入结果...\n");
    int verify_success = 0;
    for (int i = 0; i < 10; i++) {
        // 随机选择一个已写入的键
        int random_idx = rand() % WRITE_COUNT;
        char key[64];
        snprintf(key, sizeof(key), "test_key_%d", random_idx);

        if (astra_lv_get(client, key, &response) == 0) {
            if (response.type == RESP_BULK_STRING_C) {
                char expected_value[128];
                snprintf(expected_value, sizeof(expected_value), "test_value_%d", random_idx);

                if (response.str && strcmp(response.str, expected_value) == 0) {
                    verify_success++;
                } else {
                    printf("❌ 验证失败: %s 预期值=%s, 实际值=%s\n",
                           key, expected_value, response.str ? response.str : "(空)");
                }
            } else {
                printf("❌ 验证响应类型错误: %s ", key);
                print_response_type(response.type);
                printf("\n");
            }
        } else {
            fprintf(stderr, "❌ 验证GET失败: %s - %s\n", key, astra_lv_last_error());
        }
    }
    printf("验证结果: %d/10 验证成功\n", verify_success);

    // 测试 MSET 和 MGET 不同规模
    printf("\n================ 测试 MSET 和 MGET（不同规模） ================\n");
    
    // 小规模测试（10个键值对）
    printf("\n小规模测试（10个键值对）:\n");
    if (test_mset(client, 10) == 0) {
        printf("✅ MSET 小规模测试通过\n");
    } else {
        printf("❌ MSET 小规模测试失败\n");
    }
    if (test_mget(client, 10) == 0) {
        printf("✅ MGET 小规模测试通过\n");
    } else {
        printf("❌ MGET 小规模测试失败\n");
    }
    
    // 中规模测试（100个键值对）
    printf("\n中规模测试（100个键值对）:\n");
    if (test_mset(client, 100) == 0) {
        printf("✅ MSET 中规模测试通过\n");
    } else {
        printf("❌ MSET 中规模测试失败\n");
    }
    if (test_mget(client, 100) == 0) {
        printf("✅ MGET 中规模测试通过\n");
    } else {
        printf("❌ MGET 中规模测试失败\n");
    }
    
    // 大规模测试（1000个键值对）
    printf("\n大规模测试（1000个键值对）:\n");
    if (test_mset(client, 1000) == 0) {
        printf("✅ MSET 大规模测试通过\n");
    } else {
        printf("❌ MSET 大规模测试失败\n");
    }
    if (test_mget(client, 1000) == 0) {
        printf("✅ MGET 大规模测试通过\n");
    } else {
        printf("❌ MGET 大规模测试失败\n");
    }
    
    // 清理测试数据（删除所有写入的键）
    printf("\n清理测试数据...\n");
    int delete_success = 0;
    for (int i = 0; i < WRITE_COUNT; i++) {
        char key[64];
        snprintf(key, sizeof(key), "test_key_%d", i);
        const char* del_keys[] = {key};

        if (astra_lv_del(client, del_keys, 1, &response) == 0 &&
            response.type == RESP_INTEGER_C && response.integer > 0) {
            delete_success++;
        }
    }
    printf("清理完成: 成功删除 %d/%d 个测试键\n", delete_success, WRITE_COUNT);

    // 清理资源
    astra_lv_client_destroy(client);
    printf("\n✅ 客户端已销毁，测试结束\n");
    return 0;
}

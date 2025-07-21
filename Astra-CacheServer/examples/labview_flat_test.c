#include "sdk/astra_client_labview.h"
#include "sdk/astra_resp.h"
#include <stdio.h>
#include <string.h>

int main() {
    // 1. 创建客户端连接
    void *client = astra_lv_client_create("127.0.0.1", 6380);
    if (!client) {
        printf("❌ 客户端创建失败: %s\n", astra_lv_last_error());
        return 1;
    }
    printf("✅ 客户端创建成功\n");

    // 2. Ping 测试
    {
        int type;
        const char *str = NULL;
        long long integer;
        int array_size;

        printf("📌 测试 astra_lv_ping_flat...\n");
        int result = astra_lv_ping_flat(client, &type, &str, &integer, &array_size);
        if (result == 0) {
            printf("✅ Ping 成功 | 类型: ");
            switch (type) {
                case RESP_SIMPLE_STRING_C: printf("Simple String"); break;
                case RESP_BULK_STRING_C:   printf("Bulk String"); break;
                case RESP_INTEGER_C:       printf("Integer"); break;
                case RESP_ARRAY_C:         printf("Array"); break;
                case RESP_ERROR_C:         printf("Error"); break;
                case RESP_NIL_C:           printf("Nil"); break;
                default:                   printf("Unknown");
            }
            if (str) printf(" | 值: %s\n", str);
            else if (type == RESP_INTEGER_C) printf(" | 值: %lld\n", integer);
            else printf("\n");
        } else {
            printf("❌ Ping 失败: %s\n", astra_lv_last_error());
        }
    }

    // 3. Set 测试
    {
        int type;
        const char *str = NULL;
        long long integer;
        int array_size;

        printf("📌 测试 astra_lv_set_flat...\n");
        int result = astra_lv_set_flat(client, "test_key", "test_value", &type, &str, &integer, &array_size);
        if (result == 0) {
            printf("✅ Set 成功 | 类型: ");
            switch (type) {
                case RESP_SIMPLE_STRING_C: printf("Simple String | 值: %s\n", str); break;
                case RESP_BULK_STRING_C:   printf("Bulk String | 值: %s\n", str); break;
                case RESP_INTEGER_C:       printf("Integer | 值: %lld\n", integer); break;
                case RESP_ARRAY_C:         printf("Array | 数组大小: %d\n", array_size); break;
                case RESP_ERROR_C:         printf("Error | 值: %s\n", str); break;
                case RESP_NIL_C:           printf("Nil\n"); break;
                default:                   printf("Unknown\n");
            }
        } else {
            printf("❌ Set 失败: %s\n", astra_lv_last_error());
        }
    }

    // 4. Get 测试
    {
        int type;
        const char *str = NULL;
        long long integer;
        int array_size;

        printf("📌 测试 astra_lv_get_flat...\n");
        int result = astra_lv_get_flat(client, "test_key", &type, &str, &integer, &array_size);
        if (result == 0) {
            printf("✅ Get 成功 | 类型: ");
            switch (type) {
                case RESP_SIMPLE_STRING_C: printf("Simple String | 值: %s\n", str); break;
                case RESP_BULK_STRING_C:   printf("Bulk String | 值: %s\n", str); break;
                case RESP_INTEGER_C:       printf("Integer | 值: %lld\n", integer); break;
                case RESP_ARRAY_C:         printf("Array | 数组大小: %d\n", array_size); break;
                case RESP_ERROR_C:         printf("Error | 值: %s\n", str); break;
                case RESP_NIL_C:           printf("Nil\n"); break;
                default:                   printf("Unknown\n");
            }
        } else {
            printf("❌ Get 失败: %s\n", astra_lv_last_error());
        }
    }

    // 5. MSet 测试
    {
        int type;
        const char *str = NULL;
        long long integer;
        int array_size;

        // 模拟两个键值对
        const char keys_buffer[] = "mset_key1\0mset_key2\0";
        const char values_buffer[] = "mset_value1\0mset_value2\0";
        int item_count = 2;

        printf("📌 测试 astra_lv_mset_flat...\n");
        int result = astra_lv_mset_flat(client,
                                        keys_buffer, sizeof(keys_buffer),
                                        values_buffer, sizeof(values_buffer),
                                        item_count,
                                        &type, &str, &integer, &array_size);
        if (result == 0) {
            printf("✅ MSet 成功 | 类型: ");
            switch (type) {
                case RESP_SIMPLE_STRING_C: printf("Simple String | 值: %s\n", str); break;
                case RESP_BULK_STRING_C:   printf("Bulk String | 值: %s\n", str); break;
                case RESP_INTEGER_C:       printf("Integer | 值: %lld\n", integer); break;
                case RESP_ARRAY_C:         printf("Array | 数组大小: %d\n", array_size); break;
                case RESP_ERROR_C:         printf("Error | 值: %s\n", str); break;
                case RESP_NIL_C:           printf("Nil\n"); break;
                default:                   printf("Unknown\n");
            }
        } else {
            printf("❌ MSet 失败: %s\n", astra_lv_last_error());
        }
    }

    // 6. MGet 测试
    {
        int type;
        const char *str = NULL;
        long long integer;
        int array_size;

        const char keys_buffer[] = "mset_key1\0mset_key2\0";

        printf("📌 测试 astra_lv_mget_flat...\n");
        int result = astra_lv_mget_flat(client,
                                        keys_buffer, sizeof(keys_buffer),
                                        2,
                                        &type, &str, &integer, &array_size);
        if (result == 0) {
            printf("✅ MGet 成功 | 类型: ");
            switch (type) {
                case RESP_SIMPLE_STRING_C: printf("Simple String | 值: %s\n", str); break;
                case RESP_BULK_STRING_C:   printf("Bulk String | 值: %s\n", str); break;
                case RESP_INTEGER_C:       printf("Integer | 值: %lld\n", integer); break;
                case RESP_ARRAY_C:         printf("Array | 数组大小: %d\n", array_size); break;
                case RESP_ERROR_C:         printf("Error | 值: %s\n", str); break;
                case RESP_NIL_C:           printf("Nil\n"); break;
                default:                   printf("Unknown\n");
            }
        } else {
            printf("❌ MGet 失败: %s\n", astra_lv_last_error());
        }
    }

    // 7. 销毁客户端
    astra_lv_client_destroy(client);
    printf("✅ 客户端已销毁\n");

    return 0;
}
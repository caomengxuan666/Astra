#include "sdk/astra_client_c.h"
#include <stdio.h>
#include <string.h>
#include "core/macros.hpp"
// 打印响应值详情（辅助函数）
void print_resp_details(const RespValue_C* resp) {
    if (!resp) {
        printf("Response is NULL\n");
        return;
    }

    switch (astra_resp_value_type(resp)) {
        case RESP_SIMPLE_STRING_C:
            printf("Type: Simple String, Value: %s\n", astra_resp_value_get_string(resp));
            break;
        case RESP_BULK_STRING_C:
            printf("Type: Bulk String, Value: %s\n", astra_resp_value_get_string(resp));
            break;
        case RESP_INTEGER_C:
            printf("Type: Integer, Value: %lld\n", astra_resp_value_get_integer(resp));
            break;
        case RESP_ARRAY_C: {
            size_t size = astra_resp_value_array_size(resp);
            printf("Type: Array, Size: %zu\n", size);
            for (size_t i = 0; i < size; i++) {
                const RespValue_C* elem = astra_resp_value_array_element(resp, i);
                printf("  Element %zu: ", i);
                if (astra_resp_value_type(elem) == RESP_BULK_STRING_C) {
                    printf("%s\n", astra_resp_value_get_string(elem));
                } else if (astra_resp_value_type(elem) == RESP_INTEGER_C) {
                    printf("%lld\n", astra_resp_value_get_integer(elem));
                } else {
                    printf("(Type: %d)\n", astra_resp_value_type(elem));
                }
            }
            break;
        }
        case RESP_ERROR_C:
            printf("Type: Error, Message: %s\n", astra_resp_value_get_string(resp));
            break;
        case RESP_NIL_C:
            printf("Type: Nil (null)\n");
            break;
        default:
            printf("Type: Unknown\n");
    }
}

int main() {
    // 1. 创建客户端
    AstraClient_C* client = astra_client_create("127.0.0.1", 6380);  // 注意端口是否正确
    if (!client) {
        printf("Failed to create client: %s\n", astra_client_last_error());
        return 1;
    }
    printf("Client created successfully\n\n");

    // 2. 测试PING命令
    printf("=== Testing PING ===\n");
    RespValue_C* ping_resp = astra_client_ping(client);
    if (!ping_resp) {
        printf("PING failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(ping_resp);
    astra_resp_value_destroy(ping_resp);
    printf("\n");

    // 3. 测试SET命令
    printf("=== Testing SET ===\n");
    RespValue_C* set_resp = astra_client_set(client, "test_key", "test_value");
    if (!set_resp) {
        printf("SET failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(set_resp);
    astra_resp_value_destroy(set_resp);
    printf("\n");

    // 4. 测试GET命令
    printf("=== Testing GET ===\n");
    RespValue_C* get_resp = astra_client_get(client, "test_key");
    if (!get_resp) {
        printf("GET failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(get_resp);
    astra_resp_value_destroy(get_resp);
    printf("\n");

    // 5. 测试SETEX命令（带过期时间）
    printf("=== Testing SETEX ===\n");
    RespValue_C* setex_resp = astra_client_setex(client, "expire_key", "temp_value", 60);  // 60秒过期
    if (!setex_resp) {
        printf("SETEX failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(setex_resp);
    astra_resp_value_destroy(setex_resp);
    printf("\n");

    // 6. 测试TTL命令（查看过期时间）
    printf("=== Testing TTL ===\n");
    RespValue_C* ttl_resp = astra_client_ttl(client, "expire_key");
    if (!ttl_resp) {
        printf("TTL failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(ttl_resp);
    astra_resp_value_destroy(ttl_resp);
    printf("\n");

    // 7. 测试EXISTS命令（检查键是否存在）
    printf("=== Testing EXISTS ===\n");
    RespValue_C* exists_resp = astra_client_exists(client, "test_key");
    if (!exists_resp) {
        printf("EXISTS failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(exists_resp);
    astra_resp_value_destroy(exists_resp);
    printf("\n");

    // 8. 测试INCR命令（自增）
    printf("=== Testing INCR ===\n");
    // 先设置初始值
    astra_client_set(client, "counter", "0");
    RespValue_C* incr_resp = astra_client_incr(client, "counter");
    if (!incr_resp) {
        printf("INCR failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(incr_resp);
    astra_resp_value_destroy(incr_resp);
    printf("\n");

    // 9. 测试DECR命令（自减）
    printf("=== Testing DECR ===\n");
    RespValue_C* decr_resp = astra_client_decr(client, "counter");
    if (!decr_resp) {
        printf("DECR failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(decr_resp);
    astra_resp_value_destroy(decr_resp);
    printf("\n");

    // 10. 测试KEYS命令（列出键）
    printf("=== Testing KEYS ===\n");
    RespValue_C* keys_resp = astra_client_keys(client, "*");  // 匹配所有键
    if (!keys_resp) {
        printf("KEYS failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(keys_resp);
    astra_resp_value_destroy(keys_resp);
    printf("\n");

    // 11. 测试DEL命令（删除键）
    printf("=== Testing DEL ===\n");
    const char* keys_to_del[] = {"test_key", "expire_key", "counter"};
    RespValue_C* del_resp = astra_client_del(client, keys_to_del, 3);
    if (!del_resp) {
        printf("DEL failed: %s\n", astra_client_last_error());
        astra_client_destroy(client);
        return 1;
    }
    print_resp_details(del_resp);
    astra_resp_value_destroy(del_resp);
    printf("\n");

    // 12. 清理资源
    astra_client_destroy(client);
    printf("Client destroyed successfully\n");

    return 0;
}
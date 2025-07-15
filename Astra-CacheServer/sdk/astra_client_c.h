#ifndef ASTRA_CLIENT_C_H
#define ASTRA_CLIENT_C_H

#ifdef __cplusplus
extern "C" {
#endif
#include <core/macros.hpp>
typedef long long int64;

// 不透明指针类型
typedef struct AstraClient_C AstraClient_C;
typedef struct RespValue_C RespValue_C;

// RESP 类型枚举
typedef enum {
    RESP_SIMPLE_STRING_C,
    RESP_BULK_STRING_C,
    RESP_INTEGER_C,
    RESP_ARRAY_C,
    RESP_ERROR_C,
    RESP_NIL_C
} RespType_C;

// 客户端创建与销毁
ZEN_API AstraClient_C *astra_client_create(const char *host, int port);
ZEN_API void astra_client_destroy(AstraClient_C *client);

// 通用命令执行
ZEN_API RespValue_C *astra_client_send_command(AstraClient_C *client, const char **argv,
                                               int argc);

// 响应值操作
ZEN_API RespType_C astra_resp_value_type(const RespValue_C *value);
ZEN_API const char *astra_resp_value_get_string(const RespValue_C *value);
ZEN_API int64 astra_resp_value_get_integer(const RespValue_C *value);
ZEN_API size_t astra_resp_value_array_size(const RespValue_C *value);
ZEN_API const RespValue_C *astra_resp_value_array_element(const RespValue_C *value,
                                                          size_t index);
ZEN_API void astra_resp_value_destroy(RespValue_C *value);

// 常用命令封装
ZEN_API RespValue_C *astra_client_ping(AstraClient_C *client);
ZEN_API RespValue_C *astra_client_set(AstraClient_C *client, const char *key,
                                      const char *value);
ZEN_API RespValue_C *astra_client_setex(AstraClient_C *client, const char *key,
                                        const char *value, int ttl);
ZEN_API RespValue_C *astra_client_get(AstraClient_C *client, const char *key);
ZEN_API RespValue_C *astra_client_del(AstraClient_C *client, const char **keys,
                                      int key_count);
ZEN_API RespValue_C *astra_client_keys(AstraClient_C *client, const char *pattern);
ZEN_API RespValue_C *astra_client_ttl(AstraClient_C *client, const char *key);
ZEN_API RespValue_C *astra_client_exists(AstraClient_C *client, const char *key);
ZEN_API RespValue_C *astra_client_incr(AstraClient_C *client, const char *key);
ZEN_API RespValue_C *astra_client_decr(AstraClient_C *client, const char *key);

// 错误处理
ZEN_API const char *astra_client_last_error();

#ifdef __cplusplus
}
#endif

#endif// ASTRA_CLIENT_C_H
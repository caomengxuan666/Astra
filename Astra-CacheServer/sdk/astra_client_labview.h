#ifndef ASTRA_CLIENT_LABVIEW_H
#define ASTRA_CLIENT_LABVIEW_H

#ifdef __cplusplus
extern "C" {
#endif
#include "astra_resp.h"
#include <core/macros.hpp>

typedef struct {
    int type;         // RESP类型枚举值
    const char *str;  // 字符串值
    long long integer;// 整数值
    int array_size;   // 数组大小
    void *reserved;   // 内部使用，保持原始数据生命周期
} SimpleRespValue_C;


// 数组访问上下文
typedef struct {
    void *context_id;// 内部使用的上下文ID
} ArrayContext_C;

// 客户端管理
ZEN_API void *astra_lv_client_create(const char *host, int port);
ZEN_API void astra_lv_client_destroy(void *client);

// 基本命令接口
ZEN_API int astra_lv_ping(void *client, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_set(void *client, const char *key, const char *value, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_get(void *client, const char *key, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_del(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_keys(void *client, const char *pattern, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_ttl(void *client, const char *key, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_exists(void *client, const char *key, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_incr(void *client, const char *key, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_decr(void *client, const char *key, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_setex(void *client, const char *key, const char *value, int ttl, SimpleRespValue_C *out_response);

// 批量操作接口
ZEN_API int astra_lv_mset(void *client, const char **keys, const char **values, int item_count, SimpleRespValue_C *out_response);
ZEN_API int astra_lv_mget(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response);

// 数组访问接口
ZEN_API int astra_lv_begin_array_access(void *client, const SimpleRespValue_C *array_response,
                                        ArrayContext_C *out_context);
ZEN_API int astra_lv_get_array_element(void *client, const ArrayContext_C *context,
                                       int index, SimpleRespValue_C *out_element);
ZEN_API void astra_lv_end_array_access(ArrayContext_C *context);

// 内存管理
ZEN_API void astra_lv_free_response(SimpleRespValue_C *response);

// 错误处理
ZEN_API const char *astra_lv_last_error();

#ifdef __cplusplus
}
#endif

#endif// ASTRA_CLIENT_LABVIEW_H
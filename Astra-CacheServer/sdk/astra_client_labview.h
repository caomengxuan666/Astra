// astra_client_labview.h
#ifndef ASTRA_CLIENT_LABVIEW_H
#define ASTRA_CLIENT_LABVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <core/macros.hpp>

/**
 * @brief RESP 响应结构体
 */
typedef struct {
    int type;          /**< RESP 类型枚举 */
    const char *str;   /**< 字符串值 */
    long long integer; /**< 整数 */
    int array_size;    /**< 数组大小 */
    void *reserved;    /**< 保留字段 */
} SimpleRespValue_C;

/**
 * @brief 数组访问上下文
 */
typedef struct {
    void *context_id;
} ArrayContext_C;

// 客户端管理
/**
 * @brief 创建客户端
 * @param host 主机名
 * @param port 端口号
 * @return 客户端指针
 */
ZEN_API void *ZEN_CALL astra_lv_client_create(const char *host, int port);
/**
 * @brief 销毁客户端
 * @param client 客户端指针
 */
ZEN_API void ZEN_CALL astra_lv_client_destroy(void *client);

// 基本命令接口
/**
 * @brief Ping 命令
 * @param client 客户端指针
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_ping(void *client, SimpleRespValue_C *out_response);
/**
 * @brief 设置键值
 * @param client 客户端指针
 * @param key 键
 * @param value 值
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_set(void *client, const char *key, const char *value, SimpleRespValue_C *out_response);
/**
 * @brief 获取键值
 * @param client 客户端指针
 * @param key 键
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_get(void *client, const char *key, SimpleRespValue_C *out_response);
/**
 * @brief 删除键
 * @param client 客户端指针
 * @param keys 键数组
 * @param key_count 键数量
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_del(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response);
/**
 * @brief 获取键列表
 * @param client 客户端指针
 * @param pattern 模式
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_keys(void *client, const char *pattern, SimpleRespValue_C *out_response);
/**
 * @brief 获取键的生存时间
 * @param client 客户端指针
 * @param key 键
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_ttl(void *client, const char *key, SimpleRespValue_C *out_response);
/**
 * @brief 检查键是否存在
 * @param client 客户端指针
 * @param key 键
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_exists(void *client, const char *key, SimpleRespValue_C *out_response);
/**
 * @brief 增加键的值
 * @param client 客户端指针
 * @param key 键
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_incr(void *client, const char *key, SimpleRespValue_C *out_response);
/**
 * @brief 减少键的值
 * @param client 客户端指针
 * @param key 键
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_decr(void *client, const char *key, SimpleRespValue_C *out_response);
/**
 * @brief 设置键值并指定生存时间
 * @param client 客户端指针
 * @param key 键
 * @param value 值
 * @param ttl 生存时间
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_setex(void *client, const char *key, const char *value, int ttl, SimpleRespValue_C *out_response);

// 批量操作接口
/**
 * @brief 批量设置键值
 * @param client 客户端指针
 * @param keys 键数组
 * @param values 值数组
 * @param item_count 键值对数量
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_mset(void *client, const char **keys, const char **values, int item_count, SimpleRespValue_C *out_response);
/**
 * @brief 批量获取键值
 * @param client 客户端指针
 * @param keys 键数组
 * @param key_count 键数量
 * @param out_response 输出响应
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_mget(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response);

// 数组访问接口
/**
 * @brief 开始数组访问
 * @param client 客户端指针
 * @param array_response 数组响应
 * @param out_context 输出上下文
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_begin_array_access(void *client, const SimpleRespValue_C *array_response, ArrayContext_C *out_context);
/**
 * @brief 获取数组元素
 * @param client 客户端指针
 * @param context 上下文
 * @param index 索引
 * @param out_element 输出元素
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_get_array_element(void *client, const ArrayContext_C *context, int index, SimpleRespValue_C *out_element);
/**
 * @brief 结束数组访问
 * @param context 上下文
 */
ZEN_API void ZEN_CALL astra_lv_end_array_access(ArrayContext_C *context);

// 内存管理
/**
 * @brief 释放响应
 * @param response 响应
 */
ZEN_API void ZEN_CALL astra_lv_free_response(SimpleRespValue_C *response);

// 错误处理
/**
 * @brief 获取最后的错误信息
 * @return 错误信息
 */
ZEN_API const char *ZEN_CALL astra_lv_last_error();

// LabVIEW 友好封装函数声明
/**
 * @brief LabVIEW 友好的 Ping 命令
 * @param client 客户端指针
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_ping_flat(void *client, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的设置键值
 * @param client 客户端指针
 * @param key 键
 * @param value 值
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_set_flat(void *client, const char *key, const char *value, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的获取键值
 * @param client 客户端指针
 * @param key 键
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_get_flat(void *client, const char *key, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的批量设置键值
 * @param client 客户端指针
 * @param keys_buffer 键缓冲区
 * @param keys_buffer_size 键缓冲区大小
 * @param values_buffer 值缓冲区
 * @param values_buffer_size 值缓冲区大小
 * @param item_count 键值对数量
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_mset_flat(void *client, const char *keys_buffer, int keys_buffer_size, const char *values_buffer, int values_buffer_size, int item_count, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的批量获取键值
 * @param client 客户端指针
 * @param keys_buffer 键缓冲区
 * @param keys_buffer_size 键缓冲区大小
 * @param key_count 键数量
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_mget_flat(void *client, const char *keys_buffer, int keys_buffer_size, int key_count, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的删除键
 * @param client 客户端指针
 * @param keys_buffer 键缓冲区
 * @param keys_buffer_size 键缓冲区大小
 * @param key_count 键数量
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_del_flat(void *client, const char *keys_buffer, int keys_buffer_size, int key_count, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的获取键列表
 * @param client 客户端指针
 * @param pattern 模式
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_keys_flat(void *client, const char *pattern, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的开始数组访问
 * @param client 客户端指针
 * @param array_response_reserved 数组响应保留字段
 * @param out_context_id 输出上下文ID
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_begin_array_access_flat(void *client, void *array_response_reserved, void **out_context_id);
/**
 * @brief LabVIEW 友好的获取数组元素
 * @param client 客户端指针
 * @param context_id 上下文ID
 * @param index 索引
 * @param out_type 输出类型
 * @param out_str 输出字符串
 * @param out_integer 输出整数
 * @param out_array_size 输出数组大小
 * @return 操作结果
 */
ZEN_API int ZEN_CALL astra_lv_get_array_element_flat(void *client, void *context_id, int index, int *out_type, const char **out_str, long long *out_integer, int *out_array_size);
/**
 * @brief LabVIEW 友好的释放响应
 * @param type 类型
 * @param str 字符串
 * @param integer 整数
 * @param array_size 数组大小
 * @param reserved 保留字段
 */
ZEN_API void ZEN_CALL astra_lv_free_response_flat(int type, const char *str, long long integer, int array_size, void *reserved);

#ifdef __cplusplus
}
#endif

#endif// ASTRA_CLIENT_LABVIEW_H
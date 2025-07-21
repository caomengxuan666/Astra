extern "C" {
#include "astra_client_labview.h"
#include "astra_client_c.h"
}
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "astra_resp.h"

// 线程局部错误存储
thread_local std::string last_error_lv;

// 数组上下文管理结构（修正：去掉create_time的const修饰，允许更新）
struct ArrayContext {
    std::shared_ptr<RespValue_C> root_value;
    const RespValue_C *array_value;
    std::chrono::steady_clock::time_point create_time;// 修正：移除const
};

// 上下文管理全局变量
static std::mutex context_mutex;
static std::unordered_map<uintptr_t, ArrayContext> active_contexts;
static std::atomic<uintptr_t> next_context_id(1);
static constexpr auto CONTEXT_TIMEOUT = std::chrono::minutes(5);

// 清理超时上下文
static void ZEN_CALL cleanup_expired_contexts() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(context_mutex);

    for (auto it = active_contexts.begin(); it != active_contexts.end();) {
        if (now - it->second.create_time > CONTEXT_TIMEOUT) {
            it = active_contexts.erase(it);
        } else {
            ++it;
        }
    }
}

// 转换复杂RespValue到简单结构
static void ZEN_CALL convert_to_simple_resp(const RespValue_C *resp, SimpleRespValue_C *simple) {
    if (!resp || !simple) return;

    simple->type = astra_resp_value_type(resp);
    simple->reserved = const_cast<RespValue_C *>(resp);

    switch (simple->type) {
        case RESP_SIMPLE_STRING_C:
        case RESP_BULK_STRING_C:
        case RESP_ERROR_C:
            simple->str = astra_resp_value_get_string(resp);
            simple->integer = 0;
            break;
        case RESP_INTEGER_C:
            simple->integer = astra_resp_value_get_integer(resp);
            simple->str = nullptr;
            break;
        case RESP_ARRAY_C:
            simple->array_size = static_cast<int>(astra_resp_value_array_size(resp));
            simple->str = nullptr;
            simple->integer = 0;
            break;
        case RESP_NIL_C:
        default:
            simple->str = nullptr;
            simple->integer = 0;
            simple->array_size = 0;
    }
}

// 客户端管理
void *ZEN_CALL astra_lv_client_create(const char *host, int port) {
    try {
        return astra_client_create(host, port);
    } catch (const std::exception &e) {
        last_error_lv = e.what();
        return nullptr;
    }
}

void ZEN_CALL astra_lv_client_destroy(void *client) {
    astra_client_destroy(static_cast<AstraClient_C *>(client));
}

// 基本命令接口实现（修正宏定义中的括号错误）
#define IMPLEMENT_SIMPLE_COMMAND(func_name, c_func)                       \
    int func_name(void *client, SimpleRespValue_C *out_response) {        \
        if (!client || !out_response) {                                   \
            last_error_lv = "Invalid arguments";                          \
            return -1;                                                    \
        }                                                                 \
        RespValue_C *resp = c_func(static_cast<AstraClient_C *>(client)); \
        if (!resp) {                                                      \
            last_error_lv = astra_client_last_error();                    \
            return -1;                                                    \
        }                                                                 \
        convert_to_simple_resp(resp, out_response);                       \
        return 0;                                                         \
    }

#define IMPLEMENT_KEY_COMMAND(func_name, c_func)                                    \
    int func_name(void *client, const char *key, SimpleRespValue_C *out_response) { \
        if (!client || !key || !out_response) {                                     \
            last_error_lv = "Invalid arguments";                                    \
            return -1;                                                              \
        }                                                                           \
        RespValue_C *resp = c_func(static_cast<AstraClient_C *>(client), key);      \
        if (!resp) {                                                                \
            last_error_lv = astra_client_last_error();                              \
            return -1;                                                              \
        }                                                                           \
        convert_to_simple_resp(resp, out_response);                                 \
        return 0;                                                                   \
    }

IMPLEMENT_SIMPLE_COMMAND(astra_lv_ping, astra_client_ping)

IMPLEMENT_KEY_COMMAND(astra_lv_get, astra_client_get)

IMPLEMENT_KEY_COMMAND(astra_lv_ttl, astra_client_ttl)

IMPLEMENT_KEY_COMMAND(astra_lv_exists, astra_client_exists)

IMPLEMENT_KEY_COMMAND(astra_lv_incr, astra_client_incr)

IMPLEMENT_KEY_COMMAND(astra_lv_decr, astra_client_decr)

// 修正括号错误：将多余的右括号移到参数列表末尾
int ZEN_CALL astra_lv_set(void *client, const char *key, const char *value, SimpleRespValue_C *out_response) {
    if (!client || !key || !value || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }
    RespValue_C *resp = astra_client_set(static_cast<AstraClient_C *>(client), key, value);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }
    convert_to_simple_resp(resp, out_response);
    return 0;
}

int ZEN_CALL astra_lv_setex(void *client, const char *key, const char *value, int ttl, SimpleRespValue_C *out_response) {
    if (!client || !key || !value || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }
    RespValue_C *resp = astra_client_setex(static_cast<AstraClient_C *>(client), key, value, ttl);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }
    convert_to_simple_resp(resp, out_response);
    return 0;
}

int ZEN_CALL astra_lv_del(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response) {
    if (!client || !keys || key_count <= 0 || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }
    RespValue_C *resp = astra_client_del(static_cast<AstraClient_C *>(client), keys, key_count);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }
    convert_to_simple_resp(resp, out_response);
    return 0;
}

int ZEN_CALL astra_lv_keys(void *client, const char *pattern, SimpleRespValue_C *out_response) {
    if (!client || !pattern || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }
    RespValue_C *resp = astra_client_keys(static_cast<AstraClient_C *>(client), pattern);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }
    convert_to_simple_resp(resp, out_response);
    return 0;
}

int ZEN_CALL astra_lv_mset(void *client, const char **keys, const char **values, int item_count, SimpleRespValue_C *out_response) {
    if (!client || !keys || !values || item_count <= 0 || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }

    // 创建 MSetItem 数组
    std::vector<MSetItem> items;
    for (int i = 0; i < item_count; ++i) {
        items.push_back({keys[i], values[i]});
    }

    RespValue_C *resp = astra_client_mset(static_cast<AstraClient_C *>(client), items.data(), item_count);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }

    convert_to_simple_resp(resp, out_response);
    return 0;
}

int ZEN_CALL astra_lv_mget(void *client, const char **keys, int key_count, SimpleRespValue_C *out_response) {
    if (!client || !keys || key_count <= 0 || !out_response) {
        last_error_lv = "Invalid arguments";
        return -1;
    }

    RespValue_C *resp = astra_client_mget(static_cast<AstraClient_C *>(client), keys, key_count);
    if (!resp) {
        last_error_lv = astra_client_last_error();
        return -1;
    }

    convert_to_simple_resp(resp, out_response);
    return 0;
}

// 数组访问接口
int ZEN_CALL astra_lv_begin_array_access(void *client, const SimpleRespValue_C *array_response,
                                ArrayContext_C *out_context) {
    if (!array_response || array_response->type != RESP_ARRAY_C || !out_context) {
        last_error_lv = "Invalid arguments";
        return -1;
    }

    RespValue_C *root_value = static_cast<RespValue_C *>(array_response->reserved);
    if (!root_value) {
        last_error_lv = "Invalid array context";
        return -1;
    }

    cleanup_expired_contexts();

    std::lock_guard<std::mutex> lock(context_mutex);

    uintptr_t context_id = next_context_id.fetch_add(1);
    active_contexts[context_id] = {
            std::shared_ptr<RespValue_C>(root_value, [](RespValue_C *) { /* 不删除，由响应释放函数管理 */ }),
            root_value,
            std::chrono::steady_clock::now()};

    out_context->context_id = reinterpret_cast<void *>(context_id);
    return 0;
}

int ZEN_CALL astra_lv_get_array_element(void *client, const ArrayContext_C *context,
                               int index, SimpleRespValue_C *out_element) {
    if (!context || !out_element) {
        last_error_lv = "Invalid arguments";
        return -1;
    }

    uintptr_t context_id = reinterpret_cast<uintptr_t>(context->context_id);

    std::lock_guard<std::mutex> lock(context_mutex);
    auto it = active_contexts.find(context_id);
    if (it == active_contexts.end()) {
        last_error_lv = "Invalid context ID";
        return -1;
    }

    ArrayContext &array_ctx = it->second;                    // 修正：去掉const，允许修改create_time
    array_ctx.create_time = std::chrono::steady_clock::now();// 现在可以正常更新时间

    if (index < 0 || static_cast<size_t>(index) >= astra_resp_value_array_size(array_ctx.array_value)) {
        last_error_lv = "Array index out of bounds";
        return -1;
    }

    const RespValue_C *element = astra_resp_value_array_element(array_ctx.array_value, index);
    if (!element) {
        last_error_lv = "Failed to get array element";
        return -1;
    }

    convert_to_simple_resp(element, out_element);
    return 0;
}

void ZEN_CALL astra_lv_end_array_access(ArrayContext_C *context) {
    if (!context || !context->context_id) return;

    uintptr_t context_id = reinterpret_cast<uintptr_t>(context->context_id);

    std::lock_guard<std::mutex> lock(context_mutex);
    active_contexts.erase(context_id);
    context->context_id = nullptr;
}

// 内存管理
void ZEN_CALL astra_lv_free_response(SimpleRespValue_C *response) {
    if (!response) return;

    if (response->reserved) {
        astra_resp_value_destroy(static_cast<RespValue_C *>(response->reserved));
        response->reserved = nullptr;
    }

    response->str = nullptr;
    response->integer = 0;
    response->array_size = 0;
    response->type = RESP_NIL_C;
}

// 错误处理
const char *ZEN_CALL astra_lv_last_error() {
    return last_error_lv.empty() ? "No error" : last_error_lv.c_str();
}


// LabVIEW 友好封装函数实现

int ZEN_CALL astra_lv_ping_flat(
    void *client,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    SimpleRespValue_C response = {0};
    int result = astra_lv_ping(client, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_set_flat(
    void *client,
    const char *key,
    const char *value,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    SimpleRespValue_C response = {0};
    int result = astra_lv_set(client, key, value, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_get_flat(
    void *client,
    const char *key,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    SimpleRespValue_C response = {0};
    int result = astra_lv_get(client, key, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_mset_flat(
    void *client,
    const char *keys_buffer, int keys_buffer_size,
    const char *values_buffer, int values_buffer_size,
    int item_count,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    std::vector<const char*> keys;
    std::vector<const char*> values;

    const char *k_ptr = keys_buffer;
    for (int i = 0; i < item_count && k_ptr < keys_buffer + keys_buffer_size; ++i) {
        keys.push_back(k_ptr);
        k_ptr += strlen(k_ptr) + 1;
    }

    const char *v_ptr = values_buffer;
    for (int i = 0; i < item_count && v_ptr < values_buffer + values_buffer_size; ++i) {
        values.push_back(v_ptr);
        v_ptr += strlen(v_ptr) + 1;
    }

    SimpleRespValue_C response = {0};
    int result = astra_lv_mset(client, keys.data(), values.data(), item_count, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_mget_flat(
    void *client,
    const char *keys_buffer, int keys_buffer_size,
    int key_count,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    std::vector<const char*> keys;

    const char *ptr = keys_buffer;
    for (int i = 0; i < key_count && ptr < keys_buffer + keys_buffer_size; ++i) {
        keys.push_back(ptr);
        ptr += strlen(ptr) + 1;
    }

    SimpleRespValue_C response = {0};
    int result = astra_lv_mget(client, keys.data(), key_count, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_del_flat(
    void *client,
    const char *keys_buffer, int keys_buffer_size,
    int key_count,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    std::vector<const char*> keys;

    const char *ptr = keys_buffer;
    for (int i = 0; i < key_count && ptr < keys_buffer + keys_buffer_size; ++i) {
        keys.push_back(ptr);
        ptr += strlen(ptr) + 1;
    }

    SimpleRespValue_C response = {0};
    int result = astra_lv_del(client, keys.data(), key_count, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_keys_flat(
    void *client,
    const char *pattern,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    SimpleRespValue_C response = {0};
    int result = astra_lv_keys(client, pattern, &response);
    if (out_type) *out_type = response.type;
    if (out_str) *out_str = response.str;
    if (out_integer) *out_integer = response.integer;
    if (out_array_size) *out_array_size = response.array_size;
    return result;
}

int ZEN_CALL astra_lv_begin_array_access_flat(
    void *client,
    void *array_response_reserved,
    void **out_context_id) {
    ArrayContext_C context_out = {0};
    int result = astra_lv_begin_array_access(client, (SimpleRespValue_C *)array_response_reserved, &context_out);
    if (result == 0 && out_context_id) {
        *out_context_id = context_out.context_id;
    }
    return result;
}

int ZEN_CALL astra_lv_get_array_element_flat(
    void *client,
    void *context_id,
    int index,
    int *out_type,
    const char **out_str,
    long long *out_integer,
    int *out_array_size) {
    ArrayContext_C context = {0};
    context.context_id = context_id;
    SimpleRespValue_C element = {0};
    int result = astra_lv_get_array_element(client, &context, index, &element);
    if (out_type) *out_type = element.type;
    if (out_str) *out_str = element.str;
    if (out_integer) *out_integer = element.integer;
    if (out_array_size) *out_array_size = element.array_size;
    return result;
}

void ZEN_CALL astra_lv_free_response_flat(
    int type,
    const char *str,
    long long integer,
    int array_size,
    void *reserved) {
    SimpleRespValue_C response = {
        type, str, integer, array_size, reserved
    };
    astra_lv_free_response(&response);
}
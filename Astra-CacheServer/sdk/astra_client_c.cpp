extern "C" {
#include "astra_client_c.h"// C头文件
}
#include "astra_client.hpp"
#include <cstring>
#include <vector>

// 包装 C++ 类的结构体
struct AstraClient_C {
    Astra::Client::AstraClient *client;
};

struct RespValue_C {
    Astra::Client::RespValue *value;
};

// 线程局部存储错误信息
thread_local std::string last_error;

// 辅助函数：将 C++ RespValue 转换为 C RespValue_C
static RespValue_C *wrap_resp_value(Astra::Client::RespValue *value) {
    if (!value)
        return nullptr;
    RespValue_C *c_value = new RespValue_C;
    c_value->value = value;
    return c_value;
}

// 辅助函数：将 C++ vector<string> 转换为 C char**
static std::vector<std::string> c_array_to_vector(const char **arr, int count) {
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) {
        result.emplace_back(arr[i]);
    }
    return result;
}

// 客户端创建与销毁
AstraClient_C *astra_client_create(const char *host, int port) {
    try {
        AstraClient_C *client = new AstraClient_C;
        client->client = new Astra::Client::AstraClient(host, port);
        return client;
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

void astra_client_destroy(AstraClient_C *client) {
    if (client) {
        delete client->client;
        delete client;
    }
}

// 通用命令执行
RespValue_C *astra_client_send_command(AstraClient_C *client, const char **argv,
                                       int argc) {
    if (!client || !argv || argc <= 0) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            args.push_back(argv[i]);
        }

        // 创建一个临时命令对象
        class TempCommand : public Astra::Client::Command::ICommand {
            std::vector<std::string> args_;

        public:
            TempCommand(const std::vector<std::string> &args) : args_(args) {}
            std::vector<std::string> GetArgs() const override { return args_; }
        } cmd(args);

        auto result =
                new Astra::Client::RespValue(client->client->SendCommand(cmd));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

// 响应值操作
RespType_C astra_resp_value_type(const RespValue_C *value) {
    if (!value || !value->value)
        return RESP_NIL_C;

    switch (value->value->type) {
        case Astra::Client::RespType::SimpleString:
            return RESP_SIMPLE_STRING_C;
        case Astra::Client::RespType::BulkString:
            return RESP_BULK_STRING_C;
        case Astra::Client::RespType::Integer:
            return RESP_INTEGER_C;
        case Astra::Client::RespType::Array:
            return RESP_ARRAY_C;
        case Astra::Client::RespType::Error:
            return RESP_ERROR_C;
        case Astra::Client::RespType::Nil:
            return RESP_NIL_C;
        default:
            return RESP_NIL_C;
    }
}

const char *astra_resp_value_get_string(const RespValue_C *value) {
    if (!value || !value->value)
        return nullptr;
    return value->value->str.c_str();
}

int64 astra_resp_value_get_integer(const RespValue_C *value) {
    if (!value || !value->value)
        return 0;
    return value->value->integer;
}

size_t astra_resp_value_array_size(const RespValue_C *value) {
    if (!value || !value->value ||
        value->value->type != Astra::Client::RespType::Array)
        return 0;
    return value->value->array.size();
}

const RespValue_C *astra_resp_value_array_element(const RespValue_C *value,
                                                  size_t index) {
    if (!value || !value->value ||
        value->value->type != Astra::Client::RespType::Array ||
        index >= value->value->array.size()) {
        return nullptr;
    }

    static RespValue_C wrapper;
    wrapper.value =
            const_cast<Astra::Client::RespValue *>(&value->value->array[index]);
    return &wrapper;
}

void astra_resp_value_destroy(RespValue_C *value) {
    if (value) {
        delete value->value;
        delete value;
    }
}

// 常用命令封装
RespValue_C *astra_client_ping(AstraClient_C *client) {
    if (!client)
        return nullptr;
    try {
        auto result = new Astra::Client::RespValue(client->client->Ping());
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_set(AstraClient_C *client, const char *key,
                              const char *value) {
    if (!client || !key || !value)
        return nullptr;
    try {
        auto result = new Astra::Client::RespValue(client->client->Set(key, value));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

const char *astra_client_last_error() { return last_error.c_str(); }

// 实现剩余的命令封装
RespValue_C *astra_client_setex(AstraClient_C *client, const char *key,
                                const char *value, int ttl) {
    if (!client || !key || !value) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(
                client->client->Set(key, value, std::chrono::seconds(ttl)));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_get(AstraClient_C *client, const char *key) {
    if (!client || !key) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->Get(key));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_mset(AstraClient_C *client, const MSetItem *items, int item_count) {
    if (!client || !items || item_count <= 0) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        std::vector<std::pair<std::string, std::string>> keyValuePairs;
        for (int i = 0; i < item_count; ++i) {
            keyValuePairs.emplace_back(items[i].key, items[i].value);
        }

        auto result = new Astra::Client::RespValue(client->client->MSet(keyValuePairs));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_mget(AstraClient_C *client, const char **keys, int key_count) {
    if (!client || !keys || key_count <= 0) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        std::vector<std::string> key_list = c_array_to_vector(keys, key_count);
        auto result = new Astra::Client::RespValue(client->client->MGet(key_list));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_del(AstraClient_C *client, const char **keys,
                              int key_count) {
    if (!client || !keys || key_count <= 0) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        std::vector<std::string> key_list = c_array_to_vector(keys, key_count);
        auto result = new Astra::Client::RespValue(client->client->Del(key_list));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_keys(AstraClient_C *client, const char *pattern) {
    if (!client || !pattern) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->Keys(pattern));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_ttl(AstraClient_C *client, const char *key) {
    if (!client || !key) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->TTL(key));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_exists(AstraClient_C *client, const char *key) {
    if (!client || !key) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->Exists(key));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_incr(AstraClient_C *client, const char *key) {
    if (!client || !key) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->Incr(key));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}

RespValue_C *astra_client_decr(AstraClient_C *client, const char *key) {
    if (!client || !key) {
        last_error = "Invalid arguments";
        return nullptr;
    }

    try {
        auto result = new Astra::Client::RespValue(client->client->Decr(key));
        return wrap_resp_value(result);
    } catch (const std::exception &e) {
        last_error = e.what();
        return nullptr;
    }
}
#include "astra_client.hpp"
#include "astra_client_c.h"
#include "astra_client_labview.h"
#include "core/astra.hpp"
#include <benchmark/benchmark.h>
#include <hiredis/hiredis.h>
#include <random>
#include <thread>

// 测试配置
constexpr const char *TEST_HOST = "127.0.0.1";
constexpr int TEST_PORT = 6380;
constexpr int KEY_COUNT = 1000;// 测试键数量
constexpr int VALUE_SIZE = 100;// 测试值大小(字节)

// 添加缺失的AstraCPPClient类型定义
using AstraCPPClient = Astra::Client::AstraClient;

class ClientBenchmark : public benchmark::Fixture {
protected:
    void SetUp(const benchmark::State &state) override {
        // 生成随机测试数据
        std::uniform_int_distribution<int> dist('a', 'z');
        std::mt19937 gen(std::random_device{}());

        keys.resize(KEY_COUNT);
        values.resize(KEY_COUNT);

        for (int i = 0; i < KEY_COUNT; ++i) {
            keys[i] = "key_" + std::to_string(i);
            values[i].resize(VALUE_SIZE);
            for (auto &c: values[i]) {
                c = static_cast<char>(dist(gen));
            }
        }
    }

    void TearDown(const benchmark::State &state) override {
        // 清理测试数据
    }

    std::vector<std::string> keys;  // 测试键列表
    std::vector<std::string> values;// 测试值列表
};

BENCHMARK_DEFINE_F(ClientBenchmark, AstraC_SetGet)
(benchmark::State &state) {
    AstraClient_C *client = astra_client_create(TEST_HOST, TEST_PORT);
    if (!client) {
        state.SkipWithError(astra_client_last_error());
        return;
    }

    for (auto _: state) {
        // 测试SET
        for (int i = 0; i < KEY_COUNT; ++i) {
            RespValue_C *resp = astra_client_set(client, keys[i].c_str(), values[i].c_str());
            if (!resp) {
                state.SkipWithError(astra_client_last_error());
                astra_client_destroy(client);
                return;
            }
            astra_resp_value_destroy(resp);
        }

        // 测试GET
        for (int i = 0; i < KEY_COUNT; ++i) {
            RespValue_C *resp = astra_client_get(client, keys[i].c_str());
            if (!resp || astra_resp_value_type(resp) != RESP_BULK_STRING_C) {
                state.SkipWithError("GET failed or wrong response type");
                astra_resp_value_destroy(resp);
                astra_client_destroy(client);
                return;
            }
            astra_resp_value_destroy(resp);
        }
    }

    // 清理
    for (const auto &key: keys) {
        const char *key_cstr = key.c_str();
        astra_client_del(client, &key_cstr, 1);
    }
    astra_client_destroy(client);

    state.SetItemsProcessed(state.iterations() * KEY_COUNT * 2);// SET+GET
}

BENCHMARK_DEFINE_F(ClientBenchmark, AstraLV_SetGet)
(benchmark::State &state) {
    void *client = astra_lv_client_create(TEST_HOST, TEST_PORT);
    if (!client) {
        state.SkipWithError(astra_lv_last_error());
        return;
    }

    SimpleRespValue_C resp;

    for (auto _: state) {
        // 测试SET
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (astra_lv_set(client, keys[i].c_str(), values[i].c_str(), &resp) != 0) {
                state.SkipWithError(astra_lv_last_error());
                astra_lv_client_destroy(client);
                return;
            }
        }

        // 测试GET
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (astra_lv_get(client, keys[i].c_str(), &resp) != 0 || resp.type != RESP_BULK_STRING_C) {
                state.SkipWithError("GET failed or wrong response type");
                astra_lv_client_destroy(client);
                return;
            }
        }
    }

    // 清理
    for (const auto &key: keys) {
        const char *k = key.c_str();
        astra_lv_del(client, &k, 1, &resp);
    }
    astra_lv_client_destroy(client);

    state.SetItemsProcessed(state.iterations() * KEY_COUNT * 2);
}

BENCHMARK_DEFINE_F(ClientBenchmark, AstraCPP_SetGet)
(benchmark::State &state) {
    Astra::Client::AstraClient client(TEST_HOST, TEST_PORT);

    for (auto _: state) {
        // 测试SET
        for (int i = 0; i < KEY_COUNT; ++i) {
            auto resp = client.Set(keys[i], values[i]);
            if (resp.type != Astra::Client::RespType::SimpleString || resp.str != "OK") {
                state.SkipWithError("SET failed");
                return;
            }
        }

        // 测试GET
        for (int i = 0; i < KEY_COUNT; ++i) {
            auto resp = client.Get(keys[i]);
            if (resp.type != Astra::Client::RespType::BulkString || resp.str != values[i]) {
                state.SkipWithError("GET failed or wrong value");
                return;
            }
        }
    }

    // 清理
    client.Del(keys);

    state.SetItemsProcessed(state.iterations() * KEY_COUNT * 2);
}

BENCHMARK_DEFINE_F(ClientBenchmark, Hiredis_SetGet)
(benchmark::State &state) {
    redisContext *c = redisConnect(TEST_HOST, TEST_PORT);
    if (c == nullptr || c->err) {
        state.SkipWithError(c ? c->errstr : "Can't allocate redis context");
        if (c) redisFree(c);
        return;
    }

    for (auto _: state) {
        // 测试SET
        for (int i = 0; i < KEY_COUNT; ++i) {
            redisReply *reply = (redisReply *) redisCommand(c, "SET %s %s",
                                                            keys[i].c_str(), values[i].c_str());
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                state.SkipWithError(reply ? reply->str : "SET failed");
                freeReplyObject(reply);
                redisFree(c);
                return;
            }
            freeReplyObject(reply);
        }

        // 测试GET
        for (int i = 0; i < KEY_COUNT; ++i) {
            redisReply *reply = (redisReply *) redisCommand(c, "GET %s", keys[i].c_str());
            if (!reply || reply->type != REDIS_REPLY_STRING ||
                std::string(reply->str) != values[i]) {
                state.SkipWithError("GET failed or wrong value");
                freeReplyObject(reply);
                redisFree(c);
                return;
            }
            freeReplyObject(reply);
        }
    }

    // 清理
    for (const auto &key: keys) {
        const char *key_cstr = key.c_str();
        redisReply *reply = (redisReply *) redisCommand(c, "DEL %s", key_cstr);
        freeReplyObject(reply);
    }
    redisFree(c);

    state.SetItemsProcessed(state.iterations() * KEY_COUNT * 2);
}


// 注册单线程测试
BENCHMARK_REGISTER_F(ClientBenchmark, AstraC_SetGet)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(ClientBenchmark, AstraLV_SetGet)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(ClientBenchmark, AstraCPP_SetGet)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(ClientBenchmark, Hiredis_SetGet)->Unit(benchmark::kMillisecond);


BENCHMARK_MAIN();
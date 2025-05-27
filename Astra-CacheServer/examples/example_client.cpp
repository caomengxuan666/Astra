// example_client.cpp
#include "sdk/astra_client.hpp"
#include <iostream>

int main() {
    try {
        Astra::Client::AstraClient client("127.0.0.1", 8080);

        // 基础测试
        std::cout << "PING: " << client.Ping().str << std::endl;

        // 测试 Set/Get/Delete
        client.Set("name", "Alice");
        auto val = client.Get("name");
        if (val.type == Astra::Client::RespType::BulkString) {
            std::cout << "name = " << val.str << std::endl;
        }

        // 测试 Del
        client.Del({"name"});
        auto exists = client.Exists("name");
        std::cout << "name exists after delete? " << (exists.integer ? "yes" : "no") << std::endl;

        // 测试 TTL 过期机制
        client.Set("temp_key", "value");
        client.TTL("temp_key");// 应该返回 -1（永不过期）
        std::cout << "TTL of temp_key before expire: " << val.integer << "s" << std::endl;

        // 测试 Incr/Decr
        client.Set("counter", "5");
        client.Incr("counter");
        auto incr_val = client.Get("counter");
        std::cout << "counter after incr: " << incr_val.integer << std::endl;

        client.Decr("counter");
        auto decr_val = client.Get("counter");
        std::cout << "counter after decr: " << decr_val.integer << std::endl;

        // 测试 Keys 模式匹配
        client.Set("user:1000", "Alice");
        client.Set("user:1001", "Bob");
        client.Set("session:abc", "data");

        auto keys = client.Keys("user:*");
        std::cout << "User keys:" << std::endl;
        for (const auto &k: keys.array) {
            std::cout << "- " << k.str << std::endl;
        }

        // 边界测试
        try {
            client.Get("");// 空键测试
        } catch (const std::exception &e) {
            std::cerr << "Empty key test: " << e.what() << std::endl;
        }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
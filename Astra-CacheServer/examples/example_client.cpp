#include "sdk/astra_client.hpp"
#include <iostream>
#include <thread>

int main() {
    try {
        Astra::Client::AstraClient client("127.0.0.1", 6380);

        // 基础测试
        std::cout << "PING: " << client.Ping().str << std::endl;

        // 测试 Set/Get/Delete
        client.Set("name", "Alice");
        auto val = client.Get("name");
        if (val.type == Astra::Client::RespType::BulkString) {
            std::cout << "name = " << val.str << std::endl;
        }

        // 测试带 TTL 的 Set
        client.Set("temp_key", "value", std::chrono::seconds(10));
        auto ttl_val = client.TTL("temp_key");
        std::cout << "TTL of temp_key: " << ttl_val.integer << "s" << std::endl;

        // 等待过期
        std::this_thread::sleep_for(std::chrono::seconds(11));
        auto exists = client.Exists("temp_key");
        std::cout << "temp_key exists after expiration? " << (exists.integer ? "yes" : "no") << std::endl;

        // 测试 Del
        client.Del({"name"});
        exists = client.Exists("name");
        std::cout << "name exists after delete? " << (exists.integer ? "yes" : "no") << std::endl;

        // 测试 TTL 过期机制
        client.Set("temp_key", "value");
        auto new_ttl = client.TTL("temp_key");
        std::cout << "TTL of temp_key before expire: " << new_ttl.integer << "s" << std::endl;

        // 测试 Incr/Decr
        client.Set("counter", "5");

        auto incr_val = client.Incr("counter");
        std::cout << "counter after incr: " << incr_val.integer << std::endl;

        auto decr_val = client.Decr("counter");
        std::cout << "counter after decr: " << decr_val.integer << std::endl;

        // 测试 KEYS *
        client.Set("user:1000", "Alice");
        client.Set("user:1001", "Bob");
        client.Set("session:abc", "data");

        auto keys = client.Keys("*");
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
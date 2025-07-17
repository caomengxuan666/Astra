#include "sdk/astra_client.hpp"
#include <chrono>
#include <iostream>
#include <vector>

struct PerformanceMetrics {
    double qps;
    double throughput_mb;
    double avg_latency_ms;
};

void print_performance_metrics(const std::string &op, const PerformanceMetrics &metrics) {
    std::cout << "Performance Metrics for " << op << ":" << std::endl;
    std::cout << "  QPS: " << metrics.qps << " ops/sec" << std::endl;
    std::cout << "  Throughput: " << metrics.throughput_mb << " MB/s" << std::endl;
    std::cout << "  Average Latency: " << metrics.avg_latency_ms << " ms/op" << std::endl;
}

template<typename Func>
PerformanceMetrics benchmark_op(int count, Func op_func, size_t total_data_size = 0) {
    auto start = std::chrono::high_resolution_clock::now();
    op_func();// 执行操作
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    double seconds = elapsed.count();

    PerformanceMetrics metrics;
    metrics.qps = count / seconds;
    metrics.avg_latency_ms = (seconds * 1000) / count;
    metrics.throughput_mb = total_data_size > 0 ? (total_data_size / (1024.0 * 1024.0)) / seconds : 0;

    return metrics;
}

int main() {
    int test_count = 200000;
    std::vector<int> ports = {6379, 6380};

    for (int port: ports) {
        std::cout << "\n================ Testing on port " << port << " ==================" << std::endl;
        try {
            Astra::Client::AstraClient client("127.0.0.1", port);
            std::cout << "Connected to Redis on port " << port << std::endl;

            // 构造测试数据
            std::vector<std::pair<std::string, std::string>> kv_pairs;
            for (int i = 0; i < test_count; ++i) {
                kv_pairs.emplace_back("key_" + std::to_string(i), "value_" + std::to_string(i));
            }

            size_t data_size = 0;
            for (const auto &pair: kv_pairs) {
                data_size += pair.first.size() + pair.second.size();
            }

            // === SET 测试 ===
            std::cout << "\n===== Testing SET with " << test_count << " items =====" << std::endl;
            auto set_metrics = benchmark_op(test_count, [&]() {
                for (const auto& pair : kv_pairs) {
                    client.Set(pair.first, pair.second);
                } }, data_size);

            print_performance_metrics("SET", set_metrics);

            // === GET 测试 ===
            std::cout << "\n===== Testing GET with " << test_count << " items =====" << std::endl;
            auto get_metrics = benchmark_op(test_count, [&]() {
                for (const auto &pair: kv_pairs) {
                    client.Get(pair.first);
                }
            });

            print_performance_metrics("GET", get_metrics);

            // === MSET 测试 ===
            std::cout << "\n===== Testing MSET with " << test_count << " items =====" << std::endl;
            auto mset_metrics = benchmark_op(1, [&]() { client.MSet(kv_pairs); }, data_size);

            print_performance_metrics("MSET", mset_metrics);

            // === MGET 测试 ===
            std::cout << "\n===== Testing MGET with " << test_count << " items =====" << std::endl;
            std::vector<std::string> keys;
            for (const auto &pair: kv_pairs) {
                keys.push_back(pair.first);
            }

            auto mget_metrics = benchmark_op(1, [&]() {
                auto reply = client.MGet(keys);
                if (reply.array.size() != test_count) {
                    std::cerr << "MGET returned unexpected number of results." << std::endl;
                }
            });

            print_performance_metrics("MGET", mget_metrics);

            // 清理数据
            client.Del(keys);

            std::cout << "\n🧹 Cleaned up " << test_count << " test keys" << std::endl;
            std::cout << "Disconnected from Redis on port " << port << "." << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "Failed to connect or run tests on port " << port << ": " << e.what() << std::endl;
        }
    }

    std::cout << "\nAll tests completed." << std::endl;
    return 0;
}
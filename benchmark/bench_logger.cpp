#include "core/astra.hpp"
#include "logger.hpp"
#include <benchmark/benchmark.h>

class LoggerBenchmark : public benchmark::Fixture {
protected:
    void SetUp(const benchmark::State &state) override {
        // 初始化日志系统，仅使用控制台输出
        logger = &Astra::Logger::GetInstance();
        logger->RemoveAllAppenders();
        logger->AddAppender(std::make_shared<Astra::ConsoleAppender>());
        logger->SetLevel(Astra::LogLevel::INFO);
    }

    void TearDown(const benchmark::State &state) override {
        // 清理工作
        logger->Flush();
    }

    Astra::Logger *logger;
};

// 同步日志基准测试
BENCHMARK_DEFINE_F(LoggerBenchmark, SyncConsoleLog)
(benchmark::State &state) {
    const std::string message = "Benchmark test message - 0123456789";

    for (auto _: state) {
        logger->Info("{}: {}", state.iterations(), message);
    }

    state.SetItemsProcessed(state.iterations());
}

// 异步日志基准测试
BENCHMARK_DEFINE_F(LoggerBenchmark, AsyncConsoleLog)
(benchmark::State &state) {
    const std::string message = "Benchmark test message - 0123456789";

    // 切换到异步控制台输出
    logger->RemoveAllAppenders();
    logger->AddAppender(std::make_shared<Astra::AsyncConsoleAppender>());

    for (auto _: state) {
        logger->Info("{}: {}", state.iterations(), message);
    }

    state.SetItemsProcessed(state.iterations());
}

// 注册基准测试
BENCHMARK_REGISTER_F(LoggerBenchmark, SyncConsoleLog)
        ->Threads(1)
        ->Threads(4)
        ->Threads(8)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(LoggerBenchmark, AsyncConsoleLog)
        ->Threads(1)
        ->Threads(4)
        ->Threads(8)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

// 主函数
BENCHMARK_MAIN();
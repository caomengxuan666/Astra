#include "core/astra.hpp"
#include "utils/logger.hpp"
#include "persistence/process.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// 基础测试类，统一处理资源释放（供异步和同步测试复用）
class BaseLoggerTest : public testing::Test {
protected:
    // 生成唯一测试目录
    std::string createTestDir(const std::string &type) {
        std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        return type + "_test_logs_" + testName + "_" + Astra::persistence::get_pid_str();
    }

    // 安全删除目录（处理Windows文件占用问题）
    void safeRemoveDir(const std::string &dir) {
        for (int i = 0; i < 20; ++i) {
            try {
                fs::remove_all(dir);
                break;
            } catch (...) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }
};

// 异步日志测试类
class AsyncLoggerTest : public BaseLoggerTest {
protected:
    std::string testLogDir;                           // 测试日志目录
    std::shared_ptr<Astra::FileAppender> fileAppender;// 异步文件输出器

    void SetUp() override {
        testLogDir = createTestDir("async");
        fs::remove_all(testLogDir);// 清理历史残留
    }

    void TearDown() override {
        // 移除日志输出器
        Astra::Logger::GetInstance().RemoveAllAppenders();
        // 主动关闭文件句柄
        if (fileAppender) {
            fileAppender->closeFile();
        }
        safeRemoveDir(testLogDir);
    }

    // 等待异步日志写入完成
    void waitForLogWrite() {
        std::this_thread::sleep_for(3s);// 等待异步线程处理
    }
};

// 同步日志测试类
class SyncLoggerTest : public BaseLoggerTest {
protected:
    std::string testLogDir;                                   // 测试日志目录
    std::shared_ptr<Astra::SyncFileAppender> syncFileAppender;// 同步文件输出器

    void SetUp() override {
        testLogDir = createTestDir("sync");
        fs::remove_all(testLogDir);// 清理历史残留
    }

    void TearDown() override {
        // 移除日志输出器
        Astra::Logger::GetInstance().RemoveAllAppenders();
        // 主动刷新并关闭文件
        if (syncFileAppender) {
            syncFileAppender->Flush();
        }
        safeRemoveDir(testLogDir);
    }

    // 同步日志刷新（无需等待，直接强制刷新）
    void syncLogWrite() {
        Astra::Logger::GetInstance().Flush();
    }
};

// -------------------------- 异步日志测试用例 --------------------------
/*
TEST_F(AsyncLoggerTest, FileCreationAndContent) {
    // 创建异步文件输出器
    fileAppender = std::make_shared<Astra::FileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(fileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::INFO);

    // 写入测试日志
    std::string testMsg = "Async file creation test message";
    ZEN_LOG_INFO("{}", testMsg);
    waitForLogWrite();// 等待异步写入

    // 验证文件存在
    std::string logFileName = fileAppender->GetCurrentLogFileName();
    EXPECT_TRUE(fs::exists(logFileName)) << "异步日志文件未创建: " << logFileName;

    // 验证日志内容
    std::ifstream logFile(logFileName);
    std::string logContent((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(logContent.find(testMsg), std::string::npos) << "异步日志内容不匹配";
    EXPECT_NE(logContent.find("[INFO ]"), std::string::npos) << "异步日志级别标识错误";
}
*/

TEST_F(AsyncLoggerTest, ConcurrentWrites) {
    fileAppender = std::make_shared<Astra::FileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(fileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::TRACE);

    // 多线程并发写入（5个线程，每个写200条）
    const int threadCount = 5;
    const int logsPerThread = 200;
    std::vector<std::thread> threads;

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([t, logsPerThread]() {
            for (int i = 0; i < logsPerThread; ++i) {
                ZEN_LOG_TRACE("Async thread {} log {}", t, i);
            }
        });
    }

    // 等待所有线程完成
    for (auto &th: threads) {
        th.join();
    }
    waitForLogWrite();// 等待异步写入

    // 验证总日志条数（允许±10的误差，因批量写入可能合并）
    std::string logFile = fileAppender->GetCurrentLogFileName();
    std::ifstream file(logFile);
    std::string line;
    int totalLines = 0;
    while (std::getline(file, line)) {
        totalLines++;
    }

    int expectedMin = threadCount * logsPerThread - 10;
    int expectedMax = threadCount * logsPerThread + 10;
    EXPECT_GE(totalLines, expectedMin) << "异步日志丢失过多，实际: " << totalLines;
    EXPECT_LE(totalLines, expectedMax) << "异步日志数量异常，实际: " << totalLines;
}

TEST_F(AsyncLoggerTest, LevelFiltering) {
    fileAppender = std::make_shared<Astra::FileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(fileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::WARN);// 只输出WARN及以上

    // 写入不同级别的日志
    ZEN_LOG_TRACE("Async TRACE should be filtered");
    ZEN_LOG_DEBUG("Async DEBUG should be filtered");
    ZEN_LOG_INFO("Async INFO should be filtered");
    ZEN_LOG_WARN("Async WARN should be kept");
    ZEN_LOG_ERROR("Async ERROR should be kept");
    ZEN_LOG_FATAL("Async FATAL should be kept");
    waitForLogWrite();

    // 验证日志内容
    std::string logFile = fileAppender->GetCurrentLogFileName();
    std::ifstream file(logFile);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("TRACE"), std::string::npos) << "异步TRACE日志未被过滤";
    EXPECT_EQ(content.find("DEBUG"), std::string::npos) << "异步DEBUG日志未被过滤";
    EXPECT_EQ(content.find("INFO"), std::string::npos) << "异步INFO日志未被过滤";
    EXPECT_NE(content.find("WARN"), std::string::npos) << "异步WARN日志丢失";
    EXPECT_NE(content.find("ERROR"), std::string::npos) << "异步ERROR日志丢失";
    EXPECT_NE(content.find("FATAL"), std::string::npos) << "异步FATAL日志丢失";
}

// -------------------------- 同步日志测试用例 --------------------------
TEST_F(SyncLoggerTest, FileCreationAndContent) {
    // 创建同步文件输出器
    syncFileAppender = std::make_shared<Astra::SyncFileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(syncFileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::INFO);

    // 写入测试日志（同步写入，立即生效）
    std::string testMsg = "Sync file creation test message";
    ZEN_LOG_INFO("{}", testMsg);
    syncLogWrite();// 强制刷新

    // 验证文件存在
    std::string logFileName = syncFileAppender->GetCurrentLogFileName();
    EXPECT_TRUE(fs::exists(logFileName)) << "同步日志文件未创建: " << logFileName;

    // 验证日志内容
    std::ifstream logFile(logFileName);
    std::string logContent((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(logContent.find(testMsg), std::string::npos) << "同步日志内容不匹配";
    EXPECT_NE(logContent.find("[INFO ]"), std::string::npos) << "同步日志级别标识错误";
}

TEST_F(SyncLoggerTest, ConcurrentWrites) {
    syncFileAppender = std::make_shared<Astra::SyncFileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(syncFileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::TRACE);

    // 多线程并发写入（5个线程，每个写200条）
    const int threadCount = 5;
    const int logsPerThread = 200;
    std::vector<std::thread> threads;

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([t, logsPerThread]() {
            for (int i = 0; i < logsPerThread; ++i) {
                ZEN_LOG_TRACE("Sync thread {} log {}", t, i);
            }
        });
    }

    // 等待所有线程完成
    for (auto &th: threads) {
        th.join();
    }
    syncLogWrite();// 强制刷新

    // 验证总日志条数（同步写入应无丢失）
    std::string logFile = syncFileAppender->GetCurrentLogFileName();
    std::ifstream file(logFile);
    std::string line;
    int totalLines = 0;
    while (std::getline(file, line)) {
        totalLines++;
    }

    EXPECT_EQ(totalLines, threadCount * logsPerThread)
            << "同步日志并发写入丢失，实际: " << totalLines
            << "，预期: " << threadCount * logsPerThread;
}

TEST_F(SyncLoggerTest, LevelFiltering) {
    syncFileAppender = std::make_shared<Astra::SyncFileAppender>(testLogDir);
    Astra::Logger::GetInstance().AddAppender(syncFileAppender);
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::WARN);// 只输出WARN及以上

    // 写入不同级别的日志（同步写入）
    ZEN_LOG_TRACE("Sync TRACE should be filtered");
    ZEN_LOG_DEBUG("Sync DEBUG should be filtered");
    ZEN_LOG_INFO("Sync INFO should be filtered");
    ZEN_LOG_WARN("Sync WARN should be kept");
    ZEN_LOG_ERROR("Sync ERROR should be kept");
    ZEN_LOG_FATAL("Sync FATAL should be kept");
    syncLogWrite();// 强制刷新

    // 验证日志内容
    std::string logFile = syncFileAppender->GetCurrentLogFileName();
    std::ifstream file(logFile);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("TRACE"), std::string::npos) << "同步TRACE日志未被过滤";
    EXPECT_EQ(content.find("DEBUG"), std::string::npos) << "同步DEBUG日志未被过滤";
    EXPECT_EQ(content.find("INFO"), std::string::npos) << "同步INFO日志未被过滤";
    EXPECT_NE(content.find("WARN"), std::string::npos) << "同步WARN日志丢失";
    EXPECT_NE(content.find("ERROR"), std::string::npos) << "同步ERROR日志丢失";
    EXPECT_NE(content.find("FATAL"), std::string::npos) << "同步FATAL日志丢失";
}

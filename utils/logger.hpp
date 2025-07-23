#pragma once

#include <condition_variable>
#include <core/noncopyable.hpp>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/color.h>
#include <fmt/format.h>

#include <lockfree_queue.hpp>
#include <memory>

#ifndef _WIN32
#undef ERROR
#endif

namespace Astra {
    // 日志级别枚举
    enum class LogLevel {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERR,
        FATAL
    };

    // 日志配置参数
    struct LogConfig {
        size_t max_file_size = 10 * 1024 * 1024;// 默认单个日志文件最大10MB
        size_t max_backup_files = 5;            // 默认保留5个备份文件
        size_t flush_interval = 3;              // 默认3秒强制刷新一次
        size_t queue_threshold = 100;           // 队列达到100条日志时触发写入
    };

    // 日志条目结构
    struct LogEntry {
        LogLevel level;
        std::string message;
        std::string timestamp;
    };

    // 日志输出目标接口（策略模式）
    class LogAppender {
    public:
        virtual ~LogAppender() = default;
        virtual void Append(LogLevel level, const std::string &message) = 0;
    };

    // 控制台输出器
    class ConsoleAppender : public LogAppender {
    public:
        void Append(LogLevel level, const std::string &message) override;
    };

    // 同步文件输出器
    class SyncFileAppender : public LogAppender {
    public:
        SyncFileAppender(const std::string &base_dir, const LogConfig &config = LogConfig());
        ~SyncFileAppender() override;

        void Append(LogLevel level, const std::string &message) override;
        void Flush();
        void SetConfig(const LogConfig &config);
        const LogConfig &GetConfig() const;
        std::string GetCurrentLogFileName() const;

    private:
        void rollLogFileIfNeeded();
        std::string generateLogFileName(int index = 0) const;
        void openCurrentFile();
        void closeCurrentFile();

        std::string base_dir_;
        LogConfig config_;
        mutable std::mutex file_name_mutex_;
        std::ofstream file_;
        std::string current_file_name_;
        size_t current_file_size_ = 0;
        int current_file_index_ = 0;
        std::string pid_str_;
        std::string start_time_str_;
    };

    // 异步文件输出器
    class FileAppender : public LogAppender {
    public:
        explicit FileAppender(const std::string &base_dir, const LogConfig &config = LogConfig());
        ~FileAppender() override;

        void Append(LogLevel level, const std::string &message) override;
        void SetConfig(const LogConfig &config);
        const LogConfig &GetConfig() const;
        std::string GetCurrentLogFileName() const;
        void closeFile();// 新增：主动关闭文件句柄
        mutable std::mutex file_name_mutex_;

    private:
        // 生成日志文件名（带索引参数）
        std::string generateLogFileName(int index = 0) const;
        // 工作线程函数
        void workerThread();
        // 检查并滚动日志文件
        void rollLogFileIfNeeded();
        // 写入日志到文件
        void writeToFile(const std::vector<LogEntry> &entries);

        std::string base_dir_;         // 日志目录
        std::string current_file_name_;// 当前日志文件名
        std::string start_time_str_;   // 启动时间字符串（用于文件名）
        std::string pid_str_;          // 进程ID字符串（用于文件名）
        size_t current_file_size_ = 0; // 当前文件大小
        int current_file_index_ = 0;   // 当前文件索引
        LogConfig config_;             // 日志配置

        // 线程安全队列相关
        std::condition_variable queue_cv_;// 队列条件变量
        std::mutex dummy_mutex_;          // 新增：用于条件变量的哑元锁
        mutable std::mutex config_mutex_;
        Astra::datastructures::LockFreeQueue<LogEntry> log_queue_;// 日志队列
        std::thread worker_thread_;                               // 工作线程
        bool running_ = true;                                     // 线程运行标志

        // 文件流
        std::ofstream file_;   // 文件流（类成员变量）
        std::mutex file_mutex_;// 文件操作互斥锁
    };

    // 异步控制台输出器
    class AsyncConsoleAppender : public LogAppender {
    public:
        explicit AsyncConsoleAppender();
        ~AsyncConsoleAppender() override;

        void Append(LogLevel level, const std::string &message) override;
        void Flush();


    private:
        // 工作线程函数
        void workerThread();
        // 日志条目结构，包含日志级别和消息

        constexpr const fmt::text_style GetStyle(LogLevel level);
        struct LogEntry {
            LogLevel level;
            std::string message;
            std::string timestamp;// 添加timestamp成员
        };

        // 线程安全队列相关
        std::condition_variable queue_cv_;// 队列条件变量
        std::mutex dummy_mutex_;          // 新增：用于条件变量的哑元锁
        std::mutex config_mutex_;
        Astra::datastructures::LockFreeQueue<LogEntry, 4096> log_queue_;// 日志队列
        std::thread worker_thread_;                                     // 工作线程
        bool running_ = true;                                           // 线程运行标志
        size_t flush_interval_ = 3;                                     // 默认3秒强制刷新一次
    };

    // 日志核心类（单例模式）
    class Logger : private NonCopyable {
    public:
        static Logger &GetInstance();
        void SetLevel(LogLevel level);
        LogLevel GetLevel() const;
        void AddAppender(std::shared_ptr<LogAppender> appender);
        void RemoveAllAppenders();
        void SetDefaultLogDir(const std::string &dir);
        std::string GetDefaultLogDir() const;
        void Log(LogLevel level, const std::string &message);
        static std::string LevelToString(LogLevel level);
        void Flush();

        // 新增：更新时间戳缓存
        void startTimestampUpdater();
        // 新增：获取当前时间戳（从缓存）
        static std::string GetTimestamp();

        template<typename... Args>
        void Trace(const std::string &fmt, Args &&...args) {
            // 使用 fmt::runtime 明确指示 fmt 是运行时格式字符串
            Log(LogLevel::TRACE, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Debug(const std::string &fmt, Args &&...args) {
            Log(LogLevel::DEBUG, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Info(const std::string &fmt, Args &&...args) {
            Log(LogLevel::INFO, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Warn(const std::string &fmt, Args &&...args) {
            Log(LogLevel::WARN, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Error(const std::string &fmt, Args &&...args) {
            Log(LogLevel::ERR, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Fatal(const std::string &fmt, Args &&...args) {
            Log(LogLevel::FATAL, fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...));
        }

    private:
        Logger();
        ~Logger();
        static std::string generateTimestamp();

        LogLevel level_;                                     // 当前日志级别
        std::vector<std::shared_ptr<LogAppender>> appenders_;// 日志输出器列表
        mutable std::mutex mutex_;                           // 互斥锁（mutable允许const函数使用）
        std::string default_log_dir_;                        // 默认日志目录

        // 新增：时间戳缓存相关
        static std::string cached_timestamp_;
        static std::mutex timestamp_mutex_;
        std::thread timestamp_update_thread_;
        bool running_ = true;
    };


}// namespace Astra

// 宏定义简化调用
#define ZEN_LOG_TRACE(...) Astra::Logger::GetInstance().Trace(__VA_ARGS__)
#define ZEN_LOG_DEBUG(...) Astra::Logger::GetInstance().Debug(__VA_ARGS__)
#define ZEN_LOG_INFO(...) Astra::Logger::GetInstance().Info(__VA_ARGS__)
#define ZEN_LOG_WARN(...) Astra::Logger::GetInstance().Warn(__VA_ARGS__)
#define ZEN_LOG_ERROR(...) Astra::Logger::GetInstance().Error(__VA_ARGS__)
#define ZEN_LOG_FATAL(...) Astra::Logger::GetInstance().Fatal(__VA_ARGS__)
#define ZEN_SET_LEVEL(level) Astra::Logger::GetInstance().SetLevel(level)

inline static Astra::LogLevel parseLogLevel(const std::string &levelStr) {
    static const std::unordered_map<std::string, Astra::LogLevel> levels = {
            {"trace", Astra::LogLevel::TRACE},
            {"debug", Astra::LogLevel::DEBUG},
            {"info", Astra::LogLevel::INFO},
            {"warn", Astra::LogLevel::WARN},
            {"error", Astra::LogLevel::ERR},
            {"fatal", Astra::LogLevel::FATAL}};

    auto it = levels.find(levelStr);
    if (it != levels.end()) {
        return it->second;
    }
    throw std::invalid_argument("Invalid log level: " + levelStr);
}
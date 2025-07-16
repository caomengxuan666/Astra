#pragma once
#include <chrono>
#include <core/noncopyable.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <fstream>
#define FMT_HEADER_ONLY
#include <fmt/color.h>
#include <fmt/format.h>
#include <iomanip>
#include <memory>
#include <sstream>
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
        size_t max_file_size = 10 * 1024 * 1024;  // 默认单个日志文件最大10MB
        size_t max_backup_files = 5;              // 默认保留5个备份文件
        size_t flush_interval = 3;                // 默认3秒强制刷新一次
        size_t queue_threshold = 100;             // 队列达到100条日志时触发写入
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
        virtual void Append(LogLevel level, const std::string& message) = 0;
    };

    // 控制台输出器
    class ConsoleAppender : public LogAppender {
    public:
        void Append(LogLevel level, const std::string& message) override;
    };

    // 同步文件输出器
    class SyncFileAppender : public LogAppender {
    public:
        SyncFileAppender(const std::string& base_dir, const LogConfig& config = LogConfig());
        ~SyncFileAppender() override;

        void Append(LogLevel level, const std::string& message) override;
        void Flush() ;
        void SetConfig(const LogConfig& config) ;
        const LogConfig& GetConfig() const ;
        std::string GetCurrentLogFileName() const;

    private:
        void rollLogFileIfNeeded();
        std::string generateLogFileName(int index = 0) const;
        void openCurrentFile();
        void closeCurrentFile();

        std::string base_dir_;
        LogConfig config_;
        mutable std::mutex mutex_;
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
        explicit FileAppender(const std::string& base_dir, const LogConfig& config = LogConfig());
        ~FileAppender() override;

        void Append(LogLevel level, const std::string& message) override;
        void SetConfig(const LogConfig& config);
        const LogConfig& GetConfig() const;
        std::string GetCurrentLogFileName() const;
        void closeFile();  // 新增：主动关闭文件句柄

    private:
        // 生成日志文件名（带索引参数）
        std::string generateLogFileName(int index = 0) const;
        // 工作线程函数
        void workerThread();
        // 检查并滚动日志文件
        void rollLogFileIfNeeded();
        // 写入日志到文件
        void writeToFile(const std::vector<LogEntry>& entries);

        std::string base_dir_;              // 日志目录
        std::string current_file_name_;     // 当前日志文件名
        std::string start_time_str_;        // 启动时间字符串（用于文件名）
        std::string pid_str_;               // 进程ID字符串（用于文件名）
        size_t current_file_size_ = 0;      // 当前文件大小
        int current_file_index_ = 0;        // 当前文件索引
        LogConfig config_;                  // 日志配置

        // 线程安全队列相关
        mutable std::mutex queue_mutex_;    // 队列互斥锁（mutable允许const函数使用）
        std::condition_variable queue_cv_;  // 队列条件变量
        std::queue<LogEntry> log_queue_;    // 日志队列
        std::thread worker_thread_;         // 工作线程
        bool running_ = true;               // 线程运行标志

        // 文件流
        std::ofstream file_;                // 文件流（类成员变量）
        std::mutex file_mutex_;             // 文件操作互斥锁
    };

    // 日志核心类（单例模式）
    class Logger : private NonCopyable {
    public:
        static Logger& GetInstance();
        void SetLevel(LogLevel level);
        LogLevel GetLevel() const;
        void AddAppender(std::shared_ptr<LogAppender> appender);
        void RemoveAllAppenders();
        void SetDefaultLogDir(const std::string& dir);
        std::string GetDefaultLogDir() const;
        void Flush();

        // 写日志接口
        void Log(LogLevel level, const std::string& message);

        // 日志输出模板函数
        template<typename... Args>
        void Trace(const std::string& fmt, Args&&... args) {
            Log(LogLevel::TRACE, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Debug(const std::string& fmt, Args&&... args) {
            Log(LogLevel::DEBUG, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Info(const std::string& fmt, Args&&... args) {
            Log(LogLevel::INFO, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Warn(const std::string& fmt, Args&&... args) {
            Log(LogLevel::WARN, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Error(const std::string& fmt, Args&&... args) {
            Log(LogLevel::ERR, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Fatal(const std::string& fmt, Args&&... args) {
            Log(LogLevel::FATAL, fmt::format(fmt, std::forward<Args>(args)...));
        }

        std::string CurrentTimestamp() const;
        static const std::string LevelToString(LogLevel level);

    private:
        Logger();
        LogLevel level_;                             // 当前日志级别
        std::vector<std::shared_ptr<LogAppender>> appenders_;  // 日志输出器列表
        mutable std::mutex mutex_;                   // 互斥锁（mutable允许const函数使用）
        std::string default_log_dir_;                // 默认日志目录
    };

    // 获取当前时间戳（格式：YYYY-MM-DD HH:MM:SS.EEE)
    inline std::string Logger::CurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        std::tm tm;
        localtime_s(&tm, &in_time_t);
        
        // 修改时间格式为YYYYMMDD_HHMMSS格式
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return ss.str();
    }

}

// 宏定义简化调用
#define ZEN_LOG_TRACE(...) Astra::Logger::GetInstance().Trace(__VA_ARGS__)
#define ZEN_LOG_DEBUG(...) Astra::Logger::GetInstance().Debug(__VA_ARGS__)
#define ZEN_LOG_INFO(...) Astra::Logger::GetInstance().Info(__VA_ARGS__)
#define ZEN_LOG_WARN(...) Astra::Logger::GetInstance().Warn(__VA_ARGS__)
#define ZEN_LOG_ERROR(...) Astra::Logger::GetInstance().Error(__VA_ARGS__)
#define ZEN_LOG_FATAL(...) Astra::Logger::GetInstance().Fatal(__VA_ARGS__)
#define ZEN_SET_LEVEL(level) Astra::Logger::GetInstance().SetLevel(level)
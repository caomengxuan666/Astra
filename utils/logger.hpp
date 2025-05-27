#pragma once
#include <chrono>
#include <core/noncopyable.hpp>
#define FMT_HEADER_ONLY
#include <fmt/color.h>
#include <fmt/core.h>
#include <iomanip>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Astra {

    // 日志级别枚举
    enum class LogLevel {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
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

    // 文件输出器（暂不实现，预留接口）
    class FileAppender : public LogAppender {
    public:
        explicit FileAppender(const std::string &filename);
        void Append(LogLevel level, const std::string &message) override;

    private:
        std::string filename_;
    };

    // 日志核心类（单例模式）
    class Logger : private NonCopyable {
    public:
        static Logger &GetInstance();

        void SetLevel(LogLevel level);
        void AddAppender(std::shared_ptr<LogAppender> appender);

        // 写日志接口
        void Log(LogLevel level, const std::string &message);

        // 日志输出模板函数
        template<typename... Args>
        void Trace(const std::string &fmt, Args &&...args) {
            Log(LogLevel::TRACE, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Debug(const std::string &fmt, Args &&...args) {
            Log(LogLevel::DEBUG, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Info(const std::string &fmt, Args &&...args) {
            Log(LogLevel::INFO, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Warn(const std::string &fmt, Args &&...args) {
            Log(LogLevel::WARN, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Error(const std::string &fmt, Args &&...args) {
            Log(LogLevel::ERROR, fmt::format(fmt, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void Fatal(const std::string &fmt, Args &&...args) {
            Log(LogLevel::FATAL, fmt::format(fmt, std::forward<Args>(args)...));
        }

        std::string CurrentTimestamp() const;

    private:
        Logger();
        ~Logger() = default;


    private:
        LogLevel level_;
        std::vector<std::shared_ptr<LogAppender>> appenders_;
        std::mutex mutex_;
    };

    // 获取当前时间戳（格式：YYYY-MM-DD HH:MM:SS.EEE)
    inline std::string Logger::CurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 100;// 改为两位毫秒
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S.") << std::setfill('0') << std::setw(2) << ms.count();// 调整宽度为2
        return ss.str();
    }

// 宏定义简化调用
#define ZEN_LOG_TRACE(...) Astra::Logger::GetInstance().Trace(__VA_ARGS__)
#define ZEN_LOG_DEBUG(...) Astra::Logger::GetInstance().Debug(__VA_ARGS__)
#define ZEN_LOG_INFO(...) Astra::Logger::GetInstance().Info(__VA_ARGS__)
#define ZEN_LOG_WARN(...) Astra::Logger::GetInstance().Warn(__VA_ARGS__)
#define ZEN_LOG_ERROR(...) Astra::Logger::GetInstance().Error(__VA_ARGS__)
#define ZEN_LOG_FATAL(...) Astra::Logger::GetInstance().Fatal(__VA_ARGS__)
#define ZEN_SET_LEVEL(level) Astra::Logger::GetInstance().SetLevel(level)// 添加设置日志级别宏


}// namespace Astra
#include "logger.hpp"
#include "core/astra.hpp"
#include <chrono>
#include <fmt/color.h>
#include <fmt/core.h>
#include <iomanip>

namespace Astra {

    // ConsoleAppender 实现
    void ConsoleAppender::Append(LogLevel level, const std::string &message) {
        fmt::text_style style;

        switch (level) {
            case LogLevel::TRACE:
                style = fmt::fg(fmt::color::gray);
                break;
            case LogLevel::DEBUG:
                style = fmt::fg(fmt::color::blue);
                break;
            case LogLevel::INFO:
                style = fmt::fg(fmt::color::green);
                break;
            case LogLevel::WARN:
                style = fmt::fg(fmt::color::orange);
                break;
            case LogLevel::ERROR:
                style = fmt::fg(fmt::color::red) | fmt::emphasis::bold;
                break;
            case LogLevel::FATAL:
                style = fmt::fg(fmt::color::white) | fmt::bg(fmt::color::red) | fmt::emphasis::bold;
                break;
            default:
                style = fmt::fg(fmt::color::black);
                break;
        }

        fmt::print(style, "{}\n", message);// 带颜色输出
        fmt::print(fmt::text_style{}, ""); // 重置文本样式
    }

    // FileAppender 实现（暂留空，后续可完善）
    FileAppender::FileAppender(const std::string &filename) : filename_(filename) {}

    void FileAppender::Append(LogLevel level, const std::string &message) {
        // TODO: 后续实现文件写入逻辑
    }

    // Logger 实现
    Logger::Logger() : level_(LogLevel::INFO) {
        // 默认添加控制台输出
        AddAppender(std::make_shared<ConsoleAppender>());
    }

    Logger &Logger::GetInstance() {
        static Logger instance;
        return instance;
    }

    void Logger::SetLevel(LogLevel level) {
        level_ = level;
    }

    void Logger::AddAppender(std::shared_ptr<LogAppender> appender) {
        std::lock_guard<std::mutex> lock(mutex_);
        appenders_.push_back(std::move(appender));
    }

    void Logger::Log(LogLevel level, const std::string &message) {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &appender: appenders_) {
            appender->Append(level, message);
        }
    }


    // 获取日志级别名称
    std::string LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE:
                return "TRACE";
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO ";
            case LogLevel::WARN:
                return "WARN ";
            case LogLevel::ERROR:
                return "ERROR";
            case LogLevel::FATAL:
                return "FATAL";
        }
        return "UNKNOWN";
    }


}// namespace Astra
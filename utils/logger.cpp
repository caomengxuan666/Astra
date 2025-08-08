#include "logger.hpp"
#include "core/astra.hpp"
#include "utf8_encode.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <iostream>
#include <sstream>
#include <sys/types.h>

namespace Astra {
    namespace fs = std::filesystem;

    // 控制台输出器实现
    void ConsoleAppender::Append(LogLevel level, const std::string &message) {
        fmt::text_style style;
        switch (level) {
            case LogLevel::TRACE:
                style = fmt::fg(fmt::color::gray);
                break;
            case LogLevel::DEBUG:
                style = fmt::fg(fmt::color::cyan);
                break;
            case LogLevel::INFO:
                style = fmt::fg(fmt::color::lime_green);
                break;
            case LogLevel::WARN:
                style = fmt::fg(fmt::color::orange);
                break;
            case LogLevel::ERR:
                style = fmt::fg(fmt::color::red) | fmt::emphasis::bold;
                break;
            case LogLevel::FATAL:
                style = fmt::fg(fmt::color::crimson) | fmt::emphasis::bold;
                break;
            default:
                style = fmt::fg(fmt::color::white);
                break;
        }
        fmt::print(style, "[{}] [{}] {}\n",
                   Logger::GetTimestamp(),
                   Logger::LevelToString(level),
                   message);
        fmt::print("");// 重置文本样式
    }

    // 文件输出器实现
    FileAppender::FileAppender(const std::string &base_dir, const LogConfig &config)
        : base_dir_(base_dir), config_(config) {
        pid_str_ = persistence::get_pid_str();

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;

        #ifdef _WIN32
        localtime_s(&tm, &in_time_t);
        #elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&in_time_t, &tm);
        #endif

        std::stringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        start_time_str_ = ss.str();

        if (!fs::exists(base_dir_)) {
            fs::create_directories(base_dir_);
        }

        current_file_name_ = generateLogFileName();
        file_.open(current_file_name_, std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            ZEN_LOG_ERROR("Failed to open log file: {}", current_file_name_);
        }

        worker_thread_ = std::thread(&FileAppender::workerThread, this);
    }

    FileAppender::~FileAppender() {
        running_ = false;
        queue_cv_.notify_one();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        closeFile();
    }

    void FileAppender::closeFile() {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void FileAppender::SetConfig(const LogConfig &config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
    }

    const LogConfig &FileAppender::GetConfig() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_;
    }

    std::string FileAppender::GetCurrentLogFileName() const {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        return current_file_name_;
    }

    void FileAppender::Append(LogLevel level, const std::string &message) {
        LogEntry entry;
        entry.level = level;
        entry.message = message;

        log_queue_.push(entry);

        size_t threshold = 0;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            threshold = config_.queue_threshold;
        }
        if (log_queue_.size() >= threshold || level == LogLevel::FATAL) {
            std::lock_guard<std::mutex> lock(dummy_mutex_);
            queue_cv_.notify_one();
        }
    }

    void FileAppender::rollLogFileIfNeeded() {
        if (current_file_size_ >= config_.max_file_size) {
            // 关闭当前文件
            {
                std::lock_guard<std::mutex> lock(file_mutex_);
                if (file_.is_open()) {
                    file_.flush();
                    file_.close();
                }
            }

            // 生成新文件名
            current_file_index_++;
            std::string old_name = current_file_name_;
            std::string new_name = generateLogFileName(current_file_index_);

            // 如果达到最大备份数，清理旧文件
            if (current_file_index_ > static_cast<int>(config_.max_backup_files)) {
                std::string oldest = generateLogFileName(1);
                if (fs::exists(oldest)) {
                    fs::remove(oldest);
                }
                current_file_index_ = config_.max_backup_files;
                new_name = generateLogFileName(current_file_index_);
            }

            // 从后往前重命名旧日志文件
            for (int i = static_cast<int>(config_.max_backup_files); i > 1; --i) {
                std::string src = generateLogFileName(i - 1);
                std::string dst = generateLogFileName(i);
                if (fs::exists(src)) {
                    if (fs::exists(dst)) {
                        fs::remove(dst);// 删除目标文件（如果存在）
                    }
                    fs::rename(src, dst);// 移动文件
                }
            }

            // 重命名当前日志文件为 1.log
            {
                std::lock_guard<std::mutex> lock(file_name_mutex_);
                std::string dst = generateLogFileName(1);
                if (fs::exists(dst)) {
                    fs::remove(dst);// 删除已存在的备份
                }
                if (fs::exists(old_name)) {
                    try {
                        fs::rename(old_name, dst);
                    } catch (const fs::filesystem_error &e) {
                        ZEN_LOG_ERROR("Failed to rename log file: {}", e.what());
                    }
                }
            }

            // 重新打开日志文件
            current_file_name_ = generateLogFileName();
            file_.open(current_file_name_, std::ios::app | std::ios::binary);
            if (!file_.is_open()) {
                ZEN_LOG_ERROR("Failed to reopen log file: {}", current_file_name_);
            } else {
                current_file_size_ = 0;
            }
        }
    }

    std::string FileAppender::generateLogFileName(int index) const {
        std::stringstream ss;
        ss << base_dir_ << "/astra_cache_" << start_time_str_ << "_" << pid_str_;
        if (index > 0) {
            ss << "." << index;
        }
        ss << ".log";
        return ss.str();
    }

    void FileAppender::writeToFile(const std::vector<LogEntry> &entries) {
        rollLogFileIfNeeded();

        std::lock_guard<std::mutex> lock(file_mutex_);
        for (const auto &entry: entries) {
            std::string log_line = fmt::format("[{}] [{}] {}\n",
                                               entry.timestamp,
                                               Logger::LevelToString(entry.level),
                                               entry.message);

            file_ << log_line;
            current_file_size_ += log_line.size();
        }
        file_.flush();
    }

    void FileAppender::workerThread() {
        while (running_) {
            std::vector<LogEntry> entries;
            LogEntry entry;

            LogConfig current_config;
            {
                std::lock_guard<std::mutex> lock(config_mutex_);
                current_config = config_;
            }

            std::unique_lock<std::mutex> lock(dummy_mutex_);
            auto timeout = std::chrono::seconds(current_config.flush_interval);
            queue_cv_.wait_for(lock, timeout, [this]() {
                return !log_queue_.empty() || !running_;
            });

            if (!running_ && log_queue_.empty()) {
                break;
            }

            lock.unlock();
            while (log_queue_.pop(entry)) {
                entry.timestamp = Logger::GetTimestamp();
                entries.push_back(entry);
            }

            if (!entries.empty()) {
                writeToFile(entries);
            }
        }
    }

    // 同步文件输出器实现
    SyncFileAppender::SyncFileAppender(const std::string &base_dir, const LogConfig &config)
        : base_dir_(base_dir), config_(config) {
        pid_str_ = persistence::get_pid_str();

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        #ifdef _WIN32
        localtime_s(&tm, &in_time_t);
        #elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&in_time_t, &tm);
        #endif
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        start_time_str_ = ss.str();

        if (!fs::exists(base_dir_)) {
            fs::create_directories(base_dir_);
        }

        current_file_name_ = generateLogFileName();
        openCurrentFile();
    }

    SyncFileAppender::~SyncFileAppender() {
        closeCurrentFile();
    }

    std::string SyncFileAppender::GetCurrentLogFileName() const {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        return current_file_name_;
    }

    void SyncFileAppender::openCurrentFile() {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        file_.open(current_file_name_, std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            std::string msg = fmt::format("Failed to open log file: {}", current_file_name_);
            printf(msg.c_str());
        } else {
            file_.seekp(0, std::ios::end);
            current_file_size_ = static_cast<size_t>(file_.tellp());
        }
    }

    void SyncFileAppender::closeCurrentFile() {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void SyncFileAppender::Append(LogLevel level, const std::string &message) {
        std::lock_guard<std::mutex> lock(file_name_mutex_);

        rollLogFileIfNeeded();

        if (!file_.is_open()) {
            openCurrentFile();
            if (!file_.is_open()) {
                return;
            }
        }

        std::string log_line = fmt::format("[{}] {}\n",
                                           Logger::LevelToString(level),
                                           message);

        file_ << log_line;
        file_.flush();
        current_file_size_ += log_line.size();
    }

    void SyncFileAppender::Flush() {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void SyncFileAppender::SetConfig(const LogConfig &config) {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        config_ = config;
    }

    const LogConfig &SyncFileAppender::GetConfig() const {
        std::lock_guard<std::mutex> lock(file_name_mutex_);
        return config_;
    }

    void SyncFileAppender::rollLogFileIfNeeded() {
        if (current_file_size_ >= config_.max_file_size && config_.max_file_size > 0) {
            file_.flush();
            file_.close();

            current_file_index_++;
            if (current_file_index_ > static_cast<int>(config_.max_backup_files)) {
                std::string old_file = generateLogFileName(1);
                if (fs::exists(old_file)) {
                    fs::remove(old_file);
                }
                current_file_index_ = config_.max_backup_files;
            }

            for (int i = static_cast<int>(config_.max_backup_files); i > 1; --i) {
                std::string src = generateLogFileName(i);
                if (fs::exists(src)) {
                    std::string dest = generateLogFileName(i - 1);
                    fs::rename(src, dest);
                }
            }

            std::string old_name = current_file_name_;
            current_file_name_ = generateLogFileName();

            if (fs::exists(old_name)) {
                fs::rename(old_name, generateLogFileName(current_file_index_));
            }

            current_file_size_ = 0;
            file_.open(current_file_name_, std::ios::app | std::ios::binary);
        }
    }

    std::string SyncFileAppender::generateLogFileName(int index) const {
        std::stringstream ss;
        ss << base_dir_ << "/astra_cache_" << start_time_str_ << "_" << pid_str_;
        if (index > 0) {
            ss << "." << index;
        }
        ss << ".log";
        return ss.str();
    }

    // 日志核心类实现
    std::string Astra::Logger::cached_timestamp_;
    std::mutex Astra::Logger::timestamp_mutex_;

    Logger::Logger() : level_(LogLevel::INFO), running_(true) {
        const char *home_env = getenv("USERPROFILE");
        if (!home_env) {
            home_env = getenv("HOME");
        }
        if (home_env) {
            default_log_dir_ = std::string(home_env) + "/.astra/logs";
        } else {
            default_log_dir_ = "./astra_logs";
        }

        startTimestampUpdater();

        auto async_console_appender = std::make_shared<AsyncConsoleAppender>();
        appenders_.push_back(async_console_appender);
    }

    Logger::~Logger() {
        running_ = false;
        if (timestamp_update_thread_.joinable()) {
            timestamp_update_thread_.join();
        }
    }

    Logger &Logger::GetInstance() {
        static Logger instance;
        return instance;
    }

    void Logger::Flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &appender: appenders_) {
            if (auto sync_appender = std::dynamic_pointer_cast<SyncFileAppender>(appender)) {
                sync_appender->Flush();
            } else if (auto async_console_appender = std::dynamic_pointer_cast<AsyncConsoleAppender>(appender)) {
                async_console_appender->Flush();
            }
        }
    }

    void Logger::SetLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    LogLevel Logger::GetLevel() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    std::string Logger::GetTimestamp() {
        std::lock_guard<std::mutex> lock(timestamp_mutex_);
        return cached_timestamp_;
    }

    std::string Logger::generateTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        #ifdef _WIN32
        localtime_s(&tm, &in_time_t);
        #elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&in_time_t, &tm);
        #endif

        char buf[20];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
        return std::string(buf);
    }

    void Logger::AddAppender(std::shared_ptr<LogAppender> appender) {
        std::lock_guard<std::mutex> lock(mutex_);
        appenders_.push_back(std::move(appender));
    }

    void Logger::RemoveAllAppenders() {
        std::lock_guard<std::mutex> lock(mutex_);
        appenders_.clear();
    }

    void Logger::SetDefaultLogDir(const std::string &dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        default_log_dir_ = dir;
    }

    std::string Logger::GetDefaultLogDir() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_log_dir_;
    }

    void Logger::Log(LogLevel level, const std::string &message) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &appender: appenders_) {
            if (auto async_console_appender = std::dynamic_pointer_cast<AsyncConsoleAppender>(appender)) {
                async_console_appender->Append(level, message);
            } else {
                appender->Append(level, message);
            }
        }
    }

    std::string Logger::LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE:
                return "TRACE";
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO ";
            case LogLevel::WARN:
                return "WARN ";
            case LogLevel::ERR:
                return "ERROR";
            case LogLevel::FATAL:
                return "FATAL";
        }
        return "UNKNOWN";
    }

    void Logger::startTimestampUpdater() {
        timestamp_update_thread_ = std::thread([this]() {
            // 去掉初始延迟，改用自旋等待（适用于对启动速度敏感的场景）
            while (!running_) {
                std::this_thread::yield();// 让出CPU，等待running_被设置
            }

            // 时间戳生成逻辑不变，但延长更新间隔（如1秒，根据业务需求调整）
            while (running_) {
                try {
                    std::string new_ts = generateTimestamp();
                    std::lock_guard<std::mutex> lock(timestamp_mutex_);
                    cached_timestamp_ = new_ts;
                } catch (...) {
                    // 异常处理
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));// 延长休眠时间
            }
        });
    }

    // 异步控制台输出器实现
    constexpr const fmt::text_style AsyncConsoleAppender::GetStyle(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE:
                return fmt::fg(fmt::color::gray);
            case LogLevel::DEBUG:
                return fmt::fg(fmt::color::cyan);
            case LogLevel::INFO:
                return fmt::fg(fmt::color::lime_green);
            case LogLevel::WARN:
                return fmt::fg(fmt::color::orange);
            case LogLevel::ERR:
                return fmt::fg(fmt::color::red) | fmt::emphasis::bold;
            case LogLevel::FATAL:
                return fmt::fg(fmt::color::crimson) | fmt::emphasis::bold;
            default:
                return fmt::text_style{};
        }
    }

    AsyncConsoleAppender::AsyncConsoleAppender()
        : flush_interval_(1), running_(true) {
        worker_thread_ = std::thread(&AsyncConsoleAppender::workerThread, this);
    }

    AsyncConsoleAppender::~AsyncConsoleAppender() {
        running_ = false;
        queue_cv_.notify_one();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        Flush();
    }

    void AsyncConsoleAppender::Append(LogLevel level, const std::string &message) {
        LogEntry entry;
        entry.level = level;
        entry.message = message;

        log_queue_.push(entry);

        if (log_queue_.size() >= 100 || level == LogLevel::FATAL) {
            std::lock_guard<std::mutex> lock(dummy_mutex_);
            queue_cv_.notify_one();
        }
    }

    void AsyncConsoleAppender::Flush() {
        std::unique_lock<std::mutex> lock(dummy_mutex_);
        queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
            return log_queue_.empty();
        });
    }

    void AsyncConsoleAppender::workerThread() {
        while (running_) {
            std::vector<LogEntry> entries;
            LogEntry entry;

            std::unique_lock<std::mutex> lock(dummy_mutex_);
            auto timeout = std::chrono::seconds(flush_interval_);
            queue_cv_.wait_for(lock, timeout, [this]() {
                return !log_queue_.empty() || !running_;
            });

            if (!running_ && log_queue_.empty()) {
                break;
            }

            lock.unlock();
            while (log_queue_.pop(entry)) {
                entries.push_back(entry);
            }

            if (!entries.empty()) {
                fmt::text_style current_style;
                for (auto &entry: entries) {
                    entry.timestamp = Logger::GetTimestamp();
                    fmt::text_style entry_style = GetStyle(entry.level);

                    if (entry_style != current_style) {
                        fmt::print(entry_style, "");
                        current_style = entry_style;
                    }
#ifdef _WIN32
                    //只有Windows平台才需要处理UTF-8编码问题

                    // 确保时间戳是合法 UTF-8（通常没问题）
                    std::string safe_timestamp = is_valid_utf8(entry.timestamp) ? entry.timestamp : EnsureUTF8(entry.timestamp);

                    // 日志级别是静态字符串，通常没问题
                    std::string level_str = Logger::LevelToString(entry.level);
                    std::string safe_level = is_valid_utf8(level_str) ? level_str : EnsureUTF8(level_str);

                    // 消息内容最可能出问题，确保转换
                    std::string safe_message = EnsureUTF8(entry.message);


                    fmt::print(entry_style,
                               "[{}] [{}] {}\n",
                               safe_timestamp,
                               safe_level,
                               safe_message);

#else
                    // 其他平台都不用管
                    fmt::print("[{}] [{}] {}\n",
                               entry.timestamp,
                               Logger::LevelToString(entry.level),
                               entry.message);
#endif
                }

                if (current_style != fmt::text_style{}) {
                    fmt::print("");
                }
            }
        }
    }

}// namespace Astra
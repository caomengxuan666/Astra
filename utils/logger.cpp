#include "logger.hpp"
#include <ctime>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <iostream>
#include <process.h>
#include <sstream>

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
                   Logger::GetInstance().CurrentTimestamp(),
                   Logger::LevelToString(level),
                   message);
        fmt::print(fmt::text_style{}, "");// 重置文本样式
    }

    // 文件输出器实现
    FileAppender::FileAppender(const std::string &base_dir, const LogConfig &config)
        : base_dir_(base_dir), config_(config) {
        // 获取PID并转换为字符串
        pid_str_ = std::to_string(_getpid());

        // 获取启动时间（用于文件名）
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &in_time_t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        start_time_str_ = ss.str();

        // 创建日志目录
        if (!fs::exists(base_dir_)) {
            fs::create_directories(base_dir_);
        }

        // 生成初始日志文件名
        current_file_name_ = generateLogFileName();

        // 打开文件
        file_.open(current_file_name_, std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            ZEN_LOG_ERROR("Failed to open log file: {}", current_file_name_);
        }

        // 启动工作线程
        worker_thread_ = std::thread(&FileAppender::workerThread, this);
    }

    FileAppender::~FileAppender() {
        running_ = false;
        queue_cv_.notify_one();// 唤醒工作线程
        if (worker_thread_.joinable()) {
            worker_thread_.join();// 等待线程完全退出
        }

        // 关闭文件流
        closeFile();
    }

    void FileAppender::closeFile() {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void FileAppender::SetConfig(const LogConfig &config) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        config_ = config;
    }

    const LogConfig &FileAppender::GetConfig() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return config_;
    }

    std::string FileAppender::GetCurrentLogFileName() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return current_file_name_;
    }

    void FileAppender::Append(LogLevel level, const std::string &message) {
        LogEntry entry;
        entry.level = level;
        entry.message = message;
        entry.timestamp = Logger::GetInstance().CurrentTimestamp();

        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);

        // 如果队列达到阈值或收到致命错误，立即通知工作线程
        if (log_queue_.size() >= config_.queue_threshold || level == LogLevel::FATAL) {
            queue_cv_.notify_one();
        }
    }

    void FileAppender::workerThread() {
        while (running_) {
            std::vector<LogEntry> entries;

            // 等待队列消息或超时
            std::unique_lock<std::mutex> lock(queue_mutex_);
            auto timeout = std::chrono::seconds(config_.flush_interval);
            queue_cv_.wait_for(lock, timeout, [this]() {
                return !log_queue_.empty() || !running_;
            });

            // 退出信号处理
            if (!running_ && log_queue_.empty()) {
                break;
            }

            // 批量取出日志条目
            while (!log_queue_.empty()) {
                entries.push_back(log_queue_.front());
                log_queue_.pop();
            }
            lock.unlock();

            // 写入文件
            if (!entries.empty()) {
                writeToFile(entries);
            }
        }
    }

    void FileAppender::rollLogFileIfNeeded() {
        // 检查文件大小是否超过限制
        if (current_file_size_ >= config_.max_file_size) {
            // 关闭当前文件
            {
                std::lock_guard<std::mutex> lock(file_mutex_);
                if (file_.is_open()) {
                    file_.flush();// 强制刷新缓冲区
                    file_.close();
                }
            }

            // 更新文件索引
            current_file_index_++;

            // 如果超过最大备份数，删除最旧的
            if (current_file_index_ > static_cast<int>(config_.max_backup_files)) {
                std::string old_file = generateLogFileName(1);
                if (fs::exists(old_file)) {
                    fs::remove(old_file);
                }
                current_file_index_ = config_.max_backup_files;
            }

            // 重命名其他备份文件（索引减1）
            for (int i = static_cast<int>(config_.max_backup_files); i > 1; --i) {
                std::string src = generateLogFileName(i);
                if (fs::exists(src)) {
                    std::string dest = generateLogFileName(i - 1);
                    fs::rename(src, dest);
                }
            }

            // 重命名当前文件为备份文件
            std::string old_name = current_file_name_;
            current_file_name_ = generateLogFileName();

            // 只有当文件存在时才重命名
            if (fs::exists(old_name)) {
                fs::rename(old_name, generateLogFileName(current_file_index_));
            }

            current_file_size_ = 0;

            // 重新打开新文件
            file_.open(current_file_name_, std::ios::app | std::ios::binary);
            if (!file_.is_open()) {
                ZEN_LOG_ERROR("Failed to reopen log file: {}", current_file_name_);
            }
        }
    }

    std::string FileAppender::generateLogFileName(int index) const {
        // 使用字符串流构造文件名
        std::stringstream ss;
        ss << base_dir_ << "/astra_cache_" << start_time_str_ << "_" << pid_str_;

        if (index > 0) {
            ss << "." << index;
        }

        ss << ".log";
        return ss.str();
    }

    void FileAppender::writeToFile(const std::vector<LogEntry> &entries) {
        // 检查并滚动日志文件
        rollLogFileIfNeeded();

        // 写入日志条目
        std::lock_guard<std::mutex> lock(file_mutex_);
        for (const auto &entry: entries) {
            std::string log_line = fmt::format("[{}] [{}] {}\n",
                                               entry.timestamp,
                                               Logger::LevelToString(entry.level),
                                               entry.message);

            file_ << log_line;
            current_file_size_ += log_line.size();
        }

        file_.flush();// 确保写入磁盘
    }

    SyncFileAppender::SyncFileAppender(const std::string &base_dir, const LogConfig &config)
        : base_dir_(base_dir), config_(config) {
        // 获取PID并转换为字符串
        pid_str_ = std::to_string(_getpid());

        // 获取启动时间（用于文件名）
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &in_time_t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        start_time_str_ = ss.str();

        // 创建日志目录
        if (!fs::exists(base_dir_)) {
            fs::create_directories(base_dir_);
        }

        // 生成初始日志文件名并打开
        current_file_name_ = generateLogFileName();
        openCurrentFile();
    }

    SyncFileAppender::~SyncFileAppender() {
        closeCurrentFile();
    }

    void SyncFileAppender::openCurrentFile() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 以追加模式打开，如果文件不存在则创建
        file_.open(current_file_name_, std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            // 这里不能使用日志，只能输出到调试窗口
            std::string msg = fmt::format("Failed to open log file: {}", current_file_name_);
            printf(msg.c_str());
        } else {
            // 获取当前文件大小
            file_.seekp(0, std::ios::end);
            current_file_size_ = static_cast<size_t>(file_.tellp());
        }
    }

    void SyncFileAppender::closeCurrentFile() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void SyncFileAppender::Append(LogLevel level, const std::string &message) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 检查是否需要滚动日志
        rollLogFileIfNeeded();

        if (!file_.is_open()) {
            openCurrentFile();
            if (!file_.is_open()) {
                return;
            }
        }

        // 生成日志行
        std::string timestamp = Logger::GetInstance().CurrentTimestamp();
        std::string log_line = fmt::format("[{}] [{}] {}\n",
                                           timestamp,
                                           Logger::LevelToString(level),
                                           message);

        // 立即写入并刷新
        file_ << log_line;
        file_.flush();// 强制刷新到磁盘
        current_file_size_ += log_line.size();
    }

    void SyncFileAppender::Flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void SyncFileAppender::SetConfig(const LogConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

    const LogConfig &SyncFileAppender::GetConfig() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    std::string SyncFileAppender::GetCurrentLogFileName() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_file_name_;
    }

    void SyncFileAppender::rollLogFileIfNeeded() {
        // 检查文件大小是否超过限制
        if (current_file_size_ >= config_.max_file_size && config_.max_file_size > 0) {
            // 关闭当前文件
            file_.flush();
            file_.close();

            // 更新文件索引
            current_file_index_++;

            // 如果超过最大备份数，删除最旧的
            if (current_file_index_ > static_cast<int>(config_.max_backup_files)) {
                std::string old_file = generateLogFileName(1);
                if (fs::exists(old_file)) {
                    fs::remove(old_file);
                }
                current_file_index_ = config_.max_backup_files;
            }

            // 重命名其他备份文件（索引减1）
            for (int i = static_cast<int>(config_.max_backup_files); i > 1; --i) {
                std::string src = generateLogFileName(i);
                if (fs::exists(src)) {
                    std::string dest = generateLogFileName(i - 1);
                    fs::rename(src, dest);
                }
            }

            // 重命名当前文件为备份文件
            std::string old_name = current_file_name_;
            current_file_name_ = generateLogFileName();

            // 只有当文件存在时才重命名
            if (fs::exists(old_name)) {
                fs::rename(old_name, generateLogFileName(current_file_index_));
            }

            // 打开新文件
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
    Logger::Logger() : level_(LogLevel::INFO) {
        // 设置默认日志目录
        const char *home_env = getenv("USERPROFILE");// Windows
        if (!home_env) {
            home_env = getenv("HOME");// Linux/macOS
        }
        if (home_env) {
            default_log_dir_ = std::string(home_env) + "/.astra/logs";
        } else {
            default_log_dir_ = "./astra_logs";
        }
        // 可选：同时保留默认控制台输出（如果需要控制台+文件双输出）
        auto console_appender = std::make_shared<ConsoleAppender>();
        appenders_.push_back(console_appender);
    }

    Logger &Logger::GetInstance() {
        static Logger instance;
        return instance;
    }

    void Logger::Flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &appender: appenders_) {
            // 尝试将appender转换为SyncFileAppender并调用Flush
            if (auto sync_appender = std::dynamic_pointer_cast<SyncFileAppender>(appender)) {
                sync_appender->Flush();
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
            appender->Append(level, message);
        }
    }

    std::string const Logger::LevelToString(LogLevel level) {
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
}// namespace Astra
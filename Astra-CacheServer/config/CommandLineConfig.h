#pragma once
#include "IConfigSource.h"
#include "args.hxx"
#include "utils/logger.hpp"
#include <mutex>

namespace Astra::apps {

    class CommandLineConfig : public IConfigSource {
    public:
        CommandLineConfig()
            : listening_port_(6380),
              log_level_(Astra::LogLevel::INFO),
              max_lru_size_(std::numeric_limits<size_t>::max()),
              enable_logging_file_(false) {}

        // 基础初始化方法（供普通模式使用）
        bool initialize(int argc, char *argv[]) override {
            std::lock_guard<std::mutex> lock(mutex_);
            return parseArguments(argc, argv);
        }

        // 服务模式专用初始化
        bool initializeForService(int argc, char *argv[]) {
            std::lock_guard<std::mutex> lock(mutex_);

            // 过滤掉 --service 参数
            std::vector<char *> filteredArgs;
            filteredArgs.push_back(argv[0]);
            for (int i = 1; i < argc; ++i) {
                if (std::string(argv[i]) != "--service") {
                    filteredArgs.push_back(argv[i]);
                }
            }

            return parseArguments(filteredArgs.size(), filteredArgs.data());
        }

        // 配置访问接口
        uint16_t getListeningPort() const override {
            std::lock_guard<std::mutex> lock(mutex_);
            return listening_port_;
        }

        Astra::LogLevel getLogLevel() const override {
            std::lock_guard<std::mutex> lock(mutex_);
            return log_level_;
        }

        std::string getPersistenceFileName() const override {
            std::lock_guard<std::mutex> lock(mutex_);
            return persistence_file_;
        }

        size_t getMaxLRUSize() const override {
            std::lock_guard<std::mutex> lock(mutex_);
            return max_lru_size_;
        }

        bool getEnableLoggingFile() const override {
            std::lock_guard<std::mutex> lock(mutex_);
            return enable_logging_file_;
        }

        void setListeningPort(uint16_t port) override {
            std::lock_guard<std::mutex> lock(mutex_);
            listening_port_ = port;
        }

        void setLogLevel(Astra::LogLevel level) override {
            std::lock_guard<std::mutex> lock(mutex_);
            log_level_ = level;
        }

    private:
        // 实际参数解析逻辑
        bool parseArguments(int argc, char *argv[]) {
            args::ArgumentParser parser("Astra-Cache Server", "Redis-compatible cache server.");
            args::ValueFlag<int> port(parser, "port", "Listening port", {'p', "port"}, 6380);
            args::ValueFlag<std::string> log_level(parser, "level", "Log level (trace/debug/info/warn/error/fatal)", {'l', "loglevel"}, "info");
            args::ValueFlag<std::string> persistence_file(parser, "file", "Persistence file path", {'c', "coredump"}, "");
            args::ValueFlag<size_t> max_lru(parser, "size", "Max LRU cache size", {'m', "maxsize"}, std::numeric_limits<size_t>::max());
            args::ValueFlag<bool> enable_log_file(parser, "enable", "Enable file logging", {'f', "file"}, false);

            try {
                parser.ParseCLI(argc, argv);
            } catch (const args::Help &) {
                std::cout << parser;
                return false;
            } catch (const args::ParseError &e) {
                std::cerr << "Command line parse error: " << e.what() << std::endl;
                return false;
            }

            listening_port_ = static_cast<uint16_t>(args::get(port));
            log_level_ = parseLogLevel(args::get(log_level));
            persistence_file_ = args::get(persistence_file);
            max_lru_size_ = args::get(max_lru);
            enable_logging_file_ = args::get(enable_log_file);
            return true;
        }

        // 日志级别转换
        Astra::LogLevel parseLogLevel(const std::string &level) {
            static const std::unordered_map<std::string, Astra::LogLevel> level_map = {
                    {"trace", Astra::LogLevel::TRACE},
                    {"debug", Astra::LogLevel::DEBUG},
                    {"info", Astra::LogLevel::INFO},
                    {"warn", Astra::LogLevel::WARN},
                    {"error", Astra::LogLevel::ERR},
                    {"fatal", Astra::LogLevel::FATAL}};

            auto it = level_map.find(level);
            return it != level_map.end() ? it->second : Astra::LogLevel::INFO;
        }

        mutable std::mutex mutex_;
        uint16_t listening_port_;
        Astra::LogLevel log_level_;
        std::string persistence_file_;
        size_t max_lru_size_;
        bool enable_logging_file_;
    };

}// namespace Astra::apps
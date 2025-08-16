#pragma once
#include "CommandLineConfig.h"
#include "IConfigSource.h"
#include "network/Singleton.h"// 你的Singleton模板
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>


namespace Astra::apps {

    // 配置管理器（单例，统一管理所有配置源）
    class ConfigManager : public Singleton<ConfigManager> {
        friend class Singleton<ConfigManager>;// 允许Singleton访问私有构造

    public:
        // 初始化所有配置源（命令行/文件等）
        bool initialize(int argc, char *argv[]) {
            std::lock_guard<std::mutex> lock(mutex_);

            // 添加命令行配置源（当前默认实现）
            auto cmd_config = std::make_unique<CommandLineConfig>();
            if (!cmd_config->initialize(argc, argv)) {
                return false;
            }
            config_sources_.push_back(std::move(cmd_config));

            // 未来添加文件配置源的示例：
            // auto file_config = std::make_unique<FileConfig>();
            // if (!file_config->initialize(argc, argv)) { return false; }
            // config_sources_.push_back(std::move(file_config));

            return true;
        }

        bool initializeForService(int argc, char *argv[]) {
            auto cmd_config = std::make_unique<CommandLineConfig>();
            if (!cmd_config->initializeForService(argc, argv)) {
                return false;
            }
            config_sources_.push_back(std::move(cmd_config));
            return true;
        }

        // 配置项访问接口（统一入口）
        uint16_t getListeningPort() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getListeningPort();
        }

        Astra::LogLevel getLogLevel() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getLogLevel();
        }

        std::string getPersistenceFileName() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getPersistenceFileName();
        }

        size_t getMaxLRUSize() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getMaxLRUSize();
        }

        bool getEnableLoggingFile() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getEnableLoggingFile();
        }

        // 集群相关配置访问接口
        bool getEnableCluster() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getEnableCluster();
        }

        uint16_t getClusterPort() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return getLatestConfig()->getClusterPort();
        }

        // 动态更新配置（同步到所有配置源）
        void setListeningPort(uint16_t port) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &source: config_sources_) {
                source->setListeningPort(port);
            }
        }

        void setLogLevel(Astra::LogLevel level) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &source: config_sources_) {
                source->setLogLevel(level);
            }
        }

    private:
        ConfigManager() = default;// 私有构造，通过Singleton创建

        // 获取优先级最高的配置源（最后添加的源）
        const IConfigSource *getLatestConfig() const {
            if (config_sources_.empty()) {
                throw std::runtime_error("No config sources initialized");
            }
            return config_sources_.back().get();
        }

        mutable std::mutex mutex_;                                  // 线程安全锁
        std::vector<std::unique_ptr<IConfigSource>> config_sources_;// 配置源列表（可扩展）
    };

}// namespace Astra::apps
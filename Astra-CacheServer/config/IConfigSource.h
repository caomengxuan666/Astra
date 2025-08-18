#pragma once
#include "core/astra.hpp"
#include <cstdint>
#include <string>

namespace Astra::apps {

    // 配置源接口（定义所有配置项的访问方式）
    class IConfigSource {
    public:
        virtual ~IConfigSource() = default;

        // 初始化配置（命令行解析/文件读取等）
        virtual bool initialize(int argc, char *argv[]) = 0;


        virtual uint16_t getListeningPort() const = 0;
        virtual std::string getBindAddress() const = 0;
        virtual Astra::LogLevel getLogLevel() const = 0;
        virtual std::string getPersistenceFileName() const = 0;
        virtual size_t getMaxLRUSize() const = 0;
        virtual bool getEnableLoggingFile() const = 0;
        // 集群相关配置访问接口
        virtual bool getEnableCluster() const = 0;
        virtual uint16_t getClusterPort() const = 0;

        // 配置项更新接口（预留用于动态修改）
        virtual void setListeningPort(uint16_t port) = 0;
        virtual void setLogLevel(Astra::LogLevel level) = 0;
    };

}// namespace Astra::apps
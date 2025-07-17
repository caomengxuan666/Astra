// astra_client.hpp
#pragma once
#include "commands.hpp"
#include "core/macros.hpp"
#include <chrono>// 添加chrono头文件以支持时间相关功能
#include <string>
#include <vector>
namespace Astra::Client {

    // RESP 响应类型枚举
    enum class RespType {
        SimpleString,
        BulkString,
        Integer,
        Array,
        Error,
        Nil
    };

    // RESP 值类型
    struct RespValue {
        RespType type;
        std::string str;
        int64_t integer = 0;
        std::vector<RespValue> array;
    };

    class ZEN_API AstraClient {
    public:
        // 构造函数：连接到指定地址和端口
        explicit AstraClient(const std::string &host, int port);

        // 析构函数：关闭 socket
        ~AstraClient();

        // 执行任意命令
        RespValue SendCommand(const Command::ICommand &command);

        // 封装常用命令
        RespValue Set(const std::string &key, const std::string &value);
        RespValue Set(const std::string &key, const std::string &value, std::chrono::seconds ttl);
        RespValue Get(const std::string &key);
        RespValue Del(const std::vector<std::string> &keys);
        RespValue Ping();
        RespValue Keys(const std::string &pattern = "*");
        RespValue TTL(const std::string &key);
        RespValue Exists(const std::string &key);
        RespValue Incr(const std::string &key);
        RespValue Decr(const std::string &key);
        RespValue MGet(const std::vector<std::string> &keys);
        RespValue MSet(const std::vector<std::pair<std::string, std::string>> &keyValues);

    private:
        int sockfd_;
        std::string host_;
        int port_;

        void Connect();
        void Disconnect();
        void SendRaw(const std::string &data);
        RespValue ReadResponse();

        // 内部 RESP 解析逻辑
        RespValue ParseSingleLine(char prefix, const std::string &line);
        RespValue ParseBulkString(const std::string &line);
        RespValue ParseArray(int64_t len);
    };

}// namespace Astra::Client
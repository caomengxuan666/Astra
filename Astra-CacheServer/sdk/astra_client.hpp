// astra_client.hpp
#pragma once
#include "commands.hpp"
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

    class AstraClient {
    public:
        // 构造函数：连接到指定地址和端口
        explicit AstraClient(const std::string &host, int port);

        // 析构函数：关闭 socket
        ~AstraClient();

        // 执行任意命令
        RespValue SendCommand(const Command::ICommand &command);

        // 封装常用命令
        const RespValue Set(const std::string &key, const std::string &value) noexcept;
        const RespValue Get(const std::string &key) noexcept;
        const RespValue Del(const std::vector<std::string> &keys) noexcept;
        const RespValue Ping() noexcept;
        const RespValue Keys(const std::string &pattern = "*") noexcept;
        const RespValue TTL(const std::string &key) noexcept;
        const RespValue Exists(const std::string &key) noexcept;
        const RespValue Incr(const std::string &key) noexcept;
        const RespValue Decr(const std::string &key) noexcept;

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
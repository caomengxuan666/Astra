// astra_client.cpp
#include "astra_client.hpp"
#include "sdk/commands.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace Astra::Client {
    using namespace Command;

    AstraClient::AstraClient(const std::string &host, int port)
        : host_(host), port_(port), sockfd_(-1) {
        Connect();
    }

    AstraClient::~AstraClient() {
        Disconnect();
    }

    void AstraClient::Connect() {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ == -1) {
            throw std::runtime_error("Failed to create socket");
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);

        if (connect(sockfd_, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
            close(sockfd_);
            throw std::runtime_error("Failed to connect to server");
        }
    }

    void AstraClient::Disconnect() {
        if (sockfd_ != -1) {
            close(sockfd_);
            sockfd_ = -1;
        }
    }

    std::string BuildRedisCommand(const std::vector<std::string> &argv) {
        std::string cmd;
        cmd += "*" + std::to_string(argv.size()) + "\r\n";
        for (const auto &arg: argv) {
            cmd += "$" + std::to_string(arg.size()) + "\r\n";
            cmd += arg + "\r\n";
        }
        return cmd;
    }

    void AstraClient::SendRaw(const std::string &data) {
        if (send(sockfd_, data.data(), data.size(), 0) != static_cast<ssize_t>(data.size())) {
            throw std::runtime_error("Failed to send data");
        }
    }

    std::string ReadLine(int sockfd) {
        std::string line;
        char c;
        while (true) {
            ssize_t n = recv(sockfd, &c, 1, 0);
            if (n <= 0) {
                throw std::runtime_error("Connection closed or read error");
            }
            if (c == '\n') break;
            if (c != '\r') {// skip \r
                line += c;
            }
        }
        return line;
    }

    RespValue AstraClient::ReadResponse() {
        std::string line = ReadLine(sockfd_);
        if (line.empty()) {
            throw std::runtime_error("Empty response");
        }

        char type = line[0];
        std::string content = line.substr(1);

        switch (type) {
            case '+': {// Simple String
                return RespValue{RespType::SimpleString, content, 0, {}};
            }
            case '$': {// Bulk String
                int64_t len = std::stoll(content);
                if (len == -1) {
                    return RespValue{RespType::Nil, "", 0, {}};
                }
                std::string str(len, '\0');
                recv(sockfd_, str.data(), len, 0);
                ReadLine(sockfd_);// consume \r\n
                return RespValue{RespType::BulkString, str, 0, {}};
            }
            case ':': {// Integer
                return RespValue{RespType::Integer, "", std::stoll(content), {}};
            }
            case '*': {// Array
                int64_t len = std::stoll(content);
                RespValue arr{RespType::Array, "", 0, {}};
                arr.array.reserve(len);
                for (int64_t i = 0; i < len; ++i) {
                    arr.array.push_back(ReadResponse());
                }
                return arr;
            }
            case '-': {// Error
                return RespValue{RespType::Error, content, 0, {}};
            }
            default:
                throw std::runtime_error("Unknown response type: " + std::string(1, type));
        }
    }

    // 执行任意命令
    RespValue AstraClient::SendCommand(const ICommand &command) {
        std::vector<std::string> args = command.GetArgs();
        std::string req = BuildRedisCommand(args);
        SendRaw(req);
        return ReadResponse();
    }

    // 命令封装
    const RespValue AstraClient::Set(const std::string &key, const std::string &value) noexcept {
        return std::move(SendCommand(SetCommand(key, value)));
    }

    const RespValue AstraClient::Get(const std::string &key) noexcept {
        return std::move(SendCommand(GetCommand(key)));
    }

    const RespValue AstraClient::Del(const std::vector<std::string> &keys) noexcept {
        return std::move(SendCommand(DelCommand(keys)));
    }

    const RespValue AstraClient::Ping() noexcept {
        return std::move(SendCommand(PingCommand()));
    }

    const RespValue AstraClient::Keys(const std::string &pattern) noexcept {
        return std::move(SendCommand(KeysCommand(pattern)));
    }

    const RespValue AstraClient::TTL(const std::string &key) noexcept {
        return std::move(SendCommand(TTLCommand(key)));
    }

    const RespValue AstraClient::Exists(const std::string &key) noexcept {
        return std::move(SendCommand(ExistsCommand(key)));
    }

    const RespValue AstraClient::Incr(const std::string &key) noexcept {
        return std::move(SendCommand(IncrCommand(key)));
    }

    const RespValue AstraClient::Decr(const std::string &key) noexcept {
        return std::move(SendCommand(DecrCommand(key)));
    }

}// namespace Astra::Client
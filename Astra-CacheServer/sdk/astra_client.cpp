// astra_client.cpp
#include "astra_client.hpp"
#include "sdk/commands.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// Windows平台网络编程头文件
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")// 链接Windows Sockets库
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// 跨平台定义 ssize_t 类型
#ifdef _WIN32
// Windows 中没有 ssize_t，用 int64_t 替代（兼容 32/64 位）
typedef int64_t ssize_t;
#endif

namespace Astra::Client {
    using namespace Command;

    // Windows平台初始化Winsock
    void InitWinsock() {
#ifdef _WIN32
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }

    // Windows平台清理Winsock
    void CleanupWinsock() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    AstraClient::AstraClient(const std::string &host, int port)
        : host_(host), port_(port), sockfd_(-1) {
        InitWinsock();// 初始化Winsock
        Connect();
    }

    AstraClient::~AstraClient() {
        Disconnect();
        CleanupWinsock();// 清理Winsock
    }

    void AstraClient::Connect() {
#ifdef _WIN32
        sockfd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
#endif

        if (sockfd_ == -1) {
            throw std::runtime_error("Failed to create socket");
        }

#ifdef _WIN32
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);

        // 转换IP地址
        inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);

        if (connect(sockfd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
            int error = WSAGetLastError();
            closesocket(sockfd_);
            throw std::runtime_error("Failed to connect to server: " + std::to_string(error));
        }
#else
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);

        if (connect(sockfd_, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
            close(sockfd_);
            throw std::runtime_error("Failed to connect to server");
        }
#endif
    }

    void AstraClient::Disconnect() {
        if (sockfd_ != -1) {
#ifdef _WIN32
            closesocket(sockfd_);
#else
            close(sockfd_);
#endif
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
    RespValue AstraClient::Set(const std::string &key, const std::string &value) {
        return std::move(SendCommand(SetCommand(key, value)));
    }

    RespValue AstraClient::Set(const std::string &key, const std::string &value, std::chrono::seconds ttl) {
        return std::move(SendCommand(SetCommand(key, value, ttl)));
    }

    RespValue AstraClient::Get(const std::string &key) {
        return std::move(SendCommand(GetCommand(key)));
    }

    RespValue AstraClient::Del(const std::vector<std::string> &keys) {
        return std::move(SendCommand(DelCommand(keys)));
    }

    RespValue AstraClient::Ping() {
        return std::move(SendCommand(PingCommand()));
    }

    RespValue AstraClient::Keys(const std::string &pattern) {
        return std::move(SendCommand(KeysCommand(pattern)));
    }

    RespValue AstraClient::TTL(const std::string &key) {
        return std::move(SendCommand(TTLCommand(key)));
    }

    RespValue AstraClient::Exists(const std::string &key) {
        return std::move(SendCommand(ExistsCommand(key)));
    }

    RespValue AstraClient::Incr(const std::string &key) {
        return std::move(SendCommand(IncrCommand(key)));
    }

    RespValue AstraClient::Decr(const std::string &key) {
        return std::move(SendCommand(DecrCommand(key)));
    }

    RespValue AstraClient::MGet(const std::vector<std::string> &keys) {
        return std::move(SendCommand(MGetCommand(keys)));
    }

    RespValue AstraClient::MSet(const std::vector<std::pair<std::string, std::string>> &keyValues) {
        return std::move(SendCommand(MSetCommand(keyValues)));
    }

}// namespace Astra::Client
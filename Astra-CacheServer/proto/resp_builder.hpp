// resp_builder.hpp
#pragma once
#include <string>
#include <vector>

namespace Astra::proto {

    class RespBuilder {
    public:
        static std::string BulkString(const std::string &str);
        static std::string Integer(int64_t value);
        static std::string Array(const std::vector<std::string> &elements);
        static std::string SimpleString(const std::string &str);
        static std::string Nil();
        static std::string Error(const std::string &str);
    };

// 实现错误响应格式
    inline std::string RespBuilder::Error(const std::string &str) {
        return "-ERR " + str + "\r\n";
    }

    inline std::string RespBuilder::BulkString(const std::string &str) {
        return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
    }

    inline std::string RespBuilder::Integer(int64_t value) {
        return ":" + std::to_string(value) + "\r\n";
    }

    inline std::string RespBuilder::SimpleString(const std::string &str) {
        return "+" + str + "\r\n";
    }

    inline std::string RespBuilder::Nil() {
        return "$-1\r\n";
    }

    inline std::string RespBuilder::Array(const std::vector<std::string> &elements) {
        std::string result = "*" + std::to_string(elements.size()) + "\r\n";
        for (const auto &item: elements) {
            result += item;
        }
        return result;
    }
}// namespace Astra::proto
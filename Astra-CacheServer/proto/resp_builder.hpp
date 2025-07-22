// resp_builder.hpp
#pragma once
#include <string>
#include <unordered_set>
#include <vector>


namespace Astra::proto {

    class RespBuilder {
    public:
        static std::string BulkString(const std::string &str) noexcept;
        static std::string Integer(int64_t value) noexcept;
        static std::string Array(const std::vector<std::string> &elements) noexcept;
        static std::string SimpleString(const std::string &str) noexcept;
        static std::string Nil() noexcept;
        static std::string Error(const std::string &str) noexcept;

        // PUB/SUB 专用响应构建方法（新增）
        static std::string SubscribeResponse(const std::unordered_set<std::string> &channels) noexcept;
        static std::string UnsubscribeResponse(const std::unordered_set<std::string> &channels) noexcept;
        // 重载MessageResponse，支持普通消息和模式消息
        static std::string MessageResponse(const std::string &type,
                                           const std::string &channel,
                                           const std::string &message) noexcept;
        static std::string MessageResponse(const std::string &type,
                                           const std::string &pattern,
                                           const std::string &channel,
                                           const std::string &message) noexcept;

        static std::string PSubscribeResponse(
                const std::unordered_set<std::string> &patterns,
                size_t remaining) noexcept;

        static std::string PUnsubscribeResponse(
                const std::unordered_set<std::string> &patterns,
                size_t remaining) noexcept;
    };

    // 实现错误响应格式
    inline std::string RespBuilder::Error(const std::string &str) noexcept {
        return "-ERR " + str + "\r\n";
    }

    inline std::string RespBuilder::BulkString(const std::string &str) noexcept {
        return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
    }

    inline std::string RespBuilder::Integer(int64_t value) noexcept {
        return ":" + std::to_string(value) + "\r\n";
    }

    inline std::string RespBuilder::SimpleString(const std::string &str) noexcept {
        return "+" + str + "\r\n";
    }

    inline std::string RespBuilder::Nil() noexcept {
        return "$-1\r\n";
    }

    inline std::string RespBuilder::Array(const std::vector<std::string> &elements) noexcept {
        std::string result = "*" + std::to_string(elements.size()) + "\r\n";
        for (const auto &item: elements) {
            result += item;
        }
        return result;
    }

    // PUB/SUB 响应实现（新增）
    inline std::string RespBuilder::SubscribeResponse(const std::unordered_set<std::string> &channels) noexcept {
        std::string response;
        // 为每个订阅的频道生成响应
        for (const auto &channel: channels) {
            // 构建 ["subscribe", "channel", 剩余订阅数] 数组
            std::vector<std::string> elements = {
                    BulkString("subscribe"),// 第一个元素："subscribe"
                    BulkString(channel),    // 第二个元素：频道名
                    Integer(channels.size())// 第三个元素：当前订阅总数
            };
            response += Array(elements);// 拼接数组响应
        }
        return response;
    }

    inline std::string RespBuilder::UnsubscribeResponse(const std::unordered_set<std::string> &channels) noexcept {
        std::string response;
        // 为每个取消订阅的频道生成响应
        for (const auto &channel: channels) {
            // 构建 ["unsubscribe", "channel", 剩余订阅数] 数组
            std::vector<std::string> elements = {
                    BulkString("unsubscribe"),// 第一个元素："unsubscribe"
                    BulkString(channel),      // 第二个元素：频道名
                    Integer(channels.size())  // 第三个元素：剩余订阅数（取消后的值）
            };
            response += Array(elements);// 拼接数组响应
        }
        return response;
    }

    // 普通消息响应 (message类型)
    inline std::string RespBuilder::MessageResponse(const std::string &type,
                                                    const std::string &channel,
                                                    const std::string &message) noexcept {
        std::vector<std::string> elements;
        elements.push_back(BulkString(type));
        elements.push_back(BulkString(channel));
        elements.push_back(BulkString(message));
        return Array(elements);
    }

    // 模式消息响应 (pmessage类型)
    inline std::string RespBuilder::MessageResponse(const std::string &type,
                                                    const std::string &pattern,
                                                    const std::string &channel,
                                                    const std::string &message) noexcept {
        std::vector<std::string> elements;
        elements.push_back(BulkString(type));
        elements.push_back(BulkString(pattern));
        elements.push_back(BulkString(channel));
        elements.push_back(BulkString(message));
        return Array(elements);
    }

    // 正确：用当前会话的订阅数作为第3个字段
    inline std::string RespBuilder::PSubscribeResponse(
            const std::unordered_set<std::string> &patterns,
            size_t session_pattern_count// 当前会话的模式订阅总数
            ) noexcept {
        std::string response;
        // 为每个模式生成一个独立的响应数组
        for (const auto &pattern: patterns) {
            std::vector<std::string> elements;
            elements.push_back(BulkString("psubscribe"));      // 类型："psubscribe"
            elements.push_back(BulkString(pattern));           // 模式名：如 "news*"
            elements.push_back(Integer(session_pattern_count));// 当前会话的订阅总数

            // 每个模式的响应单独作为一个数组，拼接到最终响应中
            response += Array(elements);
        }
        return response;
    }

    inline std::string RespBuilder::PUnsubscribeResponse(
            const std::unordered_set<std::string> &patterns,
            size_t remaining) noexcept {
        std::string response;
        for (const auto &pattern: patterns) {
            std::vector<std::string> elements;
            elements.push_back(BulkString("punsubscribe"));
            elements.push_back(BulkString(pattern));// 正确填入被取消的模式
            elements.push_back(Integer(remaining));
            response += Array(elements);
        }
        // 若未传入任何模式（如无参数取消），返回 nil
        if (patterns.empty()) {
            std::vector<std::string> elements;
            elements.push_back(BulkString("punsubscribe"));
            elements.push_back(Nil());// 无参数时显示 nil
            elements.push_back(Integer(remaining));
            response += Array(elements);
        }
        return response;
    }
}// namespace Astra::proto
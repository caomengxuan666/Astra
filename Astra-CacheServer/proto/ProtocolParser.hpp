#pragma once

#include <string>
#include <vector>

namespace Astra::proto {

    class ProtocolParser {
    public:
        // 解析状态枚举
        enum class ParseState {
            ReadingArrayHeader,
            ReadingBulkHeader,
            ReadingBulkContent
        };

        // 构造函数
        ProtocolParser();

        // 处理缓冲区中的协议数据
        size_t ProcessBuffer(std::string& buffer, std::vector<std::string>& argv);

        // 处理数组头（如 *3\r\n）
        bool HandleArrayHeader(const std::string& line, std::vector<std::string>& argv, int& remaining_args);

        // 处理批量数据头（如 $5\r\n）
        bool HandleBulkHeader(const std::string& line, int& current_bulk_size);

        // 处理批量数据内容
        bool HandleBulkContent(const std::string& buffer, std::string& content, int current_bulk_size);

        // 重置解析器状态
        void Reset();

        // Getter和Setter方法
        ParseState GetParseState() const;
        void SetParseState(ParseState state);
        int GetRemainingArgs() const;
        void SetRemainingArgs(int count);
        int GetCurrentBulkSize() const;
        void SetCurrentBulkSize(int size);
        void ClearArgs(std::vector<std::string>& argv);
        void ReserveArgs(std::vector<std::string>& argv, size_t count);

    private:
        ParseState parse_state_;
        int remaining_args_;
        int current_bulk_size_;
    };

} // namespace Astra::proto
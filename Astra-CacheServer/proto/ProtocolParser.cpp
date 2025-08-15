#include "ProtocolParser.hpp"
#include "logger.hpp"
#include <utils/logger.hpp>

namespace Astra::proto {

    ProtocolParser::ProtocolParser()
        : parse_state_(ParseState::ReadingArrayHeader),
          remaining_args_(0),
          current_bulk_size_(0) {}

    size_t ProtocolParser::ProcessBuffer(std::string &buffer, std::vector<std::string> &argv) {
        if (parse_state_ == ParseState::ReadingArrayHeader) {
            size_t pos = buffer.find("\r\n");
            if (pos == std::string::npos) return 0;

            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);

            if (!HandleArrayHeader(line, argv, remaining_args_)) {
                return 0;// Error occurred
            }
            return pos + 2;
        } else if (parse_state_ == ParseState::ReadingBulkHeader) {
            size_t pos = buffer.find("\r\n");
            if (pos == std::string::npos) return 0;

            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);

            if (!HandleBulkHeader(line, current_bulk_size_)) {
                return 0;// Error occurred
            }
            return pos + 2;
        } else if (parse_state_ == ParseState::ReadingBulkContent) {
            if (current_bulk_size_ < 0 || buffer.size() < static_cast<size_t>(current_bulk_size_ + 2)) {
                return 0;
            }

            std::string content;
            if (!HandleBulkContent(buffer, content, current_bulk_size_)) {
                return 0;// Error occurred
            }

            argv.push_back(content);
            remaining_args_--;

            if (remaining_args_ > 0) {
                parse_state_ = ParseState::ReadingBulkHeader;
            } else {
                parse_state_ = ParseState::ReadingArrayHeader;
            }

            buffer.erase(0, current_bulk_size_ + 2);
            return current_bulk_size_ + 2;
        }

        return 0;
    }

    bool ProtocolParser::HandleArrayHeader(const std::string &line, std::vector<std::string> &argv, int &remaining_args) {
        if (line.empty() || line[0] != '*') {
            ZEN_LOG_WARN("Protocol error: expected array header");
            return false;
        }

        try {
            int64_t arg_count = std::stoll(line.substr(1));
            if (arg_count < 0) {
                throw std::invalid_argument("Negative argument count");
            }

            remaining_args = static_cast<int>(arg_count);
            argv.clear();
            argv.reserve(remaining_args);

            ZEN_LOG_DEBUG("Array header parsed: {} arguments", remaining_args);
            parse_state_ = ParseState::ReadingBulkHeader;
            return true;
        } catch (...) {
            ZEN_LOG_WARN("Invalid argument count");
            return false;
        }
    }

    bool ProtocolParser::HandleBulkHeader(const std::string &line, int &current_bulk_size) {
        if (line.empty() || line[0] != '$') {
            ZEN_LOG_WARN("Protocol error: expected bulk header");
            return false;
        }

        try {
            int64_t bulk_size = std::stoll(line.substr(1));
            if (bulk_size < -1) {
                throw std::invalid_argument("Invalid bulk size");
            }

            current_bulk_size = static_cast<int>(bulk_size);
            ZEN_LOG_DEBUG("Bulk string size: {}", current_bulk_size);

            parse_state_ = ParseState::ReadingBulkContent;

            if (current_bulk_size == -1) {
                parse_state_ = ParseState::ReadingBulkHeader;
                return true;
            }

            return true;
        } catch (...) {
            ZEN_LOG_WARN("Invalid bulk length");
            return false;
        }
    }

    bool ProtocolParser::HandleBulkContent(const std::string &buffer, std::string &content, int current_bulk_size) {
        if (buffer.size() < static_cast<size_t>(current_bulk_size + 2)) {
            ZEN_LOG_ERROR("Insufficient data for bulk content");
            return false;
        }

        content = buffer.substr(0, current_bulk_size);
        ZEN_LOG_DEBUG("Bulk content parsed: {}", content);
        return true;
    }

    void ProtocolParser::Reset() {
        parse_state_ = ParseState::ReadingArrayHeader;
        remaining_args_ = 0;
        current_bulk_size_ = 0;
    }

    ProtocolParser::ParseState ProtocolParser::GetParseState() const {
        return parse_state_;
    }

    void ProtocolParser::SetParseState(ParseState state) {
        parse_state_ = state;
    }

    int ProtocolParser::GetRemainingArgs() const {
        return remaining_args_;
    }

    void ProtocolParser::SetRemainingArgs(int count) {
        remaining_args_ = count;
    }

    int ProtocolParser::GetCurrentBulkSize() const {
        return current_bulk_size_;
    }

    void ProtocolParser::SetCurrentBulkSize(int size) {
        current_bulk_size_ = size;
    }

    void ProtocolParser::ClearArgs(std::vector<std::string> &argv) {
        argv.clear();
    }

    void ProtocolParser::ReserveArgs(std::vector<std::string> &argv, size_t count) {
        argv.reserve(count);
    }

}// namespace Astra::proto
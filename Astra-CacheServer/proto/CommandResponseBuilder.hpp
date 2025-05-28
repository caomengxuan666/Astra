#pragma once
#include "command_info.hpp"
#include "command_parser.hpp"
#include "resp_builder.hpp"
#include <algorithm>
#include <sstream>
#include <vector>

namespace Astra::proto {

    class CommandResponseBuilder {
    public:
        static std::string BuildCommandListResponse(const std::vector<CommandInfo> &commands, bool isForRedisCli = false);
        static std::string BuildCommandDocsResponse(const std::vector<CommandInfo> &allCommands,
                                                    const std::vector<std::string> &requestedCommands = {});

    private:
        static std::string BuildFullCommandDetail(const CommandInfo &cmd);
        static std::string BuildCommandDocEntry(const CommandInfo &cmd);
    };

    // 构造原始 COMMAND 响应格式（数组）
    inline std::string CommandResponseBuilder::BuildCommandListResponse(
            const std::vector<CommandInfo> &commands, bool isForRedisCli) {

        if (isForRedisCli) {
            std::vector<std::string> names;
            for (const auto &cmd: commands) {
                names.push_back(RespBuilder::BulkString(cmd.name));
            }
            return RespBuilder::Array(names);
        }

        std::vector<std::string> fullDetails;
        for (const auto &cmd: commands) {
            fullDetails.push_back(BuildFullCommandDetail(cmd));
        }
        return RespBuilder::Array(fullDetails);
    }

    // 构造 COMMAND DOCS 响应格式（map）
    inline std::string CommandResponseBuilder::BuildCommandDocsResponse(
            const std::vector<CommandInfo> &allCommands,
            const std::vector<std::string> &requestedCommands) {

        std::ostringstream oss;

        if (requestedCommands.empty()) {
            oss << "%" << allCommands.size() << "\r\n";
            for (const auto &cmd: allCommands) {
                oss << RespBuilder::BulkString(cmd.name);
                oss << BuildCommandDocEntry(cmd);
            }
        } else {
            oss << "%" << requestedCommands.size() << "\r\n";
            for (const auto &reqName: requestedCommands) {
                auto it = std::find_if(allCommands.begin(), allCommands.end(),
                                       [&](const CommandInfo &info) {
                                           return ICaseCmp(info.name, reqName);
                                       });
                if (it != allCommands.end()) {
                    oss << RespBuilder::BulkString(it->name);
                    oss << BuildCommandDocEntry(*it);
                } else {
                    oss << RespBuilder::BulkString(reqName);
                    oss << "$-1\r\n";// Nil
                }
            }
        }

        return oss.str();
    }

    // 构造单个命令的 DOC entry（RESP3 map 格式）
    inline std::string CommandResponseBuilder::BuildCommandDocEntry(const CommandInfo &cmd) {
        std::ostringstream oss;

        // 固定字段：summary, since, group, complexity, doc_flags
        int fieldCount = 5;
        if (!cmd.history.empty()) ++fieldCount;
        if (!cmd.arguments.empty()) ++fieldCount;

        oss << "%" << fieldCount << "\r\n";

        // summary
        oss << RespBuilder::BulkString("summary");
        oss << RespBuilder::BulkString(cmd.summary);

        // since
        oss << RespBuilder::BulkString("since");
        oss << RespBuilder::BulkString(cmd.since);

        // group
        oss << RespBuilder::BulkString("group");
        oss << RespBuilder::BulkString(cmd.category);

        // complexity
        oss << RespBuilder::BulkString("complexity");
        oss << RespBuilder::BulkString(cmd.complexity);

        // doc_flags
        oss << RespBuilder::BulkString("doc_flags");
        oss << "$2\r\n[]\r\n";

        // history（可选）
        if (!cmd.history.empty()) {
            oss << RespBuilder::BulkString("history");
            oss << "*" << cmd.history.size() << "\r\n";
            for (const auto &entry: cmd.history) {
                oss << "%2\r\n";
                oss << RespBuilder::BulkString("version");
                oss << RespBuilder::BulkString(entry.version);
                oss << RespBuilder::BulkString("change");
                oss << RespBuilder::BulkString(entry.change);
            }
        }

        // arguments（可选）
        if (!cmd.arguments.empty()) {
            oss << RespBuilder::BulkString("arguments");
            oss << "*" << cmd.arguments.size() << "\r\n";
            for (const auto &arg: cmd.arguments) {
                oss << "%2\r\n";
                oss << RespBuilder::BulkString("name");
                oss << RespBuilder::BulkString(arg.name);
                oss << RespBuilder::BulkString("type");
                oss << RespBuilder::BulkString(arg.type);
            }
        }

        return oss.str();
    }

    // 构造原始 COMMAND 返回结构（不变）
    inline std::string CommandResponseBuilder::BuildFullCommandDetail(const CommandInfo &cmd) {
        std::vector<std::string> fields;

        fields.push_back(RespBuilder::BulkString(cmd.name));// name
        fields.push_back(RespBuilder::Integer(cmd.arity));  // arity
        {
            std::vector<std::string> flagItems;
            for (const auto &flag: cmd.flags) {
                flagItems.push_back(RespBuilder::BulkString(flag));
            }
            fields.push_back(RespBuilder::Array(flagItems));// flags
        }
        fields.push_back(RespBuilder::Integer(cmd.first_key));   // first_key
        fields.push_back(RespBuilder::Integer(cmd.last_key));    // last_key
        fields.push_back(RespBuilder::Integer(cmd.key_step));    // key_step
        fields.push_back(RespBuilder::BulkString(""));           // tips
        fields.push_back(RespBuilder::Integer(cmd.microseconds));// microseconds
        {
            std::vector<std::string> categoryItems;
            categoryItems.push_back(RespBuilder::BulkString(cmd.category));
            fields.push_back(RespBuilder::Array(categoryItems));// category
        }

        return RespBuilder::Array(fields);
    }

}// namespace Astra::proto
#pragma once
#include "command_info.hpp"
#include "command_parser.hpp"
#include "resp_builder.hpp"
#include <algorithm>
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

        std::vector<std::string> result;

        auto buildEntry = [&](const std::string &name, const std::string &docArray) {
            std::vector<std::string> pair;
            pair.push_back(RespBuilder::BulkString(name));
            pair.push_back(docArray);
            return RespBuilder::Array(pair);
        };

        if (requestedCommands.empty()) {
            for (const auto &cmd: allCommands) {
                std::string doc = BuildCommandDocEntry(cmd);
                result.push_back(buildEntry(cmd.name, doc));
            }
        } else {
            for (const auto &reqName: requestedCommands) {
                auto it = std::find_if(allCommands.begin(), allCommands.end(),
                                       [&](const CommandInfo &info) {
                                           return ICaseCmp(info.name, reqName);
                                       });

                std::string doc;
                if (it != allCommands.end()) {
                    doc = BuildCommandDocEntry(*it);
                } else {
                    doc = "$-1\r\n";// nil
                }
                result.push_back(buildEntry(reqName, doc));
            }
        }

        return RespBuilder::Array(result);
    }

    // 构造单个命令的 DOC entry（RESP3 map 格式）
    inline std::string CommandResponseBuilder::BuildCommandDocEntry(const CommandInfo &cmd) {
        std::vector<std::string> mapElements;

        // summary
        mapElements.push_back(RespBuilder::BulkString("summary"));
        mapElements.push_back(RespBuilder::BulkString(cmd.summary));

        // since
        mapElements.push_back(RespBuilder::BulkString("since"));
        mapElements.push_back(RespBuilder::BulkString(cmd.since));

        // group
        mapElements.push_back(RespBuilder::BulkString("group"));
        mapElements.push_back(RespBuilder::BulkString(cmd.category));

        // complexity
        mapElements.push_back(RespBuilder::BulkString("complexity"));
        mapElements.push_back(RespBuilder::BulkString(cmd.complexity));

        // doc_flags
        mapElements.push_back(RespBuilder::BulkString("doc_flags"));
        mapElements.push_back(RespBuilder::Array({}));

        // history（可选）
        if (!cmd.history.empty()) {
            std::vector<std::string> historyElements;
            for (const auto &entry: cmd.history) {
                std::vector<std::string> historyMapElements;
                historyMapElements.push_back(RespBuilder::BulkString("version"));
                historyMapElements.push_back(RespBuilder::BulkString(entry.version));
                historyMapElements.push_back(RespBuilder::BulkString("change"));
                historyMapElements.push_back(RespBuilder::BulkString(entry.change));
                historyElements.push_back(RespBuilder::Array(historyMapElements));
            }
            mapElements.push_back(RespBuilder::BulkString("history"));
            mapElements.push_back(RespBuilder::Array(historyElements));
        }

        // arguments（可选）
        if (!cmd.arguments.empty()) {
            std::vector<std::string> argElements;
            for (const auto &arg: cmd.arguments) {
                std::vector<std::string> argMapElements;
                argMapElements.push_back(RespBuilder::BulkString("name"));
                argMapElements.push_back(RespBuilder::BulkString(arg.name));
                argMapElements.push_back(RespBuilder::BulkString("type"));
                argMapElements.push_back(RespBuilder::BulkString(arg.type));
                argElements.push_back(RespBuilder::Array(argMapElements));
            }
            mapElements.push_back(RespBuilder::BulkString("arguments"));
            mapElements.push_back(RespBuilder::Array(argElements));
        }

        return RespBuilder::Array(mapElements);
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
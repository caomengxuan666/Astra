#pragma once
#include "command_info.hpp"
#include "command_parser.hpp"
#include "resp_builder.hpp"
#include <algorithm>
#include <vector>
#include <sstream>

// 根据平台自动选择 RESP 版本
#ifdef _WIN32
    #define RESP_V2
#else
    #define RESP_V3
#endif

namespace Astra::proto {

class CommandResponseBuilder {
public:
    static std::string BuildCommandListResponse(const std::vector<CommandInfo>& commands, bool isForRedisCli = false);
    static std::string BuildCommandDocsResponse(const std::vector<CommandInfo>& allCommands,
                                                const std::vector<std::string>& requestedCommands = {});

private:
    static std::string BuildFullCommandDetail(const CommandInfo& cmd);
    static std::string BuildCommandDocEntry_RESP2(const CommandInfo& cmd);
#ifdef RESP_V3
    static std::string BuildCommandDocEntry_RESP3(const CommandInfo& cmd);
#endif
};

// 构造原始 COMMAND 响应格式（数组）—— 兼容 V2/V3
inline std::string CommandResponseBuilder::BuildCommandListResponse(
    const std::vector<CommandInfo>& commands, bool isForRedisCli) {

    if (isForRedisCli) {
        std::vector<std::string> names;
        for (const auto& cmd : commands) {
            names.push_back(RespBuilder::BulkString(cmd.name));
        }
        return RespBuilder::Array(names);
    }

    std::vector<std::string> fullDetails;
    for (const auto& cmd : commands) {
        fullDetails.push_back(BuildFullCommandDetail(cmd));
    }
    return RespBuilder::Array(fullDetails);
}

// 构造 COMMAND DOCS 响应格式：根据平台选择 RESP2 或 RESP3
inline std::string CommandResponseBuilder::BuildCommandDocsResponse(
    const std::vector<CommandInfo>& allCommands,
    const std::vector<std::string>& requestedCommands) {

#ifdef RESP_V3
    std::ostringstream oss;
    size_t count = requestedCommands.empty() ? allCommands.size() : requestedCommands.size();
    oss << "%" << count << "\r\n";

    auto processEntry = [&](const std::string& name, const CommandInfo* cmd) {
        oss << RespBuilder::BulkString(name);
        if (cmd) {
            oss << BuildCommandDocEntry_RESP3(*cmd);
        } else {
            oss << "$-1\r\n";  // nil
        }
    };

    if (requestedCommands.empty()) {
        for (const auto& cmd : allCommands) {
            processEntry(cmd.name, &cmd);
        }
    } else {
        for (const auto& reqName : requestedCommands) {
            auto it = std::find_if(allCommands.begin(), allCommands.end(),
                [&](const CommandInfo& info) { return ICaseCmp(info.name, reqName); });

            if (it != allCommands.end()) {
                processEntry(reqName, &(*it));
            } else {
                processEntry(reqName, nullptr);
            }
        }
    }

    return oss.str();

#else // RESP_V2: 使用数组模拟 map

    auto buildEntry = [&](const std::string& name, const std::string& docValue) {
        std::vector<std::string> pair;
        pair.push_back(RespBuilder::BulkString(name));
        pair.push_back(docValue);
        return RespBuilder::Array(pair);
    };

    std::vector<std::string> result;

    if (requestedCommands.empty()) {
        for (const auto& cmd : allCommands) {
            std::string doc = BuildCommandDocEntry_RESP2(cmd);
            result.push_back(buildEntry(cmd.name, doc));
        }
    } else {
        for (const auto& reqName : requestedCommands) {
            auto it = std::find_if(allCommands.begin(), allCommands.end(),
                [&](const CommandInfo& info) { return ICaseCmp(info.name, reqName); });

            std::string doc;
            if (it != allCommands.end()) {
                doc = BuildCommandDocEntry_RESP2(*it);
            } else {
                doc = "$-1\r\n"; // nil
            }
            result.push_back(buildEntry(reqName, doc));
        }
    }

    return RespBuilder::Array(result);

#endif // RESP_V2
}

// 构造单个命令的详情（原始 COMMAND 响应）—— 不变，始终是数组
inline std::string CommandResponseBuilder::BuildFullCommandDetail(const CommandInfo& cmd) {
    std::vector<std::string> fields;

    fields.push_back(RespBuilder::BulkString(cmd.name));           // name
    fields.push_back(RespBuilder::Integer(cmd.arity));             // arity
    {
        std::vector<std::string> flagItems;
        for (const auto& flag : cmd.flags) {
            flagItems.push_back(RespBuilder::BulkString(flag));
        }
        fields.push_back(RespBuilder::Array(flagItems));           // flags
    }
    fields.push_back(RespBuilder::Integer(cmd.first_key));         // first_key
    fields.push_back(RespBuilder::Integer(cmd.last_key));          // last_key
    fields.push_back(RespBuilder::Integer(cmd.key_step));          // key_step
    fields.push_back(RespBuilder::BulkString(""));                 // tips
    fields.push_back(RespBuilder::Integer(cmd.microseconds));      // microseconds
    {
        std::vector<std::string> categoryItems;
        categoryItems.push_back(RespBuilder::BulkString(cmd.category));
        fields.push_back(RespBuilder::Array(categoryItems));       // category
    }

    return RespBuilder::Array(fields);
}

// ==================== RESP2 实现：map 用数组表示 ====================

inline std::string CommandResponseBuilder::BuildCommandDocEntry_RESP2(const CommandInfo& cmd) {
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
    mapElements.push_back(RespBuilder::Array({})); // empty array

    // history（可选）
    if (!cmd.history.empty()) {
        std::vector<std::string> historyElements;
        for (const auto& entry : cmd.history) {
            std::vector<std::string> historyPair;
            historyPair.push_back(RespBuilder::BulkString("version"));
            historyPair.push_back(RespBuilder::BulkString(entry.version));
            historyPair.push_back(RespBuilder::BulkString("change"));
            historyPair.push_back(RespBuilder::BulkString(entry.change));
            historyElements.push_back(RespBuilder::Array(historyPair));
        }
        mapElements.push_back(RespBuilder::BulkString("history"));
        mapElements.push_back(RespBuilder::Array(historyElements));
    }

    // arguments（可选）
    if (!cmd.arguments.empty()) {
        std::vector<std::string> argElements;
        for (const auto& arg : cmd.arguments) {
            std::vector<std::string> argPair;
            argPair.push_back(RespBuilder::BulkString("name"));
            argPair.push_back(RespBuilder::BulkString(arg.name));
            argPair.push_back(RespBuilder::BulkString("type"));
            argPair.push_back(RespBuilder::BulkString(arg.type));
            argElements.push_back(RespBuilder::Array(argPair));
        }
        mapElements.push_back(RespBuilder::BulkString("arguments"));
        mapElements.push_back(RespBuilder::Array(argElements));
    }

    return RespBuilder::Array(mapElements);
}

// ==================== RESP3 实现：map 用 % 表示 ====================

#ifdef RESP_V3

inline std::string CommandResponseBuilder::BuildCommandDocEntry_RESP3(const CommandInfo& cmd) {
    std::ostringstream oss;

    // 计算字段数量
    int fieldCount = 5; // summary, since, group, complexity, doc_flags
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
    oss << "*" << 0 << "\r\n"; // empty array

    // history
    if (!cmd.history.empty()) {
        oss << RespBuilder::BulkString("history");
        oss << "*" << cmd.history.size() << "\r\n";
        for (const auto& entry : cmd.history) {
            oss << "%2\r\n";
            oss << RespBuilder::BulkString("version");
            oss << RespBuilder::BulkString(entry.version);
            oss << RespBuilder::BulkString("change");
            oss << RespBuilder::BulkString(entry.change);
        }
    }

    // arguments
    if (!cmd.arguments.empty()) {
        oss << RespBuilder::BulkString("arguments");
        oss << "*" << cmd.arguments.size() << "\r\n";
        for (const auto& arg : cmd.arguments) {
            oss << "%2\r\n";
            oss << RespBuilder::BulkString("name");
            oss << RespBuilder::BulkString(arg.name);
            oss << RespBuilder::BulkString("type");
            oss << RespBuilder::BulkString(arg.type);
        }
    }

    return oss.str();
}

#endif // RESP_V3

} // namespace Astra::proto
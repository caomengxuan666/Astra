#pragma once
#include <string>
#include <vector>

namespace Astra::proto {

struct HistoryEntry {
    std::string version;
    std::string change;
};

struct ArgumentEntry {
    std::string name;
    std::string type;
};

struct CommandInfo {
    std::string name;
    int arity;
    std::vector<std::string> flags;
    int first_key;
    int last_key;
    int key_step;
    long long microseconds;
    std::string category;

    // COMMAND DOCS 所需字段
    std::string summary;
    std::string since;
    std::string complexity;
    std::vector<std::string> doc_flags;

    std::vector<HistoryEntry> history;   // 可选
    std::vector<ArgumentEntry> arguments; // 可选
};

} // namespace Astra::proto
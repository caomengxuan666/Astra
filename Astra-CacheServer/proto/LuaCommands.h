// proto/LuaCommands.h
#pragma once
#include "ICommand.hpp"
#include "LuaExecutor.h"
#include <memory>

namespace Astra::proto {
    // EVAL 命令：直接执行 Lua 脚本
    class EvalCommand : public ICommand {
    public:
        explicit EvalCommand(std::shared_ptr<LuaExecutor> executor) : executor_(std::move(executor)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 参数格式：EVAL script numkeys key1 [key2 ...] arg1 [arg2 ...]
            if (argv.size() < 3) {
                return RespBuilder::Error("wrong number of arguments for 'EVAL'");
            }

            // 解析脚本和参数数量
            std::string script = argv[1];
            int num_keys;
            try {
                num_keys = std::stoi(argv[2]);
            } catch (...) {
                return RespBuilder::Error("invalid numkeys (must be integer)");
            }

            // 校验参数数量合法性
            if (num_keys < 0 || static_cast<size_t>(num_keys) > argv.size() - 3) {
                return RespBuilder::Error("numkeys out of range");
            }

            // 提取 KEYS 和 ARGV（从 argv[3] 开始）
            std::vector<std::string> args(argv.begin() + 3, argv.end());
            return executor_->Execute(script, num_keys, args);
        }

    private:
        std::shared_ptr<LuaExecutor> executor_;
    };

    // EVALSHA 命令：通过 SHA1 执行缓存的脚本
    class EvalShaCommand : public ICommand {
    public:
        explicit EvalShaCommand(std::shared_ptr<LuaExecutor> executor) : executor_(std::move(executor)) {}

        std::string Execute(const std::vector<std::string> &argv) override {
            // 参数格式：EVALSHA sha1 numkeys key1 [key2 ...] arg1 [arg2 ...]
            if (argv.size() < 3) {
                return RespBuilder::Error("wrong number of arguments for 'EVALSHA'");
            }

            std::string sha1 = argv[1];
            int num_keys;
            try {
                num_keys = std::stoi(argv[2]);
            } catch (...) {
                return RespBuilder::Error("invalid numkeys (must be integer)");
            }

            if (num_keys < 0 || static_cast<size_t>(num_keys) > argv.size() - 3) {
                return RespBuilder::Error("numkeys out of range");
            }

            std::vector<std::string> args(argv.begin() + 3, argv.end());
            return executor_->ExecuteCached(sha1, num_keys, args);
        }

    private:
        std::shared_ptr<LuaExecutor> executor_;
    };
}// namespace Astra::proto
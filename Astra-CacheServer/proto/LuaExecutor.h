// proto/LuaExecutor.h
#pragma once

#include "ICommand.hpp"// Include ICommand
#include "SHA1.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "datastructures/lru_cache.hpp"
#include "logger.hpp"
#include "resp_builder.hpp"
#include "sol/sol.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace Astra::proto {

    using CachePtr = std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>>;

    class LuaExecutor {
    public:
        explicit LuaExecutor(CachePtr cache);
        ~LuaExecutor() = default;

        // Delete copy constructor and assignment operator
        LuaExecutor(const LuaExecutor &) = delete;
        LuaExecutor &operator=(const LuaExecutor &) = delete;

        std::string Execute(const std::string &script, int num_keys, const std::vector<std::string> &args);
        std::string CacheScript(const std::string &script);
        std::string ExecuteCached(const std::string &sha1, int num_keys, const std::vector<std::string> &args);

        // --- New method for registering ICommand handlers ---
        // Method to register an ICommand for a specific Redis command name
        void RegisterCommandHandler(const std::string &redis_cmd_name, std::shared_ptr<ICommand> handler);
        // --- End of new method ---

    private:
        void InitLuaVM();
        std::string ConvertToResp(const sol::object &result);

        // --- Modified members ---
        // Map to hold command handlers (e.g., "get" -> GetCommand instance)
        std::unordered_map<std::string, std::shared_ptr<ICommand>> command_handlers_;
        // --- End of modified members ---

        CachePtr cache_;
        sol::state lua_;
        std::unordered_map<std::string, std::string> cached_scripts_;
    };

}// namespace Astra::proto

#include <algorithm>
#include <cctype>  // for std::tolower
#include <charconv>// for std::from_chars
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Astra::proto {

    inline LuaExecutor::LuaExecutor(CachePtr cache) : cache_(std::move(cache)) {
        InitLuaVM();
    }

    inline void LuaExecutor::InitLuaVM() {
        lua_.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

        // --- Register ICommand handlers for Redis commands ---
        // Note: These are typically registered *after* LuaExecutor is created,
        // usually in CommandFactory. Registering them here would require
        // including the specific command headers and creating instances,
        // which is less flexible. We leave the registration to the factory.
        // Example (if done here, needs includes):
        /*
        RegisterCommandHandler("get", std::make_shared<GetCommand>(cache_));
        RegisterCommandHandler("set", std::make_shared<SetCommand>(cache_));
        RegisterCommandHandler("del", std::make_shared<DelCommand>(cache_));
        RegisterCommandHandler("exists", std::make_shared<ExistsCommand>(cache_));
        */

        // Create the 'redis' table in the Lua environment
        sol::table redis_table = lua_.create_table();

        // Register the 'redis.call' function within the 'redis' table
        redis_table.set_function("call", [this](sol::variadic_args va) -> sol::object {
            if (va.size() == 0) {
                throw std::runtime_error("redis.call() requires at least one argument (the command)");
            }

            // Get the command name (first argument)
            sol::optional<std::string> cmd_opt = va[0].as<sol::optional<std::string>>();
            if (!cmd_opt.has_value()) {
                throw std::runtime_error("First argument to redis.call() must be a string (the command)");
            }
            std::string command = cmd_opt.value();
            // Convert command name to lowercase for consistency
            std::transform(command.begin(), command.end(), command.begin(), ::tolower);

            // Find the corresponding ICommand handler
            auto it = command_handlers_.find(command);
            if (it == command_handlers_.end()) {
                throw std::runtime_error("Unsupported command in redis.call(): " + command);
            }

            // --- Convert sol::variadic_args to std::vector<std::string> ---
            std::vector<std::string> argv;
            argv.reserve(va.size());
            for (const auto &arg: va) {
                sol::optional<std::string> str_opt = arg.as<sol::optional<std::string>>();
                if (str_opt.has_value()) {
                    argv.push_back(str_opt.value());
                } else {
                    // Handle non-string arguments if necessary (e.g., convert to string)
                    // For simplicity, we try to get a string representation.
                    // This might not be perfect for all types, but works for basic ones.
                    argv.push_back(arg.as<std::string>());
                }
            }
            // --- End of conversion ---

            // --- Execute the command via ICommand ---
            std::string resp_result_str = it->second->Execute(argv);
            // --- End of execution ---

            // --- Parse the RESP string result and convert it back to a sol::object ---
            // This is a simplified parser for common cases returned by your ICommand classes.
            if (resp_result_str.empty()) {
                throw std::runtime_error("Empty response from command: " + command);
            }

            char prefix = resp_result_str[0];
            switch (prefix) {
                case '+': {// Simple String (e.g., "+OK\r\n")
                    size_t end_pos = resp_result_str.find("\r\n");
                    if (end_pos != std::string::npos) {
                        std::string value = resp_result_str.substr(1, end_pos - 1);
                        // Special case for {ok="OK"} expected by Lua scripts for SET/other commands
                        // Check if the simple string is "OK". You might need to refine this logic
                        // based on which commands should return {ok="OK"}.
                        // Standard Redis Lua returns {ok="OK"} for commands like SET.
                        if (value == "OK") {
                            sol::table result_table = lua_.create_table();
                            result_table["ok"] = "OK";
                            return sol::make_object(lua_, result_table);
                        }
                        // For other simple strings, return as Lua string
                        return sol::make_object(lua_, value);
                    }
                    break;// Malformed, fall through to error
                }
                case '-': {// Error (e.g., "-ERR some message\r\n")
                    size_t end_pos = resp_result_str.find("\r\n");
                    if (end_pos != std::string::npos) {
                        std::string error_msg = resp_result_str.substr(1, end_pos - 1);
                        throw std::runtime_error(error_msg);
                    }
                    break;// Malformed, fall through to error
                }
                case ':': {// Integer (e.g., ":123\r\n")
                    size_t end_pos = resp_result_str.find("\r\n");
                    if (end_pos != std::string::npos) {
                        std::string number_str = resp_result_str.substr(1, end_pos - 1);
                        long long value;
                        // Use std::from_chars for safer integer parsing
                        auto [ptr, ec] = std::from_chars(number_str.data(), number_str.data() + number_str.size(), value);
                        if (ec == std::errc()) {
                            return sol::make_object(lua_, value);
                        }
                        // If parsing fails, fall through to error
                    }
                    break;// Malformed, fall through to error
                }
                case '$': {// Bulk String or Nil (e.g., "$5\r\nhello\r\n" or "$-1\r\n")
                    if (resp_result_str.substr(0, 3) == "$-1") {
                        return sol::make_object(lua_, sol::nil);// Nil
                    }
                    // Parse Bulk String: "$len\r\n<data>\r\n"
                    size_t first_cr = resp_result_str.find('\r');
                    if (first_cr != std::string::npos && first_cr > 1) {
                        std::string len_str = resp_result_str.substr(1, first_cr - 1);
                        size_t len;
                        auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), len);
                        if (ec == std::errc()) {
                            size_t data_start = first_cr + 2;// Skip \r\n after length
                            if (data_start + len <= resp_result_str.size()) {
                                std::string value = resp_result_str.substr(data_start, len);
                                return sol::make_object(lua_, value);
                            }
                        }
                    }
                    // If parsing fails, fall through to error
                    break;// Malformed, fall through to error
                }
                case '*': {// Array - Parsing arrays back to sol::table
                    // Parse RESP Array header: "*<count>\r\n"
                    size_t cr_pos = resp_result_str.find('\r', 1);// Find first \r after '*'
                    if (cr_pos == std::string::npos) {
                        break;// Malformed
                    }
                    std::string count_str = resp_result_str.substr(1, cr_pos - 1);
                    long long element_count;
                    auto [ptr, ec] = std::from_chars(count_str.data(), count_str.data() + count_str.size(), element_count);
                    if (ec != std::errc() || element_count < 0) {
                        break;// Invalid count
                    }

                    if (element_count == 0) {
                        // Return empty Lua table for *0\r\n
                        return sol::make_object(lua_, lua_.create_table());
                    }

                    // For non-empty arrays, we need to parse elements.
                    // This is a simplified parser assuming elements are Bulk Strings or Integers.
                    // It's complex to do perfectly, but we can handle common cases from PubSubCommand.
                    // This part is tricky and a full implementation is substantial.
                    // As a quick fix for PubSub commands returning simple lists, we can try to parse them.
                    // However, a robust solution requires a proper RESP parser.

                    // Let's implement a basic parser for the specific arrays returned by PubSubCommand
                    sol::table result_table = lua_.create_table();
                    size_t pos = cr_pos + 2;// Start after \r\n of the header
                    for (long long i = 1; i <= element_count; ++i) {
                        if (pos >= resp_result_str.size()) {
                            throw std::runtime_error("Premature end of RESP array data");
                        }
                        char elem_prefix = resp_result_str[pos];
                        size_t elem_cr_pos = resp_result_str.find('\r', pos + 1);
                        if (elem_cr_pos == std::string::npos) {
                            throw std::runtime_error("Malformed RESP element in array");
                        }
                        std::string elem_len_or_val_str = resp_result_str.substr(pos + 1, elem_cr_pos - (pos + 1));

                        if (elem_prefix == '$') {// Bulk String
                            long long str_len;
                            auto [elem_ptr, elem_ec] = std::from_chars(elem_len_or_val_str.data(), elem_len_or_val_str.data() + elem_len_or_val_str.size(), str_len);
                            if (elem_ec != std::errc()) {
                                throw std::runtime_error("Invalid bulk string length in RESP array");
                            }
                            if (str_len == -1) {
                                // Nil Bulk String
                                result_table[i] = sol::nil;
                                pos = elem_cr_pos + 2;// Skip \r\n after header
                            } else {
                                pos = elem_cr_pos + 2;// Skip \r\n after header
                                if (pos + str_len > resp_result_str.size()) {
                                    throw std::runtime_error("Bulk string data exceeds response size");
                                }
                                std::string elem_value = resp_result_str.substr(pos, str_len);
                                result_table[i] = elem_value;
                                pos += str_len + 2;// Skip data and trailing \r\n
                            }
                        } else if (elem_prefix == ':') {// Integer
                            long long elem_value;
                            auto [elem_ptr, elem_ec] = std::from_chars(elem_len_or_val_str.data(), elem_len_or_val_str.data() + elem_len_or_val_str.size(), elem_value);
                            if (elem_ec != std::errc()) {
                                throw std::runtime_error("Invalid integer in RESP array");
                            }
                            result_table[i] = elem_value;
                            pos = elem_cr_pos + 2;// Skip \r\n after value
                        } else {
                            // Unsupported element type within array for this simple parser
                            // You might need to extend this for other types like nested arrays (*), maps (%), etc.
                            throw std::runtime_error("Unsupported RESP element type '" + std::string(1, elem_prefix) + "' in array response from command '" + command + "'");
                        }
                    }
                    return sol::make_object(lua_, result_table);
                    // break; // Not needed as we return
                }
                    // '%' for maps (RESP3) would also need handling if used.
            }

            // If we reach here, the response format was unexpected or parsing failed
            throw std::runtime_error("Failed to parse RESP response from command '" + command + "': " + resp_result_str);
            // --- End of parsing ---
        });

        // Set the 'redis' table in the global Lua state
        lua_["redis"] = redis_table;
    }

    // --- Implementation of the new RegisterCommandHandler method ---
    inline void LuaExecutor::RegisterCommandHandler(const std::string &redis_cmd_name, std::shared_ptr<ICommand> handler) {
        if (handler) {
            std::string lower_name = redis_cmd_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            command_handlers_[lower_name] = std::move(handler);
        }
    }
    // --- End of implementation ---

    inline std::string LuaExecutor::Execute(const std::string &script, int num_keys, const std::vector<std::string> &args) {
        try {
            // --- 新增：缓存脚本 ---
            // 为了使 EVALSHA 能工作，我们需要在执行脚本时将其缓存。
            // 这会计算 SHA1 并存储脚本，如果已存在则会覆盖（SHA1 不变）。
            std::string script_sha = CacheScript(script);// 调用 CacheScript
            // --- 新增结束 ---

            // Set up KEYS and ARGV tables in the Lua environment
            sol::table keys = lua_.create_table();
            sol::table argv = lua_.create_table();
            for (int i = 0; i < num_keys && i < static_cast<int>(args.size()); ++i) {
                keys[i + 1] = args[i];// Lua indices start at 1
            }
            for (size_t i = num_keys; i < args.size(); ++i) {
                argv[i - num_keys + 1] = args[i];// Lua indices start at 1
            }
            lua_["KEYS"] = keys;
            lua_["ARGV"] = argv;

            // Execute the script
            sol::protected_function_result res = lua_.safe_script(script, sol::script_pass_on_error);
            if (!res.valid()) {
                sol::error err = res;
                return std::string("-ERR Lua error: ") + err.what() + "\r\n";
            }
            sol::object result = res;
            return ConvertToResp(result);// Convert final script result to RESP
        } catch (const sol::error &e) {
            return std::string("-ERR Lua error: ") + e.what() + "\r\n";
        } catch (const std::exception &e) {
            return RespBuilder::Error("Execution failed: " + std::string(e.what()));
        }
    }
    inline std::string LuaExecutor::CacheScript(const std::string &script) {
        sha1::SHA1 sha1;
        sha1.processBytes(script.data(), script.size());
        sha1::SHA1::digest8_t digest;
        sha1.getDigestBytes(digest);

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 20; ++i) {
            ss << std::setw(2) << static_cast<int>(digest[i]);
        }
        std::string sha1_str = ss.str();
        ZEN_LOG_TRACE("Cached script with SHA1: {}", sha1_str);// <--- 添加日志
        cached_scripts_[sha1_str] = script;
        return sha1_str;
    }

    inline std::string LuaExecutor::ExecuteCached(const std::string &sha1, int num_keys, const std::vector<std::string> &args) {
        auto it = cached_scripts_.find(sha1);
        if (it == cached_scripts_.end()) {
            return RespBuilder::Error("NOSCRIPT No matching script. Use EVAL to load.");
        }
        return Execute(it->second, num_keys, args);
    }

    // Converts a Lua object (the final result of the script) to a RESP string
    inline std::string LuaExecutor::ConvertToResp(const sol::object &result) {
        // --- Handle Redis-style return tables {ok = ...} or {err = ...} ---
        // This is primarily for the script's final return value, not intermediate redis.call results
        // (which are handled inside the redis.call C++ function)
        if (result.is<sol::table>()) {
            sol::table t = result.as<sol::table>();
            sol::optional<std::string> ok_opt = t["ok"];
            if (ok_opt.has_value()) {
                // Return Simple String OK if {ok="OK"} is the final script return
                // Or Bulk String, depending on desired consistency. RespBuilder::BulkString used here.
                // Standard Redis returns Simple String for OK. You might want RespBuilder::SimpleString("+OK\r\n")
                // return std::string("+OK\r\n"); // Simple String (More standard)
                return RespBuilder::BulkString(ok_opt.value());// Bulk String (Current style)
            }
            sol::optional<std::string> err_opt = t["err"];
            if (err_opt.has_value()) {
                return RespBuilder::Error(err_opt.value());
            }
            // If it's a regular table, fall through to the general table handling logic below
        }
        // --- End of Redis-style table handling ---

        if (result.is<sol::nil_t>()) {
            return RespBuilder::Nil();
        } else if (result.is<bool>()) {
            return RespBuilder::Integer(result.as<bool>() ? 1 : 0);
        } else if (result.is<int64_t>()) {
            return RespBuilder::Integer(result.as<int64_t>());
        } else if (result.is<double>()) {// Handle floating point numbers
            double d = result.as<double>();
            // Check if it's actually an integer value
            if (d == std::floor(d) && d >= static_cast<double>(std::numeric_limits<int64_t>::min()) && d <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
                return RespBuilder::Integer(static_cast<int64_t>(d));
            } else {
                // Format as string with precision, then remove trailing zeros
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(10) << d;
                std::string s = oss.str();
                s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                s.erase(s.find_last_not_of('.') + 1, std::string::npos);
                return RespBuilder::BulkString(s);
            }
        } else if (result.is<std::string>()) {
            return RespBuilder::BulkString(result.as<std::string>());
        } else if (result.is<sol::table>()) {
            sol::table table = result.as<sol::table>();
            std::vector<std::pair<size_t, std::string>> elements;
            size_t max_index = 0;

            table.for_each([&](sol::object key_obj, sol::object) {
                if (key_obj.is<size_t>()) {
                    max_index = std::max(max_index, key_obj.as<size_t>());
                }
            });

            for (size_t i = 1; i <= max_index; ++i) {
                sol::object elem = table[i];
                elements.emplace_back(i, ConvertToResp(elem));// Recursive call
            }

            std::sort(elements.begin(), elements.end());

            std::vector<std::string> values;
            for (const auto &elem: elements) {
                values.push_back(elem.second);
            }

            return RespBuilder::Array(values);
        } else {
            std::string type_str = sol::type_name(lua_, result.get_type());
            return RespBuilder::Error("Unsupported Lua type: " + type_str);
        }
    }

}// namespace Astra::proto
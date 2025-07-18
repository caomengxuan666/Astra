#pragma once

#include <string>

// 判断字符串是否为合法 UTF-8
inline bool is_valid_utf8(const std::string &str) {
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(str.c_str());
    while (*bytes) {
        if ((*bytes & 0x80) == 0x00) {
            // 1-byte (ASCII)
            bytes += 1;
        } else if ((*bytes & 0xE0) == 0xC0) {
            // 2-byte
            if ((bytes[1] & 0xC0) == 0x80) {
                bytes += 2;
            } else {
                return false;
            }
        } else if ((*bytes & 0xF0) == 0xE0) {
            // 3-byte
            if ((bytes[1] & 0xC0) == 0x80 && (bytes[2] & 0xC0) == 0x80) {
                bytes += 3;
            } else {
                return false;
            }
        } else if ((*bytes & 0xF8) == 0xF0) {
            // 4-byte
            if ((bytes[1] & 0xC0) == 0x80 &&
                (bytes[2] & 0xC0) == 0x80 &&
                (bytes[3] & 0xC0) == 0x80) {
                bytes += 4;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32

#include <windows.h>

// Windows 下将 ANSI 字符串转为 UTF-8
inline std::string AnsiToUtf8(const std::string &ansi) {
    if (ansi.empty()) return "";

    int wide_len = MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), -1, nullptr, 0);
    std::wstring wide(wide_len, 0);
    MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), -1, &wide[0], wide_len);

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8_len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8_len, nullptr, nullptr);

    return utf8;
}

// 确保字符串是 UTF-8 格式（如果不是则尝试转换）
inline std::string EnsureUTF8(const std::string &input) {
    if (is_valid_utf8(input)) {
        return input;
    }

    return AnsiToUtf8(input);
}

#else

// 非 Windows 下默认假设输入是 UTF-8
inline std::string EnsureUTF8(const std::string &input) {
    return input; // 假设非 Windows 环境默认使用 UTF-8
}

#endif
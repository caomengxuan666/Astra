#pragma once

#define ZEN_UNUSED(x) (void) (x)
#define ZEN_ASSERT(cond, msg) assert((cond) && msg)
#define ZEN_LIKELY(x) __builtin_expect(!!(x), 1)
#define ZEN_UNLIKELY(x) __builtin_expect(!!(x), 0)

// Windows 平台导出/导入宏
#ifdef _WIN32
    #ifdef ZEN_DLL_EXPORT
        #define ZEN_API __declspec(dllexport)
    #else
        #define ZEN_API __declspec(dllimport)
    #endif
    #define ZEN_CALL __stdcall
#else
    #define ZEN_API
    #define ZEN_CALL
#endif
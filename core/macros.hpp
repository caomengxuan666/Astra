#pragma once


#define ZEN_UNUSED(x) (void) (x)
#define ZEN_ASSERT(cond, msg) assert((cond) && msg)
#define ZEN_LIKELY(x) __builtin_expect(!!(x), 1)
#define ZEN_UNLIKELY(x) __builtin_expect(!!(x), 0)

// 库导出
#ifdef ZEN_DLL_EXPORT
#define ZEN_API __declspec(dllexport)
#else
#define ZEN_API __declspec(dllimport)
#endif

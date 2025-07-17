#pragma once

// RESP 类型枚举
typedef enum {
    RESP_SIMPLE_STRING_C = 0,
    RESP_BULK_STRING_C = 1,
    RESP_INTEGER_C = 2,
    RESP_ARRAY_C = 3,
    RESP_ERROR_C = 4,
    RESP_NIL_C = 5// 正确的空值类型枚举值
} RespType_C;

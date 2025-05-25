#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Astra {

    using i8 = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;

    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    using f32 = float;
    using f64 = double;

    using String = std::string;
    template<typename T>
    using Vector = std::vector<T>;
    template<typename T>
    using UniquePtr = std::unique_ptr<T>;
    template<typename T>
    using SharedPtr = std::shared_ptr<T>;

}// namespace Astra
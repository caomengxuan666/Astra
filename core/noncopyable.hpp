#pragma once

namespace Astra {

    class NonCopyable {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;

        NonCopyable(const NonCopyable &) = delete;
        NonCopyable &operator=(const NonCopyable &) = delete;
    };

}// namespace Astra
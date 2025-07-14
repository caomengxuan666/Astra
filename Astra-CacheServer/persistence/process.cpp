#include "process.hpp"

#include <windows.h>

std::string get_pid_str() {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    return std::to_string(static_cast<unsigned long>(pid));
#else
    pid_t pid = getpid();
    return std::to_string(static_cast<int>(pid));
#endif
}
#pragma once
// Standard C/C++ headers
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <chrono>
#include <climits>
#include <clocale>
#include <cmath>
#include <codecvt>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>

// Standard C++ Library headers
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <deque>
#include <exception>
#include <filesystem>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>

// Project core headers
#include "core/macros.hpp"
#include "core/noncopyable.hpp"
#include "core/types.hpp"

// Project utils headers
#define ZEN_DEBUG
#include "utils/logger.hpp"
#include "utils/scope_guard.hpp"


// Project datastructures headers
#include "datastructures/double_buffer_ring.hpp"
#include "datastructures/lock_free_buffer_base.hpp"
#include "datastructures/lockfree_queue.hpp"
#include "datastructures/lru_cache.hpp"
#include "datastructures/object_pool.hpp"
#include "datastructures/ring_buffer.hpp"

//project concurrent headers
#include "concurrent/task_flow.hpp"
#include "concurrent/task_queue.hpp"


// Third party libraries
#define FMT_HEADER_ONLY
#include "third-party.h"

// Platform specific
#ifdef _WIN32
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#undef ERROR

#else//linux
#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#endif
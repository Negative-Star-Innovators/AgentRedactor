#pragma once

#include <string>
#include "platform_compat.h"

// Runtime-gated logging (controlled by the app's logging options, not
// compile-time flags):
//   LOG / LOGF     - written only when "Enable logging" is on.
//   LOG_TRAFFIC    - HTTP boundary log, written only when "Enable logging" is on.
//   LOG_LIFECYCLE  - always written; reserved for app-lifecycle diagnostics
//                    (startup, shutdown, model load, proxy listen errors) that
//                    must never contain user request/response content.
#define LOG(msg) ::AgentRedactor::Utils::LogMessage(msg)
#define LOGF(fmt, ...) ::AgentRedactor::Utils::LogMessage(::AgentRedactor::Utils::FormatString(fmt, __VA_ARGS__))
#define LOG_LIFECYCLE(msg) ::AgentRedactor::Utils::LogLifecycleMessage(msg)
#define LOGF_LIFECYCLE(fmt, ...) ::AgentRedactor::Utils::LogLifecycleMessage(::AgentRedactor::Utils::FormatString(fmt, __VA_ARGS__))
#define LOG_TRAFFIC(direction, data) ::AgentRedactor::Utils::LogTrafficMessage(direction, data)

namespace AgentRedactor {
namespace Utils {
    template<typename... Args>
    std::wstring FormatString(const wchar_t* fmt, Args... args) {
#ifdef _WIN32
        int size = _snwprintf(nullptr, 0, fmt, args...);
        if (size <= 0) return L"";
        std::wstring result(size, L'\0');
        _snwprintf(result.data(), result.size() + 1, fmt, args...);
        return result;
#else
        // glibc swprintf reports no would-be size on truncation; grow instead.
        for (size_t size = 256;; size *= 2) {
            std::wstring result(size, L'\0');
            int n = swprintf(result.data(), result.size(), fmt, args...);
            if (n >= 0 && static_cast<size_t>(n) < result.size()) {
                result.resize(static_cast<size_t>(n));
                return result;
            }
        }
#endif
    }
}
}

#include "platform_compat.h"

#ifndef _WIN32

#include "utils.h"

const wchar_t* _wgetenv(const wchar_t* name) {
    static thread_local std::wstring value;
    const char* narrow = std::getenv(AgentRedactor::Utils::WideToUtf8(name).c_str());
    if (!narrow) return nullptr;
    value = AgentRedactor::Utils::Utf8ToWide(narrow);
    return value.c_str();
}

#endif

#pragma once

// MSVC/POSIX compatibility shims for the OS-agnostic core. On Windows this
// just pulls in the Win32 headers the core was written against; elsewhere it
// provides the small set of MSVC-isms the core uses (_snwprintf, _wtof,
// localtime_s with MSVC argument order, _wgetenv, _stricmp, OutputDebugStringW)
// plus the Winsock type/constant names that leak into public core interfaces.

#ifdef _WIN32

// winsock2.h must precede windows.h (otherwise the legacy winsock.h wins and
// SOCKET stays undeclared in core headers like http_server.h, which no longer
// rely on the includer's pch.h ordering).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// POSIX name for the address-length type Winsock exposes as int.
typedef int ar_socklen_t;

#else // POSIX

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Winsock names used by http_server's public interface.
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define closesocket close
#define WSAGetLastError() errno

typedef socklen_t ar_socklen_t;
typedef unsigned char BYTE;

// MSVC returns a negative value on truncation; swprintf has the same contract.
#define _snwprintf swprintf

inline double _wtof(const wchar_t* s) { return std::wcstod(s, nullptr); }

// MSVC argument order (out, in) — the inverse of POSIX localtime_r.
inline int localtime_s(struct tm* out, const std::time_t* t) {
    return localtime_r(t, out) ? 0 : 1;
}

inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }

inline void OutputDebugStringW(const wchar_t*) {}

// Narrow env lookup converted to wide. Only ASCII/UTF-8 variable names are
// used in practice (AGENTREDACTOR_CONFIG_DIR). The returned pointer is valid
// until the next call on the same thread.
const wchar_t* _wgetenv(const wchar_t* name);

#endif // _WIN32

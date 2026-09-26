// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "app/CrashLog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace os::app {

namespace {

#if defined(_WIN32)
constexpr const char* kNewline = "\r\n"; // the log file is written in text mode
wchar_t g_logPath[2048] = {};
#else
constexpr const char* kNewline = "\n";
char g_logPath[4096] = {};
#endif
bool g_simulated = false;

// Opens the log file anew (the logger's own handle may be mid-write in the
// crashing thread) and appends the line; no allocation.
void writeCrashLine(const char* line)
{
    std::fputs(line, stderr);
    std::fflush(stderr);
    const auto size = std::strlen(line);
#if defined(_WIN32)
    if (!g_logPath[0])
        return;
    const HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line, DWORD(size), &written, nullptr);
    CloseHandle(file);
#else
    if (!g_logPath[0])
        return;
    const int fd = ::open(g_logPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0)
        return;
    const auto ignored = ::write(fd, line, size);
    (void)ignored;
    ::close(fd);
#endif
}

[[noreturn]] void onTerminate()
{
    const char* what = "no exception";
    std::string text; // terminate is not a memory-corruption path; allocating is fine
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            text = std::string("uncaught exception: ") + ex.what();
        } catch (...) {
            text = "uncaught exception of unknown type";
        }
        what = text.c_str();
    }
    char line[768];
    std::snprintf(line, sizeof line, "[error] APP: OpenShape closed unexpectedly: std::terminate (%s)%s", what, kNewline);
    writeCrashLine(line);
    std::abort();
}

#if defined(_WIN32)
const char* exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case 0xC0000409: return "stack buffer overrun / fail-fast";
    case 0xE06D7363: return "C++ exception (MSVC)";
    default: return "unhandled exception";
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    const DWORD code = record ? record->ExceptionCode : 0;
    const void* address = record ? record->ExceptionAddress : nullptr;
    // Module + offset: map it back with addr2line/gdb on that binary.
    char module[MAX_PATH] = "?";
    unsigned long long offset = reinterpret_cast<unsigned long long>(address);
    HMODULE handle = nullptr;
    if (address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCSTR>(address), &handle)) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(handle, path, MAX_PATH) > 0) {
            const char* name = std::strrchr(path, '\\');
            std::snprintf(module, sizeof module, "%s", name ? name + 1 : path);
        }
        offset -= reinterpret_cast<unsigned long long>(handle);
    }
    char line[512];
    std::snprintf(line, sizeof line, "[error] APP: OpenShape closed unexpectedly: exception 0x%08lX (%s) at %s+0x%llX%s%s",
                  static_cast<unsigned long>(code), exceptionName(code), module, offset,
                  g_simulated ? " (simulated with --simulate-crash)" : "", kNewline);
    writeCrashLine(line);
    // A simulated crash ends here, without the Windows crash dialog; a real
    // one continues to Windows Error Reporting as usual.
    return g_simulated ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

void installCrashLogging(const QString& logFile)
{
#if defined(_WIN32)
    const std::wstring path = logFile.toStdWString();
    if (path.size() < std::size(g_logPath))
        std::memcpy(g_logPath, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    SetUnhandledExceptionFilter(onUnhandledException);
#else
    const QByteArray path = logFile.toLocal8Bit();
    if (std::size_t(path.size()) < sizeof g_logPath)
        std::memcpy(g_logPath, path.constData(), std::size_t(path.size()) + 1);
#endif
    std::set_terminate(onTerminate);
}

void simulateCrash()
{
    g_simulated = true;
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    std::terminate();
}

} // namespace os::app

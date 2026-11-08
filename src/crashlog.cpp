#include "crashlog.h"

#include <windows.h>

#include "util.h"

namespace crashlog {

namespace {

wchar_t g_logPath[MAX_PATH * 2] = {};

bool AppendLine(const wchar_t* path, const wchar_t* line) {
    if (!path || !path[0] || !line || !line[0]) {
        return false;
    }
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const int wideLength = static_cast<int>(wcslen(line));
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, wideLength, nullptr, 0, nullptr, nullptr);
    bool ok = false;
    if (bytes > 0) {
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line, wideLength, utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) &&
             written == static_cast<DWORD>(utf8.size());
    }
    CloseHandle(file);
    return ok;
}

LONG WINAPI Filter(EXCEPTION_POINTERS* info) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    wchar_t line[512];
    wsprintfW(line, L"%04u-%02u-%02u %02u:%02u:%02u pid=%lu exception=0x%08lX address=%p\r\n",
              time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
              GetCurrentProcessId(), code, address);
    AppendLine(g_logPath, line);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

bool WriteLogLine(const std::wstring& path, const std::wstring& line) {
    return AppendLine(path.c_str(), line.c_str());
}

void Install() {
    const std::wstring path = DataDirectory() + L"\\logs\\crash.log";
    EnsureDirectory(DirectoryOf(path));
    wcsncpy_s(g_logPath, path.c_str(), _TRUNCATE);
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Filter);
}

}  // namespace crashlog

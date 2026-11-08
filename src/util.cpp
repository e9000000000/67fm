#include "util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring GetLastErrorText(DWORD error) {
    if (error == 0) {
        return L"Unknown error";
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result;
    if (length && buffer) {
        result.assign(buffer, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
            result.pop_back();
        }
    } else {
        result = L"Error " + std::to_wstring(error);
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return result;
}

std::wstring GetErrorText(const std::wstring& context, DWORD error) {
    return context + L": " + GetLastErrorText(error);
}

std::wstring ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return L".";
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return DirectoryOf(buffer);
}

std::wstring DataDirectory() {
    const std::wstring exeDir = ExeDirectory();
    if (FileExists(exeDir + L"\\67fm.ini")) {
        return exeDir + L"\\state";
    }
    PWSTR appData = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        result = std::wstring(appData) + L"\\67fm";
    } else {
        result = exeDir + L"\\state";
    }
    if (appData) {
        CoTaskMemFree(appData);
    }
    return result;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring FullPath(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return path;
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed) {
        return path;
    }
    buffer.resize(written);
    return buffer;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    if (pos == 2 && path.size() >= 3 && path[1] == L':') {
        return path.substr(0, 3);
    }
    return path.substr(0, pos);
}

std::wstring CompactPathLabel(const std::wstring& path) {
    std::wstring label = FileNameOf(path);
    const std::wstring directory = DirectoryOf(path);
    if (directory != L"." && directory.size() > 3) {
        const std::wstring parent = FileNameOf(directory);
        if (!parent.empty()) {
            label += L"  \u2014  ";
            label += parent;
        }
    }
    return label;
}

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buffer[64];
    if (bytes < 1024) {
        swprintf_s(buffer, L"%llu B", bytes);
    } else if (bytes < 1024ull * 1024ull) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        swprintf_s(buffer, L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

std::wstring Trim(const std::wstring& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && iswspace(text[start])) {
        ++start;
    }
    while (end > start && iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(start, end - start);
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

UINT DpiOfWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) {
        const UINT dpi = fn(hwnd);
        if (dpi) {
            return dpi;
        }
    }
    return 96;
}

namespace {

using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);

void LoadDarkModeApis() {
    static bool loaded = false;
    static HMODULE uxtheme = nullptr;
    if (loaded) {
        return;
    }
    loaded = true;
    uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (!uxtheme) {
        return;
    }
    auto setPreferred = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    if (setPreferred) {
        // ForceDark. This is the undocumented dark-mode switch used by Explorer.
        setPreferred(2);
    }
}

AllowDarkModeForWindowFn GetAllowDarkMode() {
    static AllowDarkModeForWindowFn fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
        if (uxtheme) {
            fn = reinterpret_cast<AllowDarkModeForWindowFn>(
                GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
        }
    }
    return fn;
}

}  // namespace

void EnableDarkModeForApp() {
    LoadDarkModeApis();
}

void AllowDarkModeForWindow(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    if (auto allow = GetAllowDarkMode()) {
        allow(hwnd, TRUE);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

namespace {

BOOL CALLBACK DarkenDialogChild(HWND child, LPARAM) {
    AllowDarkModeForWindow(child);
    return TRUE;
}

}  // namespace

void ApplyDarkDialogTheme(HWND dialog) {
    if (!dialog) {
        return;
    }
    AllowDarkModeForWindow(dialog);
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(dialog, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(dialog, 19, &dark, sizeof(dark));
    }
    EnumChildWindows(dialog, DarkenDialogChild, 0);
}

void ShowError(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), L"67fm", MB_OK | MB_ICONERROR);
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD got = 0;
        if (!ReadFile(file, out.data() + offset, static_cast<DWORD>(std::min<size_t>(out.size() - offset, 1u << 20)), &got, nullptr) || got == 0) {
            CloseHandle(file);
            return false;
        }
        offset += got;
    }
    CloseHandle(file);
    return true;
}

bool WriteTextFileAtomic(const std::wstring& path, std::string_view data, std::wstring& error) {
    const std::wstring dir = DirectoryOf(path);
    EnsureDirectory(dir);
    wchar_t temp[MAX_PATH] = {};
    if (GetTempFileNameW(dir.c_str(), L"67s", 0, temp) == 0) {
        error = GetErrorText(L"Cannot create temporary state file", GetLastError());
        return false;
    }
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(temp);
        error = GetErrorText(L"Cannot write state file", GetLastError());
        return false;
    }
    bool ok = true;
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        if (!WriteFile(file, data.data() + offset, want, &written, nullptr) || written == 0) {
            error = GetErrorText(L"Cannot write state file", GetLastError());
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp);
        return false;
    }
    if (!MoveFileExW(temp, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetErrorText(L"Cannot replace state file", GetLastError());
        DeleteFileW(temp);
        return false;
    }
    return true;
}

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) {
        return name;
    }
    if (name.empty()) {
        return base;
    }
    const wchar_t last = base.back();
    if (last == L'\\' || last == L'/') {
        return base + name;
    }
    return base + L"\\" + name;
}

std::wstring ParentDirectory(const std::wstring& path) {
    std::wstring clean = path;
    while (clean.size() > 3 && (clean.back() == L'\\' || clean.back() == L'/')) {
        clean.pop_back();
    }
    if (clean.size() <= 3 && clean.size() >= 2 && clean[1] == L':') {
        return clean;
    }
    const size_t pos = clean.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return clean;
    }
    if (pos == 2 && clean.size() >= 3 && clean[1] == L':') {
        return clean.substr(0, 3);
    }
    return clean.substr(0, pos);
}

namespace {

std::wstring AddCopySuffix(const std::wstring& name, int number, bool directory) {
    if (directory) {
        return name + L" (" + std::to_wstring(number) + L")";
    }
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return name + L" (" + std::to_wstring(number) + L")";
    }
    return name.substr(0, dot) + L" (" + std::to_wstring(number) + L")" + name.substr(dot);
}

}  // namespace

std::wstring UniquePath(const std::wstring& directory, const std::wstring& name,
                        bool wantDirectory) {
    const std::wstring candidate = JoinPath(directory, name);
    const DWORD attrs = GetFileAttributesW(candidate.c_str());
    const bool exists = attrs != INVALID_FILE_ATTRIBUTES;
    if (!exists) {
        return candidate;
    }
    for (int i = 2; i < 10000; ++i) {
        const std::wstring next = JoinPath(directory, AddCopySuffix(name, i, wantDirectory));
        if (GetFileAttributesW(next.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return next;
        }
    }
    return JoinPath(directory, name + L" (" + std::to_wstring(GetTickCount64()) + L")");
}

std::wstring SanitizeFileName(const std::wstring& name) {
    std::wstring result;
    result.reserve(name.size());
    for (wchar_t c : name) {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) {
            result.push_back(L'_');
        } else {
            result.push_back(c);
        }
    }
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) {
        result.pop_back();
    }
    if (result.empty()) {
        result = L"download";
    }
    static const wchar_t* reserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
        L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3",
        L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    const size_t dot = result.find(L'.');
    const std::wstring stem = ToLower(result.substr(0, dot));
    for (const wchar_t* word : reserved) {
        if (stem == word) {
            result.insert(result.begin(), L'_');
            break;
        }
    }
    return result;
}

std::wstring FileNameFromUrl(const std::wstring& url) {
    std::wstring clean = url;
    const size_t cut = clean.find_first_of(L"?#");
    if (cut != std::wstring::npos) {
        clean.resize(cut);
    }
    std::wstring name = FileNameOf(clean);
    if (!name.empty()) {
        std::wstring encoded = name;
        std::wstring decoded(encoded.size() * 3 + 1, L'\0');
        DWORD length = static_cast<DWORD>(decoded.size());
        if (SUCCEEDED(UrlUnescapeW(encoded.data(), decoded.data(), &length, URL_UNESCAPE))) {
            if (length < decoded.size()) {
                name.assign(decoded.data(), length);
            }
        }
    }
    name = SanitizeFileName(name);
    if (name.empty() || name == L"download") {
        return L"download";
    }
    return name;
}

std::wstring FormatFileTime(const FILETIME& time) {
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&time, &local) || !FileTimeToSystemTime(&local, &st)) {
        return {};
    }
    wchar_t buffer[64];
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute);
    return buffer;
}

std::wstring FileTypeName(const std::wstring& name, bool directory) {
    if (directory) {
        return L"Folder";
    }
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    if (!ext || !*ext) {
        return L"File";
    }
    std::wstring upper = ToLower(ext + 1);
    if (upper == L"txt" || upper == L"md" || upper == L"log") return L"Text document";
    if (upper == L"png" || upper == L"jpg" || upper == L"jpeg" || upper == L"gif" ||
        upper == L"bmp" || upper == L"webp" || upper == L"svg") return L"Image";
    if (upper == L"mp3" || upper == L"wav" || upper == L"flac" || upper == L"ogg") return L"Audio";
    if (upper == L"mp4" || upper == L"mkv" || upper == L"avi" || upper == L"mov") return L"Video";
    if (upper == L"zip") return L"ZIP archive";
    if (upper == L"exe" || upper == L"dll") return L"Application";
    if (upper == L"pdf") return L"PDF document";
    if (upper == L"json") return L"JSON file";
    if (upper == L"cpp" || upper == L"c" || upper == L"h" || upper == L"hpp") return L"Source file";
    std::wstring label = upper;
    for (wchar_t& c : label) {
        c = static_cast<wchar_t>(towupper(c));
    }
    return label.empty() ? L"File" : label + L" file";
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) {
        std::wstring result(path);
        CoTaskMemFree(path);
        return result;
    }
    return {};
}

void ShowInfo(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), L"67fm", MB_OK | MB_ICONINFORMATION);
}

void EnableDarkTitleBar(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

bool OpenWithShell(const std::wstring& path, const wchar_t* verb) {
    if (path.empty()) {
        return false;
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = verb ? verb : L"open";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
}

void ShowProperties(HWND owner, const std::wstring& path) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.hwnd = owner;
    info.lpVerb = L"properties";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

void OpenInExplorer(const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    if (DirectoryExists(path)) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        const std::wstring args = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

namespace {

std::wstring Find67tExecutable() {
    const std::wstring exe = [] {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return std::wstring();
            }
            if (length < buffer.size() - 1) {
                buffer.resize(length);
                return buffer;
            }
            buffer.resize(buffer.size() * 2);
        }
    }();
    if (exe.empty()) {
        return {};
    }
    const std::wstring directory = DirectoryOf(exe);
    const std::wstring candidates[] = {
        directory + L"\\67t.exe",
        directory + L"\\..\\..\\67t\\build\\67t.exe",
        directory + L"\\..\\67t\\build\\67t.exe",
        directory + L"\\..\\..\\67t\\67t.exe",
    };
    for (const auto& candidate : candidates) {
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return FullPath(candidate);
        }
    }
    return {};
}

bool RunDetached(const std::wstring& executable, const std::wstring& arguments,
                 const std::wstring& workingDirectory, DWORD flags = 0) {
    std::wstring commandLine = L"\"" + executable + L"\"";
    if (!arguments.empty()) {
        commandLine += L" " + arguments;
    }
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), buffer.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                        &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace

bool OpenTerminalAt(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const std::wstring target = DirectoryExists(path) ? path : DirectoryOf(path);
    if (!DirectoryExists(target)) {
        return false;
    }

    const std::wstring terminal = Find67tExecutable();
    if (!terminal.empty()) {
        const std::wstring arguments = L"--cwd \"" + target + L"\"";
        if (RunDetached(terminal, arguments, target)) {
            return true;
        }
    }

    SHELLEXECUTEINFOW shell{};
    shell.cbSize = sizeof(shell);
    shell.fMask = SEE_MASK_FLAG_NO_UI;
    shell.lpVerb = L"open";
    shell.lpFile = L"wt.exe";
    const std::wstring wtArguments = L"-d \"" + target + L"\"";
    shell.lpParameters = wtArguments.c_str();
    shell.lpDirectory = target.c_str();
    shell.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&shell)) {
        return true;
    }

    wchar_t comspec[MAX_PATH] = {};
    std::wstring command = L"cmd.exe";
    if (GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH) > 0) {
        command = comspec;
    }
    const std::wstring arguments = L"/k cd /d \"" + target + L"\"";
    return RunDetached(command, arguments, target, CREATE_NEW_CONSOLE);
}

void RecyclePaths(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) {
        return;
    }
    std::wstring list;
    for (const auto& path : paths) {
        list += path;
        list.push_back(L'\0');
    }
    list.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.hwnd = owner;
    op.wFunc = FO_DELETE;
    op.pFrom = list.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
    const int result = SHFileOperationW(&op);
    if (result != 0) {
        ShowError(owner, L"Delete failed (0x" + std::to_wstring(result) + L").");
    }
}

void CopyOrMovePaths(HWND owner, const std::vector<std::wstring>& paths,
                     const std::wstring& destination, bool move) {
    if (paths.empty() || destination.empty() || !DirectoryExists(destination)) {
        return;
    }
    std::wstring from;
    for (const auto& path : paths) {
        from += path;
        from.push_back(L'\0');
    }
    from.push_back(L'\0');
    std::wstring to = destination;
    to.push_back(L'\0');
    to.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.hwnd = owner;
    op.wFunc = move ? FO_MOVE : FO_COPY;
    op.pFrom = from.c_str();
    op.pTo = to.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
    const int result = SHFileOperationW(&op);
    if (result != 0) {
        ShowError(owner, std::wstring(move ? L"Move" : L"Copy") + L" failed (0x" +
                                std::to_wstring(result) + L").");
    }
}

std::wstring DriveRootOf(const std::wstring& path) {
    const std::wstring full = FullPath(path);
    if (full.size() >= 2 && full[0] == L'\\' && full[1] == L'\\') {
        const size_t serverEnd = full.find(L'\\', 2);
        if (serverEnd == std::wstring::npos) return full;
        const size_t shareEnd = full.find(L'\\', serverEnd + 1);
        return shareEnd == std::wstring::npos ? full : full.substr(0, shareEnd);
    }
    if (full.size() >= 2 && full[1] == L':') {
        return full.substr(0, 2) + L"\\";
    }
    return DirectoryOf(full);
}

unsigned long long FreeBytesOn(const std::wstring& path) {
    ULARGE_INTEGER free{};
    if (GetDiskFreeSpaceExW(DriveRootOf(path).c_str(), &free, nullptr, nullptr)) {
        return free.QuadPart;
    }
    return 0;
}

bool IsArchiveName(const std::wstring& name) {
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    return ext && _wcsicmp(ext, L".zip") == 0;
}

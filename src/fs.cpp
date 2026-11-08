#include "fs.h"

#include <algorithm>

#include "util.h"

bool ListDirectory(const std::wstring& path, bool showHidden,
                   std::vector<FileEntry>& out, std::wstring& error) {
    out.clear();
    if (path.empty()) {
        error = L"No folder path.";
        return false;
    }
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = JoinPath(path, L"*");
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        error = GetErrorText(L"Cannot open " + path, GetLastError());
        return false;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        if (!showHidden &&
            (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
            continue;
        }
        FileEntry entry;
        entry.name = name;
        entry.path = JoinPath(path, name);
        entry.attributes = data.dwFileAttributes;
        entry.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
                     data.nFileSizeLow;
        entry.modified = data.ftLastWriteTime;
        entry.archive = !entry.directory && IsArchiveName(name);
        out.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));
    const DWORD last = GetLastError();
    FindClose(find);
    if (last != ERROR_NO_MORE_FILES) {
        error = GetErrorText(L"Cannot finish reading " + path, last);
        return false;
    }
    error.clear();
    return true;
}

bool SearchDirectory(const std::wstring& root, const std::wstring& query, bool showHidden,
                     int maxResults, std::atomic<bool>* cancel,
                     std::vector<FileEntry>& out, bool& truncated, std::wstring& error) {
    out.clear();
    truncated = false;
    error.clear();
    if (root.empty() || query.empty()) {
        return true;
    }
    struct Pending {
        std::wstring path;
        std::wstring relative;
    };
    const std::wstring lowerQuery = ToLower(query);
    std::vector<Pending> stack;
    stack.push_back(Pending{root, L""});
    while (!stack.empty()) {
        if (cancel && cancel->load()) {
            return true;
        }
        const Pending current = std::move(stack.back());
        stack.pop_back();

        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW(JoinPath(current.path, L"*").c_str(), FindExInfoBasic,
                                       &data, FindExSearchNameMatch, nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            if (PathsEqual(current.path, root)) {
                error = GetErrorText(L"Cannot search " + root, GetLastError());
                return false;
            }
            continue;
        }
        bool done = false;
        int scanned = 0;
        do {
            if (cancel && ((++scanned & 0xFF) == 0) && cancel->load()) {
                done = true;
                break;
            }
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!showHidden && (data.dwFileAttributes &
                                (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                continue;
            }
            const std::wstring relative =
                current.relative.empty() ? name : current.relative + L"\\" + name;
            if (ToLower(name).find(lowerQuery) != std::wstring::npos) {
                FileEntry entry;
                entry.name = relative;
                entry.path = JoinPath(current.path, name);
                entry.attributes = data.dwFileAttributes;
                entry.directory = directory;
                entry.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
                             data.nFileSizeLow;
                entry.modified = data.ftLastWriteTime;
                entry.archive = !directory && IsArchiveName(name);
                out.push_back(std::move(entry));
                if (maxResults > 0 && static_cast<int>(out.size()) >= maxResults) {
                    truncated = true;
                    done = true;
                    break;
                }
            }
            if (directory && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                stack.push_back(Pending{JoinPath(current.path, name), relative});
            }
        } while (FindNextFileW(find, &data));
        const DWORD last = GetLastError();
        FindClose(find);
        if (done) {
            break;
        }
        if (last != ERROR_NO_MORE_FILES && PathsEqual(current.path, root)) {
            error = GetErrorText(L"Cannot finish searching " + root, last);
            return false;
        }
    }
    return true;
}

namespace {

int CompareName(const FileEntry& a, const FileEntry& b) {    return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) - CSTR_EQUAL;
}

int CompareTime(const FILETIME& a, const FILETIME& b) {
    ULARGE_INTEGER ua{};
    ULARGE_INTEGER ub{};
    ua.LowPart = a.dwLowDateTime;
    ua.HighPart = a.dwHighDateTime;
    ub.LowPart = b.dwLowDateTime;
    ub.HighPart = b.dwHighDateTime;
    if (ua.QuadPart < ub.QuadPart) return -1;
    if (ua.QuadPart > ub.QuadPart) return 1;
    return 0;
}

}  // namespace

int CompareEntries(const FileEntry& a, const FileEntry& b, int column, bool ascending) {
    int result = 0;
    switch (column) {
    case 1:
        if (a.size != b.size) result = a.size < b.size ? -1 : 1;
        break;
    case 2:
        result = CompareTime(a.modified, b.modified);
        break;
    case 3:
        result = CompareStringOrdinal(FileTypeName(a.name, a.directory).c_str(), -1,
                                      FileTypeName(b.name, b.directory).c_str(), -1,
                                      TRUE) - CSTR_EQUAL;
        break;
    default:
        break;
    }
    if (result == 0) {
        result = CompareName(a, b);
    }
    return ascending ? result : -result;
}

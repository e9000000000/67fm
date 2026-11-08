#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

struct FileEntry {
    std::wstring name;
    std::wstring path;       // full filesystem path, when applicable
    std::wstring inner;      // path inside an archive, when applicable
    unsigned long long size = 0;
    FILETIME modified{};
    DWORD attributes = 0;
    bool directory = false;
    bool archive = false;    // ZIP file that can be opened as a folder

    bool IsContainer() const { return directory || archive; }
};

bool ListDirectory(const std::wstring& path, bool showHidden,
                   std::vector<FileEntry>& out, std::wstring& error);

// Recursive name search. `out[].name` is the path relative to `root`, and
// `out[].path` is the full filesystem path. Stops at maxResults.
bool SearchDirectory(const std::wstring& root, const std::wstring& query, bool showHidden,
                     int maxResults, std::atomic<bool>* cancel,
                     std::vector<FileEntry>& out, bool& truncated, std::wstring& error);

int CompareEntries(const FileEntry& a, const FileEntry& b, int column, bool ascending);

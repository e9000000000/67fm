#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "fs.h"

// ZIP browsing is read-only. Extraction is supported for stored and deflated
// entries (everything miniz can read).
class ZipArchive {
public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    bool Open(const std::wstring& path, std::wstring& error);
    void Close();
    bool IsOpen() const { return open_; }
    const std::wstring& path() const { return path_; }
    size_t entryCount() const;

    bool List(const std::wstring& inner, std::vector<FileEntry>& out,
              std::wstring& error);
    bool Extract(const std::wstring& innerFile, const std::wstring& destination,
                 std::wstring& error);
    bool ExtractAll(const std::wstring& innerDir, const std::wstring& destination,
                    int& extracted, std::wstring& error);
    bool Search(const std::wstring& inner, const std::wstring& query, int maxResults,
                std::vector<FileEntry>& out, bool& truncated, std::wstring& error);

private:
    struct Impl;

    struct Entry {
        std::wstring name;  // normalized, no leading slash
        unsigned int index = 0;
        unsigned long long size = 0;
        FILETIME modified{};
        bool directory = false;
        bool supported = false;
        bool encrypted = false;
    };

    const Entry* Find(const std::wstring& innerFile) const;
    bool ExtractEntry(const Entry& entry, const std::wstring& destination,
                      std::wstring& error);

    bool open_ = false;
    std::wstring path_;
    std::unique_ptr<Impl> impl_;
    std::vector<Entry> entries_;
};

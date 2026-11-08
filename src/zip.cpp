#include "zip.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <set>

#include "location.h"
#include "miniz.h"
#include "util.h"

struct ZipArchive::Impl {
    mz_zip_archive zip{};
    FILE* file = nullptr;
};

namespace {

bool HasDotDot(const std::wstring& path) {
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(L'\\', start);
        const std::wstring part =
            path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part == L"..") {
            return true;
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::wstring CleanEntryName(std::wstring name) {
    name = NormalizeInner(std::move(name));
    while (name.rfind(L".\\", 0) == 0) {
        name.erase(0, 2);
    }
    return name;
}

FILETIME FileTimeFromUnix(long long seconds) {
    constexpr long long kEpoch = 11644473600LL;
    ULARGE_INTEGER value{};
    value.QuadPart = static_cast<unsigned long long>((seconds + kEpoch) * 10000000LL);
    FILETIME result{};
    result.dwLowDateTime = value.LowPart;
    result.dwHighDateTime = value.HighPart;
    return result;
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix) {
    return text.size() >= prefix.size() &&
           _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

struct WriteContext {
    HANDLE file = INVALID_HANDLE_VALUE;
    bool failed = false;
};

size_t WriteCallback(void* opaque, mz_uint64 offset, const void* buffer, size_t size) {
    auto* context = static_cast<WriteContext*>(opaque);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(context->file, position, nullptr, FILE_BEGIN)) {
        context->failed = true;
        return 0;
    }
    DWORD written = 0;
    if (!WriteFile(context->file, buffer, static_cast<DWORD>(size), &written, nullptr) ||
        written != size) {
        context->failed = true;
        return written;
    }
    return written;
}

}  // namespace

ZipArchive::ZipArchive() = default;

ZipArchive::~ZipArchive() {
    Close();
}

size_t ZipArchive::entryCount() const {
    return entries_.size();
}

bool ZipArchive::Open(const std::wstring& path, std::wstring& error) {
    Close();
    path_ = path;
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) {
        error = L"Cannot open archive: " + path;
        return false;
    }
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        error = L"Cannot read archive: " + path;
        return false;
    }
    const long long size = _ftelli64(file);
    if (size <= 0 || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        error = L"Archive is empty: " + path;
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->file = file;
    if (!mz_zip_reader_init_cfile(&impl->zip, file, static_cast<mz_uint64>(size), 0)) {
        const mz_zip_error code = mz_zip_get_last_error(&impl->zip);
        error = L"Not a readable ZIP archive: " + path + L" (" +
                Utf8ToWide(mz_zip_get_error_string(code)) + L")";
        fclose(file);
        return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&impl->zip);
    entries_.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&impl->zip, i, &stat)) {
            continue;
        }
        std::wstring name = Utf8ToWide(stat.m_filename);
        if (name.empty()) {
            const int needed = MultiByteToWideChar(CP_ACP, 0, stat.m_filename, -1, nullptr, 0);
            if (needed > 1) {
                name.assign(static_cast<size_t>(needed), L'\0');
                MultiByteToWideChar(CP_ACP, 0, stat.m_filename, -1, name.data(), needed);
                name.resize(static_cast<size_t>(needed - 1));
            }
        }
        name = CleanEntryName(std::move(name));
        if (name.empty() || name.find(L':') != std::wstring::npos || name.front() == L'\\' ||
            HasDotDot(name)) {
            continue;
        }
        Entry entry;
        entry.name = name;
        entry.index = i;
        entry.size = stat.m_uncomp_size;
        entry.directory = stat.m_is_directory != 0;
        entry.supported = stat.m_is_supported != 0;
        entry.encrypted = stat.m_is_encrypted != 0;
#ifndef MINIZ_NO_TIME
        entry.modified = FileTimeFromUnix(static_cast<long long>(stat.m_time));
#endif
        entries_.push_back(std::move(entry));
    }

    impl_ = std::move(impl);
    open_ = true;
    error.clear();
    return true;
}

void ZipArchive::Close() {
    if (impl_) {
        if (impl_->zip.m_zip_mode != MZ_ZIP_MODE_INVALID) {
            mz_zip_reader_end(&impl_->zip);
        }
        if (impl_->file) {
            fclose(impl_->file);
            impl_->file = nullptr;
        }
        impl_.reset();
    }
    entries_.clear();
    open_ = false;
    path_.clear();
}

const ZipArchive::Entry* ZipArchive::Find(const std::wstring& innerFile) const {
    for (const auto& entry : entries_) {
        if (_wcsicmp(entry.name.c_str(), innerFile.c_str()) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool ZipArchive::List(const std::wstring& inner, std::vector<FileEntry>& out,
                      std::wstring& error) {
    out.clear();
    if (!open_ || !impl_) {
        error = L"No archive is open.";
        return false;
    }
    const std::wstring clean = NormalizeInner(inner);
    const std::wstring prefix = clean.empty() ? std::wstring() : clean + L"\\";
    std::set<std::wstring> folders;

    for (const auto& entry : entries_) {
        if (!prefix.empty() && !StartsWithNoCase(entry.name, prefix)) {
            continue;
        }
        std::wstring rest = entry.name.substr(prefix.size());
        if (rest.empty()) {
            continue;
        }
        const size_t slash = rest.find(L'\\');
        if (slash != std::wstring::npos) {
            const std::wstring folderName = rest.substr(0, slash);
            if (folderName.empty()) {
                continue;
            }
            const std::wstring lower = ToLower(folderName);
            if (!folders.insert(lower).second) {
                continue;
            }
            FileEntry item;
            item.name = folderName;
            item.directory = true;
            item.attributes = FILE_ATTRIBUTE_DIRECTORY;
            item.modified = entry.modified;
            item.inner = clean.empty() ? folderName : clean + L"\\" + folderName;
            item.path = path_ + L"\\" + item.inner;
            out.push_back(std::move(item));
        } else if (entry.directory) {
            const std::wstring lower = ToLower(rest);
            if (!folders.insert(lower).second) {
                continue;
            }
            FileEntry item;
            item.name = rest;
            item.directory = true;
            item.attributes = FILE_ATTRIBUTE_DIRECTORY;
            item.modified = entry.modified;
            item.inner = entry.name;
            item.path = path_ + L"\\" + entry.name;
            out.push_back(std::move(item));
        } else {
            FileEntry item;
            item.name = rest;
            item.size = entry.size;
            item.modified = entry.modified;
            item.archive = IsArchiveName(rest);
            item.inner = entry.name;
            item.path = path_ + L"\\" + entry.name;
            out.push_back(std::move(item));
        }
    }
    error.clear();
    return true;
}

bool ZipArchive::ExtractEntry(const Entry& entry, const std::wstring& destination,
                              std::wstring& error) {
    if (entry.directory) {
        if (!EnsureDirectory(destination)) {
            error = L"Cannot create folder: " + destination;
            return false;
        }
        return true;
    }
    if (entry.encrypted) {
        error = L"Encrypted ZIP entries are not supported: " + entry.name;
        return false;
    }
    if (!entry.supported) {
        error = L"Unsupported ZIP compression method: " + entry.name;
        return false;
    }
    if (!EnsureDirectory(DirectoryOf(destination))) {
        error = L"Cannot create folder: " + DirectoryOf(destination);
        return false;
    }
    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetErrorText(L"Cannot create " + destination, GetLastError());
        return false;
    }
    WriteContext context{file, false};
    const mz_bool ok = mz_zip_reader_extract_to_callback(
        &impl_->zip, entry.index, WriteCallback, &context, 0);
    CloseHandle(file);
    if (!ok || context.failed) {
        DeleteFileW(destination.c_str());
        const mz_zip_error code = mz_zip_get_last_error(&impl_->zip);
        error = L"Cannot extract " + entry.name + L": " +
                Utf8ToWide(mz_zip_get_error_string(code));
        return false;
    }
    if (entry.modified.dwLowDateTime || entry.modified.dwHighDateTime) {
        HANDLE stamp = CreateFileW(destination.c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stamp != INVALID_HANDLE_VALUE) {
            SetFileTime(stamp, nullptr, nullptr, &entry.modified);
            CloseHandle(stamp);
        }
    }
    error.clear();
    return true;
}

bool ZipArchive::Extract(const std::wstring& innerFile, const std::wstring& destination,
                         std::wstring& error) {
    if (!open_) {
        error = L"No archive is open.";
        return false;
    }
    const std::wstring clean = NormalizeInner(innerFile);
    if (HasDotDot(clean)) {
        error = L"Unsafe path inside archive: " + innerFile;
        return false;
    }
    const Entry* entry = Find(clean);
    if (!entry) {
        error = L"Entry not found in archive: " + innerFile;
        return false;
    }
    return ExtractEntry(*entry, destination, error);
}

bool ZipArchive::ExtractAll(const std::wstring& innerDir, const std::wstring& destination,
                            int& extracted, std::wstring& error) {    extracted = 0;
    if (!open_) {
        error = L"No archive is open.";
        return false;
    }
    const std::wstring clean = NormalizeInner(innerDir);
    const std::wstring prefix = clean.empty() ? std::wstring() : clean + L"\\";
    if (!EnsureDirectory(destination)) {
        error = L"Cannot create folder: " + destination;
        return false;
    }
    for (const auto& entry : entries_) {
        if (!prefix.empty() && !StartsWithNoCase(entry.name, prefix)) {
            continue;
        }
        std::wstring relative = entry.name.substr(prefix.size());
        if (relative.empty() || HasDotDot(relative)) {
            continue;
        }
        const std::wstring target = JoinPath(destination, relative);
        if (!ExtractEntry(entry, target, error)) {
            return false;
        }
        ++extracted;
    }
    error.clear();
    return true;
}

bool ZipArchive::Search(const std::wstring& inner, const std::wstring& query, int maxResults,
                        std::vector<FileEntry>& out, bool& truncated, std::wstring& error) {
    out.clear();
    truncated = false;
    error.clear();
    if (!open_) {
        error = L"No archive is open.";
        return false;
    }
    if (query.empty()) {
        return true;
    }
    const std::wstring clean = NormalizeInner(inner);
    const std::wstring prefix = clean.empty() ? std::wstring() : clean + L"\\";
    const std::wstring lowerQuery = ToLower(query);
    for (const auto& entry : entries_) {
        if (!prefix.empty() && !StartsWithNoCase(entry.name, prefix)) {
            continue;
        }
        std::wstring relative = entry.name.substr(prefix.size());
        if (relative.empty()) {
            continue;
        }
        if (ToLower(FileNameOf(relative)).find(lowerQuery) == std::wstring::npos) {
            continue;
        }
        FileEntry item;
        item.name = relative;
        item.inner = entry.name;
        item.path = path_ + L"\\" + entry.name;
        item.size = entry.size;
        item.modified = entry.modified;
        item.attributes = entry.directory ? FILE_ATTRIBUTE_DIRECTORY : 0;
        item.directory = entry.directory;
        item.archive = !entry.directory && IsArchiveName(relative);
        out.push_back(std::move(item));
        if (maxResults > 0 && static_cast<int>(out.size()) >= maxResults) {
            truncated = true;
            break;
        }
    }
    return true;
}

#include "location.h"

#include <windows.h>

#include <algorithm>

#include "util.h"

std::wstring NormalizeInner(std::wstring inner) {
    std::replace(inner.begin(), inner.end(), L'/', L'\\');
    while (!inner.empty() && inner.front() == L'\\') {
        inner.erase(inner.begin());
    }
    while (!inner.empty() && inner.back() == L'\\') {
        inner.pop_back();
    }
    return inner;
}

Location Location::FileSystem(std::wstring path) {
    Location location;
    location.archive = false;
    location.fsPath = std::move(path);
    return location;
}

Location Location::InArchive(std::wstring archivePath, std::wstring inner) {
    Location location;
    location.archive = true;
    location.fsPath = std::move(archivePath);
    location.inner = NormalizeInner(std::move(inner));
    return location;
}

std::wstring Location::ToString() const {
    if (!archive) {
        return fsPath;
    }
    if (inner.empty()) {
        return fsPath;
    }
    return fsPath + L"\\" + inner;
}

std::wstring Location::DisplayName() const {
    if (!archive) {
        const std::wstring name = FileNameOf(fsPath);
        if (!name.empty()) {
            return name;
        }
        return fsPath;
    }
    if (inner.empty()) {
        return FileNameOf(fsPath);
    }
    return FileNameOf(inner);
}

std::wstring Location::Join(const std::wstring& name) const {
    if (!archive) {
        return JoinPath(fsPath, name);
    }
    if (inner.empty()) {
        return name;
    }
    return inner + L"\\" + name;
}

std::wstring Location::ParentPath() const {
    if (!archive) {
        const std::wstring parent = ParentDirectory(fsPath);
        return PathsEqual(parent, fsPath) ? std::wstring() : parent;
    }
    if (!inner.empty()) {
        const size_t slash = inner.find_last_of(L'\\');
        return slash == std::wstring::npos ? std::wstring() : inner.substr(0, slash);
    }
    // At the archive root, "up" means the folder that contains the archive.
    return ParentDirectory(fsPath);
}

bool Location::Up(Location& out) const {
    if (!archive) {
        const std::wstring parent = ParentDirectory(fsPath);
        if (PathsEqual(parent, fsPath)) {
            return false;
        }
        out = FileSystem(parent);
        return true;
    }
    if (!inner.empty()) {
        const size_t slash = inner.find_last_of(L'\\');
        out = InArchive(fsPath, slash == std::wstring::npos ? L"" : inner.substr(0, slash));
        return true;
    }
    out = FileSystem(ParentDirectory(fsPath));
    return true;
}

bool Location::Equals(const Location& other) const {
    if (archive != other.archive) {
        return false;
    }
    if (!PathsEqual(fsPath, other.fsPath)) {
        return false;
    }
    return !archive || _wcsicmp(inner.c_str(), other.inner.c_str()) == 0;
}

bool ParseLocation(const std::wstring& text, Location& out, std::wstring& error) {
    std::wstring clean = Trim(text);
    if (clean.size() >= 2 && clean.front() == L'"' && clean.back() == L'"') {
        clean = clean.substr(1, clean.size() - 2);
    }
    if (clean.empty()) {
        error = L"Empty path.";
        return false;
    }

    std::wstring expanded(32768, L'\0');
    const DWORD expandedLength = ExpandEnvironmentStringsW(clean.c_str(), expanded.data(),
                                                          static_cast<DWORD>(expanded.size()));
    if (expandedLength > 0 && expandedLength <= expanded.size()) {
        expanded.resize(expandedLength - 1);
        clean = expanded;
    }
    const std::wstring full = FullPath(clean);

    const DWORD attrs = GetFileAttributesW(full.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            out = Location::FileSystem(full);
            return true;
        }
        if (IsArchiveName(FileNameOf(full))) {
            out = Location::InArchive(full);
            return true;
        }
        error = L"Not a folder or ZIP archive: " + full;
        return false;
    }

    // Find a prefix that is an existing .zip file. The remainder is the inner
    // path. Checking the file exists avoids treating a real folder named
    // "something.zip" as an archive.
    const std::wstring lower = ToLower(full);
    for (size_t pos = lower.find(L".zip"); pos != std::wstring::npos;
         pos = lower.find(L".zip", pos + 1)) {
        const size_t after = pos + 4;
        if (after < full.size() && full[after] != L'\\' && full[after] != L'/') {
            continue;
        }
        const std::wstring prefix = full.substr(0, after);
        if (!FileExists(prefix)) {
            continue;
        }
        std::wstring inner = after < full.size() ? full.substr(after) : L"";
        out = Location::InArchive(prefix, NormalizeInner(inner));
        return true;
    }

    error = L"Path not found: " + full;
    return false;
}

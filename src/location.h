#pragma once

#include <string>

// A filesystem folder or a folder inside a ZIP archive. Archive locations are
// serialized as `C:\path\archive.zip\inner\folder`, which is also what the
// address bar accepts.
struct Location {
    bool archive = false;
    std::wstring fsPath;  // filesystem folder, or filesystem path to the archive
    std::wstring inner;   // inside the archive, backslash separated, no leading slash

    static Location FileSystem(std::wstring path);
    static Location InArchive(std::wstring archivePath, std::wstring inner = {});

    std::wstring ToString() const;
    std::wstring DisplayName() const;
    std::wstring Join(const std::wstring& name) const;
    std::wstring ParentPath() const;  // display path of the parent, empty at root
    bool Up(Location& out) const;
    bool Equals(const Location& other) const;
    bool Valid() const { return !fsPath.empty(); }
};

bool ParseLocation(const std::wstring& text, Location& out, std::wstring& error);
std::wstring NormalizeInner(std::wstring inner);

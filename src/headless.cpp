#include "headless.h"

#include <windows.h>
#include <objidl.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include "download.h"
#include "dragout.h"
#include "fs.h"
#include "ipc.h"
#include "json.h"
#include "location.h"
#include "miniz.h"
#include "settings.h"
#include "session.h"
#include "util.h"
#include "zip.h"

namespace {

struct TestLog {
    std::string text;
    int passed = 0;
    int failed = 0;

    void result(const std::wstring& name, bool ok, const std::wstring& detail = {}) {
        if (ok) {
            ++passed;
            text += "ok ";
            text += WideToUtf8(name);
        } else {
            ++failed;
            text += "FAIL ";
            text += WideToUtf8(name);
            if (!detail.empty()) {
                text += ": ";
                text += WideToUtf8(detail);
            }
        }
        text += "\r\n";
    }
};

bool WriteSmallFile(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                              nullptr) != FALSE &&
                    written == data.size();
    CloseHandle(file);
    return ok;
}

bool ReadSmallFile(const std::wstring& path, std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    data.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = data.empty() ||
                    (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) &&
                     read == data.size());
    CloseHandle(file);
    return ok;
}

void RemoveTree(const std::wstring& directory) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(JoinPath(directory, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring item = JoinPath(directory, data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(item);
            } else {
                DeleteFileW(item.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(directory.c_str());
}

bool MakeZip(const std::wstring& path) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) {
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
        fclose(file);
        return false;
    }
    const char alpha[] = "alpha";
    const char bravo[] = "bravo";
    bool ok = mz_zip_writer_add_mem(&zip, "a.txt", alpha, 5, MZ_BEST_COMPRESSION) != 0;
    ok = ok && mz_zip_writer_add_mem(&zip, "dir/", "", 0, MZ_BEST_COMPRESSION) != 0;
    ok = ok && mz_zip_writer_add_mem(&zip, "dir/b.txt", bravo, 5, MZ_BEST_COMPRESSION) != 0;
    ok = mz_zip_writer_finalize_archive(&zip) != 0 && ok;
    mz_zip_writer_end(&zip);
    fclose(file);
    return ok;
}

}  // namespace

int RunSelfTest(const std::wstring& logPath) {
    TestLog log;

    log.result(L"utf8 round trip", Utf8ToWide(WideToUtf8(L"h\u00e9llo \U0001F600")) ==
                                      L"h\u00e9llo \U0001F600");
    log.result(L"format bytes", FormatBytes(1536) == L"1.5 KB", FormatBytes(1536));
    const std::wstring sanitized = SanitizeFileName(L"a<b>:c?.png");
    log.result(L"sanitize filename",
               sanitized.find_first_of(L"<>:\"/\\|?*") == std::wstring::npos, sanitized);
    log.result(L"url filename",
               FileNameFromUrl(L"https://example.com/a/photo%20one.jpg?x=1") == L"photo one.jpg",
               FileNameFromUrl(L"https://example.com/a/photo%20one.jpg?x=1"));

    const std::wstring html =
        L"Version:0.9\r\nSourceURL:https://example.com/page/\r\n"
        L"<html><body><img src=\"image.png\" width=\"10\"></body></html>";
    const std::wstring base = BaseUrlFromHtml(html);
    const std::wstring found = UrlFromHtml(html);
    log.result(L"html source url", base == L"https://example.com/page/", base);
    log.result(L"html image url", found == L"image.png", found);
    log.result(L"resolve url",
               ResolveUrl(base, found) == L"https://example.com/page/image.png",
               ResolveUrl(base, found));
    log.result(L"text url", UrlFromText(L"see https://example.com/a.jpg now") ==
                                L"https://example.com/a.jpg");

    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
    const std::wstring root = JoinPath(temp, L"67fm-selftest-" +
                                                 std::to_wstring(GetTickCount64()));
    if (!EnsureDirectory(root)) {
        log.result(L"create temp folder", false, root);
    } else {
        log.result(L"create temp folder", true);
        const std::wstring folder = JoinPath(root, L"folder");
        EnsureDirectory(folder);
        EnsureDirectory(JoinPath(folder, L"sub"));
        WriteSmallFile(JoinPath(folder, L"one.txt"), "one");
        WriteSmallFile(JoinPath(folder, L"two.bin"), "two");
        WriteSmallFile(JoinPath(folder, L"findme2.txt"), "two");
        WriteSmallFile(JoinPath(folder, L"sub\\findme.txt"), "three");
        WriteSmallFile(JoinPath(folder, L"sub\\other.bin"), "four");
        const std::wstring hiddenFile = JoinPath(folder, L"hidden.txt");
        WriteSmallFile(hiddenFile, "hidden");
        SetFileAttributesW(hiddenFile.c_str(), FILE_ATTRIBUTE_HIDDEN);
        WriteSmallFile(JoinPath(root, L"archive.zip"), "not really");

        std::vector<FileEntry> entries;
        std::wstring error;
        log.result(L"list directory", ListDirectory(folder, false, entries, error) &&
                                          entries.size() == 4,
                   error + L" count=" + std::to_wstring(entries.size()));
        std::vector<FileEntry> hiddenEntries;
        log.result(L"list hidden", ListDirectory(folder, true, hiddenEntries, error) &&
                                       hiddenEntries.size() == 5,
                   error + L" count=" + std::to_wstring(hiddenEntries.size()));

        std::vector<FileEntry> searchResults;
        bool truncated = false;
        log.result(L"recursive search",
                   SearchDirectory(folder, L"find", false, 100, nullptr, searchResults, truncated,
                                   error) &&
                       searchResults.size() == 2,
                   error + L" count=" + std::to_wstring(searchResults.size()));
        bool nestedFound = false;
        for (const auto& item : searchResults) {
            if (item.name == L"sub\\findme.txt" &&
                PathsEqual(item.path, JoinPath(folder, L"sub\\findme.txt"))) {
                nestedFound = true;
            }
        }
        log.result(L"recursive search relative path", nestedFound);
        std::vector<FileEntry> limited;
        bool limitedTruncated = false;
        log.result(L"recursive search limit",
                   SearchDirectory(folder, L"find", false, 1, nullptr, limited,
                                   limitedTruncated, error) &&
                       limited.size() == 1 && limitedTruncated);
        if (entries.size() >= 2) {
            const bool folderFirst =
                CompareEntries(entries[0], entries[1], 0, true) <= 0;
            log.result(L"compare entries", folderFirst);
        }

        const std::wstring unique = UniquePath(folder, L"one.txt");
        const std::wstring numberedFile = UniqueNumberedPath(folder, L"one.txt", false);
        const std::wstring numberedFolder = UniqueNumberedPath(folder, L"sub", true);
        log.result(L"unique path",
                   FileNameOf(unique) == L"one (2).txt" &&
                       FileNameOf(numberedFile) == L"one_2.txt" &&
                       FileNameOf(numberedFolder) == L"sub_2",
                   FileNameOf(unique) + L" / " + FileNameOf(numberedFile) + L" / " +
                       FileNameOf(numberedFolder));

        Location archiveRoot;
        log.result(L"parse archive root", ParseLocation(JoinPath(root, L"archive.zip"),
                                                       archiveRoot, error) &&
                                              archiveRoot.archive && archiveRoot.inner.empty(),
                   error);
        Location archiveInner;
        log.result(L"parse archive inner",
                   ParseLocation(JoinPath(root, L"archive.zip\\dir"), archiveInner, error) &&
                       archiveInner.archive && archiveInner.inner == L"dir",
                   error);
        Location up;
        log.result(L"archive up", archiveInner.Up(up) && up.archive && up.inner.empty());
        log.result(L"location display", archiveInner.DisplayName() == L"dir",
                   archiveInner.DisplayName());
        log.result(L"location join",
                   Location::InArchive(archiveInner.fsPath, L"dir").Join(L"b.txt") ==
                       L"dir\\b.txt");
        Location realFolder;
        log.result(L"parse real folder",
                   ParseLocation(folder, realFolder, error) && !realFolder.archive &&
                       PathsEqual(realFolder.fsPath, folder),
                   error);
        log.result(L"archive parent path",
                   Location::InArchive(archiveInner.fsPath, L"a\\b").ParentPath() == L"a");

        log.result(L"session round trip", [&]() {
            SessionState state;
            TabSession tab;
            tab.path = L"C:\\Windows";
            tab.scrollRow = 12;
            tab.sortColumn = 2;
            tab.sortAscending = false;
            state.tabs.push_back(tab);
            state.active = 0;
            state.window = RECT{10, 20, 900, 700};
            state.hasWindow = true;
            const std::wstring file = JoinPath(root, L"session.json");
            if (!SaveSessionFile(file, state)) return false;
            SessionState loaded;
            return LoadSessionFile(file, loaded) && loaded.tabs.size() == 1 &&
                   loaded.tabs[0].path == L"C:\\Windows" && loaded.tabs[0].scrollRow == 12 &&
                   loaded.tabs[0].sortColumn == 2 && !loaded.tabs[0].sortAscending;
        }());

        log.result(L"settings round trip", [&]() {
            Settings settings;
            settings.showHidden = true;
            settings.sortColumn = 3;
            settings.sortAscending = false;
            settings.viewMode = 4;
            settings.listFontFace = L"Consolas";
            settings.listFontSize = 13;
            settings.uiFontFace = L"Segoe UI";
            settings.uiFontSize = 10;
            settings.columns[0] = 500;
            settings.columns[1] = 100;
            settings.columns[2] = 200;
            settings.columns[3] = 200;
            settings.recursiveSearch = true;
            settings.recentFolders = {L"C:\\", L"D:\\work"};
            const std::wstring file = JoinPath(root, L"settings.json");
            if (!SaveSettingsFile(file, settings)) return false;
            Settings loaded;
            return LoadSettingsFile(file, loaded) && loaded.showHidden &&
                   loaded.sortColumn == 3 && !loaded.sortAscending &&
                   loaded.viewMode == 4 && loaded.listFontFace == L"Consolas" &&
                   loaded.listFontSize == 13 && loaded.uiFontFace == L"Segoe UI" &&
                   loaded.uiFontSize == 10 &&
                   loaded.columns[0] == 500 && loaded.columns[1] == 100 &&
                   loaded.columns[2] == 200 && loaded.columns[3] == 200 &&
                   loaded.recursiveSearch && loaded.recentFolders.size() == 2;
        }());

        log.result(L"ipc payload", [&]() {
            const std::wstring payload = BuildPathPayload({L"C:\\one", L"D:\\two"});
            const auto paths = ParsePathPayload(payload.c_str(), payload.size());
            return paths.size() == 2 && paths[0] == L"C:\\one" && paths[1] == L"D:\\two";
        }());
        log.result(L"ipc empty payload", [&]() {
            const std::wstring payload = BuildPathPayload({});
            const auto paths = ParsePathPayload(payload.c_str(), payload.size());
            return payload.size() == 1 && paths.empty();
        }());
        log.result(L"drag out data object", [&]() {
            const std::vector<std::wstring> paths = {L"C:\\one\\a.txt", L"C:\\two\\b c.png"};
            IDataObject* data = CreateFileDataObject(paths);
            if (!data) return false;
            FORMATETC format{};
            format.cfFormat = static_cast<CLIPFORMAT>(CF_HDROP);
            format.dwAspect = DVASPECT_CONTENT;
            format.lindex = -1;
            format.tymed = TYMED_HGLOBAL;
            bool ok = data->QueryGetData(&format) == S_OK;
            STGMEDIUM medium{};
            if (ok) {
                ok = data->GetData(&format, &medium) == S_OK &&
                     medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr;
                if (ok) {
                    auto* drop = static_cast<DROPFILES*>(GlobalLock(medium.hGlobal));
                    ok = drop != nullptr && drop->fWide;
                    if (drop) {
                        const auto* text = reinterpret_cast<const wchar_t*>(
                            reinterpret_cast<const BYTE*>(drop) + drop->pFiles);
                        std::vector<std::wstring> read;
                        for (const wchar_t* cursor = text; *cursor;
                             cursor += wcslen(cursor) + 1) {
                            read.emplace_back(cursor);
                        }
                        ok = ok && read == paths;
                        GlobalUnlock(medium.hGlobal);
                    }
                    ReleaseStgMedium(&medium);
                }
            }
            data->Release();
            return ok;
        }());

        const std::wstring zipPath = JoinPath(root, L"test.zip");
        log.result(L"make zip", MakeZip(zipPath));
        ZipArchive badArchive;
        log.result(L"reject invalid zip", !badArchive.Open(JoinPath(root, L"archive.zip"), error));
        ZipArchive archive;
        bool zipOpen = archive.Open(zipPath, error);
        log.result(L"open zip", zipOpen, error);
        if (zipOpen) {
            std::vector<FileEntry> rootEntries;
            archive.List(L"", rootEntries, error);
            bool hasFile = false;
            bool hasFolder = false;
            for (const auto& item : rootEntries) {
                if (item.name == L"a.txt") hasFile = true;
                if (item.name == L"dir" && item.directory) hasFolder = true;
            }
            log.result(L"zip root listing", hasFile && hasFolder, error);
            std::vector<FileEntry> dirEntries;
            archive.List(L"dir", dirEntries, error);
            log.result(L"zip nested listing",
                       dirEntries.size() == 1 && dirEntries[0].name == L"b.txt", error);
            const std::wstring extracted = JoinPath(root, L"b.txt");
            log.result(L"zip extract", archive.Extract(L"dir/b.txt", extracted, error), error);
            std::string content;
            log.result(L"zip content", ReadSmallFile(extracted, content) && content == "bravo",
                       Utf8ToWide(content));
            const std::wstring allRoot = JoinPath(root, L"all");
            int extractedCount = 0;
            log.result(L"zip extract all",
                       archive.ExtractAll(L"", allRoot, extractedCount, error) &&
                           extractedCount >= 3,
                       error + L" count=" + std::to_wstring(extractedCount));
            std::string allContent;
            log.result(L"zip extract all content",
                       ReadSmallFile(JoinPath(allRoot, L"dir\\b.txt"), allContent) &&
                           allContent == "bravo");
            log.result(L"zip entry count", archive.entryCount() == 3,
                       std::to_wstring(archive.entryCount()));
            std::vector<FileEntry> zipFound;
            bool zipTruncated = false;
            log.result(L"zip search",
                       archive.Search(L"", L"b", 100, zipFound, zipTruncated, error) &&
                           zipFound.size() == 1 && zipFound[0].name == L"dir\\b.txt",
                       error + L" count=" + std::to_wstring(zipFound.size()));
            log.result(L"zip missing entry",
                       !archive.Extract(L"nope.txt", JoinPath(root, L"nope.txt"), error));
        }
        log.result(L"invalid location", !ParseLocation(JoinPath(root, L"missing-no-such"),
                                                      archiveRoot, error));

        RemoveTree(root);
    }

    log.text += "summary passed=" + std::to_string(log.passed) +
                " failed=" + std::to_string(log.failed) + "\r\n";
    if (!logPath.empty()) {
        std::wstring error;
        WriteTextFileAtomic(logPath, log.text, error);
    }
    OutputDebugStringA(log.text.c_str());
    return log.failed == 0 ? 0 : 1;
}


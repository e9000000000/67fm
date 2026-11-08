#pragma once

#include <windows.h>

#include <string>
#include <shlobj.h>
#include <string_view>
#include <vector>

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

std::wstring GetLastErrorText(DWORD error);
std::wstring GetErrorText(const std::wstring& context, DWORD error);

std::wstring ExeDirectory();
std::wstring DataDirectory();
bool EnsureDirectory(const std::wstring& path);

std::wstring FullPath(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);
std::wstring CompactPathLabel(const std::wstring& path);
bool PathsEqual(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);

std::wstring JoinPath(const std::wstring& base, const std::wstring& name);
std::wstring ParentDirectory(const std::wstring& path);
std::wstring UniquePath(const std::wstring& directory, const std::wstring& name,
                        bool wantDirectory = false);
std::wstring SanitizeFileName(const std::wstring& name);
std::wstring FileNameFromUrl(const std::wstring& url);

std::wstring FormatBytes(unsigned long long bytes);
std::wstring FormatFileTime(const FILETIME& time);
std::wstring FileTypeName(const std::wstring& name, bool directory);
std::wstring Trim(const std::wstring& text);

int ScaleForDpi(int value, UINT dpi);
UINT DpiOfWindow(HWND hwnd);

void ShowError(HWND owner, const std::wstring& message);
void ShowInfo(HWND owner, const std::wstring& message);

void EnableDarkModeForApp();
void EnableDarkTitleBar(HWND hwnd);
void AllowDarkModeForWindow(HWND hwnd);
void ApplyDarkDialogTheme(HWND dialog);

bool ReadTextFile(const std::wstring& path, std::string& out);
bool WriteTextFileAtomic(const std::wstring& path, std::string_view data, std::wstring& error);

std::wstring ToLower(std::wstring text);
std::string ToLowerAscii(std::string text);

// Known folders; empty when unavailable.
std::wstring KnownFolder(REFKNOWNFOLDERID id);

// Shell helpers.
bool OpenWithShell(const std::wstring& path, const wchar_t* verb = L"open");
void ShowProperties(HWND owner, const std::wstring& path);
void OpenInExplorer(const std::wstring& path);
bool OpenTerminalAt(const std::wstring& path);
void RecyclePaths(HWND owner, const std::vector<std::wstring>& paths);
void CopyOrMovePaths(HWND owner, const std::vector<std::wstring>& paths,
                     const std::wstring& destination, bool move);

std::wstring DriveRootOf(const std::wstring& path);
unsigned long long FreeBytesOn(const std::wstring& path);
bool IsArchiveName(const std::wstring& name);

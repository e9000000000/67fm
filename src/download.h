#pragma once

#include <string>

struct DownloadResult {
    bool ok = false;
    std::wstring url;
    std::wstring path;
    std::wstring error;
};

// Downloads a single HTTP(S) URL into `folder`. Derives the file name from
// `suggestedName`, Content-Disposition, the final URL, and the content type, in
// that order. Never overwrites an existing file.
DownloadResult DownloadUrlToFolder(const std::wstring& url, const std::wstring& folder,
                                   const std::wstring& suggestedName = {});

// Browser drag-and-drop helpers.
std::wstring UrlFromHtml(const std::wstring& html);
std::wstring BaseUrlFromHtml(const std::wstring& html);
std::wstring UrlFromText(const std::wstring& text);
std::wstring ResolveUrl(const std::wstring& base, const std::wstring& relative);

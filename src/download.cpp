#include "download.h"

#include <windows.h>
#include <shlwapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>

#include "util.h"

namespace {

struct UrlParts {
    std::wstring host;
    std::wstring requestPath;
    INTERNET_PORT port = 0;
    bool secure = false;
};

bool CrackUrl(const std::wstring& url, UrlParts& parts, std::wstring& error) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t host[256] = {};
    wchar_t path[8192] = {};
    wchar_t extra[4096] = {};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = GetErrorText(L"Invalid URL " + url, GetLastError());
        return false;
    }
    parts.host.assign(host, components.dwHostNameLength);
    parts.requestPath.assign(path, components.dwUrlPathLength);
    parts.requestPath.append(extra, components.dwExtraInfoLength);
    if (parts.requestPath.empty()) {
        parts.requestPath = L"/";
    }
    parts.port = components.nPort;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

std::wstring HeaderValue(HINTERNET request, DWORD query) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value.data(),
                             &size, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    value.resize(size / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::wstring PercentDecode(const std::wstring& text) {
    std::wstring buffer = text;
    DWORD length = static_cast<DWORD>(buffer.size());
    std::wstring output(buffer.size() + 1, L'\0');
    if (SUCCEEDED(UrlUnescapeW(output.data(), buffer.data(), &length, URL_UNESCAPE))) {
        output.resize(length);
        return output;
    }
    return text;
}

std::wstring ContentDispositionName(const std::wstring& header) {
    if (header.empty()) {
        return {};
    }
    const std::wstring lower = ToLower(header);
    size_t pos = lower.find(L"filename*=");
    bool encoded = pos != std::wstring::npos;
    if (!encoded) {
        pos = lower.find(L"filename=");
    }
    if (pos == std::wstring::npos) {
        return {};
    }
    pos = header.find(L'=', pos);
    if (pos == std::wstring::npos) {
        return {};
    }
    std::wstring value = Trim(header.substr(pos + 1));
    const size_t semicolon = value.find(L';');
    if (semicolon != std::wstring::npos) {
        value.resize(semicolon);
    }
    value = Trim(value);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    if (encoded) {
        const size_t quote = value.find(L"''");
        if (quote != std::wstring::npos) {
            value = value.substr(quote + 2);
        }
        value = PercentDecode(value);
    }
    return SanitizeFileName(value);
}

std::wstring ExtensionForType(std::wstring type) {
    type = ToLower(std::move(type));
    const size_t semicolon = type.find(L';');
    if (semicolon != std::wstring::npos) {
        type.resize(semicolon);
    }
    type = Trim(type);
    if (type == L"image/jpeg") return L".jpg";
    if (type == L"image/png") return L".png";
    if (type == L"image/gif") return L".gif";
    if (type == L"image/webp") return L".webp";
    if (type == L"image/svg+xml") return L".svg";
    if (type == L"image/bmp") return L".bmp";
    if (type == L"text/html") return L".html";
    if (type == L"application/pdf") return L".pdf";
    if (type == L"application/json") return L".json";
    if (type == L"text/plain") return L".txt";
    return {};
}

bool HasExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    const size_t slash = name.find_last_of(L'\\');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash + 1);
}

std::wstring FindAttribute(const std::wstring& text, const std::wstring& attribute) {
    const std::wstring lower = ToLower(text);
    size_t pos = 0;
    for (;;) {
        pos = lower.find(attribute, pos);
        if (pos == std::wstring::npos) {
            return {};
        }
        const bool boundary = pos == 0 || iswspace(lower[pos - 1]) || lower[pos - 1] == L'<' ||
                              lower[pos - 1] == L'/';
        if (!boundary) {
            pos += attribute.size();
            continue;
        }
        size_t value = pos + attribute.size();
        while (value < lower.size() && (lower[value] == L' ' || lower[value] == L'\t')) {
            ++value;
        }
        if (value >= lower.size() || lower[value] != L'=') {
            pos += attribute.size();
            continue;
        }
        ++value;
        while (value < lower.size() && (lower[value] == L' ' || lower[value] == L'\t')) {
            ++value;
        }
        if (value >= lower.size()) {
            return {};
        }
        wchar_t quote = 0;
        if (lower[value] == L'"' || lower[value] == L'\'') {
            quote = lower[value++];
        }
        const size_t start = value;
        while (value < lower.size()) {
            const wchar_t c = lower[value];
            if (quote ? c == quote : (c == L' ' || c == L'>' || c == L'\r' || c == L'\n')) {
                break;
            }
            ++value;
        }
        return text.substr(start, value - start);
    }
}

std::wstring HtmlUrl(const std::wstring& html) {
    const std::wstring lower = ToLower(html);
    size_t img = lower.find(L"<img");
    while (img != std::wstring::npos) {
        const size_t end = lower.find(L'>', img);
        const std::wstring tag = html.substr(img, end == std::wstring::npos ? std::wstring::npos
                                                                           : end - img);
        std::wstring url = FindAttribute(tag, L"src");
        if (!url.empty()) {
            return url;
        }
        img = lower.find(L"<img", img + 4);
    }
    return FindAttribute(html, L"href");
}

std::wstring DecodeHtml(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&') {
            const size_t semi = text.find(L';', i + 1);
            if (semi != std::wstring::npos && semi - i < 12) {
                const std::wstring entity = text.substr(i + 1, semi - i - 1);
                if (_wcsicmp(entity.c_str(), L"amp") == 0) {
                    result.push_back(L'&');
                } else if (_wcsicmp(entity.c_str(), L"quot") == 0) {
                    result.push_back(L'"');
                } else if (_wcsicmp(entity.c_str(), L"apos") == 0) {
                    result.push_back(L'\'');
                } else if (_wcsicmp(entity.c_str(), L"lt") == 0) {
                    result.push_back(L'<');
                } else if (_wcsicmp(entity.c_str(), L"gt") == 0) {
                    result.push_back(L'>');
                } else {
                    result.append(text, i, semi - i + 1);
                }
                i = semi;
                continue;
            }
        }
        result.push_back(text[i]);
    }
    return result;
}

}  // namespace

std::wstring UrlFromHtml(const std::wstring& html) {
    std::wstring url = HtmlUrl(html);
    return DecodeHtml(url);
}

std::wstring BaseUrlFromHtml(const std::wstring& html) {
    const std::wstring lower = ToLower(html);
    size_t pos = lower.find(L"sourceurl:");
    if (pos == std::wstring::npos) {
        return {};
    }
    pos += 10;
    const size_t end = lower.find_first_of(L"\r\n", pos);
    return Trim(html.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos));
}

std::wstring UrlFromText(const std::wstring& text) {
    const std::wstring lower = ToLower(text);
    size_t pos = lower.find(L"http://");
    const size_t https = lower.find(L"https://");
    if (pos == std::wstring::npos || (https != std::wstring::npos && https < pos)) {
        pos = https;
    }
    if (pos == std::wstring::npos) {
        return {};
    }
    size_t end = pos;
    while (end < text.size() && !iswspace(text[end]) && text[end] != L'"' &&
           text[end] != L'\'' && text[end] != L'>' && text[end] != L'<') {
        ++end;
    }
    return text.substr(pos, end - pos);
}

std::wstring ResolveUrl(const std::wstring& base, const std::wstring& relative) {
    if (relative.empty()) {
        return {};
    }
    const std::wstring lower = ToLower(relative);
    if (lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0) {
        return relative;
    }
    if (base.empty()) {
        return relative;
    }
    wchar_t buffer[8192] = {};
    DWORD length = static_cast<DWORD>(std::size(buffer));
    if (SUCCEEDED(UrlCombineW(base.c_str(), relative.c_str(), buffer, &length, 0))) {
        return buffer;
    }
    return relative;
}

DownloadResult DownloadUrlToFolder(const std::wstring& url, const std::wstring& folder,
                                   const std::wstring& suggestedName) {
    DownloadResult result;
    result.url = url;
    if (url.empty() || folder.empty()) {
        result.error = L"No URL or destination folder.";
        return result;
    }
    if (!DirectoryExists(folder)) {
        result.error = L"Destination folder does not exist: " + folder;
        return result;
    }

    UrlParts parts;
    if (!CrackUrl(url, parts, result.error)) {
        return result;
    }

    HINTERNET session = WinHttpOpen(L"67fm/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = GetErrorText(L"Cannot start HTTP", GetLastError());
        return result;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);

    HINTERNET connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    if (!connect) {
        result.error = GetErrorText(L"Cannot connect to " + parts.host, GetLastError());
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", parts.requestPath.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        result.error = GetErrorText(L"Cannot create HTTP request", GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    bool ok = false;
    std::wstring tempPath;
    do {
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            result.error = GetErrorText(L"HTTP request failed", GetLastError());
            break;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            result.error = GetErrorText(L"HTTP response failed", GetLastError());
            break;
        }
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);
        if (status < 200 || status >= 300) {
            result.error = L"HTTP status " + std::to_wstring(status) + L" for " + url;
            break;
        }

        wchar_t temp[MAX_PATH] = {};
        if (GetTempFileNameW(folder.c_str(), L"67d", 0, temp) == 0) {
            result.error = GetErrorText(L"Cannot create a temporary download file", GetLastError());
            break;
        }
        tempPath = temp;
        HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            result.error = GetErrorText(L"Cannot create " + tempPath, GetLastError());
            break;
        }

        bool writeOk = true;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                result.error = GetErrorText(L"Download interrupted", GetLastError());
                writeOk = false;
                break;
            }
            if (available == 0) {
                break;
            }
            std::string buffer(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read)) {
                result.error = GetErrorText(L"Download interrupted", GetLastError());
                writeOk = false;
                break;
            }
            DWORD written = 0;
            if (read && (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)) {
                result.error = GetErrorText(L"Cannot write " + tempPath, GetLastError());
                writeOk = false;
                break;
            }
        }
        CloseHandle(file);
        if (!writeOk) {
            break;
        }

        std::wstring name = suggestedName.empty() ? std::wstring()
                                                  : SanitizeFileName(suggestedName);
        if (name.empty()) {
            name = ContentDispositionName(HeaderValue(request, WINHTTP_QUERY_CONTENT_DISPOSITION));
        }
        if (name.empty()) {
            DWORD length = 0;
            WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &length);
            if (length > 0) {
                std::wstring finalUrl(length / sizeof(wchar_t) + 1, L'\0');
                if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, finalUrl.data(), &length)) {
                    finalUrl.resize(length / sizeof(wchar_t));
                    while (!finalUrl.empty() && finalUrl.back() == L'\0') finalUrl.pop_back();
                    name = FileNameFromUrl(finalUrl);
                }
            }
        }
        if (name.empty()) {
            name = FileNameFromUrl(url);
        }
        if (!HasExtension(name)) {
            const std::wstring extension =
                ExtensionForType(HeaderValue(request, WINHTTP_QUERY_CONTENT_TYPE));
            if (!extension.empty()) {
                name += extension;
            }
        }
        name = SanitizeFileName(name);
        const std::wstring destination = UniquePath(folder, name);
        if (!MoveFileExW(tempPath.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
            result.error = GetErrorText(L"Cannot save " + destination, GetLastError());
            break;
        }
        tempPath.clear();
        result.ok = true;
        result.path = destination;
        ok = true;
    } while (false);

    if (!tempPath.empty()) {
        DeleteFileW(tempPath.c_str());
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

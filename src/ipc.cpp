#include "ipc.h"

#include <utility>

std::wstring BuildPathPayload(const std::vector<std::wstring>& paths) {
    std::wstring payload;
    for (const auto& path : paths) {
        if (!path.empty()) {
            payload += path;
            payload.push_back(L'\0');
        }
    }
    if (payload.empty()) {
        payload.push_back(L'\0');
    }
    return payload;
}

std::vector<std::wstring> ParsePathPayload(const wchar_t* data, size_t units) {
    std::vector<std::wstring> paths;
    if (!data) {
        return paths;
    }
    std::wstring current;
    for (size_t i = 0; i < units; ++i) {
        const wchar_t c = data[i];
        if (c == L'\0') {
            if (!current.empty()) {
                paths.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        paths.push_back(std::move(current));
    }
    return paths;
}

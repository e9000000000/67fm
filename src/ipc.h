#pragma once

#include <cstddef>
#include <string>
#include <vector>

// WM_COPYDATA payload: UTF-16 paths separated by NUL. An empty list encodes as
// one NUL so the receiver can distinguish "no paths, add a tab" from malformed.
std::wstring BuildPathPayload(const std::vector<std::wstring>& paths);
std::vector<std::wstring> ParsePathPayload(const wchar_t* data, size_t units);

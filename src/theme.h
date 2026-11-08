#pragma once

#include <windows.h>
#include <algorithm>

namespace theme {

// The same flat, near-black palette as the other 67* apps. The file list is
// the "document" surface, so it shares kBg with the active tab.
constexpr COLORREF kBg = RGB(9, 9, 9);
constexpr COLORREF kPanel = RGB(15, 15, 15);
constexpr COLORREF kPanelAlt = RGB(23, 23, 23);
constexpr COLORREF kTabActive = RGB(9, 9, 9);
constexpr COLORREF kTabInactive = RGB(19, 19, 19);
constexpr COLORREF kTabHover = RGB(30, 30, 30);
constexpr COLORREF kBorder = RGB(36, 36, 36);
constexpr COLORREF kText = RGB(216, 216, 216);
constexpr COLORREF kTextDim = RGB(146, 146, 146);
constexpr COLORREF kTextFaint = RGB(98, 98, 98);
constexpr COLORREF kAccent = RGB(43, 107, 154);
constexpr COLORREF kSelection = RGB(29, 58, 85);
constexpr COLORREF kSelectionDim = RGB(21, 40, 57);
constexpr COLORREF kInput = RGB(21, 21, 21);
constexpr COLORREF kDanger = RGB(224, 82, 82);

// File list.
constexpr COLORREF kHeader = RGB(15, 15, 15);
constexpr COLORREF kHeaderText = RGB(128, 128, 128);
constexpr COLORREF kRowHover = RGB(18, 18, 18);
constexpr COLORREF kRowAlt = RGB(11, 11, 11);
constexpr COLORREF kFolder = RGB(92, 148, 222);
constexpr COLORREF kArchive = RGB(120, 164, 203);
constexpr COLORREF kFile = RGB(150, 150, 150);
constexpr COLORREF kSuccess = RGB(112, 196, 120);

constexpr int kMenuHeight = 30;
constexpr int kTabHeight = 28;
constexpr int kToolbarHeight = 38;
constexpr int kSearchHeight = 36;
constexpr int kTabWidth = 190;
constexpr int kTabMinWidth = 96;
constexpr int kTabMaxWidth = 240;
constexpr int kRowHeight = 25;
constexpr int kHeaderHeight = 27;

inline int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

inline UINT DpiOf(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) {
        const UINT dpi = fn(hwnd);
        if (dpi) {
            return dpi;
        }
    }
    return 96;
}

}  // namespace theme

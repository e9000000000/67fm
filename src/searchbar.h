#pragma once

#include <windows.h>

#include <string>

namespace searchbar_events {
constexpr UINT kFilter = WM_APP + 0x1C1;
constexpr UINT kClose = WM_APP + 0x1C2;
constexpr UINT kScope = WM_APP + 0x1C3;
}  // namespace searchbar_events

class SearchBar {
public:
    ~SearchBar();
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void show();
    void hide();
    bool isVisible() const { return visible_; }
    void focus();
    void clear();
    std::wstring query() const;
    bool recursive() const { return recursive_; }
    void setRecursive(bool recursive);
    void toggleRecursive();
    void setFont(const std::wstring& face, int points);

    void layout(int x, int y, int width, int height);
    int preferredHeight(HWND reference) const;
    void invalidate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR id, DWORD_PTR refData);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFont();
    void computeLayout();
    void paint(HDC target);
    void notify(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
    bool pointInClose(int x, int y) const;
    bool pointInScope(int x, int y) const;

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HWND edit_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::wstring fontFace_ = L"Segoe UI";
    int fontSize_ = 9;
    bool visible_ = false;
    bool closeHover_ = false;
    bool scopeHover_ = false;
    bool recursive_ = false;
    RECT closeRect_{};
    RECT scopeRect_{};
    RECT editRect_{};
};

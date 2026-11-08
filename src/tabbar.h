#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct TabVisual {
    std::wstring title;
    bool dirty = false;
    bool archive = false;
};

namespace tabbar_events {
constexpr UINT kSelect = WM_APP + 0x101;
constexpr UINT kClose = WM_APP + 0x102;
constexpr UINT kNew = WM_APP + 0x103;
constexpr UINT kReorder = WM_APP + 0x104;
constexpr UINT kContext = WM_APP + 0x105;
constexpr UINT kMenu = WM_APP + 0x106;
}  // namespace tabbar_events

class TabBar {
public:
    ~TabBar();
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void setTabs(std::vector<TabVisual> tabs, int active);
    void setActive(int active);
    void setFont(const std::wstring& face, int points);
    void layout(int x, int y, int width, int height);
    int preferredHeight(HWND reference) const;
    void invalidate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFont();
    void computeLayout();
    void paint(HDC dc);
    int hitTest(int x, int y) const;
    int dropIndexFromPoint(int x) const;
    bool pointIn(const RECT& rect, int x, int y) const;
    void notify(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
    void ensureActiveVisible();

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::wstring fontFace_ = L"Segoe UI";
    int fontSize_ = 9;

    std::vector<TabVisual> tabs_;
    int active_ = -1;
    int hover_ = -1;
    bool closeHover_ = false;
    bool menuHover_ = false;
    int scrollX_ = 0;

    bool dragging_ = false;
    int dragIndex_ = -1;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int dropIndex_ = -1;

    std::vector<RECT> tabRects_;
    std::vector<RECT> closeRects_;
    RECT menuRect_{};
    RECT plusRect_{};
};

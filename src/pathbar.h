#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct PathCrumb {
    std::wstring label;
    std::wstring path;
    bool archive = false;
};

namespace pathbar_events {
constexpr UINT kCrumb = WM_APP + 0x184;
constexpr UINT kGo = WM_APP + 0x185;
}  // namespace pathbar_events

class PathBar {
public:
    ~PathBar();
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void setState(std::vector<PathCrumb> crumbs, const std::wstring& pathText);
    void setMessage(const std::wstring& message);
    void setFont(const std::wstring& face, int points);
    void layout(int x, int y, int width, int height);
    int preferredHeight(HWND reference) const;
    void invalidate();

    void beginEdit();
    void endEdit();
    bool isEditing() const { return editing_; }
    std::wstring editedText() const;
    const std::wstring& crumbPath(int index) const;
    int crumbCount() const { return static_cast<int>(crumbs_.size()); }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR id, DWORD_PTR refData);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFont();
    void computeLayout();
    void paint(HDC target);
    void notify(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
    int crumbFromPoint(int x, int y) const;

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HWND pathEdit_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::wstring fontFace_ = L"Segoe UI";
    int fontSize_ = 9;

    std::vector<PathCrumb> crumbs_;
    std::wstring pathText_;
    std::wstring message_;
    bool editing_ = false;

    RECT pathArea_{};
    RECT messageArea_{};
    RECT addressArea_{};
    std::vector<RECT> crumbRects_;
    std::vector<int> crumbIndices_;
    int hoverCrumb_ = -1;
};

#include "searchbar.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kSearchBarClass[] = L"67fmSearchBar";
constexpr UINT kEditId = 1;
constexpr UINT_PTR kSubclassId = 1;

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

}  // namespace

bool SearchBar::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSearchBarClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kSearchBarClass, L"", WS_CHILD | WS_CLIPSIBLINGS |
                                                        WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    editBrush_ = CreateSolidBrush(theme::kInput);
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                            0, 0, 0, 0, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), instance,
                            nullptr);
    if (!edit_) {
        return false;
    }
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(theme::Scale(8, dpi_), theme::Scale(8, dpi_)));
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter this folder"));
    SetWindowSubclass(edit_, EditProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowTheme(edit_, L"DarkMode_Explorer", nullptr);
    return true;
}

SearchBar::~SearchBar() {
    if (editBrush_) {
        DeleteObject(editBrush_);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

void SearchBar::ensureFont() {
    const UINT dpi = DpiOfWindow(hwnd_);
    if (font_ && dpi == dpi_) {
        return;
    }
    dpi_ = dpi;
    if (font_) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(fontSize_, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE,
                        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace_.c_str());
    if (edit_) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(theme::Scale(8, dpi_), theme::Scale(8, dpi_)));
    }
}

void SearchBar::setFont(const std::wstring& face, int points) {
    const std::wstring nextFace = face.empty() ? L"Segoe UI" : face;
    points = std::max(6, std::min(36, points));
    if (points == fontSize_ && _wcsicmp(nextFace.c_str(), fontFace_.c_str()) == 0) {
        return;
    }
    fontFace_ = nextFace;
    fontSize_ = points;
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    ensureFont();
    computeLayout();
    invalidate();
}

void SearchBar::show() {
    visible_ = true;
    ShowWindow(hwnd_, SW_SHOW);
    computeLayout();
    focus();
    invalidate();
}

void SearchBar::hide() {
    visible_ = false;
    if (edit_ && GetFocus() == edit_) {
        SetFocus(parent_);
    }
    ShowWindow(hwnd_, SW_HIDE);
}

void SearchBar::focus() {
    if (edit_) {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
}

void SearchBar::clear() {
    if (edit_) {
        SetWindowTextW(edit_, L"");
        notify(searchbar_events::kFilter);
    }
}

std::wstring SearchBar::query() const {
    if (!edit_) {
        return {};
    }
    const int length = GetWindowTextLengthW(edit_);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit_, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SearchBar::layout(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    computeLayout();
    invalidate();
}

int SearchBar::preferredHeight(HWND reference) const {
    return theme::Scale(std::max(theme::kSearchHeight, fontSize_ + 27), DpiOfWindow(reference));
}

void SearchBar::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void SearchBar::notify(UINT message, WPARAM wParam, LPARAM lParam) {
    if (parent_) {
        SendMessageW(parent_, message, wParam, lParam);
    }
}

void SearchBar::computeLayout() {
    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = theme::Scale(12, dpi_);
    const int closeWidth = theme::Scale(std::max(28, fontSize_ + 16), dpi_);
    const int scopeWidth = theme::Scale(std::max(96, 96 + (fontSize_ - 9) * 4), dpi_);
    const int gap = theme::Scale(8, dpi_);
    const int controlHeight = std::max(theme::Scale(22, dpi_),
                                       theme::Scale(fontSize_ + 10, dpi_));
    const int controlTop = std::max(0, (height - controlHeight) / 2);
    const int editHeight = std::max(theme::Scale(18, dpi_),
                                    theme::Scale(fontSize_ + 8, dpi_));
    const int editTop = std::max(0, (height - editHeight) / 2);
    closeRect_ = RECT{width - margin - closeWidth, controlTop, width - margin,
                      controlTop + controlHeight};
    scopeRect_ = RECT{closeRect_.left - gap - scopeWidth, controlTop,
                      closeRect_.left - gap, controlTop + controlHeight};
    editRect_ = RECT{margin, editTop, scopeRect_.left - gap, editTop + editHeight};
    if (edit_) {
        SetWindowPos(edit_, nullptr, editRect_.left, editRect_.top,
                     std::max(1, static_cast<int>(editRect_.right - editRect_.left)),
                     std::max(1, static_cast<int>(editRect_.bottom - editRect_.top)),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SearchBar::paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    Fill(dc, client, theme::kPanel);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    RECT top{0, 0, width, theme::Scale(1, dpi_)};
    Fill(dc, top, theme::kBorder);

    if (closeHover_) {
        Fill(dc, closeRect_, theme::kPanelAlt);
    }
    if (scopeHover_ || recursive_) {
        Fill(dc, scopeRect_, recursive_ ? theme::kSelection : theme::kPanelAlt);
    }
    SetTextColor(dc, recursive_ ? theme::kText : theme::kTextDim);
    RECT scopeText = scopeRect_;
    DrawTextW(dc, recursive_ ? L"Subfolders" : L"This folder", -1, &scopeText,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

    const int cx = (closeRect_.left + closeRect_.right) / 2;
    const int cy = (closeRect_.top + closeRect_.bottom) / 2;
    const int half = theme::Scale(5, dpi_);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, theme::Scale(1, dpi_)),
                         closeHover_ ? theme::kText : theme::kTextFaint);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, cx - half, cy - half, nullptr);
    LineTo(dc, cx + half, cy + half);
    MoveToEx(dc, cx + half, cy - half, nullptr);
    LineTo(dc, cx - half, cy + half);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

bool SearchBar::pointInClose(int x, int y) const {
    return x >= closeRect_.left && x < closeRect_.right && y >= closeRect_.top &&
           y < closeRect_.bottom;
}

bool SearchBar::pointInScope(int x, int y) const {
    return x >= scopeRect_.left && x < scopeRect_.right && y >= scopeRect_.top &&
           y < scopeRect_.bottom;
}

void SearchBar::toggleRecursive() {
    setRecursive(!recursive_);
    notify(searchbar_events::kScope);
}

void SearchBar::setRecursive(bool recursive) {
    recursive_ = recursive;
    if (edit_) {
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(recursive_ ? L"Filter folder and subfolders"
                                                         : L"Filter this folder"));
    }
    invalidate();
}

LRESULT CALLBACK SearchBar::EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchBar*>(refData);
    if (self && message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            self->notify(searchbar_events::kClose);
            return 0;
        }
        if (wParam == VK_RETURN) {
            SetFocus(self->parent_);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchBar::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SearchBar* self = reinterpret_cast<SearchBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SearchBar*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->hwnd_ = hwnd;
        }
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SearchBar::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        paint(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        dpi_ = DpiOfWindow(hwnd_);
        ensureFont();
        computeLayout();
        invalidate();
        return 0;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        ensureFont();
        computeLayout();
        invalidate();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme::kText);
        SetBkColor(dc, theme::kInput);
        return reinterpret_cast<LRESULT>(editBrush_);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kEditId && HIWORD(wParam) == EN_CHANGE) {
            notify(searchbar_events::kFilter);
        }
        return 0;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const bool closeHover = pointInClose(x, y);
        const bool scopeHover = pointInScope(x, y);
        if (closeHover != closeHover_ || scopeHover != scopeHover_) {
            closeHover_ = closeHover;
            scopeHover_ = scopeHover;
            invalidate();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (closeHover_ || scopeHover_) {
            closeHover_ = false;
            scopeHover_ = false;
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (pointInClose(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            notify(searchbar_events::kClose);
            return 0;
        }
        if (pointInScope(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            toggleRecursive();
            return 0;
        }
        if (edit_) {
            SetFocus(edit_);
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

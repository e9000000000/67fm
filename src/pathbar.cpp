#include "pathbar.h"

#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kPathBarClass[] = L"67fmPathBar";
constexpr UINT kPathEditId = 1;
constexpr UINT_PTR kSubclassId = 1;

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

std::wstring ControlText(HWND edit) {
    if (!edit) {
        return {};
    }
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

int MeasureText(HDC dc, HFONT font, const std::wstring& text) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, oldFont);
    return size.cx;
}

}  // namespace

bool PathBar::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPathBarClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kPathBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                                        WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    editBrush_ = CreateSolidBrush(theme::kInput);

    pathEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                                0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathEditId)),
                                instance, nullptr);
    if (!pathEdit_) {
        return false;
    }
    SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(pathEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(theme::Scale(8, dpi_), theme::Scale(8, dpi_)));
    SetWindowSubclass(pathEdit_, EditProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowTheme(pathEdit_, L"DarkMode_Explorer", nullptr);
    ShowWindow(pathEdit_, SW_HIDE);
    return true;
}

PathBar::~PathBar() {
    if (editBrush_) {
        DeleteObject(editBrush_);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

void PathBar::ensureFont() {
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
    if (pathEdit_) {
        SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(pathEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(theme::Scale(8, dpi_), theme::Scale(8, dpi_)));
    }
}

void PathBar::setFont(const std::wstring& face, int points) {
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

void PathBar::setState(std::vector<PathCrumb> crumbs, const std::wstring& pathText) {
    crumbs_ = std::move(crumbs);
    pathText_ = pathText;
    computeLayout();
    invalidate();
}

void PathBar::setMessage(const std::wstring& message) {
    if (message_ == message) {
        return;
    }
    message_ = message;
    computeLayout();
    invalidate();
}

void PathBar::layout(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    computeLayout();
    invalidate();
}

int PathBar::preferredHeight(HWND reference) const {
    return theme::Scale(std::max(theme::kToolbarHeight, fontSize_ + 29), DpiOfWindow(reference));
}

void PathBar::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PathBar::notify(UINT message, WPARAM wParam, LPARAM lParam) {
    if (parent_) {
        SendMessageW(parent_, message, wParam, lParam);
    }
}

void PathBar::computeLayout() {
    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = theme::Scale(8, dpi_);
    const int gap = theme::Scale(8, dpi_);
    int messageWidth = 0;
    if (!message_.empty()) {
        HDC dc = GetDC(hwnd_);
        messageWidth = MeasureText(dc, font_, message_);
        ReleaseDC(hwnd_, dc);
        const int maxWidth = width / 2;
        messageWidth = std::min(messageWidth, maxWidth);
    }
    messageArea_ = RECT{std::max(margin, width - margin - messageWidth), 0, width - margin,
                        height};
    pathArea_ = RECT{margin, 0, messageWidth > 0 ? messageArea_.left - gap : width - margin,
                     height};
    if (pathArea_.right < pathArea_.left + theme::Scale(40, dpi_)) {
        pathArea_.right = pathArea_.left + theme::Scale(40, dpi_);
    }
    if (pathEdit_) {
        const int editHeight = std::max(theme::Scale(18, dpi_),
                                        theme::Scale(fontSize_ + 8, dpi_));
        const int editTop = std::max(0, (height - editHeight) / 2);
        addressArea_ = RECT{pathArea_.left, editTop, pathArea_.right, editTop + editHeight};
        SetWindowPos(pathEdit_, nullptr, addressArea_.left, addressArea_.top,
                     std::max(1, static_cast<int>(addressArea_.right - addressArea_.left)),
                     std::max(1, static_cast<int>(addressArea_.bottom - addressArea_.top)),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    crumbRects_.clear();
    crumbIndices_.clear();
    if (!crumbs_.empty() && pathArea_.right > pathArea_.left) {
        HDC dc = GetDC(hwnd_);
        std::vector<int> widths(crumbs_.size());
        for (size_t i = 0; i < crumbs_.size(); ++i) {
            widths[i] = MeasureText(dc, font_, crumbs_[i].label);
        }
        const int separator = MeasureText(dc, font_, L"\\") + theme::Scale(2, dpi_);
        const int ellipsisWidth = MeasureText(dc, font_, L"\u2026") + theme::Scale(2, dpi_);
        ReleaseDC(hwnd_, dc);

        const int available = pathArea_.right - pathArea_.left;
        int used = 0;
        std::vector<int> keep;
        for (int i = static_cast<int>(crumbs_.size()) - 1; i >= 0; --i) {
            const int needed = widths[i] + (keep.empty() ? 0 : separator);
            if (used + needed > available && !keep.empty()) {
                break;
            }
            used += needed;
            keep.push_back(i);
        }
        std::reverse(keep.begin(), keep.end());
        int cursor = pathArea_.left;
        if (keep.size() < crumbs_.size()) {
            crumbIndices_.push_back(-1);
            crumbRects_.push_back(RECT{cursor, theme::Scale(4, dpi_),
                                       cursor + ellipsisWidth, height - theme::Scale(4, dpi_)});
            cursor += ellipsisWidth;
        }
        for (size_t k = 0; k < keep.size(); ++k) {
            const int index = keep[k];
            if (!crumbIndices_.empty()) {
                cursor += separator;
            }
            RECT rect{cursor, theme::Scale(4, dpi_), cursor + widths[index],
                      height - theme::Scale(4, dpi_)};
            crumbRects_.push_back(rect);
            crumbIndices_.push_back(index);
            cursor = rect.right;
        }
    }
}

void PathBar::paint(HDC target) {
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

    if (!editing_) {
        const int separator = MeasureText(dc, font_, L"\\") + theme::Scale(2, dpi_);
        for (size_t i = 0; i < crumbRects_.size(); ++i) {
            const int index = crumbIndices_[i];
            RECT rect = crumbRects_[i];
            if (i > 0) {
                SetTextColor(dc, theme::kTextFaint);
                RECT sepRect{rect.left - separator, rect.top, rect.left, rect.bottom};
                DrawTextW(dc, L"\\", -1, &sepRect,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            }
            if (index < 0) {
                SetTextColor(dc, theme::kTextFaint);
                DrawTextW(dc, L"\u2026", -1, &rect,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                continue;
            }
            const bool last = i + 1 == crumbRects_.size();
            const bool hover = static_cast<int>(i) == hoverCrumb_;
            SetTextColor(dc, hover ? theme::kText
                                   : (crumbs_[index].archive
                                          ? theme::kArchive
                                          : (last ? theme::kText : theme::kTextDim)));
            DrawTextW(dc, crumbs_[index].label.c_str(), -1, &rect,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    if (!message_.empty() && messageArea_.right > messageArea_.left) {
        SetTextColor(dc, theme::kTextDim);
        DrawTextW(dc, message_.c_str(), -1, &messageArea_,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    RECT border{0, height - theme::Scale(1, dpi_), width, height};
    Fill(dc, border, theme::kBorder);
    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

int PathBar::crumbFromPoint(int x, int y) const {
    for (size_t i = 0; i < crumbRects_.size(); ++i) {
        const RECT& rect = crumbRects_[i];
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PathBar::beginEdit() {
    editing_ = true;
    SetWindowTextW(pathEdit_, pathText_.c_str());
    ShowWindow(pathEdit_, SW_SHOW);
    SetFocus(pathEdit_);
    SendMessageW(pathEdit_, EM_SETSEL, 0, -1);
    invalidate();
}

void PathBar::endEdit() {
    if (!editing_) {
        return;
    }
    editing_ = false;
    ShowWindow(pathEdit_, SW_HIDE);
    SetFocus(parent_);
    invalidate();
}

std::wstring PathBar::editedText() const {
    return ControlText(pathEdit_);
}

const std::wstring& PathBar::crumbPath(int index) const {
    static const std::wstring empty;
    return index >= 0 && index < static_cast<int>(crumbs_.size()) ? crumbs_[index].path : empty;
}

LRESULT CALLBACK PathBar::EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<PathBar*>(refData);
    if (self && message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            const std::wstring text = self->editedText();
            self->endEdit();
            if (!text.empty()) {
                self->notify(pathbar_events::kGo);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->endEdit();
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK PathBar::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PathBar* self = reinterpret_cast<PathBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<PathBar*>(create->lpCreateParams);
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

LRESULT PathBar::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int crumb = editing_ ? -1 : crumbFromPoint(x, y);
        if (crumb != hoverCrumb_) {
            hoverCrumb_ = crumb;
            invalidate();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hoverCrumb_ != -1) {
            hoverCrumb_ = -1;
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (!editing_) {
            const int crumb = crumbFromPoint(x, y);
            if (crumb >= 0 && crumb < static_cast<int>(crumbIndices_.size()) &&
                crumbIndices_[crumb] >= 0) {
                notify(pathbar_events::kCrumb, static_cast<WPARAM>(crumbIndices_[crumb]));
                return 0;
            }
            if (x >= pathArea_.left && x < pathArea_.right) {
                beginEdit();
                return 0;
            }
        }
        break;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

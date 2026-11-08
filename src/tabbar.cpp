#include "tabbar.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>

#include "taborder.h"
#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kTabBarClass[] = L"67fmTabBar";

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FrameRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

}  // namespace

bool TabBar::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kTabBarClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kTabBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = DpiOfWindow(parent);
    ensureFont();
    return true;
}

TabBar::~TabBar() {
    if (font_) {
        DeleteObject(font_);
    }
}

void TabBar::ensureFont() {
    const UINT dpi = DpiOfWindow(hwnd_);
    if (font_ && dpi == dpi_) {
        return;
    }
    dpi_ = dpi;
    if (font_) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(fontSize_, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace_.c_str());
}

void TabBar::setFont(const std::wstring& face, int points) {
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
    if (hwnd_) {
        computeLayout();
    }
    invalidate();
}

void TabBar::setTabs(std::vector<TabVisual> tabs, int active) {
    tabs_ = std::move(tabs);
    active_ = active;
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
        active_ = tabs_.empty() ? -1 : 0;
    }
    ensureActiveVisible();
    invalidate();
}

void TabBar::setActive(int active) {
    active_ = active;
    ensureActiveVisible();
    invalidate();
}

void TabBar::layout(int x, int y, int width, int height) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        dpi_ = DpiOfWindow(hwnd_);
        ensureFont();
        invalidate();
    }
}

int TabBar::preferredHeight(HWND reference) const {
    return theme::Scale(std::max(theme::kTabHeight, fontSize_ + 19), DpiOfWindow(reference));
}

void TabBar::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TabBar::notify(UINT message, WPARAM wParam, LPARAM lParam) {
    if (parent_) {
        SendMessageW(parent_, message, wParam, lParam);
    }
}

int TabBar::hitTest(int x, int y) const {
    for (size_t i = 0; i < tabRects_.size(); ++i) {
        if (pointIn(tabRects_[i], x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int TabBar::dropIndexFromPoint(int x) const {
    for (size_t i = 0; i < tabRects_.size(); ++i) {
        const int center = (tabRects_[i].left + tabRects_[i].right) / 2;
        if (x < center) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(tabRects_.size());
}

bool TabBar::pointIn(const RECT& rect, int x, int y) const {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void TabBar::computeLayout() {
    tabRects_.clear();
    closeRects_.clear();
    plusRect_ = {};

    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom;
    const int burgerWidth = theme::Scale(32, dpi_);
    const int plusWidth = theme::Scale(34, dpi_);
    const int count = static_cast<int>(tabs_.size());
    const int available = std::max(1, width - burgerWidth - plusWidth);
    int tabWidth = 0;
    if (count > 0) {
        tabWidth = std::max(theme::Scale(theme::kTabMinWidth, dpi_), available / count);
        if (tabWidth > available) {
            tabWidth = available;
        }
    }

    menuRect_ = {0, 0, burgerWidth, height};
    plusRect_ = {width - plusWidth, 0, width, height};
    int x = burgerWidth - scrollX_;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        RECT tab{x, 0, x + tabWidth, height};
        tabRects_.push_back(tab);
        RECT close{tab.right - theme::Scale(28, dpi_), theme::Scale(2, dpi_),
                   tab.right - theme::Scale(2, dpi_), tab.bottom - theme::Scale(2, dpi_)};
        closeRects_.push_back(close);
        x += tabWidth;
    }

    // Keep the plus button fixed at the right edge; only the tabs scroll.
    const int contentRight = burgerWidth + static_cast<int>(tabs_.size()) * tabWidth;
    const int viewportRight = width - plusWidth;
    if (contentRight <= viewportRight) {
        if (scrollX_ != 0) {
            scrollX_ = 0;
        }
    } else if (scrollX_ > contentRight - viewportRight) {
        scrollX_ = contentRight - viewportRight;
    }
    if (scrollX_ < 0) {
        scrollX_ = 0;
    }
}

void TabBar::ensureActiveVisible() {
    if (!hwnd_ || active_ < 0) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    computeLayout();
    if (active_ >= static_cast<int>(tabRects_.size())) {
        return;
    }
    const int width = client.right - client.left;
    const int leftEdge = theme::Scale(32, dpi_);
    const int rightEdge = width - theme::Scale(34, dpi_);
    const RECT& rect = tabRects_[active_];
    if (rect.left < leftEdge) {
        scrollX_ += rect.left - leftEdge;
    } else if (rect.right > rightEdge) {
        scrollX_ += rect.right - rightEdge;
    }
    if (scrollX_ < 0) {
        scrollX_ = 0;
    }
}

void TabBar::paint(HDC target) {
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

    FillRectColor(dc, client, theme::kPanel);

    computeLayout();
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    const auto drawCross = [&](const RECT& rect, COLORREF color, int half) {
        const int cx = (rect.left + rect.right) / 2;
        const int cy = (rect.top + rect.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(2, theme::Scale(1, dpi_)), color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - half, cy - half, nullptr);
        LineTo(dc, cx + half, cy + half);
        MoveToEx(dc, cx + half, cy - half, nullptr);
        LineTo(dc, cx - half, cy + half);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    };

    if (menuHover_) {
        FillRectColor(dc, menuRect_, theme::kPanelAlt);
    }
    {
        const int cx = (menuRect_.left + menuRect_.right) / 2;
        const int cy = (menuRect_.top + menuRect_.bottom) / 2;
        const int half = theme::Scale(6, dpi_);
        int thickness = std::max(2, theme::Scale(1, dpi_));
        if ((thickness % 2) != 0) {
            ++thickness;
        }
        const int step = theme::Scale(4, dpi_);
        const COLORREF color = menuHover_ ? theme::kText : theme::kTextDim;
        for (const int offset : {-step, 0, step}) {
            RECT bar{cx - half, cy + offset - thickness / 2,
                     cx + half, cy + offset + thickness / 2};
            FillRectColor(dc, bar, color);
        }
    }

    const int viewportLeft = theme::Scale(32, dpi_);
    const int viewportRight = width - theme::Scale(34, dpi_);
    const int savedClip = SaveDC(dc);
    IntersectClipRect(dc, viewportLeft, 0, viewportRight, height);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        const RECT& rect = tabRects_[i];
        if (rect.right < 0 || rect.left > width) {
            continue;
        }
        const bool active = static_cast<int>(i) == active_;
        const bool hover = static_cast<int>(i) == hover_;
        const bool closeHover = closeHover_ && hover;

        COLORREF background = active ? theme::kTabActive : (hover ? theme::kTabHover : theme::kTabInactive);
        FillRectColor(dc, rect, background);

        int textLeft = rect.left + theme::Scale(12, dpi_);
        if (tabs_[i].dirty || tabs_[i].archive) {
            const int dotSize = theme::Scale(7, dpi_);
            const int dotX = textLeft;
            const int dotY = (rect.top + rect.bottom) / 2 - dotSize / 2;
            HBRUSH dotBrush = CreateSolidBrush(tabs_[i].archive ? theme::kArchive : theme::kTextDim);
            HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, dotX, dotY, dotX + dotSize, dotY + dotSize);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(dotBrush);
            textLeft += dotSize + theme::Scale(8, dpi_);
        }

        RECT textRect = rect;
        textRect.left = textLeft;
        textRect.right = rect.right - theme::Scale(30, dpi_);
        textRect.top += theme::Scale(1, dpi_);
        SetTextColor(dc, active ? theme::kText : theme::kTextDim);
        DrawTextW(dc, tabs_[i].title.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        RECT closeRect = closeRects_[i];
        if (closeHover) {
            FillRectColor(dc, closeRect, theme::kPanelAlt);
        }
        drawCross(closeRect, closeHover ? theme::kText : theme::kTextFaint,
                  theme::Scale(6, dpi_));
    }

    // Drag insertion marker.
    if (dragging_ && dropIndex_ >= 0 && !tabs_.empty()) {
        int lineX = 0;
        if (dropIndex_ <= 0) {
            lineX = tabRects_.front().left - theme::Scale(2, dpi_);
        } else if (dropIndex_ >= static_cast<int>(tabRects_.size())) {
            lineX = tabRects_.back().right + theme::Scale(1, dpi_);
        } else {
            lineX = tabRects_[dropIndex_].left - theme::Scale(2, dpi_);
        }
        RECT marker{lineX, theme::Scale(4, dpi_), lineX + theme::Scale(2, dpi_), height - theme::Scale(4, dpi_)};
        FillRectColor(dc, marker, theme::kAccent);
    }
    RestoreDC(dc, savedClip);

    // New-tab plus. Draw it manually so it is exactly centred in its hit area.
    if (plusRect_.right >= 0 && plusRect_.left <= width) {
        const int cx = (plusRect_.left + plusRect_.right) / 2;
        const int cy = (plusRect_.top + plusRect_.bottom) / 2;
        const int half = theme::Scale(6, dpi_);
        int thickness = std::max(2, theme::Scale(1, dpi_));
        if ((thickness % 2) != 0) {
            ++thickness;
        }
        RECT horizontal{cx - half, cy - thickness / 2,
                        cx + half, cy + thickness / 2};
        RECT vertical{cx - thickness / 2, cy - half,
                      cx + thickness / 2, cy + half};
        FillRectColor(dc, horizontal, theme::kTextDim);
        FillRectColor(dc, vertical, theme::kTextDim);
    }

    // Bottom border.
    RECT bottom{0, height - theme::Scale(1, dpi_), width, height};
    FillRectColor(dc, bottom, theme::kBorder);

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK TabBar::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TabBar* self = reinterpret_cast<TabBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<TabBar*>(create->lpCreateParams);
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

LRESULT TabBar::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
        computeLayout();
        invalidate();
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        computeLayout();
        if (hitTest(point.x, point.y) >= 0 ||
            pointIn(plusRect_, point.x, point.y) ||
            pointIn(menuRect_, point.x, point.y)) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (dragIndex_ >= 0) {
            if (!dragging_ &&
                (std::abs(x - dragStartX_) >= theme::Scale(10, dpi_) ||
                 std::abs(y - dragStartY_) >= theme::Scale(10, dpi_))) {
                dragging_ = true;
            }
            if (dragging_) {
                const int drop = dropIndexFromPoint(x);
                if (drop != dropIndex_) {
                    dropIndex_ = drop;
                    invalidate();
                }
                return 0;
            }
        }
        const int hover = hitTest(x, y);
        const bool overMenu = pointIn(menuRect_, x, y);
        if (overMenu != menuHover_) {
            menuHover_ = overMenu;
            invalidate();
        }
        if (overMenu) {
            if (hover_ != -1 || closeHover_) {
                hover_ = -1;
                closeHover_ = false;
                invalidate();
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        bool closeHover = false;
        if (hover >= 0 && hover < static_cast<int>(closeRects_.size())) {
            closeHover = pointIn(closeRects_[hover], x, y);
        }
        if (hover != hover_ || closeHover != closeHover_) {
            hover_ = hover;
            closeHover_ = closeHover;
            invalidate();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hover_ != -1 || closeHover_ || menuHover_) {
            hover_ = -1;
            closeHover_ = false;
            menuHover_ = false;
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        computeLayout();
        if (pointIn(menuRect_, x, y)) {
            notify(tabbar_events::kMenu);
            return 0;
        }
        if (pointIn(plusRect_, x, y)) {
            notify(tabbar_events::kNew);
            return 0;
        }
        const int index = hitTest(x, y);
        if (index >= 0) {
            if (index < static_cast<int>(closeRects_.size()) && pointIn(closeRects_[index], x, y)) {
                notify(tabbar_events::kClose, static_cast<WPARAM>(index));
            } else {
                notify(tabbar_events::kSelect, static_cast<WPARAM>(index));
                dragIndex_ = index;
                dragStartX_ = x;
                dragStartY_ = y;
                dropIndex_ = index;
                dragging_ = false;
                SetCapture(hwnd_);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (dragIndex_ >= 0) {
            const int from = dragIndex_;
            const int drop = dropIndex_;
            const bool wasDragging = dragging_;
            dragging_ = false;
            dragIndex_ = -1;
            dropIndex_ = -1;
            if (GetCapture() == hwnd_) {
                ReleaseCapture();
            }
            if (wasDragging) {
                const int target = taborder::TargetFromDrop(drop, from);
                if (target != from && target >= 0) {
                    notify(tabbar_events::kReorder, static_cast<WPARAM>(from),
                           static_cast<LPARAM>(target));
                }
            }
            invalidate();
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (dragIndex_ >= 0) {
            dragging_ = false;
            dragIndex_ = -1;
            dropIndex_ = -1;
            invalidate();
        }
        return 0;
    case WM_MBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int index = hitTest(x, y);
        if (index >= 0) {
            notify(tabbar_events::kClose, static_cast<WPARAM>(index));
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        computeLayout();
        const int index = hitTest(x, y);
        if (index >= 0) {
            notify(tabbar_events::kSelect, static_cast<WPARAM>(index));
            notify(tabbar_events::kContext, static_cast<WPARAM>(index),
                   static_cast<LPARAM>(MAKELPARAM(x, y)));
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scrollX_ -= (delta / WHEEL_DELTA) * theme::Scale(80, dpi_);
        computeLayout();
        invalidate();
        return 0;
    }
    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        ensureFont();
        invalidate();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

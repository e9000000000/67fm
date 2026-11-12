#include "menubar.h"

#include <windowsx.h>

#include <algorithm>

#include "commands.h"
#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kMenuBarClass[] = L"67fmMenuBar";
constexpr wchar_t kMenuPopupClass[] = L"67fmMenuPopup";

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

MenuItem MakeItem(UINT id, const wchar_t* label, const wchar_t* shortcut = L"",
                  bool enabled = true, bool checked = false) {
    MenuItem item;
    item.id = id;
    item.label = label;
    item.shortcut = shortcut;
    item.enabled = enabled;
    item.checked = checked;
    return item;
}

MenuItem MakeSeparator() {
    MenuItem item;
    item.separator = true;
    return item;
}

}  // namespace

bool MenuBar::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMenuBarClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW pc{};
    pc.cbSize = sizeof(pc);
    pc.style = CS_HREDRAW | CS_VREDRAW | CS_SAVEBITS;
    pc.lpfnWndProc = PopupWndProc;
    pc.hInstance = instance;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.lpszClassName = kMenuPopupClass;
    if (!RegisterClassExW(&pc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kMenuBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    dpi_ = DpiOfWindow(parent);
    ensureFont();
    rebuildMenus();
    return true;
}

void MenuBar::ensureFont() {
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

void MenuBar::setFont(const std::wstring& face, int points) {
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
        RECT client{};
        GetClientRect(hwnd_, &client);
        layout(0, 0, client.right, client.bottom);
    }
}

void MenuBar::layout(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();

    titleRects_.clear();
    HDC dc = GetDC(hwnd_);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    int cursorX = theme::Scale(10, dpi_);
    for (const auto& menu : menus_) {
        SIZE size{};
        GetTextExtentPoint32W(dc, menu.title.c_str(), static_cast<int>(menu.title.size()), &size);
        const int itemWidth = size.cx + theme::Scale(22, dpi_);
        RECT rect{cursorX, 0, cursorX + itemWidth, height};
        titleRects_.push_back(rect);
        cursorX += itemWidth;
    }
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd_, dc);
    invalidate();
}

int MenuBar::preferredHeight(HWND reference) const {
    return theme::Scale(std::max(28, fontSize_ + 19), DpiOfWindow(reference));
}

void MenuBar::setState(const MenuState& state) {
    state_ = state;
}

void MenuBar::rebuildMenus() {
    menus_.clear();
    const bool has = state_.hasTab;
    const bool selection = state_.hasSelection;

    MenuDef file;
    file.title = L"File";
    file.items.push_back(MakeItem(kCmdNewTab, L"New Tab", L"Ctrl+T"));
    file.items.push_back(MakeItem(kCmdNewFolder, L"New Folder", L"Ctrl+Shift+N", has));
    file.items.push_back(MakeItem(kCmdNewFile, L"New File", L"Ctrl+Alt+N", has));
    file.items.push_back(MakeSeparator());
    file.items.push_back(MakeItem(kCmdOpen, L"Open", L"Enter", selection));
    file.items.push_back(MakeItem(kCmdOpenExplorer, L"Show in Explorer", L"", selection));
    file.items.push_back(MakeItem(kCmdExtract, L"Extract...", L"Ctrl+E", state_.canExtract && selection));
    file.items.push_back(MakeItem(kCmdExtractAll, L"Extract All...", L"", state_.canExtract && has));
    file.items.push_back(MakeSeparator());
    file.items.push_back(MakeItem(kCmdCloseTab, L"Close Tab", L"Ctrl+W", has));
    file.items.push_back(MakeItem(kCmdCloseOthers, L"Close Other Tabs", L"", has));
    file.items.push_back(MakeItem(kCmdCloseAll, L"Close All Tabs", L"", has));
    file.items.push_back(MakeSeparator());
    file.items.push_back(MakeItem(kCmdExit, L"Exit"));
    menus_.push_back(std::move(file));

    MenuDef edit;
    edit.title = L"Edit";
    edit.items.push_back(MakeItem(kCmdCut, L"Cut", L"Ctrl+X", selection));
    edit.items.push_back(MakeItem(kCmdCopy, L"Copy", L"Ctrl+C", selection));
    edit.items.push_back(MakeItem(kCmdPaste, L"Paste", L"Ctrl+V", has));
    edit.items.push_back(MakeSeparator());
    edit.items.push_back(MakeItem(kCmdRename, L"Rename", L"F2", selection));
    edit.items.push_back(MakeItem(kCmdDelete, L"Delete", L"Del", selection));
    edit.items.push_back(MakeSeparator());
    edit.items.push_back(MakeItem(kCmdSelectAll, L"Select All", L"Ctrl+A", has));
    edit.items.push_back(MakeItem(kCmdInvertSelection, L"Invert Selection", L"", has));
    edit.items.push_back(MakeSeparator());
    edit.items.push_back(MakeItem(kCmdCopyPath, L"Copy Path", L"", selection));
    edit.items.push_back(MakeItem(kCmdProperties, L"Properties", L"Alt+Enter", selection));
    menus_.push_back(std::move(edit));

    MenuDef view;
    view.title = L"View";
    view.items.push_back(MakeItem(kCmdRefresh, L"Refresh", L"F5", has));
    view.items.push_back(MakeSeparator());
    view.items.push_back(MakeItem(kCmdBack, L"Back", L"Alt+Left", state_.canBack));
    view.items.push_back(MakeItem(kCmdForward, L"Forward", L"Alt+Right", state_.canForward));
    view.items.push_back(MakeItem(kCmdUp, L"Up", L"Alt+Up", state_.canUp));
    view.items.push_back(MakeSeparator());
    view.items.push_back(MakeItem(kCmdAddress, L"Address Bar", L"Ctrl+L", has));
    view.items.push_back(MakeItem(kCmdFind, L"Find / Filter", L"Ctrl+F", has));
    view.items.push_back(MakeSeparator());
    view.items.push_back(MakeItem(kCmdShowHidden, L"Show Hidden Files", L"", true, state_.showHidden));
    view.items.push_back(MakeSeparator());
    view.items.push_back(MakeItem(kCmdSortName, L"Sort by Name", L"", true, state_.sortColumn == 0));
    view.items.push_back(MakeItem(kCmdSortSize, L"Sort by Size", L"", true, state_.sortColumn == 1));
    view.items.push_back(MakeItem(kCmdSortDate, L"Sort by Date", L"", true, state_.sortColumn == 2));
    view.items.push_back(MakeItem(kCmdSortType, L"Sort by Type", L"", true, state_.sortColumn == 3));
    view.items.push_back(MakeItem(kCmdSortAscending, L"Ascending", L"", true, state_.sortAscending));
    view.items.push_back(MakeItem(kCmdSortDescending, L"Descending", L"", true, !state_.sortAscending));
    menus_.push_back(std::move(view));

    MenuDef go;
    go.title = L"Go";
    go.items.push_back(MakeItem(kCmdDesktop, L"Desktop"));
    go.items.push_back(MakeItem(kCmdDocuments, L"Documents"));
    go.items.push_back(MakeItem(kCmdDownloads, L"Downloads"));
    go.items.push_back(MakeItem(kCmdPictures, L"Pictures"));
    go.items.push_back(MakeItem(kCmdVideos, L"Videos"));
    go.items.push_back(MakeItem(kCmdHome, L"Home"));
    go.items.push_back(MakeSeparator());
    go.items.push_back(MakeItem(kCmdThisPc, L"This PC"));
    menus_.push_back(std::move(go));

    MenuDef help;
    help.title = L"Help";
    help.items.push_back(MakeItem(kCmdAbout, L"About 67fm"));
    menus_.push_back(std::move(help));
}

void MenuBar::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

int MenuBar::titleFromPoint(int x, int y) const {
    for (size_t i = 0; i < titleRects_.size(); ++i) {
        const RECT& rect = titleRects_[i];
        if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MenuBar::paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (titleRects_.empty()) {
        layout(0, 0, width, height);
    }

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRectColor(dc, client, theme::kPanel);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    for (size_t i = 0; i < titleRects_.size() && i < menus_.size(); ++i) {
        const RECT& rect = titleRects_[i];
        const bool active = static_cast<int>(i) == openMenu_;
        const bool hover = static_cast<int>(i) == hoverTitle_;
        if (active || hover) {
            FillRectColor(dc, rect, theme::kPanelAlt);
        }
        RECT textRect = rect;
        SetTextColor(dc, active || hover ? theme::kText : theme::kTextDim);
        DrawTextW(dc, menus_[i].title.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        if (active) {
            RECT accent{rect.left + theme::Scale(4, dpi_), rect.bottom - theme::Scale(2, dpi_),
                        rect.right - theme::Scale(4, dpi_), rect.bottom};
            FillRectColor(dc, accent, theme::kAccent);
        }
    }

    RECT border{0, height - theme::Scale(1, dpi_), width, height};
    FillRectColor(dc, border, theme::kBorder);

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

int MenuBar::popupItemHeight() const {
    return theme::Scale(std::max(26, fontSize_ + 17), dpi_);
}

bool MenuBar::hasActivePopup() const {
    return contextMode_ || (openMenu_ >= 0 && openMenu_ < static_cast<int>(menus_.size()));
}

const std::vector<MenuItem>& MenuBar::activeItems() const {
    static const std::vector<MenuItem> empty;
    if (contextMode_) {
        return contextItems_;
    }
    if (openMenu_ >= 0 && openMenu_ < static_cast<int>(menus_.size())) {
        return menus_[openMenu_].items;
    }
    return empty;
}

int MenuBar::popupWidth() const {
    if (!hasActivePopup()) {
        return theme::Scale(200, dpi_);
    }
    HDC dc = GetDC(hwnd_);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    int maxWidth = theme::Scale(220, dpi_);
    for (const auto& item : activeItems()) {
        if (item.separator) {
            continue;
        }
        SIZE label{};
        SIZE shortcut{};
        GetTextExtentPoint32W(dc, item.label.c_str(), static_cast<int>(item.label.size()), &label);
        GetTextExtentPoint32W(dc, item.shortcut.c_str(), static_cast<int>(item.shortcut.size()), &shortcut);
        int width = theme::Scale(24, dpi_) + label.cx + theme::Scale(20, dpi_);
        if (!item.shortcut.empty()) {
            width += theme::Scale(18, dpi_) + shortcut.cx;
        }
        maxWidth = std::max(maxWidth, width);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd_, dc);
    return maxWidth;
}

int MenuBar::popupHeight() const {
    if (!hasActivePopup()) {
        return theme::Scale(40, dpi_);
    }
    int height = theme::Scale(8, dpi_);
    for (const auto& item : activeItems()) {
        height += item.separator ? theme::Scale(9, dpi_) : popupItemHeight();
    }
    return height + theme::Scale(4, dpi_);
}

void MenuBar::showPopup(int index) {
    if (index < 0 || index >= static_cast<int>(menus_.size())) {
        return;
    }
    if (titleRects_.empty() && hwnd_) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        layout(0, 0, client.right, client.bottom);
    }
    if (titleRects_.empty()) {
        return;
    }
    openMenu_ = index;
    contextMode_ = false;
    contextItems_.clear();
    hoverItem_ = -1;
    pressedItem_ = -1;

    if (!popup_) {
        popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuPopupClass, L"",
                                 WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance_, this);
        if (!popup_) {
            openMenu_ = -1;
            return;
        }
    }

    const int width = popupWidth();
    const int height = popupHeight();
    RECT anchor = titleRects_[index];
    POINT point{anchor.left, anchor.bottom};
    ClientToScreen(hwnd_, &point);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (point.x + width > work.right) {
        point.x = work.right - width;
    }
    if (point.y + height > work.bottom) {
        point.y = std::max(work.top, anchor.top + static_cast<int>(anchor.bottom - anchor.top) - height);
        POINT above{anchor.left, anchor.top};
        ClientToScreen(hwnd_, &above);
        point.y = above.y - height;
        if (point.y < work.top) {
            point.y = work.top;
        }
    }
    if (point.x < work.left) {
        point.x = work.left;
    }

    SetWindowPos(popup_, HWND_TOPMOST, point.x, point.y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(popup_, SW_SHOW);
    SetForegroundWindow(popup_);
    SetFocus(popup_);
    SetCapture(popup_);
    InvalidateRect(popup_, nullptr, FALSE);
    invalidate();
}

void MenuBar::hidePopup() {
    const bool restoreFocus = popup_ && GetForegroundWindow() == popup_ && GetFocus() == popup_;
    if (popup_) {
        if (GetCapture() == popup_) {
            ReleaseCapture();
        }
        ShowWindow(popup_, SW_HIDE);
    }
    openMenu_ = -1;
    contextMode_ = false;
    contextItems_.clear();
    hoverItem_ = -1;
    pressedItem_ = -1;
    invalidate();
    if (restoreFocus && parent_) {
        SetFocus(parent_);
    }
}

void MenuBar::showContextMenu(POINT screenPoint, std::vector<MenuItem> items) {
    if (items.empty()) {
        return;
    }
    hidePopup();
    contextMode_ = true;
    contextItems_ = std::move(items);
    openMenu_ = -1;
    hoverItem_ = -1;
    pressedItem_ = -1;

    if (!popup_) {
        popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuPopupClass, L"",
                                 WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance_, this);
        if (!popup_) {
            contextMode_ = false;
            contextItems_.clear();
            return;
        }
    }

    const int width = popupWidth();
    const int height = popupHeight();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = screenPoint.x;
    int y = screenPoint.y;
    if (x + width > work.right) {
        x = work.right - width;
    }
    if (y + height > work.bottom) {
        y = screenPoint.y - height;
        if (y < work.top) {
            y = work.top;
        }
    }
    x = std::max(x, static_cast<int>(work.left));
    y = std::max(y, static_cast<int>(work.top));

    SetWindowPos(popup_, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(popup_, SW_SHOW);
    SetForegroundWindow(popup_);
    SetFocus(popup_);
    SetCapture(popup_);
    InvalidateRect(popup_, nullptr, FALSE);
}

void MenuBar::openMenu(int index) {
    if (index < 0 || index >= static_cast<int>(menus_.size())) {
        return;
    }
    if (contextMode_) {
        hidePopup();
    }
    if (openMenu_ == index) {
        hidePopup();
        return;
    }
    if (openMenu_ >= 0) {
        hidePopup();
    }
    rebuildMenus();
    showPopup(index);
}

void MenuBar::closeMenu() {
    hidePopup();
}

int MenuBar::itemFromPoint(int x, int y) const {
    (void)x;
    if (!hasActivePopup()) {
        return -1;
    }
    const int padding = theme::Scale(4, dpi_);
    int cursorY = padding;
    const auto& items = activeItems();
    for (size_t i = 0; i < items.size(); ++i) {
        const int height = items[i].separator ? theme::Scale(9, dpi_) : popupItemHeight();
        if (y >= cursorY && y < cursorY + height) {
            return static_cast<int>(i);
        }
        cursorY += height;
    }
    return -1;
}

void MenuBar::paintPopup(HDC target) {
    if (!hasActivePopup()) {
        return;
    }
    RECT client{};
    GetClientRect(popup_, &client);
    const int width = client.right;
    const int height = client.bottom;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRectColor(dc, client, theme::kPanel);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    const int padding = theme::Scale(4, dpi_);
    int cursorY = padding;
    const auto& items = activeItems();
    for (size_t i = 0; i < items.size(); ++i) {
        const MenuItem& item = items[i];
        if (item.separator) {
            const int lineY = cursorY + theme::Scale(4, dpi_);
            RECT line{theme::Scale(10, dpi_), lineY, width - theme::Scale(10, dpi_), lineY + 1};
            FillRectColor(dc, line, theme::kBorder);
            cursorY += theme::Scale(9, dpi_);
            continue;
        }
        RECT row{theme::Scale(5, dpi_), cursorY, width - theme::Scale(5, dpi_), cursorY + popupItemHeight()};
        if (static_cast<int>(i) == hoverItem_ && item.enabled) {
            FillRectColor(dc, row, theme::kPanelAlt);
        }
        if (item.checked) {
            RECT check{theme::Scale(8, dpi_), cursorY, theme::Scale(24, dpi_), cursorY + popupItemHeight()};
            SetTextColor(dc, theme::kAccent);
            DrawTextW(dc, L"\u2713", -1, &check,
                      DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }
        RECT labelRect{theme::Scale(28, dpi_), cursorY, width - theme::Scale(12, dpi_),
                       cursorY + popupItemHeight()};
        RECT shortcutRect{};
        if (!item.shortcut.empty()) {
            SIZE shortcutSize{};
            GetTextExtentPoint32W(dc, item.shortcut.c_str(),
                                  static_cast<int>(item.shortcut.size()), &shortcutSize);
            shortcutRect = RECT{labelRect.right - shortcutSize.cx, cursorY, labelRect.right,
                                cursorY + popupItemHeight()};
            labelRect.right = shortcutRect.left - theme::Scale(12, dpi_);
        }
        SetTextColor(dc, item.enabled ? theme::kText : theme::kTextFaint);
        DrawTextW(dc, item.label.c_str(), -1, &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

        if (!item.shortcut.empty()) {
            SetTextColor(dc, theme::kTextFaint);
            DrawTextW(dc, item.shortcut.c_str(), -1, &shortcutRect,
                      DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }
        cursorY += popupItemHeight();
    }

    RECT border{0, 0, width, height};
    HBRUSH borderBrush = CreateSolidBrush(theme::kBorder);
    FrameRect(dc, &border, borderBrush);
    DeleteObject(borderBrush);
    if (GetFocus() == popup_) {
        RECT inner{1, 1, width - 1, height - 1};
        HBRUSH shadow = CreateSolidBrush(theme::kAccent);
        FrameRect(dc, &inner, shadow);
        DeleteObject(shadow);
    }

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void MenuBar::executePopupItem(int index) {
    if (!hasActivePopup()) {
        return;
    }
    const auto& items = activeItems();
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return;
    }
    const MenuItem item = items[index];
    if (item.separator || !item.enabled) {
        return;
    }
    hidePopup();
    SendMessageW(parent_, WM_COMMAND, item.id, 0);
}

void MenuBar::movePopupSelection(int direction) {
    if (!hasActivePopup()) {
        return;
    }
    const auto& items = activeItems();
    if (items.empty()) {
        return;
    }
    int index = hoverItem_;
    if (index < 0 || index >= static_cast<int>(items.size())) {
        index = direction > 0 ? -1 : static_cast<int>(items.size());
    }
    for (int steps = 0; steps < static_cast<int>(items.size()); ++steps) {
        index = (index + direction + static_cast<int>(items.size())) % static_cast<int>(items.size());
        if (!items[index].separator && items[index].enabled) {
            hoverItem_ = index;
            InvalidateRect(popup_, nullptr, FALSE);
            return;
        }
    }
}

void MenuBar::notify(UINT id) {
    SendMessageW(parent_, WM_COMMAND, id, 0);
}

LRESULT CALLBACK MenuBar::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MenuBar* self = reinterpret_cast<MenuBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MenuBar*>(create->lpCreateParams);
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

LRESULT MenuBar::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        if (titleFromPoint(point.x, point.y) >= 0) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int title = titleFromPoint(x, y);
        if (title != hoverTitle_) {
            hoverTitle_ = title;
            invalidate();
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hoverTitle_ != -1) {
            hoverTitle_ = -1;
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const int title = titleFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (title >= 0) {
            openMenu(title);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        ensureFont();
        RECT client{};
        GetClientRect(hwnd_, &client);
        layout(0, 0, client.right, client.bottom);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK MenuBar::PopupWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MenuBar* self = reinterpret_cast<MenuBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MenuBar*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->popup_ = hwnd;
        }
    }
    if (self) {
        return self->handlePopupMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MenuBar::handlePopupMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(popup_, &ps);
        paintPopup(dc);
        EndPaint(popup_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        const int item = itemFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (item != hoverItem_) {
            hoverItem_ = item;
            InvalidateRect(popup_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, popup_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT client{};
        GetClientRect(popup_, &client);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (x < 0 || y < 0 || x >= client.right || y >= client.bottom) {
            hidePopup();
            return 0;
        }
        pressedItem_ = itemFromPoint(x, y);
        hoverItem_ = pressedItem_;
        InvalidateRect(popup_, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        RECT client{};
        GetClientRect(popup_, &client);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (x < 0 || y < 0 || x >= client.right || y >= client.bottom) {
            // The button-up that follows the click that opened the menu arrives
            // here because of SetCapture; do not treat it as an outside click.
            pressedItem_ = -1;
            return 0;
        }
        const int item = itemFromPoint(x, y);
        if (item >= 0 && item == pressedItem_) {
            executePopupItem(item);
        }
        pressedItem_ = -1;
        return 0;
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE:
            hidePopup();
            return 0;
        case VK_DOWN:
            movePopupSelection(1);
            return 0;
        case VK_UP:
            movePopupSelection(-1);
            return 0;
        case VK_LEFT:
        case VK_RIGHT: {
            if (openMenu_ >= 0) {
                const int next = (openMenu_ + (wParam == VK_RIGHT ? 1 : -1) +
                                  static_cast<int>(menus_.size())) % static_cast<int>(menus_.size());
                hidePopup();
                openMenu(next);
            }
            return 0;
        }
        case VK_RETURN:
        case VK_SPACE:
            if (hoverItem_ >= 0) {
                executePopupItem(hoverItem_);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_ACTIVATEAPP:
        if (!wParam) {
            hidePopup();
        }
        return 0;
    case WM_CLOSE:
        hidePopup();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(popup_, message, wParam, lParam);
}

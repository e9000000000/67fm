#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct MenuState {
    bool hasTab = false;
    bool hasSelection = false;
    bool canBack = false;
    bool canForward = false;
    bool canUp = false;
    bool showHidden = false;
    bool canExtract = false;
    bool isDefaultFileManager = false;
    int sortColumn = 0;
    bool sortAscending = true;
};

struct MenuItem {
    UINT id = 0;
    std::wstring label;
    std::wstring shortcut;
    bool separator = false;
    bool enabled = true;
    bool checked = false;
};

struct MenuDef {
    std::wstring title;
    std::vector<MenuItem> items;
};

class MenuBar {
public:
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }
    void layout(int x, int y, int width, int height);
    int preferredHeight(HWND reference) const;
    void setState(const MenuState& state);
    void setFont(const std::wstring& face, int points);
    void openMenu(int index);
    void showContextMenu(POINT screenPoint, std::vector<MenuItem> items);
    void closeMenu();
    bool isOpen() const { return openMenu_ >= 0; }
    void invalidate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handlePopupMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFont();
    void rebuildMenus();
    void paint(HDC dc);
    void paintPopup(HDC dc);
    void showPopup(int index);
    void hidePopup();
    void notify(UINT id);
    const std::vector<MenuItem>& activeItems() const;
    bool hasActivePopup() const;
    int titleFromPoint(int x, int y) const;
    int itemFromPoint(int x, int y) const;
    int popupItemHeight() const;
    int popupWidth() const;
    int popupHeight() const;
    void movePopupSelection(int direction);
    void executePopupItem(int index);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HWND popup_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::wstring fontFace_ = L"Segoe UI";
    int fontSize_ = 9;
    MenuState state_;
    std::vector<MenuDef> menus_;
    std::vector<MenuItem> contextItems_;
    bool contextMode_ = false;
    std::vector<RECT> titleRects_;
    int hoverTitle_ = -1;
    int openMenu_ = -1;
    int hoverItem_ = -1;
    int pressedItem_ = -1;
};

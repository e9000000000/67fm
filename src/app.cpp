#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

#include "commands.h"
#include "download.h"
#include "ipc.h"
#include "shellintegration.h"
#include "theme.h"
#include "util.h"
#include "windowrect.h"

namespace {

constexpr wchar_t kMainClass[] = L"67fmMainWindow";
constexpr ULONG_PTR kCopyDataTag = 0x67F0;
constexpr UINT kTimerSession = 1;
constexpr UINT kTimerMessage = 2;
constexpr UINT kDownloadDone = WM_APP + 0x300;
constexpr UINT kSearchDone = WM_APP + 0x301;

void CenterWindow(HWND hwnd, int width, int height) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool FocusIsEdit() {
    HWND focus = GetFocus();
    if (!focus) {
        return false;
    }
    wchar_t className[32] = {};
    GetClassNameW(focus, className, static_cast<int>(std::size(className)));
    return _wcsicmp(className, L"Edit") == 0;
}

std::wstring HomeFolder() {
    std::wstring home = KnownFolder(FOLDERID_Profile);
    if (home.empty()) {
        home = L"C:\\";
    }
    return home;
}

bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        Sleep(5);
    }
    return false;
}

bool PutClipboardFiles(HWND owner, const std::vector<std::wstring>& paths, bool move) {
    if (paths.empty()) {
        return false;
    }
    size_t characters = 0;
    for (const auto& path : paths) {
        characters += path.size() + 1;
    }
    const size_t bytes = sizeof(DROPFILES) + (characters + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GHND, bytes);
    if (!memory) {
        return false;
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    if (!drop) {
        GlobalFree(memory);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto* text = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (const auto& path : paths) {
        memcpy(text, path.c_str(), path.size() * sizeof(wchar_t));
        text += path.size();
        *text++ = L'\0';
    }
    *text = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboardWithRetry(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_HDROP, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    if (move) {
        const UINT effectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
        HGLOBAL effectMemory = GlobalAlloc(GHND, sizeof(DWORD));
        if (effectMemory) {
            auto* effect = static_cast<DWORD*>(GlobalLock(effectMemory));
            if (effect) {
                *effect = DROPEFFECT_MOVE;
                GlobalUnlock(effectMemory);
                SetClipboardData(effectFormat, effectMemory);
            } else {
                GlobalFree(effectMemory);
            }
        }
    }
    CloseClipboard();
    return true;
}

bool GetClipboardFiles(std::vector<std::wstring>& paths, bool& move) {
    paths.clear();
    move = false;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboardWithRetry(nullptr)) {
        return false;
    }
    if (HANDLE handle = GetClipboardData(CF_HDROP)) {
        HDROP drop = static_cast<HDROP>(handle);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            if (length == 0) {
                continue;
            }
            std::wstring path(length + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            path.resize(length);
            paths.push_back(std::move(path));
        }
    }
    const UINT effectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (HANDLE effectHandle = GetClipboardData(effectFormat)) {
        const auto* effect = static_cast<const DWORD*>(GlobalLock(effectHandle));
        if (effect) {
            move = (*effect & DROPEFFECT_MOVE) != 0;
            GlobalUnlock(effectHandle);
        }
    }
    CloseClipboard();
    return !paths.empty();
}

std::wstring TempDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
    return buffer;
}

}  // namespace

App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() {
    for (auto& thread : downloads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    if (searchCancel_) {
        searchCancel_->store(true);
    }
    for (auto& thread : searchThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    if (backgroundBrush_) {
        DeleteObject(backgroundBrush_);
    }
}

bool App::create(const std::vector<std::wstring>& initialPaths) {
    EnsureDirectory(DataDirectory());
    LoadSettings(settings_);
    dpi_ = 96;
    backgroundBrush_ = CreateSolidBrush(theme::kBg);
    if (!backgroundBrush_) {
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = backgroundBrush_;
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    SessionState session;
    const bool haveSession = LoadSession(session);

    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, L"67fm",
                            (WS_OVERLAPPEDWINDOW & ~WS_CAPTION) | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr,
                            instance_, this);
    if (!hwnd_) {
        return false;
    }
    dpi_ = DpiOfWindow(hwnd_);
    EnableDarkTitleBar(hwnd_);
    AllowDarkModeForWindow(hwnd_);
    createAccelerators();

    if (!menuBar_.create(hwnd_, instance_)) {
        return false;
    }
    ShowWindow(menuBar_.hwnd(), SW_HIDE);
    if (!tabBar_.create(hwnd_, instance_) || !pathBar_.create(hwnd_, instance_) ||
        !searchBar_.create(hwnd_, instance_) || !fileList_.create(hwnd_, instance_)) {
        return false;
    }
    ShowWindow(searchBar_.hwnd(), SW_HIDE);
    fileList_.setViewMode(static_cast<ViewMode>(settings_.viewMode));
    fileList_.setColumnRatios(settings_.columns);
    applyUiFont();
    searchBar_.setRecursive(settings_.recursiveSearch);

    fileList_.setDropHandler([this](const DropPayload& payload) {
        handleDropPayload(payload);
    });
    fileList_.setDragOutProvider([this]() {
        std::vector<std::wstring> paths;
        Tab* tab = activeTab();
        if (!tab || tab->location.archive) {
            return paths;
        }
        for (const auto& entry : fileList_.selectedEntries()) {
            if (entry.inner.empty() && !entry.path.empty()) {
                paths.push_back(entry.path);
            }
        }
        return paths;
    });
    DragAcceptFiles(hwnd_, TRUE);

    bool placed = false;
    if (haveSession && session.hasWindow) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const HMONITOR monitor = MonitorFromRect(&session.window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            work = info.rcWork;
        }
        const RECT placedRect = windowrect::ClampToWork(session.window, work);
        const int width = static_cast<int>(placedRect.right - placedRect.left);
        const int height = static_cast<int>(placedRect.bottom - placedRect.top);
        if (width >= 420 && height >= 280) {
            SetWindowPos(hwnd_, nullptr, placedRect.left, placedRect.top, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            placed = true;
        }
    }
    if (!placed) {
        CenterWindow(hwnd_, theme::Scale(1100, dpi_), theme::Scale(760, dpi_));
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    if (haveSession) {
        restoreSession(session);
        if (session.maximized) {
            ShowWindow(hwnd_, SW_MAXIMIZE);
        }
    }
    if (!initialPaths.empty()) {
        openPaths(initialPaths, true);
    }
    if (tabs_.empty()) {
        addTab(Location::FileSystem(HomeFolder()));
    }
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
        selectTab(0);
    } else {
        displayTab(*tabs_[active_]);
        updateTabBar();
        updatePathBar();
        updateTitle();
        updateStatus();
    }

    layout();
    fileList_.focus();
    SetTimer(hwnd_, kTimerSession, 600, nullptr);
    return true;
}

int App::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (accel_ && TranslateAcceleratorW(hwnd_, accel_, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

MenuState App::currentMenuState() {
    MenuState state;
    Tab* tab = activeTab();
    state.hasTab = tab != nullptr;
    state.hasSelection = fileList_.hasSelection();
    state.canExtract = tab && tab->location.archive;
    state.isDefaultFileManager = shellintegration::IsDefaultFileManager();
    if (tab) {
        state.canBack = tab->historyIndex > 0;
        state.canForward = tab->historyIndex + 1 < static_cast<int>(tab->history.size());
        Location up;
        state.canUp = tab->location.Up(up);
        state.sortColumn = tab->sortColumn;
        state.sortAscending = tab->sortAscending;
    }
    state.showHidden = settings_.showHidden;
    return state;
}

void App::createAccelerators() {
    ACCEL items[] = {
        {FVIRTKEY | FCONTROL, 'T', kCmdNewTab},
        {FVIRTKEY | FCONTROL, 'W', kCmdCloseTab},
        {FVIRTKEY | FCONTROL, VK_TAB, kCmdNextTab},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, kCmdPrevTab},
        {FVIRTKEY | FALT, VK_LEFT, kCmdBack},
        {FVIRTKEY | FALT, VK_RIGHT, kCmdForward},
        {FVIRTKEY | FALT, VK_UP, kCmdUp},
        {FVIRTKEY, VK_F5, kCmdRefresh},
        {FVIRTKEY | FCONTROL, 'L', kCmdAddress},
        {FVIRTKEY | FCONTROL, 'F', kCmdFind},
        {FVIRTKEY | FCONTROL, 'G', kCmdMenuGo},
        {FVIRTKEY | FCONTROL, 'A', kCmdSelectAll},
        {FVIRTKEY | FCONTROL, 'C', kCmdCopy},
        {FVIRTKEY | FCONTROL, 'X', kCmdCut},
        {FVIRTKEY | FCONTROL, 'V', kCmdPaste},
        {FVIRTKEY | FCONTROL | FSHIFT, 'N', kCmdNewFolder},
        {FVIRTKEY | FCONTROL | FALT, 'N', kCmdNewFile},
        {FVIRTKEY | FCONTROL, 'E', kCmdExtract},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, kCmdZoomIn},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS, kCmdZoomIn},
        {FVIRTKEY | FCONTROL, VK_ADD, kCmdZoomIn},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, kCmdZoomOut},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, kCmdZoomOut},
        {FVIRTKEY | FCONTROL, '0', kCmdZoomReset},
        {FVIRTKEY | FCONTROL | FSHIFT, 'F', kCmdListFont},
        {FVIRTKEY | FCONTROL | FSHIFT, 'U', kCmdUiFont},
        {FVIRTKEY | FCONTROL | FALT, 'T', kCmdOpenTerminal},
    };
    accel_ = CreateAcceleratorTableW(items, static_cast<int>(std::size(items)));
}

void App::showBurgerMenu() {
    std::vector<MenuItem> items;
    const auto add = [&](UINT id, const wchar_t* label) {
        MenuItem item;
        item.id = id;
        item.label = label;
        item.shortcut = L"\u203A";
        items.push_back(std::move(item));
    };
    add(kCmdMenuFile, L"File");
    add(kCmdMenuEdit, L"Edit");
    add(kCmdMenuView, L"View");
    add(kCmdMenuGo, L"Go");
    add(kCmdMenuHelp, L"Help");
    POINT point{theme::Scale(16, dpi_), theme::Scale(28, dpi_)};
    ClientToScreen(hwnd_, &point);
    menuBar_.showContextMenu(point, std::move(items));
}

void App::showMenuCategory(int category) {
    const MenuState state = currentMenuState();
    std::vector<MenuItem> items;
    const auto add = [&](UINT id, const wchar_t* label, bool enabled,
                         const wchar_t* shortcut = L"", bool checked = false) {
        MenuItem item;
        item.id = id;
        item.label = label;
        item.enabled = enabled;
        item.shortcut = shortcut;
        item.checked = checked;
        items.push_back(std::move(item));
    };
    const auto separator = [&]() {
        MenuItem item;
        item.separator = true;
        items.push_back(std::move(item));
    };

    switch (category) {
    case 0:  // File
        add(kCmdNewTab, L"New Tab", true, L"Ctrl+T");
        add(kCmdNewFolder, L"New Folder", state.hasTab, L"Ctrl+Shift+N");
        add(kCmdNewFile, L"New File", state.hasTab, L"Ctrl+Alt+N");
        separator();
        add(kCmdOpen, L"Open", state.hasSelection, L"Enter");
        add(kCmdOpenWith, L"Open With...", state.hasSelection && !state.canExtract);
        add(kCmdOpenExplorer, L"Show in Explorer", state.hasTab);
        add(kCmdOpenTerminal, L"Open in Terminal", state.hasTab, L"Ctrl+Alt+T");
        add(kCmdExtract, L"Extract...", state.canExtract && state.hasSelection, L"Ctrl+E");
        add(kCmdExtractAll, L"Extract All...", state.canExtract);
        separator();
        add(kCmdCloseTab, L"Close Tab", state.hasTab, L"Ctrl+W");
        add(kCmdCloseOthers, L"Close Other Tabs", tabs_.size() > 1);
        add(kCmdCloseAll, L"Close All Tabs", !tabs_.empty());
        separator();
        add(kCmdSetDefaultFileManager, L"Set as Default File Manager", true, L"",
            state.isDefaultFileManager);
        add(kCmdRestoreExplorerDefault, L"Restore Windows Explorer Default", true);
        separator();
        add(kCmdExit, L"Exit", true);
        break;
    case 1:  // Edit
        add(kCmdCut, L"Cut", state.hasSelection && !state.canExtract, L"Ctrl+X");
        add(kCmdCopy, L"Copy", state.hasSelection, L"Ctrl+C");
        add(kCmdPaste, L"Paste", state.hasTab && !state.canExtract, L"Ctrl+V");
        separator();
        add(kCmdRename, L"Rename", state.hasSelection && !state.canExtract, L"F2");
        add(kCmdDelete, L"Delete", state.hasSelection && !state.canExtract, L"Del");
        separator();
        add(kCmdSelectAll, L"Select All", state.hasTab, L"Ctrl+A");
        add(kCmdInvertSelection, L"Invert Selection", state.hasTab);
        separator();
        add(kCmdCopyPath, L"Copy Path", state.hasSelection);
        add(kCmdProperties, L"Properties", state.hasSelection || state.hasTab, L"Alt+Enter");
        break;
    case 2:  // View
        add(kCmdBack, L"Back", state.canBack, L"Alt+Left");
        add(kCmdForward, L"Forward", state.canForward, L"Alt+Right");
        add(kCmdUp, L"Up", state.canUp, L"Alt+Up");
        add(kCmdRefresh, L"Refresh", state.hasTab, L"F5");
        separator();
        add(kCmdAddress, L"Address Bar", state.hasTab, L"Ctrl+L");
        add(kCmdFind, L"Find / Filter", state.hasTab, L"Ctrl+F");
        add(kCmdShowHidden, L"Show Hidden Files", true, L"", state.showHidden);
        separator();
        add(kCmdSortName, L"Sort by Name", state.hasTab, L"", state.sortColumn == 0);
        add(kCmdSortSize, L"Sort by Size", state.hasTab, L"", state.sortColumn == 1);
        add(kCmdSortDate, L"Sort by Date", state.hasTab, L"", state.sortColumn == 2);
        add(kCmdSortType, L"Sort by Type", state.hasTab, L"", state.sortColumn == 3);
        add(kCmdSortAscending, L"Ascending", state.hasTab, L"", state.sortAscending);
        add(kCmdSortDescending, L"Descending", state.hasTab, L"", !state.sortAscending);
        separator();
        add(kCmdZoomIn, L"Zoom In (change view)", state.hasTab, L"Ctrl++");
        add(kCmdZoomOut, L"Zoom Out (change view)", state.hasTab, L"Ctrl+-");
        add(kCmdZoomReset, L"Reset View (Details)", state.hasTab, L"Ctrl+0");
        separator();
        add(kCmdListFont, L"List Font...", state.hasTab, L"Ctrl+Shift+F");
        add(kCmdUiFont, L"Interface Font...", state.hasTab, L"Ctrl+Shift+U");
        break;
    case 3:  // Go
        add(kCmdDesktop, L"Desktop", true);
        add(kCmdDocuments, L"Documents", true);
        add(kCmdDownloads, L"Downloads", true);
        add(kCmdPictures, L"Pictures", true);
        add(kCmdVideos, L"Videos", true);
        add(kCmdHome, L"Home", true);
        separator();
        add(kCmdThisPc, L"This PC", true);
        break;
    case 4:  // Help
        add(kCmdAbout, L"About 67fm", true);
        break;
    default:
        return;
    }

    POINT point{theme::Scale(16, dpi_), theme::Scale(28, dpi_)};
    ClientToScreen(hwnd_, &point);
    menuBar_.showContextMenu(point, std::move(items));
}

void App::setStatusMessage(const std::wstring& message) {
    statusMessage_ = message;
    updateStatus();
    SetTimer(hwnd_, kTimerMessage, 4000, nullptr);
}

Tab* App::activeTab() {
    return active_ >= 0 && active_ < static_cast<int>(tabs_.size()) ? tabs_[active_].get() : nullptr;
}

Tab& App::addTab(const Location& location) {
    auto tab = std::make_unique<Tab>();
    tab->id = std::to_wstring(GetTickCount64());
    tab->location = location;
    tab->history.push_back(location);
    tab->historyIndex = 0;
    tab->sortColumn = settings_.sortColumn;
    tab->sortAscending = settings_.sortAscending;
    tabs_.push_back(std::move(tab));
    const int index = static_cast<int>(tabs_.size()) - 1;
    if (active_ < 0) {
        active_ = index;
    }
    return *tabs_[index];
}

void App::saveActiveScroll() {
    if (Tab* tab = activeTab()) {
        tab->scrollRow = fileList_.firstVisibleRow();
    }
}

void App::selectTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    saveActiveScroll();
    active_ = index;
    pathBar_.endEdit();
    Tab& tab = *tabs_[active_];
    displayTab(tab);
    updateTabBar();
    updatePathBar();
    updateTitle();
    updateStatus();
    fileList_.focus();
    markSessionDirty();
}

void App::displayTab(Tab& tab) {
    if (!tab.loaded) {
        loadTab(tab, false);
        return;
    }
    fileList_.setSort(tab.sortColumn, tab.sortAscending);
    fileList_.setEntries(tab.entries, false);
    fileList_.scrollToRow(tab.scrollRow);
    fileList_.setEmptyMessage(L"Empty folder");
    updatePathBar();
    updateStatus();
    applySearch();
}

void App::loadTab(Tab& tab, bool preserveScroll) {
    std::wstring error;
    std::vector<FileEntry> entries;
    if (tab.location.archive) {
        if (!tab.zip || !PathsEqual(tab.zip->path(), tab.location.fsPath)) {
            auto zip = std::make_shared<ZipArchive>();
            if (!zip->Open(tab.location.fsPath, error)) {
                tab.zip.reset();
            } else {
                tab.zip = std::move(zip);
            }
        }
        if (tab.zip) {
            tab.zip->List(tab.location.inner, entries, error);
        }
    } else {
        if (!DirectoryExists(tab.location.fsPath)) {
            error = L"Folder not found: " + tab.location.fsPath;
        } else {
            ListDirectory(tab.location.fsPath, settings_.showHidden, entries, error);
        }
    }
    tab.entries = std::move(entries);
    tab.loaded = true;

    if (&tab == activeTab()) {
        fileList_.setSort(tab.sortColumn, tab.sortAscending);
        if (!error.empty()) {
            fileList_.clearEntries(error);
        } else {
            fileList_.setEmptyMessage(L"Empty folder");
            fileList_.setEntries(tab.entries, preserveScroll);
            if (!preserveScroll && tab.scrollRow > 0) {
                fileList_.scrollToRow(tab.scrollRow);
            }
            if (!tab.selectAfterLoad.empty()) {
                for (size_t i = 0; i < tab.entries.size(); ++i) {
                    if (_wcsicmp(tab.entries[i].name.c_str(), tab.selectAfterLoad.c_str()) == 0) {
                        fileList_.selectIndex(static_cast<int>(i));
                        fileList_.focus();
                        break;
                    }
                }
                tab.selectAfterLoad.clear();
            }
        }
        updatePathBar();
        updateStatus();
        updateTitle();
        if (error.empty()) {
            applySearch();
        }
    }
    if (!error.empty()) {
        setStatusMessage(error);
    }
}

void App::navigate(Tab& tab, const Location& location, bool addHistory) {
    if (addHistory) {
        if (tab.historyIndex >= 0 && tab.historyIndex + 1 < static_cast<int>(tab.history.size())) {
            tab.history.erase(tab.history.begin() + tab.historyIndex + 1, tab.history.end());
        }
        if (tab.history.empty() || !tab.history.back().Equals(location)) {
            tab.history.push_back(location);
            tab.historyIndex = static_cast<int>(tab.history.size()) - 1;
        }
    }
    tab.location = location;
    tab.loaded = false;
    tab.scrollRow = 0;
    loadTab(tab, false);
    updateTabBar();
    updatePathBar();
    updateTitle();
    updateStatus();
    markSessionDirty();
}

void App::navigateText(const std::wstring& text) {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    Location location;
    std::wstring error;
    if (!ParseLocation(text, location, error)) {
        ShowError(hwnd_, error);
        return;
    }
    navigate(*tab, location);
}

void App::goBack() {
    Tab* tab = activeTab();
    if (!tab || tab->historyIndex <= 0) {
        return;
    }
    const int index = tab->historyIndex - 1;
    tab->historyIndex = index;
    navigate(*tab, tab->history[index], false);
}

void App::goForward() {
    Tab* tab = activeTab();
    if (!tab || tab->historyIndex + 1 >= static_cast<int>(tab->history.size())) {
        return;
    }
    const int index = tab->historyIndex + 1;
    tab->historyIndex = index;
    navigate(*tab, tab->history[index], false);
}

void App::goUp() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    Location up;
    if (!tab->location.Up(up)) {
        setStatusMessage(L"Already at the top.");
        return;
    }
    navigate(*tab, up);
}

void App::refreshActive() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    saveActiveScroll();
    loadTab(*tab, true);
    updatePathBar();
    updateStatus();
    updateTitle();
    updateTabBar();
}

void App::activateEntry(int allIndex) {
    if (const FileEntry* entry = fileList_.entryAt(allIndex)) {
        openEntry(*entry);
    }
}

void App::openEntry(const FileEntry& entry) {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (entry.directory) {
        if (!entry.inner.empty() && tab->location.archive) {
            navigate(*tab, Location::InArchive(tab->location.fsPath, entry.inner));
        } else {
            navigate(*tab, Location::FileSystem(entry.path));
        }
        return;
    }
    if (entry.archive) {
        if (!entry.inner.empty() && tab->location.archive) {
            if (!tab->zip) {
                ShowError(hwnd_, L"The archive is not open.");
                return;
            }
            const std::wstring folder =
                JoinPath(TempDirectory(), L"67fm-open-" + std::to_wstring(GetTickCount64()));
            EnsureDirectory(folder);
            const std::wstring destination = JoinPath(folder, FileNameOf(entry.name));
            std::wstring error;
            if (!tab->zip->Extract(entry.inner, destination, error)) {
                ShowError(hwnd_, error);
                return;
            }
            navigate(*tab, Location::InArchive(destination));
        } else {
            navigate(*tab, Location::InArchive(entry.path));
        }
        return;
    }
    if (!entry.inner.empty() && tab->location.archive) {
        if (!tab->zip) {
            ShowError(hwnd_, L"The archive is not open.");
            return;
        }
        const std::wstring folder =
            JoinPath(TempDirectory(), L"67fm-open-" + std::to_wstring(GetTickCount64()));
        EnsureDirectory(folder);
        const std::wstring destination = JoinPath(folder, entry.name);
        std::wstring error;
        if (!tab->zip->Extract(entry.inner, destination, error)) {
            ShowError(hwnd_, error);
            return;
        }
        if (!OpenWithShell(destination)) {
            ShowError(hwnd_, L"Cannot open " + destination);
        }
        return;
    }
    if (!OpenWithShell(entry.path)) {
        ShowError(hwnd_, L"Cannot open " + entry.path);
    }
}

void App::openPaths(const std::vector<std::wstring>& paths, bool) {
    Tab* last = nullptr;
    for (const auto& path : paths) {
        const std::wstring full = FullPath(path);
        const DWORD attrs = GetFileAttributesW(full.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            ShowError(hwnd_, L"Path not found: " + full);
            continue;
        }
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            last = &addTab(Location::FileSystem(full));
        } else if (IsArchiveName(FileNameOf(full))) {
            last = &addTab(Location::InArchive(full));
        } else {
            Tab& tab = addTab(Location::FileSystem(DirectoryOf(full)));
            tab.selectAfterLoad = FileNameOf(full);
            last = &tab;
        }
    }
    if (last) {
        const int index = static_cast<int>(std::find_if(
            tabs_.begin(), tabs_.end(),
            [&](const std::unique_ptr<Tab>& item) { return item.get() == last; }) -
            tabs_.begin());
        selectTab(index);
    }
}

void App::restoreSession(const SessionState& session) {
    for (const auto& saved : session.tabs) {
        Location location;
        std::wstring error;
        if (!ParseLocation(saved.path, location, error)) {
            continue;
        }
        Tab& tab = addTab(location);
        tab.scrollRow = saved.scrollRow;
        tab.sortColumn = saved.sortColumn;
        tab.sortAscending = saved.sortAscending;
    }
    if (tabs_.empty()) {
        return;
    }
    int index = session.active;
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        index = 0;
    }
    active_ = index;
}

void App::saveSession() {
    saveActiveScroll();
    SessionState session;
    for (const auto& tab : tabs_) {
        TabSession item;
        item.path = tab->location.ToString();
        item.scrollRow = tab->scrollRow;
        item.sortColumn = tab->sortColumn;
        item.sortAscending = tab->sortAscending;
        session.tabs.push_back(std::move(item));
    }
    session.active = active_;
    if (hwnd_) {
        RECT window{};
        GetWindowRect(hwnd_, &window);
        session.window = window;
        session.hasWindow = true;
        session.maximized = IsZoomed(hwnd_) != FALSE;
    }
    SaveSession(session);
}

void App::markSessionDirty() {
    if (hwnd_ && !closing_) {
        SetTimer(hwnd_, kTimerSession, 600, nullptr);
    }
}

void App::updateTitle() {
    if (!hwnd_) {
        return;
    }
    Tab* tab = activeTab();
    std::wstring title = L"67fm";
    if (tab) {
        const std::wstring name = tab->location.DisplayName();
        if (!name.empty()) {
            title = name + L" \u2014 67fm";
        }
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void App::updateTabBar() {
    if (!tabBar_.hwnd()) {
        return;
    }
    std::vector<TabVisual> visuals;
    visuals.reserve(tabs_.size());
    for (const auto& tab : tabs_) {
        TabVisual visual;
        visual.title = tab->location.DisplayName();
        if (visual.title.empty()) {
            visual.title = L"67fm";
        }
        visual.archive = tab->location.archive;
        visuals.push_back(std::move(visual));
    }
    tabBar_.setTabs(std::move(visuals), active_);
}

std::vector<PathCrumb> App::buildCrumbs(const Location& location) const {
    std::vector<PathCrumb> crumbs;
    const std::wstring rootPath = location.archive ? DirectoryOf(location.fsPath)
                                                   : location.fsPath;
    std::wstring root;
    if (rootPath.size() >= 2 && rootPath[0] == L'\\' && rootPath[1] == L'\\') {
        const size_t serverEnd = rootPath.find(L'\\', 2);
        if (serverEnd != std::wstring::npos) {
            const size_t shareEnd = rootPath.find(L'\\', serverEnd + 1);
            root = shareEnd == std::wstring::npos ? rootPath : rootPath.substr(0, shareEnd);
        } else {
            root = rootPath;
        }
    } else if (rootPath.size() >= 2 && rootPath[1] == L':') {
        root = rootPath.substr(0, 2) + L"\\";
    } else {
        root = rootPath;
    }
    std::wstring rest = rootPath.size() > root.size() ? rootPath.substr(root.size()) : L"";
    if (!root.empty()) {
        std::wstring label = root;
        if (!rest.empty()) {
            while (label.size() > 1 && (label.back() == L'\\' || label.back() == L'/')) {
                label.pop_back();
            }
        }
        crumbs.push_back(PathCrumb{label, root, false});
    }
    std::wstring accumulated = root;
    size_t start = 0;
    while (start < rest.size()) {
        size_t end = rest.find(L'\\', start);
        if (end == std::wstring::npos) {
            end = rest.size();
        }
        const std::wstring part = rest.substr(start, end - start);
        if (!part.empty()) {
            accumulated = JoinPath(accumulated, part);
            crumbs.push_back(PathCrumb{part, accumulated, false});
        }
        start = end + 1;
    }
    if (location.archive) {
        crumbs.push_back(PathCrumb{FileNameOf(location.fsPath), location.fsPath, true});
        if (!location.inner.empty()) {
            std::wstring innerAccum;
            size_t innerStart = 0;
            while (innerStart < location.inner.size()) {
                size_t innerEnd = location.inner.find(L'\\', innerStart);
                if (innerEnd == std::wstring::npos) {
                    innerEnd = location.inner.size();
                }
                const std::wstring part = location.inner.substr(innerStart, innerEnd - innerStart);
                if (!part.empty()) {
                    innerAccum = innerAccum.empty() ? part : innerAccum + L"\\" + part;
                    const std::wstring target = location.fsPath + L"\\" + innerAccum;
                    crumbs.push_back(PathCrumb{part, target, true});
                }
                innerStart = innerEnd + 1;
            }
        }
    }
    return crumbs;
}

void App::updatePathBar() {
    Tab* tab = activeTab();
    if (!tab || !pathBar_.hwnd()) {
        return;
    }
    tabBar_.setActive(active_);
    pathBar_.setState(buildCrumbs(tab->location), tab->location.ToString());
}

void App::updateStatus() {
    pathBar_.setMessage(statusMessage_);
}

void App::layout() {
    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    dpi_ = DpiOfWindow(hwnd_);
    const int tabHeight = tabBar_.preferredHeight(hwnd_);
    const int toolbarHeight = pathBar_.preferredHeight(hwnd_);
    const int searchHeight = searchBar_.isVisible() ? searchBar_.preferredHeight(hwnd_) : 0;
    const int listTop = tabHeight + toolbarHeight;
    const int listHeight = std::max(0, height - listTop - searchHeight);

    tabBar_.layout(0, 0, width, tabHeight);
    pathBar_.layout(0, tabHeight, width, toolbarHeight);
    if (searchBar_.isVisible()) {
        searchBar_.layout(0, height - searchHeight, width, searchHeight);
    }
    fileList_.layout(0, listTop, width, listHeight);
    menuBar_.layout(0, 0, 0, 0);
}

void App::copyPath() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    std::wstring path;
    const auto selected = fileList_.selectedEntries();
    if (!selected.empty()) {
        path = selected.front().path;
    } else {
        path = tab->location.ToString();
    }
    if (path.empty()) {
        return;
    }
    if (OpenClipboard(hwnd_)) {
        EmptyClipboard();
        const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            if (auto* text = static_cast<wchar_t*>(GlobalLock(memory))) {
                memcpy(text, path.c_str(), bytes);
                GlobalUnlock(memory);
                SetClipboardData(CF_UNICODETEXT, memory);
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }
    setStatusMessage(L"Copied path");
}

bool App::selectionPaths(std::vector<std::wstring>& paths, bool extractArchive,
                         std::wstring& error) {
    paths.clear();
    Tab* tab = activeTab();
    if (!tab) {
        return false;
    }
    const auto selected = fileList_.selectedEntries();
    if (selected.empty()) {
        error = L"Nothing is selected.";
        return false;
    }
    if (!tab->location.archive) {
        for (const auto& entry : selected) {
            paths.push_back(entry.path);
        }
        return true;
    }
    if (!extractArchive) {
        error = L"Files inside an archive must be extracted first.";
        return false;
    }
    if (!tab->zip) {
        error = L"The archive is not open.";
        return false;
    }
    const std::wstring folder =
        JoinPath(TempDirectory(), L"67fm-copy-" + std::to_wstring(GetTickCount64()));
    EnsureDirectory(folder);
    for (const auto& entry : selected) {
        if (entry.directory) {
            int count = 0;
            if (!tab->zip->ExtractAll(entry.inner, JoinPath(folder, entry.name), count, error)) {
                return false;
            }
        } else {
            if (!tab->zip->Extract(entry.inner, JoinPath(folder, entry.name), error)) {
                return false;
            }
        }
        paths.push_back(JoinPath(folder, entry.name));
    }
    return true;
}

void App::copySelection(bool cut) {
    std::vector<std::wstring> paths;
    std::wstring error;
    Tab* tab = activeTab();
    const bool archive = tab && tab->location.archive;
    if (!selectionPaths(paths, archive, error)) {
        setStatusMessage(error);
        return;
    }
    if (!PutClipboardFiles(hwnd_, paths, cut && !archive)) {
        setStatusMessage(L"Could not access the clipboard.");
        return;
    }
    setStatusMessage(cut && !archive ? L"Cut " + std::to_wstring(paths.size()) + L" item(s)"
                                    : L"Copied " + std::to_wstring(paths.size()) + L" item(s)");
}

void App::paste() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        setStatusMessage(L"Cannot paste inside a ZIP archive.");
        return;
    }
    std::vector<std::wstring> paths;
    bool move = false;
    if (!GetClipboardFiles(paths, move)) {
        setStatusMessage(L"Clipboard has no files.");
        return;
    }
    CopyOrMovePaths(hwnd_, paths, tab->location.fsPath, move);
    loadTab(*tab, true);
    updateTabBar();
    updateTitle();
}

void App::deleteSelection() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Files inside a ZIP archive cannot be deleted. Extract the archive first.");
        return;
    }
    const auto selected = fileList_.selectedEntries();
    if (selected.empty()) {
        setStatusMessage(L"Nothing is selected.");
        return;
    }
    std::wstring message = selected.size() == 1
                               ? L"Move \"" + selected.front().name +
                                     L"\" to the Recycle Bin?"
                               : L"Move " + std::to_wstring(selected.size()) +
                                     L" items to the Recycle Bin?";
    if (MessageBoxW(hwnd_, message.c_str(), L"67fm",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    std::vector<std::wstring> paths;
    for (const auto& entry : selected) {
        paths.push_back(entry.path);
    }
    RecyclePaths(hwnd_, paths);
    loadTab(*tab, true);
    updateTabBar();
    updateTitle();
    setStatusMessage(L"Deleted " + std::to_wstring(paths.size()) + L" item(s).");
}

void App::newFolder() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Cannot create folders inside a ZIP archive.");
        return;
    }
    const std::wstring path = UniquePath(tab->location.fsPath, L"New folder", true);
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        ShowError(hwnd_, GetErrorText(L"Cannot create " + path, GetLastError()));
        return;
    }
    loadTab(*tab, true);
    const std::wstring name = FileNameOf(path);
    fileList_.beginRenameName(name);
    setStatusMessage(L"Created " + name);
}

void App::newFile() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Cannot create files inside a ZIP archive.");
        return;
    }
    const std::wstring path = UniquePath(tab->location.fsPath, L"New file.txt", false);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ShowError(hwnd_, GetErrorText(L"Cannot create " + path, GetLastError()));
        return;
    }
    CloseHandle(file);
    loadTab(*tab, true);
    const std::wstring name = FileNameOf(path);
    fileList_.beginRenameName(name);
    setStatusMessage(L"Created " + name);
}

void App::renameSelection() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Files inside a ZIP archive cannot be renamed.");
        return;
    }
    if (fileList_.selectedCount() != 1) {
        setStatusMessage(L"Select one item to rename.");
        return;
    }
    const int all = fileList_.focusedIndex();
    if (all >= 0) {
        fileList_.beginRename(all);
    }
}

void App::commitRename(int allIndex) {
    Tab* tab = activeTab();
    if (!tab || allIndex < 0 || allIndex >= static_cast<int>(tab->entries.size())) {
        return;
    }
    const std::wstring newName = fileList_.takeRenameText();
    if (newName.empty()) {
        return;
    }
    if (newName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
        newName == L"." || newName == L"..") {
        ShowError(hwnd_, L"That name contains characters Windows does not allow.");
        return;
    }
    const std::wstring oldPath = [&]() {
        const FileEntry* entry = fileList_.entryAt(allIndex);
        return entry ? entry->path : std::wstring();
    }();
    if (oldPath.empty()) {
        return;
    }
    const std::wstring newPath = JoinPath(DirectoryOf(oldPath), newName);
    if (PathsEqual(oldPath, newPath)) {
        return;
    }
    if (GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShowError(hwnd_, L"An item with that name already exists.");
        return;
    }
    if (!MoveFileW(oldPath.c_str(), newPath.c_str())) {
        ShowError(hwnd_, GetErrorText(L"Cannot rename " + oldPath, GetLastError()));
        return;
    }
    refreshActive();
    setStatusMessage(L"Renamed");
}

void App::extractSelection(bool all) {
    Tab* tab = activeTab();
    if (!tab || !tab->location.archive || !tab->zip) {
        setStatusMessage(L"No archive is open.");
        return;
    }
    const std::wstring destination = chooseFolder(L"Extract to folder");
    if (destination.empty()) {
        return;
    }
    int count = 0;
    std::wstring error;
    if (all) {
        if (!tab->zip->ExtractAll(tab->location.inner, destination, count, error)) {
            ShowError(hwnd_, error);
            return;
        }
    } else {
        const auto selected = fileList_.selectedEntries();
        if (selected.empty()) {
            setStatusMessage(L"Nothing is selected.");
            return;
        }
        for (const auto& entry : selected) {
            const std::wstring target = JoinPath(destination, entry.name);
            int subCount = 0;
            if (entry.directory) {
                if (!tab->zip->ExtractAll(entry.inner, target, subCount, error)) {
                    ShowError(hwnd_, error);
                    return;
                }
                count += subCount;
            } else {
                if (!tab->zip->Extract(entry.inner, target, error)) {
                    ShowError(hwnd_, error);
                    return;
                }
                ++count;
            }
        }
    }
    setStatusMessage(L"Extracted " + std::to_wstring(count) + L" item(s) to " +
                     FileNameOf(destination));
}

std::wstring App::chooseFolder(const std::wstring& title) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title.c_str());
    std::wstring result;
    if (SUCCEEDED(dialog->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

void App::openSelection() {
    const auto selected = fileList_.selectedEntries();
    if (selected.empty()) {
        return;
    }
    if (selected.size() == 1) {
        openEntry(selected.front());
        return;
    }
    for (const auto& entry : selected) {
        if (!entry.directory && !entry.archive) {
            openEntry(entry);
        }
    }
}

void App::openWithSelection() {
    Tab* tab = activeTab();
    if (tab && tab->location.archive) {
        setStatusMessage(L"Extract the file before opening it with another app.");
        return;
    }
    const auto selected = fileList_.selectedEntries();
    if (selected.empty()) {
        return;
    }
    if (!selected.front().path.empty()) {
        OpenWithShell(selected.front().path, L"openas");
    }
}

void App::showInExplorer() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    const auto selected = fileList_.selectedEntries();
    if (tab->location.archive) {
        OpenInExplorer(tab->location.fsPath);
    } else if (!selected.empty() && !selected.front().path.empty()) {
        OpenInExplorer(selected.front().path);
    } else {
        OpenInExplorer(tab->location.fsPath);
    }
}

void App::openTerminal() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    std::wstring target;
    if (tab->location.archive) {
        target = DirectoryOf(tab->location.fsPath);
    } else {
        const auto selected = fileList_.selectedEntries();
        if (!selected.empty()) {
            const FileEntry& entry = selected.front();
            target = entry.directory ? entry.path : DirectoryOf(entry.path);
        }
        if (target.empty()) {
            target = tab->location.fsPath;
        }
    }
    if (target.empty() || !OpenTerminalAt(target)) {
        ShowError(hwnd_, L"Could not open a terminal in " + target);
    }
}

void App::showProperties() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    const auto selected = fileList_.selectedEntries();
    if (tab->location.archive) {
        ShowProperties(hwnd_, tab->location.fsPath);
    } else if (!selected.empty() && !selected.front().path.empty()) {
        ShowProperties(hwnd_, selected.front().path);
    } else {
        ShowProperties(hwnd_, tab->location.fsPath);
    }
}

void App::showFileContextMenu(POINT point, int viewRow) {
    (void)viewRow;
    Tab* tab = activeTab();
    const bool hasSelection = fileList_.hasSelection();
    const bool archive = tab && tab->location.archive;
    std::vector<MenuItem> items;
    const auto add = [&](UINT id, const wchar_t* label, bool enabled,
                         const wchar_t* shortcut = L"", bool checked = false) {
        MenuItem item;
        item.id = id;
        item.label = label;
        item.enabled = enabled;
        item.shortcut = shortcut;
        item.checked = checked;
        items.push_back(std::move(item));
    };
    const auto separator = [&]() {
        MenuItem item;
        item.separator = true;
        items.push_back(std::move(item));
    };
    add(kCmdOpen, L"Open", hasSelection, L"Enter");
    add(kCmdOpenWith, L"Open with...", hasSelection && !archive);
    add(kCmdOpenExplorer, L"Show in Explorer", hasSelection || tab != nullptr);
    add(kCmdOpenTerminal, L"Open in Terminal", tab != nullptr, L"Ctrl+Alt+T");
    if (archive) {
        add(kCmdExtract, L"Extract...", hasSelection, L"Ctrl+E");
        add(kCmdExtractAll, L"Extract All...", true);
    }
    separator();
    add(kCmdCut, L"Cut", hasSelection && !archive, L"Ctrl+X");
    add(kCmdCopy, L"Copy", hasSelection, L"Ctrl+C");
    add(kCmdPaste, L"Paste", !archive && IsClipboardFormatAvailable(CF_HDROP), L"Ctrl+V");
    separator();
    add(kCmdRename, L"Rename", hasSelection && !archive && fileList_.selectedCount() == 1, L"F2");
    add(kCmdDelete, L"Delete", hasSelection && !archive, L"Del");
    separator();
    add(kCmdNewFolder, L"New Folder", !archive, L"Ctrl+Shift+N");
    add(kCmdNewFile, L"New File", !archive, L"Ctrl+Alt+N");
    add(kCmdCopyPath, L"Copy Path", hasSelection);
    add(kCmdProperties, L"Properties", hasSelection || tab != nullptr, L"Alt+Enter");
    separator();
    add(kCmdRefresh, L"Refresh", tab != nullptr, L"F5");
    menuBar_.showContextMenu(point, std::move(items));
}

void App::showTabContextMenu(POINT point, int index) {
    (void)index;
    std::vector<MenuItem> items;
    const auto add = [&](UINT id, const wchar_t* label, bool enabled,
                         const wchar_t* shortcut = L"") {
        MenuItem item;
        item.id = id;
        item.label = label;
        item.enabled = enabled;
        item.shortcut = shortcut;
        items.push_back(std::move(item));
    };
    add(kCmdCloseTab, L"Close", true, L"Ctrl+W");
    add(kCmdCloseOthers, L"Close Others", tabs_.size() > 1);
    add(kCmdCloseAll, L"Close All", true);
    MenuItem separator;
    separator.separator = true;
    items.push_back(std::move(separator));
    add(kCmdCopyPath, L"Copy Path", true);
    add(kCmdOpenExplorer, L"Show in Explorer", true);
    add(kCmdOpenTerminal, L"Open in Terminal", true);
    menuBar_.showContextMenu(point, std::move(items));
}

void App::handleDropPayload(const DropPayload& payload) {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Cannot add files inside a ZIP archive.\n"
                        L"Drop them in the folder next to the archive instead.");
        return;
    }
    std::wstring target = tab->location.fsPath;
    if (payload.targetRow >= 0) {
        if (const FileEntry* entry = fileList_.entryAtViewRow(payload.targetRow)) {
            if (entry->directory) {
                target = entry->path;
            }
        }
    }
    if (!payload.files.empty()) {
        CopyOrMovePaths(hwnd_, payload.files, target, payload.move);
        loadTab(*tab, true);
        updateTabBar();
        updateTitle();
        setStatusMessage((payload.move ? L"Moved " : L"Copied ") +
                         std::to_wstring(payload.files.size()) + L" item(s)");
    }

    std::vector<std::wstring> urls;
    if (payload.files.empty()) {
        for (const auto& url : payload.urls) {
            if (!url.empty()) {
                urls.push_back(url);
            }
        }
        if (urls.empty() && !payload.html.empty()) {
            const std::wstring base = BaseUrlFromHtml(payload.html);
            const std::wstring found = UrlFromHtml(payload.html);
            const std::wstring resolved = ResolveUrl(base, found);
            if (!resolved.empty()) {
                urls.push_back(resolved);
            }
        }
        if (urls.empty() && !payload.text.empty()) {
            const std::wstring found = UrlFromText(payload.text);
            if (!found.empty()) {
                urls.push_back(found);
            }
        }
    }
    for (const auto& url : urls) {
        startDownload(url, target);
    }
    if (!urls.empty()) {
        setStatusMessage(urls.size() == 1 ? L"Downloading\u2026"
                                          : L"Downloading " + std::to_wstring(urls.size()) +
                                                L" files\u2026");
    }
    if (payload.files.empty() && urls.empty()) {
        setStatusMessage(L"Nothing usable was dropped.");
    }
}

void App::startDownload(const std::wstring& url, const std::wstring& folder) {
    downloads_.emplace_back([this, url, folder]() {
        auto* result = new DownloadResult(DownloadUrlToFolder(url, folder));
        if (!PostMessageW(hwnd_, kDownloadDone, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    });
}

void App::onDownloadDone(void* pointer) {
    std::unique_ptr<DownloadResult> result(static_cast<DownloadResult*>(pointer));
    if (!result) {
        return;
    }
    if (result->ok) {
        if (Tab* tab = activeTab()) {
            if (!tab->location.archive &&
                PathsEqual(tab->location.fsPath, DirectoryOf(result->path))) {
                loadTab(*tab, true);
                updateTabBar();
                updateTitle();
            }
        }
        setStatusMessage(L"Downloaded " + FileNameOf(result->path));
    } else {
        setStatusMessage(L"Download failed.");
        ShowError(hwnd_, result->error);
    }
}


void App::onFilterChanged() {
    applySearch();
}

void App::applySearch() {
    Tab* tab = activeTab();
    if (!tab) {
        return;
    }
    const bool visible = searchBar_.isVisible();
    const std::wstring query = visible ? searchBar_.query() : std::wstring();
    ++searchGeneration_;
    if (searchCancel_) {
        searchCancel_->store(true);
        searchCancel_.reset();
    }
    if (!visible || query.empty()) {
        if (searchResultsActive_ && tab->loaded) {
            fileList_.setEntries(tab->entries, false);
            searchResultsActive_ = false;
        }
        fileList_.setFilter(L"");
        updateStatus();
        return;
    }
    if (searchBar_.recursive()) {
        if (tab->location.archive) {
            if (searchResultsActive_) {
                fileList_.setEntries(tab->entries, false);
                searchResultsActive_ = false;
            }
            std::vector<FileEntry> results;
            bool truncated = false;
            std::wstring error;
            if (tab->zip) {
                tab->zip->Search(tab->location.inner, query, 5000, results, truncated, error);
            }
            fileList_.setEmptyMessage(error.empty() ? L"No matches in subfolders" : error);
            fileList_.setEntries(std::move(results), false);
            searchResultsActive_ = true;
            if (!error.empty()) {
                setStatusMessage(error);
            } else {
                setStatusMessage(std::to_wstring(fileList_.rowCount()) +
                                 (truncated ? L" matches (first 5000)" : L" matches"));
            }
            return;
        }
        startRecursiveSearch(tab->location.fsPath, query);
        return;
    }
    if (searchResultsActive_) {
        fileList_.setEntries(tab->entries, false);
        searchResultsActive_ = false;
    }
    fileList_.setFilter(query);
    updateStatus();
}

void App::startRecursiveSearch(const std::wstring& root, const std::wstring& query) {
    const uint64_t generation = ++searchGeneration_;
    if (searchCancel_) {
        searchCancel_->store(true);
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    searchCancel_ = cancel;
    const bool showHidden = settings_.showHidden;
    searchResultsActive_ = true;
    fileList_.clearEntries(L"Searching subfolders\u2026");
    searchThreads_.emplace_back([this, root, query, showHidden, generation, cancel]() {
        auto* result = new SearchResult;
        result->generation = generation;
        result->root = root;
        result->query = query;
        SearchDirectory(root, query, showHidden, 5000, cancel.get(), result->entries,
                        result->truncated, result->error);
        if (cancel->load()) {
            delete result;
            return;
        }
        if (!PostMessageW(hwnd_, kSearchDone, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    });
}

void App::onSearchDone(void* pointer) {
    std::unique_ptr<SearchResult> result(static_cast<SearchResult*>(pointer));
    if (!result || result->generation != searchGeneration_.load()) {
        return;
    }
    Tab* tab = activeTab();
    if (!tab || tab->location.archive || !searchBar_.isVisible() ||
        !searchBar_.recursive() || !PathsEqual(tab->location.fsPath, result->root) ||
        _wcsicmp(result->query.c_str(), searchBar_.query().c_str()) != 0) {
        return;
    }
    searchResultsActive_ = true;
    if (!result->error.empty()) {
        fileList_.clearEntries(result->error);
        setStatusMessage(result->error);
        return;
    }
    fileList_.setEmptyMessage(L"No matches in subfolders");
    fileList_.setEntries(std::move(result->entries), false);
    std::wstring message = std::to_wstring(fileList_.rowCount()) + L" matches";
    if (result->truncated) {
        message += L" (first 5000)";
    }
    setStatusMessage(message);
}

void App::toggleSearch() {
    if (searchBar_.isVisible()) {
        searchBar_.focus();
        return;
    }
    pathBar_.endEdit();
    searchBar_.show();
    layout();
    applySearch();
}

void App::changeZoom(int delta) {
    static const wchar_t* labels[] = {L"Details", L"One column", L"Two columns",
                                      L"Small icons", L"Medium icons", L"Large icons", L"Extra large icons"};
    const int level = std::max(0, std::min(ViewModeCount() - 1,
                                           static_cast<int>(fileList_.viewMode()) + delta));
    fileList_.setViewMode(static_cast<ViewMode>(level));
    settings_.viewMode = level;
    SaveSettings(settings_);
    setStatusMessage(std::wstring(L"View: ") + labels[level]);
}

void App::resetZoom() {
    fileList_.setViewMode(ViewMode::Details);
    settings_.viewMode = 0;
    SaveSettings(settings_);
    setStatusMessage(L"View: Details");
}

void App::showFontDialog(bool interfaceFont) {
    const std::wstring currentFace =
        interfaceFont ? settings_.uiFontFace : settings_.listFontFace;
    const int currentSize = interfaceFont ? settings_.uiFontSize : settings_.listFontSize;
    LOGFONTW logfont{};
    logfont.lfHeight = -MulDiv(currentSize, static_cast<int>(dpi_), 72);
    logfont.lfWeight = FW_NORMAL;
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfQuality = CLEARTYPE_QUALITY;
    logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcsncpy_s(logfont.lfFaceName, currentFace.c_str(), _TRUNCATE);

    CHOOSEFONTW choose{};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = hwnd_;
    choose.lpLogFont = &logfont;
    choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    choose.nFontType = SCREEN_FONTTYPE;
    if (!ChooseFontW(&choose)) {
        return;
    }
    std::wstring face = logfont.lfFaceName;
    if (face.empty()) {
        face = L"Segoe UI";
    }
    int points = choose.iPointSize > 0 ? choose.iPointSize / 10
                                       : MulDiv(-logfont.lfHeight, 72,
                                                static_cast<int>(dpi_));
    points = std::max(6, std::min(36, points));
    if (interfaceFont) {
        settings_.uiFontFace = face;
        settings_.uiFontSize = points;
    } else {
        settings_.listFontFace = face;
        settings_.listFontSize = points;
    }
    SaveSettings(settings_);
    applyUiFont();
    setStatusMessage(std::wstring(interfaceFont ? L"Interface font: " : L"List font: ") +
                     face + L" " + std::to_wstring(points) + L" pt");
}

void App::applyUiFont() {
    tabBar_.setFont(settings_.uiFontFace, settings_.uiFontSize);
    pathBar_.setFont(settings_.uiFontFace, settings_.uiFontSize);
    searchBar_.setFont(settings_.uiFontFace, settings_.uiFontSize);
    menuBar_.setFont(settings_.uiFontFace, settings_.uiFontSize);
    fileList_.setFont(settings_.listFontFace, settings_.listFontSize);
    layout();
}

void App::onCommand(UINT id) {
    Tab* tab = activeTab();
    switch (id) {
    case kCmdMenuFile:
        showMenuCategory(0);
        break;
    case kCmdMenuEdit:
        showMenuCategory(1);
        break;
    case kCmdMenuView:
        showMenuCategory(2);
        break;
    case kCmdMenuGo:
        showMenuCategory(3);
        break;
    case kCmdMenuHelp:
        showMenuCategory(4);
        break;
    case kCmdNewTab: {
        const Location location = tab ? tab->location : Location::FileSystem(HomeFolder());
        addTab(location);
        selectTab(static_cast<int>(tabs_.size()) - 1);
        break;
    }
    case kCmdCloseTab:
        if (tab) {
            closeTab(active_);
        }
        break;
    case kCmdCloseOthers:
        if (tab) {
            const std::wstring keep = tab->id;
            for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
                if (tabs_[i]->id != keep) {
                    tabs_.erase(tabs_.begin() + i);
                }
            }
            active_ = 0;
            if (!tabs_.empty()) {
                selectTab(0);
            } else {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
        }
        break;
    case kCmdCloseAll:
        tabs_.clear();
        active_ = -1;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case kCmdRefresh:
        refreshActive();
        break;
    case kCmdUp:
        goUp();
        break;
    case kCmdBack:
        goBack();
        break;
    case kCmdForward:
        goForward();
        break;
    case kCmdAddress:
        if (tab) {
            pathBar_.beginEdit();
        }
        break;
    case kCmdFind:
        if (tab) {
            toggleSearch();
        }
        break;
    case kCmdZoomIn:
        changeZoom(1);
        break;
    case kCmdZoomOut:
        changeZoom(-1);
        break;
    case kCmdZoomReset:
        resetZoom();
        break;
    case kCmdListFont:
        showFontDialog(false);
        break;
    case kCmdUiFont:
        showFontDialog(true);
        break;
    case kCmdSetDefaultFileManager: {
        const shellintegration::Result result = shellintegration::SetAsDefaultFileManager();
        if (!result.ok) {
            ShowError(hwnd_, result.error);
        } else if (result.already) {
            MessageBoxW(hwnd_, L"67fm is already the default folder handler.",
                        L"67fm", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd_,
                        L"67fm is now the default folder handler for this user.\n\n"
                        L"Windows will open folders and drives in 67fm. Some special\n"
                        L"shell folders still belong to Windows Explorer.\n\n"
                        L"Use \"Restore Windows Explorer Default\" to undo this.",
                        L"67fm", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case kCmdRestoreExplorerDefault: {
        const shellintegration::Result result = shellintegration::RestoreExplorerDefault();
        if (!result.ok) {
            ShowError(hwnd_, result.error);
        } else {
            MessageBoxW(hwnd_, L"Windows Explorer is the default folder handler again.",
                        L"67fm", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case kCmdExit:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case kCmdOpen:
        openSelection();
        break;
    case kCmdOpenWith:
        openWithSelection();
        break;
    case kCmdOpenExplorer:
        showInExplorer();
        break;
    case kCmdOpenTerminal:
        openTerminal();
        break;
    case kCmdCut:
        if (FocusIsEdit()) {
            SendMessageW(GetFocus(), WM_CUT, 0, 0);
        } else {
            copySelection(true);
        }
        break;
    case kCmdCopy:
        if (FocusIsEdit()) {
            SendMessageW(GetFocus(), WM_COPY, 0, 0);
        } else {
            copySelection(false);
        }
        break;
    case kCmdPaste:
        if (FocusIsEdit()) {
            SendMessageW(GetFocus(), WM_PASTE, 0, 0);
        } else {
            paste();
        }
        break;
    case kCmdRename:
        renameSelection();
        break;
    case kCmdDelete:
        deleteSelection();
        break;
    case kCmdNewFolder:
        newFolder();
        break;
    case kCmdNewFile:
        newFile();
        break;
    case kCmdSelectAll:
        if (FocusIsEdit()) {
            SendMessageW(GetFocus(), EM_SETSEL, 0, -1);
        } else {
            fileList_.selectAll();
        }
        break;
    case kCmdInvertSelection:
        fileList_.invertSelection();
        break;
    case kCmdCopyPath:
        copyPath();
        break;
    case kCmdProperties:
        showProperties();
        break;
    case kCmdShowHidden:
        settings_.showHidden = !settings_.showHidden;
        SaveSettings(settings_);
        refreshActive();
        break;
    case kCmdSortName:
    case kCmdSortSize:
    case kCmdSortDate:
    case kCmdSortType: {
        if (tab) {
            tab->sortColumn = id == kCmdSortName ? 0
                              : id == kCmdSortSize ? 1
                              : id == kCmdSortDate ? 2
                                                   : 3;
            fileList_.setSort(tab->sortColumn, tab->sortAscending);
            settings_.sortColumn = tab->sortColumn;
            SaveSettings(settings_);
            updateStatus();
            markSessionDirty();
        }
        break;
    }
    case kCmdSortAscending:
    case kCmdSortDescending:
        if (tab) {
            tab->sortAscending = id == kCmdSortAscending;
            fileList_.setSort(tab->sortColumn, tab->sortAscending);
            settings_.sortAscending = tab->sortAscending;
            SaveSettings(settings_);
            markSessionDirty();
        }
        break;
    case kCmdExtract:
        extractSelection(false);
        break;
    case kCmdExtractAll:
        extractSelection(true);
        break;
    case kCmdDesktop:
    case kCmdDocuments:
    case kCmdDownloads:
    case kCmdPictures:
    case kCmdVideos:
    case kCmdHome:
    case kCmdThisPc: {
        std::wstring path;
        if (id == kCmdDesktop) path = KnownFolder(FOLDERID_Desktop);
        if (id == kCmdDocuments) path = KnownFolder(FOLDERID_Documents);
        if (id == kCmdDownloads) path = KnownFolder(FOLDERID_Downloads);
        if (id == kCmdPictures) path = KnownFolder(FOLDERID_Pictures);
        if (id == kCmdVideos) path = KnownFolder(FOLDERID_Videos);
        if (id == kCmdHome) path = KnownFolder(FOLDERID_Profile);
        if (id == kCmdThisPc) {
            wchar_t windows[MAX_PATH] = {};
            GetWindowsDirectoryW(windows, static_cast<UINT>(std::size(windows)));
            path = DriveRootOf(windows);
        }
        if (!path.empty() && tab) {
            navigate(*tab, Location::FileSystem(path));
        }
        break;
    }
    case kCmdNextTab:
        if (tabs_.size() > 1) {
            selectTab((active_ + 1) % static_cast<int>(tabs_.size()));
        }
        break;
    case kCmdPrevTab:
        if (tabs_.size() > 1) {
            selectTab((active_ - 1 + static_cast<int>(tabs_.size())) %
                      static_cast<int>(tabs_.size()));
        }
        break;
    case kCmdAbout:
        MessageBoxW(hwnd_,
                    L"67fm 0.1\nA native Windows file manager.\n\n"
                    L"Tabs, sessions, ZIP browsing, drag and drop.\n"
                    L"Drop files from Explorer to copy them, or drag an image\n"
                    L"or link from a browser to download it here.",
                    L"About 67fm", MB_OK | MB_ICONINFORMATION);
        break;
    default:
        break;
    }
}

void App::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) {
        active_ = -1;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    if (active_ > index || active_ >= static_cast<int>(tabs_.size())) {
        --active_;
    }
    if (active_ < 0) {
        active_ = 0;
    }
    selectTab(active_);
    updateTabBar();
    markSessionDirty();
}

void App::handleCopyData(LPARAM lParam) {
    const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
    if (!copy || copy->dwData != kCopyDataTag || !copy->lpData) {
        return;
    }
    const size_t units = copy->cbData / sizeof(wchar_t);
    const auto paths = ParsePathPayload(static_cast<const wchar_t*>(copy->lpData), units);
    if (paths.empty()) {
        onCommand(kCmdNewTab);
    } else {
        openPaths(paths, true);
    }
    SetForegroundWindow(hwnd_);
}

void App::handleDropFiles(WPARAM wParam) {
    HDROP drop = reinterpret_cast<HDROP>(wParam);
    Tab* tab = activeTab();
    if (!tab) {
        DragFinish(drop);
        return;
    }
    if (tab->location.archive) {
        ShowInfo(hwnd_, L"Cannot add files inside a ZIP archive.");
        DragFinish(drop);
        return;
    }
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) {
            continue;
        }
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        path.resize(length);
        paths.push_back(std::move(path));
    }
    DragFinish(drop);
    if (!paths.empty()) {
        CopyOrMovePaths(hwnd_, paths, tab->location.fsPath, false);
        loadTab(*tab, true);
        updateTabBar();
        updateTitle();
        setStatusMessage(L"Copied " + std::to_wstring(paths.size()) + L" item(s)");
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<App*>(create->lpCreateParams);
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

LRESULT App::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        onCommand(LOWORD(wParam));
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const UINT dpi = DpiOfWindow(hwnd_);
            const int frameX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int frameY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left += frameX;
            params->rgrc[0].right -= frameX;
            params->rgrc[0].top += frameY;
            params->rgrc[0].bottom -= frameY;
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &client);
        const int titleBottom = tabBar_.preferredHeight(hwnd_);
        if (client.x >= 0 && client.y >= 0 && client.y < titleBottom) {
            return HTCAPTION;
        }
        break;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_EXITSIZEMOVE:
        markSessionDirty();
        return 0;
    case WM_TIMER:
        if (wParam == kTimerSession) {
            KillTimer(hwnd_, kTimerSession);
            saveSession();
        } else if (wParam == kTimerMessage) {
            KillTimer(hwnd_, kTimerMessage);
            statusMessage_.clear();
            updateStatus();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = theme::Scale(640, dpi_);
        info->ptMinTrackSize.y = theme::Scale(400, dpi_);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_SETFOCUS:
        if (!pathBar_.isEditing()) {
            fileList_.focus();
        }
        return 0;
    case WM_COPYDATA:
        handleCopyData(lParam);
        return TRUE;
    case WM_DROPFILES:
        handleDropFiles(wParam);
        return 0;
    case kDownloadDone:
        onDownloadDone(reinterpret_cast<void*>(lParam));
        return 0;
    case tabbar_events::kSelect:
        selectTab(static_cast<int>(wParam));
        return 0;
    case tabbar_events::kClose:
        closeTab(static_cast<int>(wParam));
        return 0;
    case tabbar_events::kNew: {
        const Location location = activeTab() ? activeTab()->location
                                               : Location::FileSystem(HomeFolder());
        addTab(location);
        selectTab(static_cast<int>(tabs_.size()) - 1);
        return 0;
    }
    case tabbar_events::kReorder: {
        const int from = static_cast<int>(wParam);
        const int to = static_cast<int>(lParam);
        if (from >= 0 && to >= 0 && from < static_cast<int>(tabs_.size()) &&
            to < static_cast<int>(tabs_.size()) && from != to) {
            Tab* current = activeTab();
            auto moved = std::move(tabs_[from]);
            tabs_.erase(tabs_.begin() + from);
            tabs_.insert(tabs_.begin() + to, std::move(moved));
            active_ = current ? static_cast<int>(std::find_if(
                                    tabs_.begin(), tabs_.end(),
                                    [&](const std::unique_ptr<Tab>& item) {
                                        return item.get() == current;
                                    }) - tabs_.begin())
                              : -1;
            if (active_ < 0 && !tabs_.empty()) {
                active_ = 0;
            }
            updateTabBar();
            updateStatus();
            markSessionDirty();
        }
        return 0;
    }
    case tabbar_events::kContext: {
        const int index = static_cast<int>(wParam);
        if (index < 0 || index >= static_cast<int>(tabs_.size())) {
            return 0;
        }
        if (active_ != index) {
            selectTab(index);
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(tabBar_.hwnd(), &point);
        showTabContextMenu(point, index);
        return 0;
    }
    case tabbar_events::kMenu:
        showBurgerMenu();
        return 0;
    case filelist_events::kActivate:
        activateEntry(static_cast<int>(wParam));
        return 0;
    case filelist_events::kSelectionChanged:
        updateStatus();
        return 0;
    case filelist_events::kContext: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        showFileContextMenu(point, static_cast<int>(wParam));
        return 0;
    }
    case filelist_events::kRenameCommit:
        commitRename(static_cast<int>(wParam));
        return 0;
    case filelist_events::kSortChanged: {
        if (Tab* tab = activeTab()) {
            tab->sortColumn = fileList_.sortColumn();
            tab->sortAscending = fileList_.sortAscending();
            settings_.sortColumn = tab->sortColumn;
            settings_.sortAscending = tab->sortAscending;
            SaveSettings(settings_);
            markSessionDirty();
            updateStatus();
        }
        return 0;
    }
    case filelist_events::kUpRequest:
        goUp();
        return 0;
    case filelist_events::kEscape:
        if (searchBar_.isVisible()) {
            searchBar_.hide();
            searchBar_.clear();
            layout();
            applySearch();
        }
        fileList_.focus();
        return 0;
    case filelist_events::kDragFinished:
        if (wParam != 0) {
            refreshActive();
        }
        return 0;
    case pathbar_events::kCrumb:
        navigateText(pathBar_.crumbPath(static_cast<int>(wParam)));
        return 0;
    case pathbar_events::kGo:
        navigateText(pathBar_.editedText());
        return 0;
    case searchbar_events::kFilter:
        onFilterChanged();
        return 0;
    case searchbar_events::kScope:
        settings_.recursiveSearch = searchBar_.recursive();
        SaveSettings(settings_);
        applySearch();
        return 0;
    case searchbar_events::kClose:
        searchBar_.hide();
        searchBar_.clear();
        layout();
        applySearch();
        fileList_.focus();
        return 0;
    case filelist_events::kViewModeChanged:
        settings_.viewMode = static_cast<int>(fileList_.viewMode());
        SaveSettings(settings_);
        {
            static const wchar_t* labels[] = {L"Details", L"One column", L"Two columns",
                                              L"Small icons", L"Medium icons", L"Large icons", L"Extra large icons"};
            setStatusMessage(std::wstring(L"View: ") + labels[settings_.viewMode]);
        }
        return 0;
    case filelist_events::kFontSizeChanged:
        settings_.listFontFace = fileList_.fontFace();
        settings_.listFontSize = fileList_.fontSize();
        SaveSettings(settings_);
        applyUiFont();
        setStatusMessage(L"List font: " + settings_.listFontFace + L" " +
                         std::to_wstring(settings_.listFontSize) + L" pt");
        return 0;
    case filelist_events::kColumnsChanged:
        for (int i = 0; i < 4; ++i) {
            settings_.columns[i] = fileList_.columnRatios()[i];
        }
        SaveSettings(settings_);
        return 0;
    case kSearchDone:
        onSearchDone(reinterpret_cast<void*>(lParam));
        return 0;
    case WM_CLOSE:
        if (closing_) {
            return 0;
        }
        saveSession();
        closing_ = true;
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        saveSession();
        for (auto& thread : downloads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        downloads_.clear();
        if (searchCancel_) {
            searchCancel_->store(true);
        }
        for (auto& thread : searchThreads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        searchThreads_.clear();
        if (accel_) {
            DestroyAcceleratorTable(accel_);
            accel_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}


#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "filelist.h"
#include "location.h"
#include "menubar.h"
#include "pathbar.h"
#include "searchbar.h"
#include "session.h"
#include "settings.h"
#include "tabbar.h"
#include "zip.h"

struct Tab {
    std::wstring id;
    Location location;
    std::vector<Location> history;
    int historyIndex = -1;
    std::shared_ptr<ZipArchive> zip;
    std::vector<FileEntry> entries;
    int scrollRow = 0;
    int sortColumn = 0;
    bool sortAscending = true;
    bool loaded = false;
    std::wstring selectAfterLoad;
};

struct SearchResult {
    uint64_t generation = 0;
    std::wstring root;
    std::wstring query;
    std::vector<FileEntry> entries;
    bool truncated = false;
    std::wstring error;
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool create(const std::vector<std::wstring>& initialPaths);
    int run();

    void openPaths(const std::vector<std::wstring>& paths, bool forceNewTab);
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createAccelerators();
    MenuState currentMenuState();
    void showBurgerMenu();
    void showMenuCategory(int category);
    void onCommand(UINT id);
    void handleCopyData(LPARAM lParam);
    void handleDropFiles(WPARAM wParam);

    void restoreSession(const SessionState& session);
    void saveSession();
    void markSessionDirty();
    void updateTitle();
    void updateTabBar();
    void updatePathBar();
    void updateStatus();
    void layout();
    void setStatusMessage(const std::wstring& message);

    Tab* activeTab();
    int activeIndex() const { return active_; }
    Tab& addTab(const Location& location);
    void selectTab(int index);
    void closeTab(int index);
    void displayTab(Tab& tab);
    void loadTab(Tab& tab, bool preserveScroll);
    void navigate(Tab& tab, const Location& location, bool addHistory = true);
    void navigateText(const std::wstring& text);
    void goBack();
    void goForward();
    void goUp();
    void refreshActive();
    void activateEntry(int allIndex);
    void openEntry(const FileEntry& entry);
    void saveActiveScroll();


    void onFilterChanged();
    void toggleSearch();
    void applySearch();
    void startRecursiveSearch(const std::wstring& root, const std::wstring& query);
    void onSearchDone(void* result);
    void changeZoom(int delta);
    void resetZoom();
    void showFontDialog(bool interfaceFont);
    void applyUiFont();

    void copySelection(bool cut);
    void paste();
    void deleteSelection();
    void newFolder();
    void newFile();
    void renameSelection();
    void commitRename(int allIndex);
    void extractSelection(bool all);
    void openSelection();
    void openWithSelection();
    void showInExplorer();
    void openTerminal();
    void copyPath();
    void showProperties();

    void showFileContextMenu(POINT point, int viewRow);
    void showTabContextMenu(POINT point, int index);
    void handleDropPayload(const DropPayload& payload);
    void startDownload(const std::wstring& url, const std::wstring& folder);
    void onDownloadDone(void* result);

    std::vector<PathCrumb> buildCrumbs(const Location& location) const;
    std::wstring chooseFolder(const std::wstring& title);
    bool selectionPaths(std::vector<std::wstring>& paths, bool extractArchive,
                        std::wstring& error);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HACCEL accel_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;

    MenuBar menuBar_;
    TabBar tabBar_;
    PathBar pathBar_;
    SearchBar searchBar_;
    FileList fileList_;

    std::vector<std::unique_ptr<Tab>> tabs_;
    int active_ = -1;
    Settings settings_;
    UINT dpi_ = 96;
    bool closing_ = false;
    std::wstring statusMessage_;
    std::vector<std::thread> downloads_;
    std::vector<std::thread> searchThreads_;
    std::atomic<uint64_t> searchGeneration_{0};
    std::shared_ptr<std::atomic<bool>> searchCancel_;
    bool searchResultsActive_ = false;
};

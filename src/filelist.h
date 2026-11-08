#pragma once

#include <windows.h>
#include <ole2.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fs.h"

struct IDropTarget;

namespace filelist_events {
constexpr UINT kActivate = WM_APP + 0x201;          // wParam: index into all entries
constexpr UINT kSelectionChanged = WM_APP + 0x202;
constexpr UINT kContext = WM_APP + 0x203;           // wParam: view row (-1), lParam: screen point
constexpr UINT kRenameCommit = WM_APP + 0x204;      // wParam: index into all entries
constexpr UINT kSortChanged = WM_APP + 0x205;
constexpr UINT kUpRequest = WM_APP + 0x206;
constexpr UINT kEscape = WM_APP + 0x207;
constexpr UINT kDragFinished = WM_APP + 0x208;      // wParam: non-zero when a move may have happened
constexpr UINT kViewModeChanged = WM_APP + 0x209;
constexpr UINT kFontSizeChanged = WM_APP + 0x20A;
constexpr UINT kColumnsChanged = WM_APP + 0x20B;
}  // namespace filelist_events

enum class ViewMode {
    Details = 0,
    OneColumn,
    TwoColumns,
    SmallIcons,
    MediumIcons,
    LargeIcons,
    ExtraLargeIcons,
};

inline int ViewModeCount() { return 7; }

struct DropPayload {
    std::vector<std::wstring> files;
    std::vector<std::wstring> urls;
    std::wstring html;
    std::wstring text;
    int targetRow = -1;
    bool move = false;
};

class FileList {
public:
    using DropHandler = std::function<void(const DropPayload&)>;
    using DragOutProvider = std::function<std::vector<std::wstring>()>;

    ~FileList();
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void setEntries(std::vector<FileEntry> entries, bool preserveScroll = false);
    void clearEntries(const std::wstring& message);
    void setSort(int column, bool ascending);
    void setFilter(const std::wstring& filter);
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return viewMode_; }
    void setFont(const std::wstring& face, int points);
    const std::wstring& fontFace() const { return fontFace_; }
    int fontSize() const { return fontSize_; }
    void setColumnRatios(const int ratios[4]);
    const int* columnRatios() const { return columnRatio_; }
    void setDropHandler(DropHandler handler) { dropHandler_ = std::move(handler); }
    void setDragOutProvider(DragOutProvider provider) { dragOutProvider_ = std::move(provider); }
    void setEmptyMessage(const std::wstring& message);

    void layout(int x, int y, int width, int height);
    void focus();
    void invalidate();

    int selectedCount() const {
        int count = 0;
        for (int index : view_) {
            if (selected_.count(index)) {
                ++count;
            }
        }
        return count;
    }
    bool hasSelection() const { return selectedCount() > 0; }
    int focusedIndex() const;  // index into all entries, -1 if none
    int rowCount() const { return static_cast<int>(view_.size()); }
    int firstVisibleRow() const;
    void scrollToRow(int row);
    void selectAll();
    void invertSelection();
    void clearSelection();
    void selectIndex(int allIndex);
    void beginRename(int allIndex);
    void beginRenameName(const std::wstring& name);
    std::wstring takeRenameText();

    const std::vector<FileEntry>& entries() const { return entries_; }
    const FileEntry* entryAt(int allIndex) const;
    const FileEntry* entryAtViewRow(int viewRow) const;
    std::vector<FileEntry> selectedEntries() const;
    std::vector<std::wstring> selectedPaths() const;
    int indexFromPoint(int x, int y) const;
    int allIndexFromRow(int viewRow) const;
    int sortColumn() const { return sortColumn_; }
    bool sortAscending() const { return sortAscending_; }
    int viewRowOf(int allIndex) const;
    void setDropTargetRow(int viewRow);
    void invokeDrop(const DropPayload& payload) {
        if (dropHandler_) {
            dropHandler_(payload);
        }
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR subclassId, DWORD_PTR refData);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFont();
    bool detailsMode() const { return viewMode_ == ViewMode::Details; }
    int rowHeightPixels() const;
    int headerHeightPixels() const;
    int iconSizePixels() const;
    int fontSizePoints() const;
    int cellWidthPixels() const;
    int cellHeightPixels() const;
    int columns() const;
    int visualRows() const;
    RECT cellRect(int viewRow) const;
    void rebuildView(bool preserveScroll);
    void updateMetrics();
    void paint(HDC target);
    void drawRow(HDC dc, int viewRow, int y, int width);
    void drawTile(HDC dc, int viewRow, const RECT& cell, bool selected, bool hover,
                  bool dropTarget);
    void drawIcon(HDC dc, const FileEntry& entry, int x, int y);
    HBITMAP previewFor(const FileEntry& entry, int size);
    void drawPreview(HDC dc, HBITMAP bitmap, int x, int y, int size);
    void clearPreviews();
    void computeColumns(int width);
    RECT columnRect(int column, int y, int height) const;
    void clampScroll();
    void ensureRowVisible(int viewRow);
    void setFocusedRow(int viewRow, bool extend, bool keepOthers);
    void notify(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);
    void openInlineEdit(int viewRow);
    void commitInlineEdit(bool cancel);
    bool pointInScrollbar(int x, int y) const;
    void updateScrollFromThumb(int y);
    void startDragOut();
    bool hitHeader(int x, int y, int& column) const;
    int columnBoundaryAt(int x, int y) const;

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HWND edit_ = nullptr;
    IDropTarget* dropTarget_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    std::vector<FileEntry> entries_;
    std::vector<int> view_;  // indices into entries_, after filter and sort
    std::set<int> selected_;
    std::wstring filter_;
    std::wstring emptyMessage_ = L"Empty folder";
    int sortColumn_ = 0;
    bool sortAscending_ = true;
    ViewMode viewMode_ = ViewMode::Details;
    int fontSize_ = 9;
    std::wstring fontFace_ = L"Segoe UI";
    std::map<std::wstring, HBITMAP> previews_;

    int focusRow_ = -1;
    int anchorRow_ = -1;
    int hoverRow_ = -1;
    int dropRow_ = -1;
    int scrollY_ = 0;
    int contentHeight_ = 0;

    int nameWidth_ = 0;
    int sizeWidth_ = 0;
    int dateWidth_ = 0;
    int typeWidth_ = 0;
    int scrollbarWidth_ = 0;
    int columnRatio_[4] = {480, 120, 230, 170};

    bool resizingColumns_ = false;
    int resizeColumn_ = -1;
    int resizeStartX_ = 0;
    int resizeStartWidth_[4]{};
    int resizeAvailable_ = 0;

    bool mouseDown_ = false;
    bool draggingOut_ = false;
    POINT dragStart_{};
    int dragRow_ = -1;
    bool scrollbarDrag_ = false;
    int scrollbarGrab_ = 0;

    bool renaming_ = false;
    int renameAllIndex_ = -1;
    std::wstring renameText_;
    bool editCommitting_ = false;

    std::wstring typeBuffer_;
    ULONGLONG typeTick_ = 0;

    DropHandler dropHandler_;
    DragOutProvider dragOutProvider_;
};

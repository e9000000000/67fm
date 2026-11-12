#include "filelist.h"
#include "commands.h"
#include "dragout.h"

#include <windowsx.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kFileListClass[] = L"67fmFileList";
constexpr UINT_PTR kEditSubclass = 1;

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Frame(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
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

bool MatchesFilter(const FileEntry& entry, const std::wstring& lowerFilter) {
    if (lowerFilter.empty()) {
        return true;
    }
    return ToLower(entry.name).find(lowerFilter) != std::wstring::npos;
}

std::wstring ReadGlobalString(HGLOBAL data) {
    if (!data) {
        return {};
    }
    const SIZE_T size = GlobalSize(data);
    const void* memory = GlobalLock(data);
    if (!memory || size == 0) {
        return {};
    }
    std::wstring result = Utf8ToWide(std::string_view(static_cast<const char*>(memory), size));
    GlobalUnlock(data);
    while (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::wstring ReadGlobalWide(HGLOBAL data) {
    if (!data) {
        return {};
    }
    const SIZE_T size = GlobalSize(data);
    const void* memory = GlobalLock(data);
    if (!memory || size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring result(static_cast<const wchar_t*>(memory),
                        size / sizeof(wchar_t));
    GlobalUnlock(data);
    while (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

bool ReadStreamToFile(IStream* stream, const std::wstring& path) {
    if (!stream) {
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    for (;;) {
        char buffer[64 * 1024];
        ULONG read = 0;
        const HRESULT hr = stream->Read(buffer, sizeof(buffer), &read);
        if (hr != S_OK && hr != S_FALSE) {
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }
        DWORD written = 0;
        if (!WriteFile(file, buffer, read, &written, nullptr) || written != read) {
            ok = false;
            break;
        }
        if (hr == S_FALSE) {
            break;
        }
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
    }
    return ok;
}

void RemoveTempTree(const std::wstring& directory) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(JoinPath(directory, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring item = JoinPath(directory, data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTempTree(item);
            } else {
                DeleteFileW(item.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(directory.c_str());
}

class FileDropTarget : public IDropTarget {
public:
    explicit FileDropTarget(FileList* list) : list_(list) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return static_cast<ULONG>(refs);
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point,
                                        DWORD* effect) override {
        (void)data;
        (void)keys;
        POINT client{point.x, point.y};
        ScreenToClient(list_->hwnd(), &client);
        list_->setDropTargetRow(list_->indexFromPoint(client.x, client.y));
        if (effect) *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override {
        (void)keys;
        POINT client{point.x, point.y};
        ScreenToClient(list_->hwnd(), &client);
        list_->setDropTargetRow(list_->indexFromPoint(client.x, client.y));
        if (effect) *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        list_->setDropTargetRow(-1);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point,
                                   DWORD* effect) override {
        list_->setDropTargetRow(-1);
        if (!data || !list_) {
            return S_OK;
        }
        POINT client{point.x, point.y};
        ScreenToClient(list_->hwnd(), &client);
        DropPayload payload;
        payload.targetRow = list_->indexFromPoint(client.x, client.y);
        payload.move = (keys & MK_SHIFT) && !(keys & MK_CONTROL);

        FORMATETC filesFormat{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (data->GetData(&filesFormat, &medium) == S_OK) {
            HDROP drop = static_cast<HDROP>(medium.hGlobal);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (length == 0) {
                    continue;
                }
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                payload.files.push_back(std::move(path));
            }
            ReleaseStgMedium(&medium);
        }

        std::wstring tempDirectory;
        const UINT descriptorFormat = RegisterClipboardFormatW(L"FileGroupDescriptorW");
        FORMATETC descriptor{static_cast<CLIPFORMAT>(descriptorFormat), nullptr,
                             DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        if (data->GetData(&descriptor, &medium) == S_OK && medium.hGlobal) {
            const SIZE_T size = GlobalSize(medium.hGlobal);
            const auto* group = static_cast<const FILEGROUPDESCRIPTORW*>(
                GlobalLock(medium.hGlobal));
            if (group && size >= sizeof(UINT) &&
                group->cItems <= 1024 &&
                size >= sizeof(UINT) + group->cItems * sizeof(FILEDESCRIPTORW)) {
                wchar_t temp[MAX_PATH] = {};
                GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
                tempDirectory = JoinPath(temp, L"67fm-drop-" + std::to_wstring(GetCurrentProcessId()) +
                                                  L"-" + std::to_wstring(GetTickCount64()));
                EnsureDirectory(tempDirectory);
                const UINT contentsFormat = RegisterClipboardFormatW(L"FileContents");
                for (UINT i = 0; i < group->cItems; ++i) {
                    const std::wstring name = SanitizeFileName(group->fgd[i].cFileName);
                    if (name.empty()) {
                        continue;
                    }
                    const std::wstring path = JoinPath(tempDirectory, name);
                    FORMATETC contents{static_cast<CLIPFORMAT>(contentsFormat), nullptr,
                                       DVASPECT_CONTENT, static_cast<LONG>(i),
                                       TYMED_ISTREAM | TYMED_HGLOBAL};
                    STGMEDIUM stream{};
                    if (data->GetData(&contents, &stream) == S_OK) {
                        if (stream.tymed == TYMED_ISTREAM && stream.pstm) {
                            ReadStreamToFile(stream.pstm, path);
                        } else if (stream.tymed == TYMED_HGLOBAL && stream.hGlobal) {
                            const SIZE_T bytes = GlobalSize(stream.hGlobal);
                            const void* memory = GlobalLock(stream.hGlobal);
                            if (memory) {
                                HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                                          nullptr);
                                if (file != INVALID_HANDLE_VALUE) {
                                    DWORD written = 0;
                                    WriteFile(file, memory, static_cast<DWORD>(bytes), &written,
                                              nullptr);
                                    CloseHandle(file);
                                }
                                GlobalUnlock(stream.hGlobal);
                            }
                        }
                        ReleaseStgMedium(&stream);
                    }
                    payload.files.push_back(path);
                }
            }
            if (group) {
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
        }

        const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
        FORMATETC html{static_cast<CLIPFORMAT>(htmlFormat), nullptr, DVASPECT_CONTENT, -1,
                       TYMED_HGLOBAL};
        if (data->GetData(&html, &medium) == S_OK) {
            payload.html = ReadGlobalString(medium.hGlobal);
            ReleaseStgMedium(&medium);
        }

        FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        if (data->GetData(&text, &medium) == S_OK) {
            payload.text = ReadGlobalWide(medium.hGlobal);
            ReleaseStgMedium(&medium);
        }

        const UINT uriFormat = RegisterClipboardFormatW(L"text/uri-list");
        FORMATETC uri{static_cast<CLIPFORMAT>(uriFormat), nullptr, DVASPECT_CONTENT, -1,
                      TYMED_HGLOBAL};
        if (payload.text.empty() && data->GetData(&uri, &medium) == S_OK) {
            payload.text = ReadGlobalString(medium.hGlobal);
            ReleaseStgMedium(&medium);
        }

        if (effect) {
            *effect = payload.move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        if (!payload.files.empty() || !payload.urls.empty() || !payload.html.empty() ||
            !payload.text.empty()) {
            list_->invokeDrop(payload);
        }
        if (!tempDirectory.empty()) {
            RemoveTempTree(tempDirectory);
        }
        return S_OK;
    }

private:
    LONG refs_ = 1;
    FileList* list_ = nullptr;
};

}  // namespace

FileList::~FileList() {
    clearPreviews();
    if (dropTarget_) {
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
    if (editBrush_) {
        DeleteObject(editBrush_);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

bool FileList::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kFileListClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kFileListClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
                                                    WS_TABSTOP,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    editBrush_ = CreateSolidBrush(theme::kInput);
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT | WS_BORDER,
                            0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
    if (!edit_) {
        return false;
    }
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SetWindowSubclass(edit_, EditProc, kEditSubclass, reinterpret_cast<DWORD_PTR>(this));
    auto* target = new FileDropTarget(this);
    if (SUCCEEDED(RegisterDragDrop(hwnd_, target))) {
        dropTarget_ = target;
    } else {
        target->Release();
    }
    return true;
}

void FileList::ensureFont() {
    const UINT dpi = DpiOfWindow(hwnd_);
    if (font_ && dpi == dpi_) {
        return;
    }
    dpi_ = dpi;
    if (font_) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(fontSizePoints(), static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE,
                        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace_.c_str());
    if (edit_) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
}

int FileList::fontSizePoints() const {
    return fontSize_;
}

int FileList::rowHeightPixels() const {
    return theme::Scale(fontSize_ + 16, dpi_);
}

int FileList::headerHeightPixels() const {
    return theme::Scale(fontSize_ + 18, dpi_);
}

int FileList::iconSizePixels() const {
    const int delta = fontSize_ - 9;
    switch (viewMode_) {
    case ViewMode::Details: {
        const int desired = theme::Scale(std::max(12, 16 + delta * 2), dpi_);
        return std::max(theme::Scale(12, dpi_),
                        std::min(desired, rowHeightPixels() - theme::Scale(4, dpi_)));
    }
    case ViewMode::OneColumn:
    case ViewMode::TwoColumns:
        return theme::Scale(std::max(24, 36 + delta * 2), dpi_);
    case ViewMode::SmallIcons:
        return theme::Scale(std::max(20, 24 + delta * 2), dpi_);
    case ViewMode::MediumIcons:
        return theme::Scale(std::max(36, 48 + delta * 3), dpi_);
    case ViewMode::LargeIcons:
        return theme::Scale(std::max(48, 64 + delta * 4), dpi_);
    case ViewMode::ExtraLargeIcons:
        return theme::Scale(std::max(72, 96 + delta * 5), dpi_);
    }
    return theme::Scale(16, dpi_);
}

int FileList::cellWidthPixels() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int available = std::max(1, static_cast<int>(client.right) - scrollbarWidth_);
    const int delta = std::max(0, fontSize_ - 9);
    switch (viewMode_) {
    case ViewMode::OneColumn:
        return available;
    case ViewMode::TwoColumns:
        return std::max(1, available / 2);
    case ViewMode::SmallIcons:
        return theme::Scale(96 + delta * 3, dpi_);
    case ViewMode::MediumIcons:
        return theme::Scale(126 + delta * 3, dpi_);
    case ViewMode::LargeIcons:
        return theme::Scale(160 + delta * 4, dpi_);
    case ViewMode::ExtraLargeIcons:
        return theme::Scale(220 + delta * 5, dpi_);
    default:
        return available;
    }
}

int FileList::cellHeightPixels() const {
    switch (viewMode_) {
    case ViewMode::OneColumn:
    case ViewMode::TwoColumns:
        return theme::Scale(fontSize_ * 2 + 28, dpi_);
    case ViewMode::SmallIcons:
    case ViewMode::MediumIcons:
    case ViewMode::LargeIcons:
    case ViewMode::ExtraLargeIcons:
        return iconSizePixels() + theme::Scale(fontSize_ * 2 + 32, dpi_);
    default:
        return rowHeightPixels();
    }
}

int FileList::columns() const {
    if (viewMode_ == ViewMode::OneColumn) {
        return 1;
    }
    if (viewMode_ == ViewMode::TwoColumns) {
        return 2;
    }
    if (viewMode_ == ViewMode::Details) {
        return 1;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int available = std::max(1, static_cast<int>(client.right) - scrollbarWidth_);
    return std::max(1, available / std::max(1, cellWidthPixels()));
}

int FileList::visualRows() const {
    const int cols = columns();
    return (static_cast<int>(view_.size()) + cols - 1) / std::max(1, cols);
}

RECT FileList::cellRect(int viewRow) const {
    const int cols = std::max(1, columns());
    const int col = viewRow % cols;
    const int row = viewRow / cols;
    const int width = cellWidthPixels();
    const int height = cellHeightPixels();
    const int left = col * width;
    const int top = row * height - scrollY_;
    return RECT{left, top, left + width, top + height};
}

void FileList::layout(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = DpiOfWindow(hwnd_);
    ensureFont();
    updateMetrics();
    clampScroll();
    invalidate();
}

void FileList::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FileList::setEntries(std::vector<FileEntry> entries, bool preserveScroll) {
    if (renaming_) {
        commitInlineEdit(true);
    }
    clearPreviews();
    std::vector<std::wstring> selectedKeys;
    selectedKeys.reserve(selected_.size());
    for (const auto& entry : selectedEntries()) {
        selectedKeys.push_back(entry.path.empty() ? entry.name : entry.path);
    }
    std::wstring focusKey;
    if (focusRow_ >= 0 && focusRow_ < static_cast<int>(view_.size())) {
        const FileEntry& entry = entries_[view_[focusRow_]];
        focusKey = entry.path.empty() ? entry.name : entry.path;
    }

    entries_ = std::move(entries);
    selected_.clear();
    focusRow_ = -1;
    anchorRow_ = -1;
    hoverRow_ = -1;
    view_.clear();
    rebuildView(preserveScroll);

    for (size_t i = 0; i < view_.size(); ++i) {
        const FileEntry& entry = entries_[view_[i]];
        const std::wstring key = entry.path.empty() ? entry.name : entry.path;
        if (!focusKey.empty() && key == focusKey) {
            focusRow_ = static_cast<int>(i);
        }
        for (const auto& selectedKey : selectedKeys) {
            if (key == selectedKey) {
                selected_.insert(view_[i]);
                break;
            }
        }
    }
    if (focusRow_ >= 0) {
        anchorRow_ = focusRow_;
        ensureRowVisible(focusRow_);
    }
    if (!preserveScroll) {
        scrollY_ = 0;
    }
    updateMetrics();
    clampScroll();
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::clearEntries(const std::wstring& message) {
    clearPreviews();
    entries_.clear();
    view_.clear();
    selected_.clear();
    focusRow_ = -1;
    anchorRow_ = -1;
    hoverRow_ = -1;
    scrollY_ = 0;
    emptyMessage_ = message;
    updateMetrics();
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::setEmptyMessage(const std::wstring& message) {
    emptyMessage_ = message;
    invalidate();
}

void FileList::setSort(int column, bool ascending) {
    sortColumn_ = std::max(0, std::min(3, column));
    sortAscending_ = ascending;
    rebuildView(true);
    invalidate();
}

void FileList::setFilter(const std::wstring& filter) {
    filter_ = filter;
    rebuildView(false);
    scrollY_ = 0;
    updateMetrics();
    clampScroll();
    invalidate();
}

void FileList::setViewMode(ViewMode mode) {
    const int value = std::max(0, std::min(ViewModeCount() - 1, static_cast<int>(mode)));
    const ViewMode next = static_cast<ViewMode>(value);
    if (next == viewMode_) {
        return;
    }
    const int topRow = firstVisibleRow();
    viewMode_ = next;
    if (renaming_) {
        commitInlineEdit(true);
    }
    clearPreviews();
    updateMetrics();
    scrollY_ = topRow * (detailsMode() ? rowHeightPixels() : cellHeightPixels());
    clampScroll();
    invalidate();
}

void FileList::setFont(const std::wstring& face, int points) {
    points = std::max(6, std::min(36, points));
    const std::wstring nextFace = face.empty() ? L"Segoe UI" : face;
    if (points == fontSize_ && _wcsicmp(nextFace.c_str(), fontFace_.c_str()) == 0) {
        return;
    }
    const int topRow = firstVisibleRow();
    fontSize_ = points;
    fontFace_ = nextFace;
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    clearPreviews();
    ensureFont();
    updateMetrics();
    scrollY_ = topRow * (detailsMode() ? rowHeightPixels() : cellHeightPixels());
    clampScroll();
    invalidate();
}

void FileList::clearPreviews() {
    for (auto& item : previews_) {
        if (item.second) {
            DeleteObject(item.second);
        }
    }
    previews_.clear();
}

void FileList::setColumnRatios(const int ratios[4]) {
    int values[4] = {480, 120, 230, 170};
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        values[i] = std::max(20, std::min(900, ratios[i]));
        sum += values[i];
    }
    if (sum <= 0) {
        return;
    }
    int used = 0;
    for (int i = 0; i < 3; ++i) {
        columnRatio_[i] = values[i] * 1000 / sum;
        used += columnRatio_[i];
    }
    columnRatio_[3] = 1000 - used;
    updateMetrics();
    invalidate();
}

void FileList::rebuildView(bool preserveScroll) {
    const int oldScroll = scrollY_;
    const int oldFocusAll = focusedIndex();
    view_.clear();
    view_.reserve(entries_.size());
    const std::wstring lowerFilter = ToLower(filter_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (MatchesFilter(entries_[i], lowerFilter)) {
            view_.push_back(static_cast<int>(i));
        }
    }
    std::stable_sort(view_.begin(), view_.end(), [&](int a, int b) {
        const FileEntry& ea = entries_[a];
        const FileEntry& eb = entries_[b];
        if (ea.directory != eb.directory) {
            return ea.directory;
        }
        const int result = CompareEntries(ea, eb, sortColumn_, sortAscending_);
        return result < 0;
    });
    focusRow_ = oldFocusAll >= 0 ? viewRowOf(oldFocusAll) : -1;
    if (focusRow_ >= 0) {
        anchorRow_ = focusRow_;
    }
    if (preserveScroll) {
        scrollY_ = oldScroll;
    }
    clampScroll();
}

void FileList::updateMetrics() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = std::max(1, static_cast<int>(client.right));
    scrollbarWidth_ = theme::Scale(10, dpi_);
    const int available = std::max(1, width - scrollbarWidth_);
    int totalRatio = 0;
    for (int ratio : columnRatio_) {
        totalRatio += std::max(1, ratio);
    }
    if (totalRatio <= 0) {
        totalRatio = 1000;
    }
    int used = 0;
    const int minimum = theme::Scale(40, dpi_);
    nameWidth_ = std::max(minimum, available * columnRatio_[0] / totalRatio);
    sizeWidth_ = std::max(minimum, available * columnRatio_[1] / totalRatio);
    dateWidth_ = std::max(minimum, available * columnRatio_[2] / totalRatio);
    used = nameWidth_ + sizeWidth_ + dateWidth_;
    typeWidth_ = std::max(minimum, available - used);
    if (detailsMode()) {
        contentHeight_ = static_cast<int>(view_.size()) * rowHeightPixels();
    } else {
        contentHeight_ = visualRows() * cellHeightPixels();
    }
}

void FileList::clampScroll() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int contentTop = detailsMode() ? headerHeightPixels() : 0;
    const int viewHeight = std::max(0, static_cast<int>(client.bottom) - contentTop);
    const int maxScroll = std::max(0, contentHeight_ - viewHeight);
    scrollY_ = std::max(0, std::min(scrollY_, maxScroll));
}

void FileList::scrollToRow(int row) {
    const int step = detailsMode() ? rowHeightPixels() : cellHeightPixels();
    scrollY_ = std::max(0, row) * step;
    clampScroll();
    invalidate();
}

int FileList::firstVisibleRow() const {
    const int step = detailsMode() ? rowHeightPixels() : cellHeightPixels();
    return step > 0 ? scrollY_ / step : 0;
}

int FileList::indexFromPoint(int x, int y) const {
    if (detailsMode()) {
        const int headerHeight = headerHeightPixels();
        if (y < headerHeight || y < 0) {
            return -1;
        }
        const int rowHeight = std::max(1, rowHeightPixels());
        const int row = (y - headerHeight + scrollY_) / rowHeight;
        return row >= 0 && row < static_cast<int>(view_.size()) ? row : -1;
    }
    if (x < 0 || y < 0) {
        return -1;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (x >= client.right - scrollbarWidth_) {
        return -1;
    }
    const int cols = std::max(1, columns());
    const int width = std::max(1, cellWidthPixels());
    const int height = std::max(1, cellHeightPixels());
    const int col = x / width;
    if (col >= cols) {
        return -1;
    }
    const int row = (y + scrollY_) / height;
    const int index = row * cols + col;
    return index >= 0 && index < static_cast<int>(view_.size()) ? index : -1;
}

int FileList::allIndexFromRow(int viewRow) const {
    return viewRow >= 0 && viewRow < static_cast<int>(view_.size()) ? view_[viewRow] : -1;
}

int FileList::viewRowOf(int allIndex) const {
    for (size_t i = 0; i < view_.size(); ++i) {
        if (view_[i] == allIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FileList::focusedIndex() const {
    return allIndexFromRow(focusRow_);
}

const FileEntry* FileList::entryAt(int allIndex) const {
    return allIndex >= 0 && allIndex < static_cast<int>(entries_.size()) ? &entries_[allIndex]
                                                                         : nullptr;
}

const FileEntry* FileList::entryAtViewRow(int viewRow) const {
    return entryAt(allIndexFromRow(viewRow));
}

std::vector<FileEntry> FileList::selectedEntries() const {
    std::vector<FileEntry> result;
    for (int index : view_) {
        if (selected_.count(index)) {
            result.push_back(entries_[index]);
        }
    }
    return result;
}

std::vector<std::wstring> FileList::selectedPaths() const {
    std::vector<std::wstring> result;
    for (int index : view_) {
        if (selected_.count(index) && !entries_[index].path.empty()) {
            result.push_back(entries_[index].path);
        }
    }
    return result;
}

void FileList::selectAll() {
    selected_.clear();
    for (int index : view_) {
        selected_.insert(index);
    }
    if (!view_.empty()) {
        focusRow_ = 0;
        anchorRow_ = 0;
    }
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::invertSelection() {
    for (int index : view_) {
        if (!selected_.erase(index)) {
            selected_.insert(index);
        }
    }
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::clearSelection() {
    selected_.clear();
    focusRow_ = -1;
    anchorRow_ = -1;
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::selectIndex(int allIndex) {
    const int row = viewRowOf(allIndex);
    if (row < 0) {
        return;
    }
    selected_.clear();
    selected_.insert(allIndex);
    focusRow_ = row;
    anchorRow_ = row;
    ensureRowVisible(row);
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

void FileList::focus() {
    if (hwnd_) {
        SetFocus(hwnd_);
    }
}

void FileList::ensureRowVisible(int viewRow) {
    if (viewRow < 0) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int contentTop = detailsMode() ? headerHeightPixels() : 0;
    const int step = std::max(1, detailsMode() ? rowHeightPixels() : cellHeightPixels());
    const int visualRow = detailsMode() ? viewRow : viewRow / std::max(1, columns());
    const int viewHeight = std::max(0, static_cast<int>(client.bottom) - contentTop);
    const int top = visualRow * step;
    const int bottom = top + step;
    if (top < scrollY_) {
        scrollY_ = top;
    } else if (bottom > scrollY_ + viewHeight) {
        scrollY_ = bottom - viewHeight;
    }
    clampScroll();
}

void FileList::setFocusedRow(int viewRow, bool extend, bool keepOthers) {
    if (viewRow < 0 || viewRow >= static_cast<int>(view_.size())) {
        return;
    }
    if (anchorRow_ < 0) {
        anchorRow_ = viewRow;
    }
    if (extend) {
        if (!keepOthers) {
            selected_.clear();
        }
        const int first = std::min(anchorRow_, viewRow);
        const int last = std::max(anchorRow_, viewRow);
        for (int row = first; row <= last; ++row) {
            selected_.insert(view_[row]);
        }
    } else {
        selected_.clear();
        selected_.insert(view_[viewRow]);
        anchorRow_ = viewRow;
    }
    focusRow_ = viewRow;
    ensureRowVisible(viewRow);
    notify(filelist_events::kSelectionChanged);
    invalidate();
}

bool FileList::pointInScrollbar(int x, int y) const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int contentTop = detailsMode() ? headerHeightPixels() : 0;
    return x >= client.right - scrollbarWidth_ && y >= contentTop;
}

void FileList::updateScrollFromThumb(int y) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int contentTop = detailsMode() ? headerHeightPixels() : 0;
    const int viewHeight = std::max(1, static_cast<int>(client.bottom) - contentTop);
    if (contentHeight_ <= viewHeight) {
        return;
    }
    const int thumbHeight =
        std::max(theme::Scale(20, dpi_), viewHeight * viewHeight / contentHeight_);
    const int travel = std::max(1, viewHeight - thumbHeight);
    const int thumbY = std::max(0, std::min(y - scrollbarGrab_ - contentTop, travel));
    const int maxScroll = contentHeight_ - viewHeight;
    scrollY_ = thumbY * maxScroll / travel;
    clampScroll();
    invalidate();
}

void FileList::setDropTargetRow(int viewRow) {
    const int row = viewRow >= 0 && viewRow < static_cast<int>(view_.size()) ? viewRow : -1;
    if (row != dropRow_) {
        dropRow_ = row;
        invalidate();
    }
}

void FileList::beginRename(int allIndex) {
    const int row = viewRowOf(allIndex);
    if (row < 0) {
        return;
    }
    if (renaming_) {
        commitInlineEdit(true);
    }
    renameAllIndex_ = allIndex;
    renaming_ = true;
    renameText_.clear();
    ensureRowVisible(row);
    updateMetrics();
    const int pad = theme::Scale(6, dpi_);
    const int line = theme::Scale(fontSize_ + 7, dpi_);
    int editX = 0;
    int editY = 0;
    int editW = 0;
    int editH = line;
    if (detailsMode()) {
        const int headerHeight = headerHeightPixels();
        const int y = headerHeight + row * rowHeightPixels() - scrollY_;
        editX = theme::Scale(12, dpi_) + iconSizePixels() + pad;
        editW = std::max(theme::Scale(60, dpi_),
                         nameWidth_ - iconSizePixels() - theme::Scale(24, dpi_));
        editY = y + theme::Scale(2, dpi_);
        editH = rowHeightPixels() - theme::Scale(4, dpi_);
    } else {
        const RECT cell = cellRect(row);
        if (viewMode_ == ViewMode::OneColumn || viewMode_ == ViewMode::TwoColumns) {
            editX = cell.left + pad + iconSizePixels() + pad;
            editW = std::max(theme::Scale(50, dpi_),
                             static_cast<int>(cell.right) - editX - pad);
            editY = cell.top + pad;
        } else {
            editX = cell.left + pad;
            editW = std::max(theme::Scale(50, dpi_),
                             static_cast<int>(cell.right) - editX - pad);
            editY = cell.top + pad + iconSizePixels() + theme::Scale(2, dpi_);
        }
        editH = line;
    }
    SetWindowPos(edit_, HWND_TOP, editX, editY, editW, editH, SWP_SHOWWINDOW);
    const std::wstring editName = FileNameOf(entries_[allIndex].name);
    SetWindowTextW(edit_, editName.c_str());
    const size_t dot = editName.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        SendMessageW(edit_, EM_SETSEL, 0, static_cast<LPARAM>(dot));
    } else {
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
    SetFocus(edit_);
    invalidate();
}

void FileList::beginRenameName(const std::wstring& name) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (_wcsicmp(entries_[i].name.c_str(), name.c_str()) == 0) {
            beginRename(static_cast<int>(i));
            return;
        }
    }
}

std::wstring FileList::takeRenameText() {
    std::wstring text = std::move(renameText_);
    renameText_.clear();
    return text;
}

void FileList::openInlineEdit(int viewRow) {
    const int all = allIndexFromRow(viewRow);
    if (all >= 0) {
        beginRename(all);
    }
}

void FileList::commitInlineEdit(bool cancel) {
    if (!renaming_) {
        return;
    }
    editCommitting_ = true;
    const int all = renameAllIndex_;
    std::wstring text = ControlText(edit_);
    ShowWindow(edit_, SW_HIDE);
    renaming_ = false;
    renameAllIndex_ = -1;
    SetFocus(hwnd_);
    if (!cancel) {
        text = Trim(text);
        if (!text.empty() && all >= 0 && text != entries_[all].name) {
            renameText_ = text;
            notify(filelist_events::kRenameCommit, static_cast<WPARAM>(all));
        }
    }
    editCommitting_ = false;
    invalidate();
}

void FileList::startDragOut() {
    if (draggingOut_ || !dragOutProvider_) {
        return;
    }
    draggingOut_ = true;
    mouseDown_ = false;
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    const std::vector<std::wstring> paths = dragOutProvider_();
    if (paths.empty()) {
        draggingOut_ = false;
        dragRow_ = -1;
        return;
    }
    const DWORD effect = BeginFileDrag(paths);
    notify(filelist_events::kDragFinished,
           (effect & DROPEFFECT_MOVE) ? 1 : 0);
    draggingOut_ = false;
    dragRow_ = -1;
    invalidate();
}

bool FileList::hitHeader(int x, int y, int& column) const {
    if (!detailsMode()) {
        return false;
    }
    const int headerHeight = headerHeightPixels();
    if (y < 0 || y >= headerHeight) {
        return false;
    }
    int left = 0;
    const int widths[4] = {nameWidth_, sizeWidth_, dateWidth_, typeWidth_};
    for (int i = 0; i < 4; ++i) {
        if (x >= left && x < left + widths[i]) {
            column = i;
            return true;
        }
        left += widths[i];
    }
    return false;
}

int FileList::columnBoundaryAt(int x, int y) const {
    if (!detailsMode()) {
        return -1;
    }
    if (y < 0 || y >= headerHeightPixels()) {
        return -1;
    }
    const int threshold = theme::Scale(5, dpi_);
    const int widths[4] = {nameWidth_, sizeWidth_, dateWidth_, typeWidth_};
    int left = 0;
    for (int i = 0; i < 3; ++i) {
        left += widths[i];
        if (x >= left - threshold && x <= left + threshold) {
            return i;
        }
    }
    return -1;
}

RECT FileList::columnRect(int column, int y, int height) const {
    int left = 0;
    const int widths[4] = {nameWidth_, sizeWidth_, dateWidth_, typeWidth_};
    for (int i = 0; i < column; ++i) {
        left += widths[i];
    }
    const int width = column >= 0 && column < 4 ? widths[column] : 0;
    return RECT{left, y, left + width, y + height};
}

void FileList::paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = std::max(0, static_cast<int>(client.right));
    const int height = std::max(0, static_cast<int>(client.bottom));
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    Fill(dc, client, theme::kBg);

    updateMetrics();
    clampScroll();
    const int headerHeight = detailsMode() ? headerHeightPixels() : 0;
    const int rowHeight = rowHeightPixels();

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    // Header (details only).
    if (detailsMode() && headerHeight > 0) {
        RECT header{0, 0, width, headerHeight};
        Fill(dc, header, theme::kHeader);
        RECT line{0, headerHeight - theme::Scale(1, dpi_), width, headerHeight};
        Fill(dc, line, theme::kBorder);
        const wchar_t* titles[4] = {L"Name", L"Size", L"Date modified", L"Type"};
        for (int i = 0; i < 4; ++i) {
            RECT full = columnRect(i, 0, headerHeight);
            RECT cell = full;
            cell.left += theme::Scale(12, dpi_);
            cell.right -= theme::Scale(12, dpi_);
            if (i == sortColumn_) {
                cell.right -= theme::Scale(14, dpi_);
            }
            SetTextColor(dc, i == sortColumn_ ? theme::kText : theme::kHeaderText);
            UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
            if (i == 1) {
                flags |= DT_RIGHT;
            }
            DrawTextW(dc, titles[i], -1, &cell, flags);
            if (i == sortColumn_) {
                const int cx = full.right - theme::Scale(12, dpi_);
                const int cy = headerHeight / 2;
                const int half = theme::Scale(3, dpi_);
                POINT triangle[3];
                if (sortAscending_) {
                    triangle[0] = {cx - half, cy + half / 2};
                    triangle[1] = {cx + half, cy + half / 2};
                    triangle[2] = {cx, cy - half};
                } else {
                    triangle[0] = {cx - half, cy - half / 2};
                    triangle[1] = {cx + half, cy - half / 2};
                    triangle[2] = {cx, cy + half};
                }
                HBRUSH brush = CreateSolidBrush(theme::kTextDim);
                HPEN pen = CreatePen(PS_SOLID, 1, theme::kTextDim);
                HGDIOBJ oldBrush = SelectObject(dc, brush);
                HGDIOBJ oldPen = SelectObject(dc, pen);
                Polygon(dc, triangle, 3);
                SelectObject(dc, oldPen);
                SelectObject(dc, oldBrush);
                DeleteObject(pen);
                DeleteObject(brush);
            }
        }
    }

    if (view_.empty()) {
        const std::wstring message = filter_.empty() ? emptyMessage_ : L"No matching items";
        RECT textArea{0, headerHeight, width, height};
        SetTextColor(dc, theme::kTextFaint);
        DrawTextW(dc, message.c_str(), -1, &textArea,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else if (detailsMode()) {
        const int start = scrollY_ / std::max(1, rowHeight);
        const int offset = scrollY_ % std::max(1, rowHeight);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 0, headerHeight, width, height);
        for (int row = start; row < static_cast<int>(view_.size()); ++row) {
            const int y = headerHeight + (row - start) * rowHeight - offset;
            if (y >= height) {
                break;
            }
            if (y + rowHeight > headerHeight) {
                drawRow(dc, row, y, width);
            }
        }
        RestoreDC(dc, saved);
    } else {
        const int cols = std::max(1, columns());
        const int cellHeight = std::max(1, cellHeightPixels());
        const int rows = visualRows();
        int startRow = scrollY_ / cellHeight;
        if (startRow >= rows && rows > 0) {
            startRow = rows - 1;
        }
        const int offset = scrollY_ % cellHeight;
        for (int visualRow = startRow;; ++visualRow) {
            const int y = (visualRow - startRow) * cellHeight - offset;
            if (y >= height) {
                break;
            }
            if (y + cellHeight <= 0) {
                continue;
            }
            for (int col = 0; col < cols; ++col) {
                const int index = visualRow * cols + col;
                if (index >= static_cast<int>(view_.size())) {
                    break;
                }
                const int left = col * cellWidthPixels();
                RECT cell{left, y, left + cellWidthPixels(), y + cellHeight};
                const int all = view_[index];
                const bool selected = selected_.count(all) != 0;
                const bool hover = index == hoverRow_;
                const bool dropTarget = index == dropRow_;
                drawTile(dc, index, cell, selected, hover, dropTarget);
            }
        }
    }

    // Scrollbar.
    if (contentHeight_ > height - headerHeight && scrollbarWidth_ > 0) {
        const int viewHeight = height - headerHeight;
        const int maxScroll = contentHeight_ - viewHeight;
        const int thumbHeight =
            std::max(theme::Scale(20, dpi_), viewHeight * viewHeight / contentHeight_);
        const int travel = std::max(1, viewHeight - thumbHeight);
        const int thumbY = headerHeight + (maxScroll > 0 ? scrollY_ * travel / maxScroll : 0);
        RECT track{width - scrollbarWidth_, headerHeight, width, height};
        Fill(dc, track, theme::kPanel);
        RECT thumb{width - scrollbarWidth_ + theme::Scale(2, dpi_), thumbY,
                   width - theme::Scale(2, dpi_), thumbY + thumbHeight};
        Fill(dc, thumb, theme::kBorder);
    }

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void FileList::drawIcon(HDC dc, const FileEntry& entry, int x, int y) {
    const int size = iconSizePixels();
    const auto p = [size](int value) { return std::max(1, value * size / 16); };
    if (entry.directory) {
        HBRUSH brush = CreateSolidBrush(theme::kFolder);
        HPEN pen = CreatePen(PS_SOLID, 1, theme::kFolder);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RECT tab{x + p(1), y + p(2), x + p(7), y + p(6)};
        RoundRect(dc, tab.left, tab.top, tab.right, tab.bottom, 2, 2);
        Rectangle(dc, x + p(1), y + p(5), x + size - p(1), y + size - p(2));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    } else if (entry.archive) {
        HBRUSH brush = CreateSolidBrush(theme::kArchive);
        HPEN pen = CreatePen(PS_SOLID, 1, theme::kArchive);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        Rectangle(dc, x + p(2), y + p(1), x + size - p(2), y + size - p(1));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        RECT zipper{x + size / 2 - p(1), y + p(3), x + size / 2 + p(1), y + size - p(3)};
        Fill(dc, zipper, theme::kBg);
    } else {
        HPEN pen = CreatePen(PS_SOLID, 1, theme::kFile);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, x + p(3), y + p(1), x + size - p(3), y + size - p(1));
        MoveToEx(dc, x + p(6), y + p(4), nullptr);
        LineTo(dc, x + size - p(5), y + p(4));
        MoveToEx(dc, x + p(5), y + p(7), nullptr);
        LineTo(dc, x + size - p(5), y + p(7));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
}

HBITMAP FileList::previewFor(const FileEntry& entry, int size) {
    if (entry.path.empty() || !entry.inner.empty() || entry.directory || entry.archive) {
        return nullptr;
    }
    const std::wstring key = entry.path + L"|" + std::to_wstring(size);
    const auto found = previews_.find(key);
    if (found != previews_.end()) {
        return found->second;
    }
    HBITMAP bitmap = nullptr;
    IShellItemImageFactory* factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(entry.path.c_str(), nullptr,
                                              IID_PPV_ARGS(&factory))) &&
        factory) {
        SIZE requested{size, size};
        if (FAILED(factory->GetImage(requested, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK,
                                     &bitmap)) ||
            !bitmap) {
            factory->GetImage(requested, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
        }
        factory->Release();
    }
    previews_.emplace(key, bitmap);
    return bitmap;
}

void FileList::drawPreview(HDC dc, HBITMAP bitmap, int x, int y, int size) {
    if (!bitmap) {
        return;
    }
    BITMAP info{};
    if (!GetObjectW(bitmap, sizeof(info), &info)) {
        return;
    }
    HDC memory = CreateCompatibleDC(dc);
    if (!memory) {
        return;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    if (info.bmBitsPixel == 32) {
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!AlphaBlend(dc, x, y, size, size, memory, 0, 0, info.bmWidth, info.bmHeight,
                        blend)) {
            SetStretchBltMode(dc, HALFTONE);
            StretchBlt(dc, x, y, size, size, memory, 0, 0, info.bmWidth, info.bmHeight,
                       SRCCOPY);
        }
    } else {
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, x, y, size, size, memory, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
    }
    SelectObject(memory, old);
    DeleteDC(memory);
}

void FileList::drawRow(HDC dc, int viewRow, int y, int width) {
    const int all = view_[viewRow];
    const FileEntry& entry = entries_[all];
    const int rowHeight = rowHeightPixels();
    const bool selected = selected_.count(all) != 0;
    const bool hover = viewRow == hoverRow_;
    const bool dropTarget = viewRow == dropRow_ && entry.directory;
    RECT row{0, y, width, y + rowHeight};
    if (selected) {
        Fill(dc, row, theme::kSelection);
    } else if (dropTarget) {
        Fill(dc, row, theme::kSelectionDim);
    } else if (hover) {
        Fill(dc, row, theme::kRowHover);
    } else if ((viewRow & 1) != 0) {
        Fill(dc, row, theme::kRowAlt);
    }

    const int iconX = theme::Scale(12, dpi_);
    const int iconY = y + (rowHeight - iconSizePixels()) / 2;
    HBITMAP preview = previewFor(entry, iconSizePixels());
    if (preview) {
        drawPreview(dc, preview, iconX, iconY, iconSizePixels());
    } else {
        drawIcon(dc, entry, iconX, iconY);
    }

    const int textTop = y + theme::Scale(1, dpi_);
    const int textHeight = rowHeight - theme::Scale(2, dpi_);
    const int nameLeft = iconX + iconSizePixels() + theme::Scale(6, dpi_);

    RECT nameRect{nameLeft, textTop, nameLeft + nameWidth_ - iconSizePixels() -
                                              theme::Scale(24, dpi_),
                  textTop + textHeight};
    SetTextColor(dc, entry.directory ? theme::kText : theme::kText);
    DrawTextW(dc, entry.name.c_str(), -1, &nameRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (!entry.directory) {
        RECT sizeRect = columnRect(1, textTop, textHeight);
        sizeRect.left += theme::Scale(10, dpi_);
        sizeRect.right -= theme::Scale(14, dpi_);
        SetTextColor(dc, theme::kTextDim);
        const std::wstring size = FormatBytes(entry.size);
        DrawTextW(dc, size.c_str(), -1, &sizeRect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    RECT dateRect = columnRect(2, textTop, textHeight);
    dateRect.left += theme::Scale(10, dpi_);
    dateRect.right -= theme::Scale(10, dpi_);
    SetTextColor(dc, theme::kTextDim);
    const std::wstring date = FormatFileTime(entry.modified);
    DrawTextW(dc, date.c_str(), -1, &dateRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT typeRect = columnRect(3, textTop, textHeight);
    typeRect.left += theme::Scale(10, dpi_);
    typeRect.right -= theme::Scale(10, dpi_);
    SetTextColor(dc, theme::kTextDim);
    const std::wstring type = FileTypeName(entry.name, entry.directory);
    DrawTextW(dc, type.c_str(), -1, &typeRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void FileList::drawTile(HDC dc, int viewRow, const RECT& cell, bool selected, bool hover,
                        bool dropTarget) {
    const int all = view_[viewRow];
    const FileEntry& entry = entries_[all];
    const int pad = theme::Scale(8, dpi_);
    const int icon = iconSizePixels();
    const int line = theme::Scale(fontSize_ + 7, dpi_);
    const int detailLine = theme::Scale(fontSize_ + 4, dpi_);

    RECT background = cell;
    InflateRect(&background, -theme::Scale(1, dpi_), -theme::Scale(1, dpi_));
    if (selected) {
        Fill(dc, background, theme::kSelection);
    } else if (dropTarget && entry.directory) {
        Fill(dc, background, theme::kSelectionDim);
    } else if (hover) {
        Fill(dc, background, theme::kRowHover);
    }

    if (viewMode_ == ViewMode::OneColumn || viewMode_ == ViewMode::TwoColumns) {
        const int iconX = cell.left + pad;
        const int iconY = cell.top + (static_cast<int>(cell.bottom - cell.top) - icon) / 2;
        HBITMAP preview = previewFor(entry, icon);
        if (preview) {
            drawPreview(dc, preview, iconX, iconY, icon);
        } else {
            drawIcon(dc, entry, iconX, iconY);
        }
        const int nameLeft = iconX + icon + pad;
        const int nameTop = cell.top + pad;
        RECT nameRect{nameLeft, nameTop, cell.right - pad, nameTop + line};
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, entry.name.c_str(), -1, &nameRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const std::wstring detail =
            entry.directory ? FileTypeName(entry.name, true)
                            : FormatBytes(entry.size) + L"  \u00B7  " +
                                  FormatFileTime(entry.modified);
        RECT detailRect{nameLeft, nameRect.bottom, cell.right - pad,
                        nameRect.bottom + detailLine};
        SetTextColor(dc, theme::kTextDim);
        DrawTextW(dc, detail.c_str(), -1, &detailRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        const int iconX = cell.left + (static_cast<int>(cell.right - cell.left) - icon) / 2;
        const int iconY = cell.top + pad;
        HBITMAP preview = previewFor(entry, icon);
        if (preview) {
            drawPreview(dc, preview, iconX, iconY, icon);
        } else {
            drawIcon(dc, entry, iconX, iconY);
        }
        const int nameTop = iconY + icon + theme::Scale(3, dpi_);
        RECT nameRect{cell.left + pad, nameTop, cell.right - pad, nameTop + line};
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, entry.name.c_str(), -1, &nameRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const std::wstring detail =
            entry.directory ? L"Folder" : FormatBytes(entry.size);
        RECT detailRect{cell.left + pad, nameRect.bottom, cell.right - pad,
                        nameRect.bottom + detailLine};
        SetTextColor(dc, theme::kTextDim);
        DrawTextW(dc, detail.c_str(), -1, &detailRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

void FileList::notify(UINT message, WPARAM wParam, LPARAM lParam) {
    if (parent_) {
        SendMessageW(parent_, message, wParam, lParam);
    }
}

LRESULT CALLBACK FileList::EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<FileList*>(refData);
    if (self && !self->editCommitting_) {
        if (message == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                self->commitInlineEdit(false);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->commitInlineEdit(true);
                return 0;
            }
        } else if (message == WM_KILLFOCUS) {
            self->commitInlineEdit(false);
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK FileList::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    FileList* self = reinterpret_cast<FileList*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<FileList*>(create->lpCreateParams);
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

LRESULT FileList::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
        updateMetrics();
        clampScroll();
        invalidate();
        return 0;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        ensureFont();
        updateMetrics();
        clampScroll();
        invalidate();
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        invalidate();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme::kText);
        SetBkColor(dc, theme::kInput);
        return reinterpret_cast<LRESULT>(editBrush_);
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            if (resizingColumns_) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (detailsMode()) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(hwnd_, &point);
                if (columnBoundaryAt(point.x, point.y) >= 0) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (scrollbarDrag_) {
            updateScrollFromThumb(y);
            return 0;
        }
        if (resizingColumns_) {
            const int available = std::max(1, resizeAvailable_);
            int widths[4] = {resizeStartWidth_[0], resizeStartWidth_[1],
                             resizeStartWidth_[2], resizeStartWidth_[3]};
            const int column = resizeColumn_;
            if (column >= 0 && column < 3) {
                const int delta = x - resizeStartX_;
                int left = widths[column] + delta;
                int right = widths[column + 1] - delta;
                const int minimum = theme::Scale(50, dpi_);
                if (left < minimum) {
                    right -= minimum - left;
                    left = minimum;
                }
                if (right < minimum) {
                    left -= minimum - right;
                    right = minimum;
                }
                widths[column] = std::max(1, left);
                widths[column + 1] = std::max(1, right);
                int ratios[4];
                int total = 0;
                for (int i = 0; i < 4; ++i) {
                    ratios[i] = std::max(1, widths[i] * 1000 / available);
                    total += ratios[i];
                }
                if (total > 0) {
                    setColumnRatios(ratios);
                }
            }
            return 0;
        }
        const int row = indexFromPoint(x, y);
        if (row != hoverRow_) {
            hoverRow_ = row;
            invalidate();
        }
        if (mouseDown_ && dragRow_ >= 0 && !draggingOut_) {
            if (std::abs(x - dragStart_.x) >= theme::Scale(6, dpi_) ||
                std::abs(y - dragStart_.y) >= theme::Scale(6, dpi_)) {
                startDragOut();
            }
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hoverRow_ != -1) {
            hoverRow_ = -1;
            invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int boundary = columnBoundaryAt(x, y);
        if (boundary >= 0) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            resizeAvailable_ = std::max(1, static_cast<int>(client.right) - scrollbarWidth_);
            resizeColumn_ = boundary;
            resizeStartX_ = x;
            resizeStartWidth_[0] = nameWidth_;
            resizeStartWidth_[1] = sizeWidth_;
            resizeStartWidth_[2] = dateWidth_;
            resizeStartWidth_[3] = typeWidth_;
            resizingColumns_ = true;
            SetCapture(hwnd_);
            return 0;
        }
        int column = -1;
        if (hitHeader(x, y, column)) {
            if (column == sortColumn_) {
                sortAscending_ = !sortAscending_;
            } else {
                sortColumn_ = column;
                sortAscending_ = column == 0 || column == 3;
            }
            rebuildView(true);
            notify(filelist_events::kSortChanged);
            invalidate();
            return 0;
        }
        if (pointInScrollbar(x, y)) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int contentTop = detailsMode() ? headerHeightPixels() : 0;
            const int viewHeight = std::max(1, static_cast<int>(client.bottom) - contentTop);
            const int thumbHeight =
                contentHeight_ > viewHeight
                    ? std::max(theme::Scale(20, dpi_), viewHeight * viewHeight / contentHeight_)
                    : viewHeight;
            const int maxScroll = std::max(0, contentHeight_ - viewHeight);
            const int travel = std::max(1, viewHeight - thumbHeight);
            const int thumbY = contentTop + (maxScroll ? scrollY_ * travel / maxScroll : 0);
            if (y >= thumbY && y < thumbY + thumbHeight) {
                scrollbarGrab_ = y - thumbY;
            } else {
                scrollbarGrab_ = thumbHeight / 2;
                updateScrollFromThumb(y);
            }
            scrollbarDrag_ = true;
            SetCapture(hwnd_);
            return 0;
        }
        const int row = indexFromPoint(x, y);
        if (row >= 0) {
            const int all = view_[row];
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl) {
                if (!selected_.erase(all)) {
                    selected_.insert(all);
                }
                focusRow_ = row;
                anchorRow_ = row;
                notify(filelist_events::kSelectionChanged);
            } else if (shift) {
                setFocusedRow(row, true, false);
            } else {
                selected_.clear();
                selected_.insert(all);
                focusRow_ = row;
                anchorRow_ = row;
                notify(filelist_events::kSelectionChanged);
            }
            mouseDown_ = true;
            dragRow_ = row;
            dragStart_ = POINT{x, y};
            ensureRowVisible(row);
            SetCapture(hwnd_);
            invalidate();
        } else {
            selected_.clear();
            focusRow_ = -1;
            anchorRow_ = -1;
            notify(filelist_events::kSelectionChanged);
            invalidate();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        const int row = indexFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (row >= 0) {
            mouseDown_ = false;
            if (GetCapture() == hwnd_) {
                ReleaseCapture();
            }
            notify(filelist_events::kActivate, static_cast<WPARAM>(view_[row]));
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (resizingColumns_) {
            resizingColumns_ = false;
            resizeColumn_ = -1;
            if (GetCapture() == hwnd_) {
                ReleaseCapture();
            }
            notify(filelist_events::kColumnsChanged);
            invalidate();
            return 0;
        }
        mouseDown_ = false;
        draggingOut_ = false;
        dragRow_ = -1;
        scrollbarDrag_ = false;
        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED: {
        const bool wasResizing = resizingColumns_;
        mouseDown_ = false;
        scrollbarDrag_ = false;
        dragRow_ = -1;
        resizingColumns_ = false;
        resizeColumn_ = -1;
        if (wasResizing) {
            notify(filelist_events::kColumnsChanged);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        int row = indexFromPoint(x, y);
        if (row >= 0) {
            const int all = view_[row];
            if (!selected_.count(all)) {
                selected_.clear();
                selected_.insert(all);
                focusRow_ = row;
                anchorRow_ = row;
                notify(filelist_events::kSelectionChanged);
                invalidate();
            }
        }
        POINT screen{x, y};
        ClientToScreen(hwnd_, &screen);
        notify(filelist_events::kContext, static_cast<WPARAM>(row),
               static_cast<LPARAM>(MAKELPARAM(screen.x, screen.y)));
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const WORD keys = GET_KEYSTATE_WPARAM(wParam);
        if ((keys & MK_CONTROL) != 0) {
            if ((keys & MK_SHIFT) != 0) {
                setFont(fontFace_, fontSize_ + (delta > 0 ? 1 : -1));
                notify(filelist_events::kFontSizeChanged);
            } else {
                const int next = std::max(0, std::min(ViewModeCount() - 1,
                                                       static_cast<int>(viewMode_) +
                                                           (delta > 0 ? 1 : -1)));
                setViewMode(static_cast<ViewMode>(next));
                notify(filelist_events::kViewModeChanged);
            }
            return 0;
        }
        const int step = detailsMode() ? rowHeightPixels() : cellHeightPixels();
        scrollY_ -= (delta / WHEEL_DELTA) * step * 3;
        updateMetrics();
        clampScroll();
        invalidate();
        return 0;
    }
    case WM_KEYDOWN: {
        if (view_.empty()) {
            if (wParam == VK_BACK) {
                notify(filelist_events::kUpRequest);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                notify(filelist_events::kEscape);
                return 0;
            }
            break;
        }
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl) {
            UINT command = 0;
            if (wParam == 'A') command = kCmdSelectAll;
            else if (wParam == 'C') command = kCmdCopy;
            else if (wParam == 'X') command = kCmdCut;
            else if (wParam == 'V') command = kCmdPaste;
            else if (wParam == 'E') command = kCmdExtract;
            if (command) {
                SendMessageW(parent_, WM_COMMAND, command, 0);
                return 0;
            }
        }
        if (wParam == VK_DELETE) {
            SendMessageW(parent_, WM_COMMAND, kCmdDelete, 0);
            return 0;
        }
        if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000) != 0) {
            SendMessageW(parent_, WM_COMMAND, kCmdProperties, 0);
            return 0;
        }
        if (wParam == VK_F5) {
            SendMessageW(parent_, WM_COMMAND, kCmdRefresh, 0);
            return 0;
        }
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int contentTop = detailsMode() ? headerHeightPixels() : 0;
        const int step = std::max(1, detailsMode() ? rowHeightPixels() : cellHeightPixels());
        const int visibleRows = std::max(1, (static_cast<int>(client.bottom) - contentTop) / step);
        const int cols = detailsMode() ? 1 : std::max(1, columns());
        const int page = std::max(1, visibleRows * cols);
        int row = focusRow_;
        switch (wParam) {
        case VK_UP:
            row = row < 0 ? 0 : std::max(0, row - cols);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_DOWN:
            row = row < 0 ? 0
                          : std::min(static_cast<int>(view_.size()) - 1, row + cols);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_LEFT:
            row = row < 0 ? 0 : std::max(0, row - 1);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_RIGHT:
            row = row < 0 ? 0
                          : std::min(static_cast<int>(view_.size()) - 1, row + 1);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_PRIOR:
            row = row < 0 ? 0 : std::max(0, row - page);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_NEXT:
            row = row < 0 ? 0
                          : std::min(static_cast<int>(view_.size()) - 1, row + page);
            setFocusedRow(row, shift, false);
            return 0;
        case VK_HOME:
            setFocusedRow(0, shift, false);
            return 0;
        case VK_END:
            setFocusedRow(static_cast<int>(view_.size()) - 1, shift, false);
            return 0;
        case VK_RETURN:
            if (focusRow_ >= 0) {
                notify(filelist_events::kActivate, static_cast<WPARAM>(view_[focusRow_]));
            }
            return 0;
        case VK_BACK:
            notify(filelist_events::kUpRequest);
            return 0;
        case VK_F2:
            if (focusRow_ >= 0 && selected_.size() == 1) {
                beginRename(view_[focusRow_]);
            }
            return 0;
        case VK_ESCAPE:
            notify(filelist_events::kEscape);
            return 0;
        case VK_SPACE:
            if (!ctrl) {
                break;
            }
            break;
        default:
            break;
        }
        break;
    }
    case WM_CHAR: {
        const wchar_t c = static_cast<wchar_t>(wParam);
        if (c >= 32 && (GetKeyState(VK_CONTROL) & 0x8000) == 0) {
            const ULONGLONG now = GetTickCount64();
            if (now - typeTick_ > 800) {
                typeBuffer_.clear();
            }
            typeTick_ = now;
            typeBuffer_.push_back(c);
            const std::wstring lower = ToLower(typeBuffer_);
            int start = focusRow_ >= 0 ? focusRow_ : 0;
            for (size_t pass = 0; pass < 2; ++pass) {
                const int begin = pass == 0 ? start : 0;
                const int end = pass == 0 ? static_cast<int>(view_.size()) : start;
                for (int i = begin; i < end; ++i) {
                    if (ToLower(entries_[view_[i]].name).rfind(lower, 0) == 0) {
                        setFocusedRow(i, false, false);
                        return 0;
                    }
                }
            }
        }
        break;
    }
    case WM_NCDESTROY:
        if (dropTarget_) {
            RevokeDragDrop(hwnd_);
            dropTarget_->Release();
            dropTarget_ = nullptr;
        }
        hwnd_ = nullptr;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}





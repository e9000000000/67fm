# 67fm architecture

## Shape

```
main
 └─ App (main window, custom chrome)
     ├─ TabBar        full-width title row: category burger, tabs, close, new tab
     ├─ PathBar       breadcrumbs, address edit, transient message
     ├─ FileList      virtualized details view, selection, rename, OLE drop
     ├─ SearchBar     bottom filter bar, shown only by Ctrl+F
     └─ MenuBar       hidden owner-drawn popup menu host
```

The main window removes the native caption (`WS_CAPTION`), handles
`WM_NCCALCSIZE`, and returns `HTCAPTION` from the tab row so the custom title bar
drags. Child controls return `HTTRANSPARENT` on empty tab-bar space. There is no
status bar; transient messages are drawn at the right end of the breadcrumb row.

## Tabs and navigation

Tab width is `available / count`, so tabs always fill the caption row. The tab
strip keeps a fixed burger button on the left and a fixed plus button on the
right; only the tabs scroll when there are too many.

There are no back/forward/up buttons. History and parent navigation are keyboard
only: `Alt+Left`, `Alt+Right`, `Alt+Up` (and `Backspace` from the list). The
breadcrumb row renders the location as one path string with backslash separators;
the last segment is highlighted, and each segment remains a click target. It
reserves its right edge for a transient message when an action reports one.
`Ctrl+F` shows the bottom `SearchBar`; `Esc` closes it and clears the filter.

## Search

The bottom bar has an edit field and two right-side controls: a scope toggle and
a close button. Scope is either **This folder** (instant substring filter over the
already-loaded entries) or **Subfolders** (recursive name search). The choice is
stored in settings.

Filesystem recursion runs on a worker:

- `fs::SearchDirectory` walks with an explicit directory stack, does not follow
  reparse-point directories, skips hidden/system entries unless enabled, and
  stops at 5,000 results;
- a generation counter and a shared cancel flag discard stale answers;
- results keep the normal tab listing intact; each result's `name` is the path
  relative to the search root and `path` is the full filesystem path;
- the UI swaps the list to results and restores the normal listing when the
  filter closes or becomes empty.

ZIP recursion uses `ZipArchive::Search` synchronously over the in-memory central
directory, with the same relative-name/full-virtual-path scheme. Opening a
result uses the normal entry path, so folders navigate and files open/extract as
usual.

## View modes and font size

Zoom changes the view mode rather than scaling the details view:

0. Details
1. One column
2. Two columns
3. Small icons
4. Medium icons
5. Large icons
6. Extra large icons

`Ctrl+wheel`, `Ctrl++`, `Ctrl+-`, and `Ctrl+0` move through those modes (reset
returns to Details). Details keeps the sortable header; the other modes render a
virtualized tile grid, with one or two tiles per row in the column modes and a
computed number of icon tiles per row. Every view mode requests shell thumbnails
(images, documents, videos where available) and falls back to the shell file-type
icon, so every file has a visual.

Font face and point size are two settings, both edited through the native
`ChooseFont` dialog: **List Font** (file names/details) and **Interface Font**
(tabs, menus, path bar, search bar). They are applied independently and persisted
in `settings.json`.

## Location model

`Location` is either a filesystem folder or a folder inside a ZIP:

```cpp
struct Location {
    bool archive;
    std::wstring fsPath;   // folder, or path to the .zip
    std::wstring inner;    // backslash-separated path inside the archive
};
```

The serialized form is the string a user can type in the address bar:

- `C:\Users\me\Downloads`
- `C:\Users\me\Downloads\archive.zip`
- `C:\Users\me\Downloads\archive.zip\folder\subfolder`

`ParseLocation` first checks for a real directory, then for a real `.zip` file.
Otherwise it scans for the first `.zip` prefix that exists as a file and treats
the rest as an inner path. This avoids mistaking a real folder named
`something.zip` for an archive. `Location::Up` walks inner folders first, then
leaves the archive to its parent folder.

Tabs keep a `history` vector and index; back/forward move through it without
reloading the archive directory listing from disk.

## File listing

`fs.cpp` enumerates with `FindFirstFileExW(..., FIND_FIRST_EX_LARGE_FETCH)`,
skipping `.`/`..` and hidden/system entries unless enabled. Each `FileEntry`
carries name, full path, inner path, size, `FILETIME`, attributes, and flags for
directory and ZIP.

`FileList` keeps:

- `entries_`: the full entry vector for the active folder;
- `view_`: indices into `entries_`, filtered and sorted;
- `selected_`: indices into `entries_`;
- scroll position, focus/anchor rows, hover, drop target, and filter.

Painting is virtualized: only rows intersecting the client area are drawn. Tabs
store only the first visible row, not a view object. Sorting is folder-first,
then by the selected column, then by name. Selection is preserved across sort and
refresh by entry path (or name when an entry has no path).

The list has no native scrollbar; it draws and hit-tests a dark thumb and uses
the mouse wheel. The inline rename `EDIT` is a child of `FileList`, positioned
over the name cell, and commits on Enter/focus loss. In details mode, the header
column boundaries are drag targets; widths are stored as normalized ratios in
`settings.json`, so they survive window resizes and DPI changes.

## File operations

New Folder (`Ctrl+Shift+N`) creates `New folder` (or the next free
`New folder (2)`) and starts inline rename. New File (`Ctrl+Alt+N`) creates an
empty `New file.txt` the same way.

Paste uses `CopyOrMovePaths` in `util.cpp`. A normal multi-item copy is a single
`SHFileOperationW` call. If any destination name already exists, the copy is done
one item at a time so each collision gets a numbered name from
`UniqueNumberedPath`: `name_2`, `name_3`, ... (`file_2.txt` for files). Copying an
item into its own folder therefore yields `folder_2` instead of a replace prompt.
Moves are left to the shell's normal conflict handling, and a move into the
source folder is skipped.

## ZIP handling

`third_party/miniz` is vendored unmodified (MIT). `ZipArchive` owns the `FILE*`
and an `mz_zip_archive` opened with `mz_zip_reader_init_cfile` (so Unicode paths
work through `_wfopen`).

At open time it reads the central directory once and stores:

- normalized UTF-8 entry name;
- entry index, uncompressed size, DOS time;
- directory/supported/encrypted flags.

Listing a virtual folder walks entries whose names start with the normalized
prefix and collapses the next path component into folders. Extraction uses
`mz_zip_reader_extract_to_callback` with a Win32 `WriteFile` callback (Unicode
destination paths), sets the modified time, and rejects `..` components.

Opening a file inside a ZIP extracts it to
`%TEMP%\67fm-open-<tick>\<name>` and hands it to the shell. Copying archive entries
extracts to `%TEMP%\67fm-copy-<tick>` and places those real paths on the clipboard.
Extract Selected/All uses an `IFileDialog` folder picker and writes into a
user-chosen folder.

## Drag and drop

`FileList` registers an OLE `IDropTarget`. On `Drop` it reads, in order:

1. `CF_HDROP` — ordinary Explorer files.
2. `FileGroupDescriptorW` + `FileContents` — browser/Office virtual files.
   Virtual files are materialized under `%TEMP%\67fm-drop-*`, passed to `App`, and
   removed after the synchronous copy callback returns.
3. `HTML Format` — Chromium/Edge image or link drags.
4. `text/uri-list` / `CF_UNICODETEXT` — URL fallbacks.

`App` resolves the drop target row: a folder row means “drop into that folder”,
otherwise the current folder. Archive locations reject writes. Files are passed
to `SHFileOperationW` (copy or move). URLs go to the downloader.

Drag-out is handled by `dragout.cpp`. It builds a small custom `IDataObject`
(replacing the earlier `SHCreateDataObject` helper) that exposes:

1. `CF_HDROP` — the file list, as `DROPFILES` + wide, double-null-terminated paths.
2. `Preferred DropEffect` — `DROPEFFECT_COPY`.
3. `CF_UNICODETEXT` — fallback for text-oriented targets.

`DoDragDrop` runs with a custom `IDropSource` (Escape cancels, releasing the
button drops). This is what makes drag-out work with non-shell targets such as
browsers (file uploads) and Telegram, which expect a plain `CF_HDROP` payload.
Archive entries are not draggable yet.

## Downloads

`download.cpp` uses WinHTTP:

- `WinHttpCrackUrl` for host/path/port/HTTPS;
- GET with 10 s connect, 20 s send, 30 s receive timeouts;
- status validation (2xx only);
- streaming read into a temp file in the destination folder;
- name priority: caller hint, `Content-Disposition`, final redirected URL,
  original URL, content-type extension;
- `SanitizeFileName` + `UniquePath`, then atomic rename from the temp file.

`App` starts one `std::thread` per download. The worker allocates a
`DownloadResult`, posts it to the UI (`WM_APP + 0x300`), and the UI refreshes the
active folder. The window joins workers on destroy, before the object dies.

HTML parsing is deliberately small: find an `<img src>` first, then `href`, read
the CF_HTML `SourceURL:` header as a base, strip a few HTML entities, and resolve
relative URLs with `UrlCombineW`.

## Default file manager

File → Set as Default File Manager writes only to `HKCU\Software\Classes`:

- `Directory\shell` default verb → `open`
- `Directory\shell\open\command` → `"<67fm.exe>" "%1"`
- `Drive\shell` and `Drive\shell\open\command` → the same command

The previous values are copied to `%APPDATA%\67fm\shell-backup.json` before the
first change. Restore Windows Explorer Default replays that backup (or deletes the
keys 67fm created) and broadcasts `SHCNE_ASSOCCHANGED`. No admin rights are
required and HKLM is never touched. Windows special shell folders still belong to
Explorer.

## Terminal handoff

`Ctrl+Alt+T` (File / context menus: Open in Terminal) picks the target folder:
the selected directory, the parent of a selected file, or the current folder. For
archive tabs it uses the archive's containing folder.

`OpenTerminalAt` tries, in order:

1. `67t.exe` next to `67fm.exe`, then known sibling build paths; launched as
   `67t.exe --cwd "<folder>"`;
2. Windows Terminal `wt.exe -d "<folder>"`;
3. `cmd.exe /k cd /d "<folder>"` in a new console.

## Sessions and settings

`session.json` (in `%APPDATA%\67fm`, or `<exe>\state` when a portable
`67fm.ini` exists) stores:

- each tab's serialized location, first visible row, sort column, sort direction;
- active tab index;
- window rectangle, maximized state.

`settings.json` stores hidden-file visibility, the global sort preference, and
recent folders. Both are written through the temp-file + `MoveFileEx` helper.
Session saves are debounced with a 600 ms timer after navigation, tab changes, or
window movement. A corrupt JSON file is renamed `.bad` and replaced.

## Single instance

The first process owns a named mutex and a normal hidden top-level window. Later
launches encode their paths as NUL-separated UTF-16 in `WM_COPYDATA` and exit.
`--new-window` skips the mutex and starts a separate window. Cross-integrity-level
forwarding is documented as unsupported.

## Threading

- UI thread: all Win32, GDI, list state, tab state, session access.
- Download workers: WinHTTP + file writes only; results cross via
  `PostMessage`.
- No reader/painter races and no per-keystroke IPC.

## Palette

All colors and metrics live in `src/theme.h`. The active tab and list share
`RGB(9,9,9)`; panels are `15,15,15`, borders `36,36,36`, text `216,216,216`,
dim `146,146,146`, and the accent is `43,107,154`. Folder/archive icons use the
muted folder/blue colors defined there.

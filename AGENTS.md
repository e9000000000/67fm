# AGENTS.md — 67fm

67fm is a native Windows file manager: one small executable, dark UI, tabs,
session restore, ZIP browsing, and drag-and-drop (including browser downloads).
It is **not** a web app. There is no browser, no HTTP server, no WebView, no
Electron/Tauri, and no telemetry. Network access exists only when the user drops
a URL or image from a browser and explicitly asks for that download.

The look and interaction model follow the sibling projects `../67ed` and
`../67t`: compact custom title/tab row, burger menu, one blue accent, one panel
with tabs, small native Win32 controls, no installer, no menu bar, no sidebar.

## Current state

M0–M4 are complete. The app builds as one x64 executable (~610 KiB) and has:

- custom dark title/tab chrome with a category burger menu, full-width tabs,
  drag reorder, close buttons, and a fixed new-tab button;
- a single file panel (no side panel, no split panes) with breadcrumbs, an
  editable address bar (Ctrl+L), Alt+Left/Right/Up navigation, and a bottom
  filter bar that opens with Ctrl+F; the filter can stay in the current folder or
  include all subfolders (recursive name search);
- zoomable view modes: Ctrl+wheel, Ctrl++/Ctrl-, or Ctrl+0 moves from Details to
  One column, Two columns, Small icons, Medium icons, Large icons, and Extra
  large icons; separate List Font and Interface Font dialogs in the View menu;
- no status bar: item counts and free-space chrome are gone; transient action
  messages appear at the right side of the breadcrumb row;
- a virtualized, custom-drawn details list (Name, Size, Date modified, Type)
  with folder-first sorting, column sorting, draggable column separators,
  keyboard navigation, type-ahead, multi-select, inline rename, and a dark
  scrollbar;
- filesystem browsing, hidden-file toggle, new folder, new file, delete to Recycle
  Bin, cut/copy/paste (colliding pastes become `name_2`, `name_3`, ...), copy
  path, properties, Open With, Show in Explorer, and Open in Terminal (67t next
  to the EXE/sibling build, then Windows Terminal, then cmd);
- optional per-user default folder handler: File → Set as Default File Manager
  (with a restore-to-Explorer action and a saved backup of the prior values);
- ZIP archives opened as folders, nested archive navigation, Ctrl+E extraction
  of selected entries or the whole current folder (stored + deflated entries);
- OLE drag and drop: Explorer files can be dropped in, files can be dragged out,
  and browser image/link drops are downloaded into the current folder over
  HTTP(S) using WinHTTP;
- session restore (tabs, scroll positions, sort, window placement), settings,
  single-instance handoff, and a headless self-test.

Known gaps: drive list for “This PC” (it opens the Windows drive root), archive
creation, batch rename, preview/inspector, and archive drag-out.

## Build

Requirements: Windows 10/11 x64, Visual Studio Build Tools (MSVC 19.3x+),
Windows SDK 10.0.26100+.

```powershell
.\build.bat            # release, build\67fm.exe
.\build.bat debug      # debug, build\67fm-debug.exe
.\build.bat clean      # delete build/
```

`build.bat` locates Visual Studio with `vswhere`, imports `vcvars64.bat`,
compiles the vendored miniz C sources, and links one EXE with the static CRT
(`/MT`). Do not add a runtime DLL, installer, package manager, or third-party
runtime dependency.

## Run

```powershell
build\67fm.exe                         # restore session or open the home folder
build\67fm.exe C:\src                  # open a folder in a new tab
build\67fm.exe C:\src\archive.zip      # open an archive
build\67fm.exe C:\src\file.txt         # open the parent folder, select the file
build\67fm.exe --new-window [path]     # force a separate window
build\67fm.exe --selftest --log out.txt
```

A second launch forwards its paths over `WM_COPYDATA` to the running window and
opens them as tabs, unless `--new-window` is given.

## Layout

```
src/
  main.cpp          wWinMain, single-instance gate, CLI dispatch
  cli.*             command-line parsing
  commands.h        command IDs shared by menus, accelerators, app
  app.*             main window, tabs, commands, layout, menus, session wiring
  tabbar.*          custom dark tab strip/burger/new-tab button
  pathbar.*         breadcrumbs, address edit, transient message
  searchbar.*       bottom filter bar (Ctrl+F) and scope toggle
  filelist.*        virtualized details view, selection, rename, OLE drop/drag
  dragout.*         custom CF_HDROP IDataObject/IDropSource for drag-out
  menubar.*         owner-drawn dark popup menu host
  theme.h           palette and UI metrics
  location.*        filesystem vs archive location model and path parsing
  fs.*              directory enumeration and entry sorting
  zip.*             ZIP listing/extraction on top of miniz
  download.*        WinHTTP download, HTML/URL extraction, filename derivation
  session.*         session.json load/save
  settings.*        settings.json load/save
  shellintegration.* per-user default folder-handler registration/restore
  util.*            UTF/path/format/shell/clipboard/dark-mode helpers
  json.*            small JSON value/parser/serializer
  ipc.*             single-instance path payload encode/decode
  windowrect.*      monitor work-area clamping for restored windows
  crashlog.*        unhandled-exception log
  headless.*        --selftest, hidden host only
resources/
  app.rc            icon, manifest, version
  app.manifest      per-monitor DPI, common controls v6
third_party/
  miniz/            vendored miniz (MIT), unmodified; ZIP read/extract
docs/
  PLAN.md           milestones and acceptance criteria
  ARCHITECTURE.md   detailed design
  TESTING.md        headless checks
tests/
  run-selftest.ps1  headless self-test runner
.github/
  workflows/build.yml  CI build, artifacts, and releases
```

## Non-negotiable rules

1. **Single EXE.** Static CRT (`/MT`), static miniz, no DLLs, no installer.
2. **Native only.** Win32 + GDI. No webview, no managed runtime, no Electron/Tauri.
3. **One panel with tabs.** Do not add a File Pilot-style side panel, tree, or
   split pane. Navigation is tabs + breadcrumbs + the single list.
4. **UI thread owns the UI.** Downloads run on workers; only the UI thread touches
   window state, controls, or the file list.
5. **No hidden network activity.** Network is used only for explicit browser
   drops (HTTP/HTTPS GET). No telemetry, no auto-update, no background fetch.
6. **ZIP is read-only.** Never write archive contents in place. Extraction writes
   to a user-chosen folder; opening a file inside an archive extracts to temp.
7. **No path traversal.** Reject `..` components inside archives and sanitize
   every browser-derived filename.
8. **Per-user shell changes only.** Default-file-manager registration writes
   `HKCU\Software\Classes`, never HKLM; back up prior values before the first
   change and provide Restore Windows Explorer Default.
9. **Unicode everywhere.** Build with `UNICODE`/`_UNICODE`. Paths and UI text are
   `std::wstring`; ZIP names are UTF-8 through miniz.
10. **Atomic state writes.** Session/settings are written to a temp file then
    renamed.
11. **Never lose files.** Deletes go to the Recycle Bin; overwrites go through
    the shell file-operation prompt.

## Performance budgets

- EXE size: under 3 MB.
- Window visible: under 100 ms from process start.
- Open a 10,000-entry folder: under 150 ms.
- Scrolling/painting: only visible rows are drawn; no per-row allocation.
- Open a ZIP central directory: under 200 ms for a typical archive.
- Idle CPU at rest: effectively 0%.

Do not regress these without updating the plan and recording numbers.

## Conventions

- C++20, `/W4`, RAII, `std::wstring` for UI and paths.
- Colors and metrics live in `src/theme.h`; never hard-code random RGB values in
  widgets.
- Prefer small focused files over a god object. `App` coordinates; it does not
  implement list layout, ZIP parsing, or HTTP.
- Win32 messages are handled in `App::handleMessage`; custom controls own their
  own painting and hit testing.
- Error messages include the failing path and the Win32 error text.
- Shell operations use `SHFileOperationW` with `FOF_ALLOWUNDO` where appropriate.

## Testing

All automated testing is headless. `--selftest` creates no visible window.

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
```

`--selftest` covers UTF-8 round trips, formatting/sanitizing, URL/HTML extraction,
directory listing and hidden files, recursive search/limits, sorting, unique
names, filesystem and archive location parsing, archive up/join/display, session
and settings (including view mode, font face/size, and search-scope) round trips,
single-instance payloads, drag-out `CF_HDROP` data, ZIP
creation/listing/extraction/extract-all/search, and invalid paths. Expect
`summary passed=41 failed=0`.

Manual smoke (only when necessary, never in automated runs): tabs, breadcrumbs,
Ctrl+F bottom filter and scope toggle, Ctrl+wheel zoom, category burger, rename,
recycle bin, ZIP extraction, Open in Terminal, Explorer drag-in/out, browser image
drop, DPI 125/150/200%, second launch.

## Known pitfalls

- `WM_COPYDATA` is blocked across integrity levels; do not silently start a second
  instance. Document the limitation.
- Browser drag payloads differ: Chromium sends `HTML Format`, Firefox may send
  URI lists or virtual file descriptors. The drop target handles `CF_HDROP`,
  `FileGroupDescriptorW`/`FileContents`, `HTML Format`, `text/uri-list`, and
  `CF_UNICODETEXT`.
- Drag-out uses a custom `CF_HDROP` data object (`dragout.cpp`); do not go back to
  `SHCreateDataObject` with absolute PIDLs, which non-shell targets (browsers,
  Telegram) ignore. UIPI still blocks drops into elevated windows and from an
  elevated 67fm into non-elevated targets.
- Browser image URLs can require cookies/referers; a failed download is reported
  rather than silently ignored.
- Default-file-manager registration overrides the per-user `Directory\shell` and
  `Drive\shell` open command. Windows special shell folders still belong to
  Explorer. The previous values are saved in
  `%APPDATA%\67fm\shell-backup.json`; Restore Windows Explorer Default must be used
  before uninstalling 67fm.
- Recursive filesystem search runs on a worker; stale results are discarded by a
  generation counter and canceled jobs stop at the next directory. Symlink/
  junction directories are not followed.
- ZIP recursive search runs synchronously over the in-memory central directory.
- Icon previews use `IShellItemImageFactory` (thumbnail first, file icon
  fallback), are cached per path and requested size, and the cache is cleared on
  folder, view-mode, or font changes.
- ZIP entry names are case-sensitive; navigation compares case-insensitively to
  match Windows expectations, extraction uses the exact entry name.
- `mz_zip_reader_init_cfile` does not close the `FILE*`; `ZipArchive::Close` owns it.
- The compressed folder shell namespace is not used; ZIP handling is in-process.
- Extracting a file to open it leaves it under `%TEMP%\67fm-open-*`; it is safe to
  delete later.
- Custom chrome needs `WM_NCCALCSIZE` plus `WM_NCHITTEST`; child controls return
  `HTTRANSPARENT`/`HTCAPTION` where appropriate.

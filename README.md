# 67fm

A native Windows file manager built around one panel with tabs. Small, dark, and
fast: no browser engine, no installer, no telemetry, no background indexer.

## Features

- **Tabs, not panels.** Each tab is a folder or a folder inside a ZIP archive.
  Tabs share the full caption row. Drag tabs to reorder, middle-click to close,
  and restore the whole session on the next launch.
- **Breadcrumbs + address bar.** Click any path segment or press `Ctrl+L` to type
  a path. Use `Alt+Left`/`Alt+Right` for history and `Alt+Up` for the parent
  folder; there are no navigation buttons.
- **Fast details list.** Name, size, modified date, and type; folders first;
  click headers to sort; drag column separators to resize; type-ahead selection;
  inline rename (`F2`). Column widths are remembered.
- Zoomable view modes: `Ctrl+wheel`, `Ctrl++`, `Ctrl+-`, or `Ctrl+0` moves
  through Details → One column → Two columns → Small icons → Medium icons →
  Large icons → Extra large icons. File previews/thumbnails appear in every view
  mode. **List Font** and **Interface Font** are separate dialogs in the View
  menu; the UI font covers tabs, menus, path bar, and search bar.
- **File operations.** Cut/copy/paste, new folder, delete to the Recycle Bin,
  properties, Open With, Show in Explorer, Open in Terminal, copy path.
- **Optional default file manager.** File → Set as Default File Manager makes
  `start .` and other folder opens use 67fm for this Windows user; Restore Windows
  Explorer Default undoes it. Special shell folders still belong to Explorer.
- **ZIP archives as folders.** Double-click a `.zip` to browse it, navigate
  nested folders, and extract a selection or the whole current folder (`Ctrl+E`).
- **Drag and drop.** Drop files from Explorer to copy them into the current
  folder, drag files out to Explorer, and drop an image or link from a browser to
  download it into the current folder.
- **Bottom filter.** Press `Ctrl+F` to open a filter bar at the bottom; `Esc`
  closes it and clears the filter. The right-side button switches between
  filtering this folder and searching all subfolders recursively.

## Build

Requires Windows 10/11 x64 with Visual Studio Build Tools (MSVC) and the Windows
SDK.

```powershell
.\build.bat
build\67fm.exe
```

The result is one executable with the static CRT. No DLLs, installer, or runtime
dependencies.

## Test

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
```

The self-test is headless (`build\67fm.exe --selftest`).

## Keys

| Key | Action |
| --- | --- |
| `Ctrl+T` / `Ctrl+W` | New tab / close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `Alt+Up`, `Backspace` | Parent folder |
| `Ctrl+L` | Edit address |
| `Ctrl+F` | Open bottom filter |
| `Ctrl+wheel`, `Ctrl++`, `Ctrl+-`, `Ctrl+0` | Change view mode / reset to Details |
| `Ctrl+Shift+F` | List font dialog |
| `Ctrl+Shift+U` | Interface font dialog |
| `Ctrl+G` | Open Go menu |
| `Ctrl+Alt+T` | Open current folder in terminal |
| `F5` | Refresh |
| `Ctrl+Shift+N` | New folder |
| `Ctrl+E` | Extract from ZIP |
| `F2` | Rename |
| `Del` | Delete to Recycle Bin |
| `Alt+Enter` | Properties |
| `Ctrl+A`, `Ctrl+C/X/V` | Select all, cut/copy/paste |

## Notes

- Network access is only used for a download you start by dropping a URL or image
  from a browser.
- Files inside a ZIP are read-only; extract them to edit or open them normally.
- “This PC” currently opens the Windows drive root rather than a drive list.

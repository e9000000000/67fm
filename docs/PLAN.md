# 67fm plan

## Product

A native Windows file manager with one panel and tabs. The interaction model
follows 67ed/67t: custom dark chrome, compact tab row, burger menu, keyboard-first
navigation. FilePilot is a feature reference only; its side panel/layout is
deliberately not copied.

## Milestones

### M0 — scaffold (done)

- Single EXE, static CRT, custom dark chrome, DPI awareness, crash log.
- Headless self-test entry point.

### M1 — filesystem browser (done)

- One custom-drawn details list with Name/Size/Date/Type, sorting, and draggable column widths.
- Folder navigation via double-click, Enter, and Alt+Left/Right/Up hotkeys.
- Breadcrumbs, editable address bar, and a bottom filter bar shown by Ctrl+F.
- Recursive subfolder search toggled from the filter bar.
- View modes via zoom: Details, One column, Two columns, Small/Medium/Large/Extra large icons.
- Shell thumbnails/previews for files in every view mode.
- Separate List Font and Interface Font dialogs (face + size).
- Sorting, hidden files, multi-selection, type-ahead.
- Open, Open With, properties, Show in Explorer, copy path.
- Open current/selected folder in a terminal (`Ctrl+Alt+T`).
- Per-user Set as Default File Manager / Restore Windows Explorer Default.
- New folder, inline rename, recycle-bin delete, cut/copy/paste.
- Drag files in from Explorer and out to Explorer.

### M2 — tabs and sessions (done)

- Custom tab strip with reorder/close/new; tabs fill the full caption row.
- One tab per folder/archive; restore tabs, scroll positions, sort, active tab,
  and window placement.
- Single-instance handoff with path forwarding.

### M3 — ZIP archives (done)

- Open `.zip` as a folder, nested navigation, breadcrumbs show archive segments.
- Central-directory reading, stored and deflated extraction, extract-all.
- Encrypted/unsupported entries are reported, not silently skipped.
- Archive paths persist in sessions.

### M4 — browser drops (done)

- OLE drop target accepts Explorer files, browser `HTML Format`, URI lists,
  virtual file descriptors, and text URLs.
- WinHTTP download with Content-Disposition/final-URL/content-type filename
  derivation, unique names, and failure reporting.
- Downloads run on a worker; the UI only updates from the UI thread.

### M5 — polish/backlog

- Drive list for “This PC”.
- Batch rename.
- Preview/inspector overlay without adding a side panel.
- Archive drag-out (extract to temp and provide `CF_HDROP`).
- Optional custom `IDataObject` for drag-out to remove shell helper dependence.
- Full keyboard/mouse pass at 125/150/200% DPI.

## Non-goals

- Multi-pane/split layouts or a File Pilot-style side panel.
- Cloud sync, indexing service, background search daemon.
- Writing or modifying ZIP archives in place.
- Tabs-as-windows, plugin system, installer.

## Acceptance criteria

- One EXE under 3 MB with static CRT.
- Opening a folder with 10,000 entries stays responsive and paints only visible
  rows.
- Session restore brings back tabs and current folder exactly.
- ZIP browsing/extraction works for standard deflated archives.
- A browser image/link drop downloads a file into the current folder with a safe,
  unique name.
- No telemetry, no background network, no visible web runtime.

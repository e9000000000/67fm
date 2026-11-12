# Testing

All automated checks are headless. `--selftest` creates no window, no message
box, and no visible UI.

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
```

The runner writes `build\selftest.log` and fails if the exit code is non-zero or
the summary is not `failed=0`.

## Covered

- UTF-8 ↔ UTF-16 round trip (including astral characters).
- Byte/size formatting, filename sanitizing, URL filename derivation.
- HTML `SourceURL`, `<img src>` extraction, relative URL resolution, text URL
  extraction.
- Directory listing, hidden-file filtering, recursive search and result limits,
  folder-first comparison, unique destination names.
- Filesystem and archive-URL location parsing, `Up`, `Join`, `DisplayName`,
  `ParentPath`.
- Session and settings (including view mode, font face/size, and search-scope) JSON
  round trips.
- Single-instance path payloads, including the empty payload.
- ZIP creation through miniz, central-directory listing at root and nested
  folders, single extraction, extract-all, search, content verification, entry
  count.
- Invalid path handling.

Expected summary: `summary passed=41 failed=0`.

## Manual (only when needed)

Do not use GUI automation in automated runs. For a release smoke test:

- Open a folder, navigate with double-click and Backspace/Alt+Up.
- Open the bottom filter with Ctrl+F; close it with Esc.
- Toggle the filter between This folder and Subfolders.
- Cycle Details → One column → Two columns → icon views with Ctrl+wheel.
- Open View → List Font / Interface Font and change face/size.
- Confirm icon views show image/file previews.
- Drag details column separators and confirm widths persist after restart.
- Press Ctrl+G and confirm the Go menu opens, including Videos.
- Use Open in Terminal (Ctrl+Alt+T) on a folder.
- Set as Default File Manager, run `start .` in cmd, then restore Explorer.
- Create a folder and a file with New Folder / New File, then rename inline.
- Copy an item into its own folder and confirm `name_2`, then `name_3`.
- Create/rename/delete an item; confirm Recycle Bin behavior.
- Open a ZIP, descend into a folder, extract one file and all files.
- Drag files from Explorer in, drag selected files out to Explorer, a browser file
  input, and a Telegram chat.
- Drag an image from a browser and confirm it lands in the current folder with a
  unique, safe name.
- Restart and confirm tabs/scroll/sort/window placement restore.
- Launch a second copy with a path and confirm it opens as a tab.
- Check 125/150/200% DPI.

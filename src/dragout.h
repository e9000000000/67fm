#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct IDataObject;

// Starts an OLE drag operation that carries the given local file paths as
// CF_HDROP. Returns the resulting DROPEFFECT_* value.
//
// Uses a small purpose-built IDataObject instead of SHCreateDataObject so that
// non-shell drop targets (browsers, Telegram, Electron apps) receive a plain
// CF_HDROP payload they understand.
DWORD BeginFileDrag(const std::vector<std::wstring>& paths);

// Creates the IDataObject used by BeginFileDrag. Exposed for the headless test.
IDataObject* CreateFileDataObject(const std::vector<std::wstring>& paths);

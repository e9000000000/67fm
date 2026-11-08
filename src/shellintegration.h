#pragma once

#include <string>

// Per-user folder-handler registration. This makes HKCU\Software\Classes
// take priority over Explorer for opened directories and drives. The previous
// values are saved before the first change so the menu action can restore them.
namespace shellintegration {

struct Result {
    bool ok = false;
    bool already = false;
    std::wstring error;
};

bool IsDefaultFileManager();
bool HasBackup();
Result SetAsDefaultFileManager();
Result RestoreExplorerDefault();

}  // namespace shellintegration

#pragma once

#include <string>
#include <vector>

struct Settings {
    bool showHidden = false;
    int sortColumn = 0;
    bool sortAscending = true;
    int viewMode = 0;
    std::wstring listFontFace = L"Segoe UI";
    int listFontSize = 9;
    std::wstring uiFontFace = L"Segoe UI";
    int uiFontSize = 9;
    int columns[4] = {480, 120, 230, 170};
    bool recursiveSearch = false;
    std::vector<std::wstring> recentFolders;
};

bool LoadSettings(Settings& settings);
bool SaveSettings(const Settings& settings);
bool LoadSettingsFile(const std::wstring& path, Settings& settings);
bool SaveSettingsFile(const std::wstring& path, const Settings& settings);
void AddRecentFolder(Settings& settings, const std::wstring& path);

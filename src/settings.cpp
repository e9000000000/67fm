#include "settings.h"

#include <windows.h>

#include <algorithm>

#include "json.h"
#include "util.h"

bool LoadSettings(Settings& settings) {
    return LoadSettingsFile(DataDirectory() + L"\\settings.json", settings);
}

bool LoadSettingsFile(const std::wstring& path, Settings& settings) {
    std::string text;
    if (!ReadTextFile(path, text)) {
        return false;
    }
    Json root;
    if (!Json::parse(text, root) || !root.isObject()) {
        MoveFileExW(path.c_str(), (path + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    settings.showHidden = root["showHidden"].asBool(false);
    settings.sortColumn = static_cast<int>(root["sortColumn"].asInt(0));
    settings.sortAscending = root["sortAscending"].asBool(true);
    settings.viewMode = static_cast<int>(root["viewMode"].asInt(root["zoom"].asInt(0)));
    const std::string legacyFace = root["fontFace"].asString("Segoe UI");
    const int legacySize = static_cast<int>(root["fontSize"].asInt(9));
    settings.listFontFace = Utf8ToWide(root["listFontFace"].asString(legacyFace));
    settings.uiFontFace = Utf8ToWide(root["uiFontFace"].asString(legacyFace));
    if (settings.listFontFace.empty()) settings.listFontFace = L"Segoe UI";
    if (settings.uiFontFace.empty()) settings.uiFontFace = L"Segoe UI";
    settings.listFontSize = static_cast<int>(root["listFontSize"].asInt(legacySize));
    settings.uiFontSize = static_cast<int>(root["uiFontSize"].asInt(legacySize));
    settings.recursiveSearch = root["recursiveSearch"].asBool(false);
    const Json& columns = root["columns"];
    if (columns.isArray() && columns.size() == 4) {
        int sum = 0;
        for (int i = 0; i < 4; ++i) {
            settings.columns[i] = static_cast<int>(columns.at(i).asInt(settings.columns[i]));
            settings.columns[i] = std::max(40, std::min(900, settings.columns[i]));
            sum += settings.columns[i];
        }
        if (sum < 100) {
            settings.columns[0] = 480;
            settings.columns[1] = 120;
            settings.columns[2] = 230;
            settings.columns[3] = 170;
        }
    }
    if (settings.sortColumn < 0 || settings.sortColumn > 3) {
        settings.sortColumn = 0;
    }
    settings.viewMode = std::max(0, std::min(6, settings.viewMode));
    settings.listFontSize = std::max(6, std::min(36, settings.listFontSize));
    settings.uiFontSize = std::max(6, std::min(36, settings.uiFontSize));
    settings.recentFolders.clear();
    const Json& recent = root["recentFolders"];
    if (recent.isArray()) {
        for (size_t i = 0; i < recent.size() && settings.recentFolders.size() < 20; ++i) {
            const std::wstring item = Utf8ToWide(recent.at(i).asString());
            if (!item.empty()) {
                settings.recentFolders.push_back(item);
            }
        }
    }
    return true;
}

bool SaveSettings(const Settings& settings) {
    return SaveSettingsFile(DataDirectory() + L"\\settings.json", settings);
}

bool SaveSettingsFile(const std::wstring& path, const Settings& settings) {
    Json root = Json::MakeObject();
    root["version"] = Json(1);
    root["showHidden"] = Json(settings.showHidden);
    root["sortColumn"] = Json(settings.sortColumn);
    root["sortAscending"] = Json(settings.sortAscending);
    root["viewMode"] = Json(settings.viewMode);
    root["listFontFace"] = Json(WideToUtf8(settings.listFontFace));
    root["listFontSize"] = Json(settings.listFontSize);
    root["uiFontFace"] = Json(WideToUtf8(settings.uiFontFace));
    root["uiFontSize"] = Json(settings.uiFontSize);
    Json columns = Json::MakeArray();
    for (int width : settings.columns) {
        columns.push(Json(width));
    }
    root["columns"] = std::move(columns);
    root["recursiveSearch"] = Json(settings.recursiveSearch);
    Json recent = Json::MakeArray();
    for (const auto& folder : settings.recentFolders) {
        recent.push(Json(WideToUtf8(folder)));
    }
    root["recentFolders"] = std::move(recent);
    std::wstring error;
    return WriteTextFileAtomic(path, root.dump(2), error);
}

void AddRecentFolder(Settings& settings, const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    settings.recentFolders.erase(
        std::remove_if(settings.recentFolders.begin(), settings.recentFolders.end(),
                       [&](const std::wstring& item) { return PathsEqual(item, path); }),
        settings.recentFolders.end());
    settings.recentFolders.insert(settings.recentFolders.begin(), path);
    if (settings.recentFolders.size() > 10) {
        settings.recentFolders.resize(10);
    }
}


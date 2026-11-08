#include "session.h"

#include "json.h"
#include "util.h"

bool LoadSession(SessionState& session) {
    return LoadSessionFile(DataDirectory() + L"\\session.json", session);
}

bool LoadSessionFile(const std::wstring& path, SessionState& session) {
    session = SessionState{};
    std::string text;
    if (!ReadTextFile(path, text)) {
        return false;
    }
    Json root;
    if (!Json::parse(text, root) || !root.isObject()) {
        MoveFileExW(path.c_str(), (path + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    const Json& window = root["window"];
    if (window.isObject()) {
        session.window.left = static_cast<LONG>(window["x"].asInt(0));
        session.window.top = static_cast<LONG>(window["y"].asInt(0));
        session.window.right = session.window.left + static_cast<LONG>(window["w"].asInt(1100));
        session.window.bottom = session.window.top + static_cast<LONG>(window["h"].asInt(760));
        session.maximized = window["max"].asBool(false);
        session.hasWindow = window["valid"].asBool(true);
    }
    session.active = static_cast<int>(root["active"].asInt(-1));
    const Json& tabs = root["tabs"];
    if (tabs.isArray()) {
        for (size_t i = 0; i < tabs.size(); ++i) {
            const Json& item = tabs.at(i);
            if (!item.isObject()) {
                continue;
            }
            TabSession tab;
            tab.path = Utf8ToWide(item["path"].asString());
            tab.scrollRow = static_cast<int>(item["scrollRow"].asInt(0));
            tab.sortColumn = static_cast<int>(item["sortColumn"].asInt(0));
            tab.sortAscending = item["sortAscending"].asBool(true);
            if (tab.path.empty()) {
                continue;
            }
            if (tab.sortColumn < 0 || tab.sortColumn > 3) {
                tab.sortColumn = 0;
            }
            if (tab.scrollRow < 0) {
                tab.scrollRow = 0;
            }
            session.tabs.push_back(std::move(tab));
        }
    }
    if (session.active < 0 || session.active >= static_cast<int>(session.tabs.size())) {
        session.active = session.tabs.empty() ? -1 : 0;
    }
    return !session.tabs.empty();
}

bool SaveSession(const SessionState& session) {
    return SaveSessionFile(DataDirectory() + L"\\session.json", session);
}

bool SaveSessionFile(const std::wstring& path, const SessionState& session) {
    Json root = Json::MakeObject();
    root["version"] = Json(1);
    Json window = Json::MakeObject();
    window["x"] = Json(static_cast<long long>(session.window.left));
    window["y"] = Json(static_cast<long long>(session.window.top));
    window["w"] = Json(static_cast<long long>(session.window.right - session.window.left));
    window["h"] = Json(static_cast<long long>(session.window.bottom - session.window.top));
    window["max"] = Json(session.maximized);
    window["valid"] = Json(session.hasWindow);
    root["window"] = std::move(window);
    root["active"] = Json(session.active);

    Json tabs = Json::MakeArray();
    for (const auto& tab : session.tabs) {
        Json item = Json::MakeObject();
        item["path"] = Json(WideToUtf8(tab.path));
        item["scrollRow"] = Json(tab.scrollRow);
        item["sortColumn"] = Json(tab.sortColumn);
        item["sortAscending"] = Json(tab.sortAscending);
        tabs.push(std::move(item));
    }
    root["tabs"] = std::move(tabs);

    std::wstring error;
    return WriteTextFileAtomic(path, root.dump(2), error);
}

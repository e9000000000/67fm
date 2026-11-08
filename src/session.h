#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct TabSession {
    std::wstring path;
    int scrollRow = 0;
    int sortColumn = 0;
    bool sortAscending = true;
};

struct SessionState {
    std::vector<TabSession> tabs;
    int active = -1;
    RECT window{};
    bool hasWindow = false;
    bool maximized = false;
};

bool LoadSession(SessionState& session);
bool SaveSession(const SessionState& session);
bool LoadSessionFile(const std::wstring& path, SessionState& session);
bool SaveSessionFile(const std::wstring& path, const SessionState& session);

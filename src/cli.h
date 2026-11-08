#pragma once

#include <string>
#include <vector>

struct LaunchOptions {
    std::vector<std::wstring> paths;
    std::wstring logPath;
    bool selfTest = false;
    bool newWindow = false;
    bool help = false;
    bool version = false;
};

// Parses arguments excluding the executable path. Unknown options are ignored.
LaunchOptions ParseOptions(const std::vector<std::wstring>& args);
std::wstring UsageText();

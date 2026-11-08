#include "cli.h"

LaunchOptions ParseOptions(const std::vector<std::wstring>& args) {
    LaunchOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--selftest") {
            options.selfTest = true;
        } else if (arg == L"--new-window") {
            options.newWindow = true;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            options.help = true;
        } else if (arg == L"--version") {
            options.version = true;
        } else if (arg == L"--log" && i + 1 < args.size()) {
            options.logPath = args[++i];
        } else if (!arg.empty() && arg[0] == L'-') {
            // Unknown option.
        } else if (!arg.empty()) {
            options.paths.push_back(arg);
        }
    }
    return options;
}

std::wstring UsageText() {
    return L"67fm - native Windows file manager\n\n"
           L"Usage:\n"
           L"  67fm [folder | archive.zip | file]\n"
           L"  67fm --new-window [path]\n"
           L"  67fm --selftest [--log file]\n\n"
           L"Drop files from Explorer into the window to copy them, or drag\n"
           L"an image or link from a browser to download it into the current\n"
           L"folder. ZIP archives open as folders.";
}

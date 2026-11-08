#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "app.h"
#include "cli.h"
#include "crashlog.h"
#include "headless.h"
#include "ipc.h"
#include "util.h"

namespace {

constexpr wchar_t kMainClass[] = L"67fmMainWindow";
constexpr wchar_t kMutexName[] = L"Local\\67fm-single-instance-v1";
constexpr ULONG_PTR kCopyDataTag = 0x67F0;

void Report(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"67fm", MB_OK | MB_ICONERROR);
}

bool SendToRunningInstance(const std::vector<std::wstring>& paths) {
    HWND target = nullptr;
    for (int attempt = 0; attempt < 80; ++attempt) {
        target = FindWindowW(kMainClass, nullptr);
        if (target) {
            break;
        }
        Sleep(50);
    }
    if (!target) {
        return false;
    }
    AllowSetForegroundWindow(ASFW_ANY);
    const std::wstring payload = BuildPathPayload(paths);
    COPYDATASTRUCT copy{};
    copy.dwData = kCopyDataTag;
    copy.cbData = static_cast<DWORD>(payload.size() * sizeof(wchar_t));
    copy.lpData = const_cast<wchar_t*>(payload.data());
    return SendMessageW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy)) == TRUE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    std::vector<std::wstring> arguments;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }
    const LaunchOptions options = ParseOptions(arguments);

    if (options.help) {
        MessageBoxW(nullptr, UsageText().c_str(), L"67fm", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (options.version) {
        MessageBoxW(nullptr, L"67fm 0.1\nNative Windows file manager", L"67fm",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (options.selfTest) {
        return RunSelfTest(options.logPath);
    }

    EnableDarkModeForApp();
    if (FAILED(OleInitialize(nullptr))) {
        Report(L"Cannot initialize OLE.");
        return 1;
    }
    crashlog::Install();

    HANDLE mutex = nullptr;
    if (!options.newWindow) {
        mutex = CreateMutexW(nullptr, TRUE, kMutexName);
        if (!mutex) {
            Report(L"Cannot create the single-instance mutex.");
            OleUninitialize();
            return 1;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            const bool delivered = SendToRunningInstance(options.paths);
            CloseHandle(mutex);
            OleUninitialize();
            if (!delivered) {
                Report(L"67fm is already running, but it did not respond.\n"
                       L"If it is running elevated, start this instance elevated as well.");
            }
            return delivered ? 0 : 1;
        }
    }

    App app(instance);
    if (!app.create(options.paths)) {
        Report(L"Cannot create the 67fm window.");
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        OleUninitialize();
        return 1;
    }

    const int result = app.run();

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    OleUninitialize();
    return result;
}

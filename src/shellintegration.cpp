#include "shellintegration.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "json.h"
#include "util.h"

namespace shellintegration {
namespace {

constexpr wchar_t kDirectoryShell[] = L"Software\\Classes\\Directory\\shell";
constexpr wchar_t kDirectoryOpen[] = L"Software\\Classes\\Directory\\shell\\open";
constexpr wchar_t kDirectoryCommand[] = L"Software\\Classes\\Directory\\shell\\open\\command";
constexpr wchar_t kDriveShell[] = L"Software\\Classes\\Drive\\shell";
constexpr wchar_t kDriveOpen[] = L"Software\\Classes\\Drive\\shell\\open";
constexpr wchar_t kDriveCommand[] = L"Software\\Classes\\Drive\\shell\\open\\command";

struct ValueBackup {
    bool valueExists = false;
    std::wstring value;
    bool keyExists = false;
};

struct Backup {
    ValueBackup directoryShell;
    ValueBackup directoryCommand;
    ValueBackup driveShell;
    ValueBackup driveCommand;
};

std::wstring ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring BackupPath() {
    return DataDirectory() + L"\\shell-backup.json";
}

bool KeyExists(HKEY root, const std::wstring& subkey) {
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key);
    if (status == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

bool ReadValue(HKEY root, const std::wstring& subkey, const wchar_t* name, ValueBackup& out) {
    out = ValueBackup{};
    out.keyExists = KeyExists(root, subkey);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return true;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0) {
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        status = RegQueryValueExW(key, name, nullptr, &type,
                                  reinterpret_cast<BYTE*>(value.data()), &bytes);
        if (status == ERROR_SUCCESS) {
            while (!value.empty() && value.back() == L'\0') {
                value.pop_back();
            }
            out.valueExists = true;
            out.value = std::move(value);
        }
    }
    RegCloseKey(key);
    return true;
}

bool SetValue(HKEY root, const std::wstring& subkey, const wchar_t* name,
              const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status =
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool DeleteValue(HKEY root, const std::wstring& subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool DeleteKeyTree(HKEY root, const std::wstring& subkey) {
    const LSTATUS status = RegDeleteTreeW(root, subkey.c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

std::wstring OpenCommand() {
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        return {};
    }
    return L"\"" + exe + L"\" \"%1\"";
}

bool SaveBackup(const Backup& backup) {
    Json root = Json::MakeObject();
    root["directoryShellExists"] = Json(backup.directoryShell.valueExists);
    root["directoryShell"] = Json(WideToUtf8(backup.directoryShell.value));
    root["directoryShellKey"] = Json(backup.directoryShell.keyExists);
    root["directoryCommandExists"] = Json(backup.directoryCommand.valueExists);
    root["directoryCommand"] = Json(WideToUtf8(backup.directoryCommand.value));
    root["directoryCommandKey"] = Json(backup.directoryCommand.keyExists);
    root["driveShellExists"] = Json(backup.driveShell.valueExists);
    root["driveShell"] = Json(WideToUtf8(backup.driveShell.value));
    root["driveShellKey"] = Json(backup.driveShell.keyExists);
    root["driveCommandExists"] = Json(backup.driveCommand.valueExists);
    root["driveCommand"] = Json(WideToUtf8(backup.driveCommand.value));
    root["driveCommandKey"] = Json(backup.driveCommand.keyExists);
    EnsureDirectory(DataDirectory());
    std::wstring error;
    return WriteTextFileAtomic(BackupPath(), root.dump(2), error);
}

bool LoadBackup(Backup& backup) {
    std::string text;
    if (!ReadTextFile(BackupPath(), text)) {
        return false;
    }
    Json root;
    if (!Json::parse(text, root) || !root.isObject()) {
        return false;
    }
    backup.directoryShell.valueExists = root["directoryShellExists"].asBool(false);
    backup.directoryShell.value = Utf8ToWide(root["directoryShell"].asString());
    backup.directoryShell.keyExists = root["directoryShellKey"].asBool(false);
    backup.directoryCommand.valueExists = root["directoryCommandExists"].asBool(false);
    backup.directoryCommand.value = Utf8ToWide(root["directoryCommand"].asString());
    backup.directoryCommand.keyExists = root["directoryCommandKey"].asBool(false);
    backup.driveShell.valueExists = root["driveShellExists"].asBool(false);
    backup.driveShell.value = Utf8ToWide(root["driveShell"].asString());
    backup.driveShell.keyExists = root["driveShellKey"].asBool(false);
    backup.driveCommand.valueExists = root["driveCommandExists"].asBool(false);
    backup.driveCommand.value = Utf8ToWide(root["driveCommand"].asString());
    backup.driveCommand.keyExists = root["driveCommandKey"].asBool(false);
    return true;
}

void NotifyShell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

Result Register(const wchar_t* shellKey, const wchar_t* commandKey,
                const std::wstring& command) {
    Result result;
    if (command.empty()) {
        result.error = L"Cannot locate the 67fm executable.";
        return result;
    }
    if (!SetValue(HKEY_CURRENT_USER, shellKey, nullptr, L"open")) {
        result.error = GetErrorText(std::wstring(L"Cannot set ") + shellKey, GetLastError());
        return result;
    }
    if (!SetValue(HKEY_CURRENT_USER, commandKey, nullptr, command)) {
        result.error = GetErrorText(std::wstring(L"Cannot set ") + commandKey, GetLastError());
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace

bool HasBackup() {
    return FileExists(BackupPath());
}

bool IsDefaultFileManager() {
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        return false;
    }
    ValueBackup directory;
    ValueBackup drive;
    if (!ReadValue(HKEY_CURRENT_USER, kDirectoryCommand, nullptr, directory) ||
        !ReadValue(HKEY_CURRENT_USER, kDriveCommand, nullptr, drive)) {
        return false;
    }
    if (!directory.valueExists || !drive.valueExists) {
        return false;
    }
    const std::wstring needle = ToLower(exe);
    return ToLower(directory.value).find(needle) != std::wstring::npos &&
           ToLower(drive.value).find(needle) != std::wstring::npos;
}

Result SetAsDefaultFileManager() {
    Result result;
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        result.error = L"Cannot locate 67fm.exe.";
        return result;
    }
    if (IsDefaultFileManager()) {
        result.ok = true;
        result.already = true;
        return result;
    }

    Backup backup;
    if (!LoadBackup(backup)) {
        ReadValue(HKEY_CURRENT_USER, kDirectoryShell, nullptr, backup.directoryShell);
        ReadValue(HKEY_CURRENT_USER, kDirectoryCommand, nullptr, backup.directoryCommand);
        ReadValue(HKEY_CURRENT_USER, kDriveShell, nullptr, backup.driveShell);
        ReadValue(HKEY_CURRENT_USER, kDriveCommand, nullptr, backup.driveCommand);
        if (!SaveBackup(backup)) {
            result.error = L"Cannot save the previous folder-handler settings.";
            return result;
        }
    }

    const std::wstring command = L"\"" + exe + L"\" \"%1\"";
    const Result directory = Register(kDirectoryShell, kDirectoryCommand, command);
    if (!directory.ok) {
        result.error = directory.error;
        return result;
    }
    const Result drive = Register(kDriveShell, kDriveCommand, command);
    if (!drive.ok) {
        result.error = drive.error;
        return result;
    }
    NotifyShell();
    result.ok = true;
    return result;
}

Result RestoreExplorerDefault() {
    Result result;
    Backup backup;
    if (!LoadBackup(backup)) {
        result.error = L"No saved folder-handler settings were found.";
        return result;
    }

    if (!backup.directoryCommand.valueExists && !backup.directoryCommand.keyExists) {
        DeleteKeyTree(HKEY_CURRENT_USER, kDirectoryOpen);
    } else {
        if (backup.directoryCommand.valueExists) {
            SetValue(HKEY_CURRENT_USER, kDirectoryCommand, nullptr,
                     backup.directoryCommand.value);
        } else {
            DeleteValue(HKEY_CURRENT_USER, kDirectoryCommand, nullptr);
        }
    }
    if (backup.directoryShell.valueExists) {
        SetValue(HKEY_CURRENT_USER, kDirectoryShell, nullptr, backup.directoryShell.value);
    } else {
        DeleteValue(HKEY_CURRENT_USER, kDirectoryShell, nullptr);
    }

    if (!backup.driveCommand.valueExists && !backup.driveCommand.keyExists) {
        DeleteKeyTree(HKEY_CURRENT_USER, kDriveOpen);
    } else {
        if (backup.driveCommand.valueExists) {
            SetValue(HKEY_CURRENT_USER, kDriveCommand, nullptr, backup.driveCommand.value);
        } else {
            DeleteValue(HKEY_CURRENT_USER, kDriveCommand, nullptr);
        }
    }
    if (backup.driveShell.valueExists) {
        SetValue(HKEY_CURRENT_USER, kDriveShell, nullptr, backup.driveShell.value);
    } else {
        DeleteValue(HKEY_CURRENT_USER, kDriveShell, nullptr);
    }

    DeleteFileW(BackupPath().c_str());
    NotifyShell();
    result.ok = true;
    return result;
}

}  // namespace shellintegration


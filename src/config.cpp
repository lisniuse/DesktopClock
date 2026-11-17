#include "config.h"

#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kSection[] = L"window";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"DesktopClock";

std::wstring ReadIniString(const std::wstring& path, const wchar_t* key) {
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(kSection, key, L"", buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

int ReadIniInt(const std::wstring& path, const wchar_t* key, int fallback) {
    const std::wstring text = ReadIniString(path, key);
    if (text.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return fallback;
    }
    return static_cast<int>(value);
}

std::wstring TrimRunEntry(std::wstring value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) {
        value.pop_back();
    }
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

}  // namespace

std::wstring GetExePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring GetConfigDir() {
    wchar_t profile[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile,
                                           static_cast<DWORD>(std::size(profile)));
    if (length == 0 || static_cast<size_t>(length) >= std::size(profile)) {
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE | CSIDL_FLAG_CREATE,
                                    nullptr, 0, profile))) {
            return {};
        }
    }

    const std::wstring dir = std::wstring(profile) + L"\\.desktop-clock";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::wstring GetConfigPath() {
    const std::wstring dir = GetConfigDir();
    if (dir.empty()) {
        return {};
    }
    return dir + L"\\config.ini";
}

bool LoadConfig(AppConfig& config) {
    const std::wstring path = GetConfigPath();
    if (path.empty()) {
        return false;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    config.x = ReadIniInt(path, L"X", config.x);
    config.y = ReadIniInt(path, L"Y", config.y);
    config.width = ReadIniInt(path, L"Width", config.width);
    config.height = ReadIniInt(path, L"Height", config.height);
    config.topmost = ReadIniInt(path, L"Topmost", config.topmost ? 1 : 0) != 0;
    config.hideOnHover =
        ReadIniInt(path, L"HideOnHover", config.hideOnHover ? 1 : 0) != 0;
    return true;
}

bool SaveConfig(const AppConfig& config) {
    const std::wstring path = GetConfigPath();
    if (path.empty()) {
        return false;
    }

    wchar_t buffer[32] = {};
    auto write = [&](const wchar_t* key, int value) {
        _snwprintf_s(buffer, std::size(buffer), _TRUNCATE, L"%d", value);
        WritePrivateProfileStringW(kSection, key, buffer, path.c_str());
    };

    write(L"X", config.x);
    write(L"Y", config.y);
    write(L"Width", config.width);
    write(L"Height", config.height);
    write(L"Topmost", config.topmost ? 1 : 0);
    write(L"HideOnHover", config.hideOnHover ? 1 : 0);
    return true;
}

bool IsStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[2048] = {};
    DWORD bytes = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG result = RegQueryValueExW(key, kRunValue, nullptr, &type,
                                         reinterpret_cast<BYTE*>(buffer), &bytes);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }
    buffer[std::size(buffer) - 1] = L'\0';

    const std::wstring registered = TrimRunEntry(buffer);
    const std::wstring exePath = GetExePath();
    if (registered.empty() || exePath.empty()) {
        return false;
    }
    return _wcsicmp(registered.c_str(), exePath.c_str()) == 0;
}

bool SetStartupEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring exePath = GetExePath();
        if (exePath.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + exePath + L"\"";
        result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) *
                                                   sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

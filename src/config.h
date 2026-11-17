#pragma once

#include <windows.h>
#include <string>

struct AppConfig {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool topmost = true;
    bool hideOnHover = false;
};

// Path of the running executable.
std::wstring GetExePath();

// %USERPROFILE%\.desktop-clock
std::wstring GetConfigDir();

// %USERPROFILE%\.desktop-clock\config.ini
std::wstring GetConfigPath();

bool LoadConfig(AppConfig& config);
bool SaveConfig(const AppConfig& config);

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run
bool IsStartupEnabled();
bool SetStartupEnabled(bool enabled);

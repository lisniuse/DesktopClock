#include <windows.h>
#include <objidl.h>

#include <gdiplus.h>

#include <cwchar>

#include "clock_window.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr wchar_t kMutexName[] = L"Local\\DesktopClock.SingleInstance";
constexpr UINT kToggleEditModeMessage = WM_APP + 3;

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDPIAware();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"DesktopClockWindow", nullptr);
        if (existing) {
            PostMessageW(existing, kToggleEditModeMessage, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) !=
        Gdiplus::Ok) {
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        MessageBoxW(nullptr, L"GDI+ 初始化失败，程序无法启动。", L"桌面时钟",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    int result = 0;
    {
        ClockWindow window(instance);
        if (!window.Create()) {
            MessageBoxW(nullptr, L"创建桌面时钟窗口失败。", L"桌面时钟",
                        MB_OK | MB_ICONERROR);
            result = 1;
        } else {
            if (commandLine && std::wcsstr(commandLine, L"--edit") != nullptr) {
                window.SetEditMode(true);
            }
            window.Show();
            result = window.RunMessageLoop();
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return result;
}

#include "clock_window.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"DesktopClockWindow";
constexpr wchar_t kWindowTitle[] = L"桌面时钟";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kToggleEditModeMessage = WM_APP + 3;
constexpr UINT_PTR kClockTimerId = 1;
constexpr UINT kClockTimerIntervalMs = 500;
constexpr UINT_PTR kHoverTimerId = 2;
constexpr UINT kHoverTimerIntervalMs = 16;
constexpr int kFadeStep = 30;

const wchar_t* const kWeekdays[] = {
    L"星期日", L"星期一", L"星期二", L"星期三",
    L"星期四", L"星期五", L"星期六",
};

}  // namespace

ClockWindow::ClockWindow(HINSTANCE instance) : instance_(instance) {
    HDC screen = GetDC(nullptr);
    if (screen) {
        dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }
    if (dpi_ <= 0) {
        dpi_ = 96;
    }
    appIcon_ = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!appIcon_) {
        appIcon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
}

ClockWindow::~ClockWindow() {
    if (trayAdded_) {
        RemoveTrayIcon();
    }
    ReleaseBackBuffer();
}

bool ClockWindow::Create() {
    if (!RegisterWindowClass()) {
        return false;
    }

    AppConfig config;
    LoadConfig(config);
    RestoreRect(config);
    topmost_ = config.topmost;
    hideOnHover_ = config.hideOnHover;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                    WS_EX_TRANSPARENT;
    if (topmost_) {
        exStyle |= WS_EX_TOPMOST;
    }
    const int width = rect_.right - rect_.left;
    const int height = rect_.bottom - rect_.top;

    hwnd_ = CreateWindowExW(exStyle, kWindowClass, kWindowTitle, WS_POPUP,
                            rect_.left, rect_.top, width, height, nullptr, nullptr,
                            instance_, this);
    if (!hwnd_) {
        return false;
    }

    CreateTrayIcon();
    lastSecond_ = -1;
    timerId_ = SetTimer(hwnd_, kClockTimerId, kClockTimerIntervalMs, nullptr);
    UpdateHoverTimer();
    Render();
    return true;
}

void ClockWindow::Show() {
    if (!hwnd_) {
        return;
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, rect_.left,
                 rect_.top, rect_.right - rect_.left,
                 rect_.bottom - rect_.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Render();
}

int ClockWindow::RunMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool ClockWindow::RegisterWindowClass() {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = appIcon_;
    windowClass.hIconSm = appIcon_;
    windowClass.lpszClassName = kWindowClass;
    windowClass.style = CS_DBLCLKS;

    if (RegisterClassExW(&windowClass) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

LRESULT CALLBACK ClockWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                      LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<ClockWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return TRUE;
    }

    auto* self =
        reinterpret_cast<ClockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return self->HandleMessage(message, wparam, lparam);
}

LRESULT ClockWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            return 0;

        case WM_DESTROY:
            if (timerId_ != 0) {
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
            if (hoverTimerId_ != 0) {
                KillTimer(hwnd_, hoverTimerId_);
                hoverTimerId_ = 0;
            }
            RemoveTrayIcon();
            SaveSettings();
            ReleaseBackBuffer();
            PostQuitMessage(0);
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            BeginPaint(hwnd_, &paint);
            EndPaint(hwnd_, &paint);
            return 0;
        }

        case WM_TIMER:
            if (wparam == kClockTimerId) {
                UpdateTimeAndRender(false);
            } else if (wparam == kHoverTimerId) {
                OnHoverTimer();
            }
            return 0;

        case kTrayMessage: {
            const UINT event = LOWORD(lparam);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                ShowTrayMenu();
            } else if (event == WM_LBUTTONDBLCLK) {
                SetEditMode(!editMode_);
            }
            return 0;
        }

        case kToggleEditModeMessage:
            SetEditMode(!editMode_);
            return 0;

        case WM_LBUTTONDOWN:
            if (editMode_) {
                OnLButtonDown({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (editMode_) {
                OnLButtonUp(true);
                return 0;
            }
            break;

        case WM_LBUTTONDBLCLK:
            if (editMode_) {
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (editMode_) {
                OnMouseMove({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            if (dragMode_ != DragMode::None) {
                dragMode_ = DragMode::None;
                SaveSettings();
            }
            return 0;

        case WM_RBUTTONUP:
            if (editMode_) {
                ShowTrayMenu();
                return 0;
            }
            break;

        case WM_SETCURSOR:
            if (editMode_ && LOWORD(lparam) == HTCLIENT) {
                POINT point = {};
                GetCursorPos(&point);
                ScreenToClient(hwnd_, &point);
                UpdateCursor(point);
                return TRUE;
            }
            break;

        case WM_DISPLAYCHANGE:
            if (MonitorFromRect(&rect_, MONITOR_DEFAULTTONULL) == nullptr) {
                ApplyRect(DefaultRect(), true);
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void ClockWindow::SetEditMode(bool enabled) {
    if (!hwnd_ || editMode_ == enabled) {
        return;
    }

    editMode_ = enabled;
    if (!editMode_ && dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }
        SaveSettings();
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (editMode_) {
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    if (editMode_) {
        // Bring the widget to the top of its z-order band so the handles are
        // visible and draggable even when "置顶" is turned off.
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // Hover-to-hide is suspended while editing.
        SetWindowAlpha(255);
    }
    UpdateHoverTimer();
    SetCursor(LoadCursorW(nullptr, (editMode_ && dragMode_ != DragMode::None)
                                      ? IDC_SIZEALL
                                      : IDC_ARROW));
    UpdateTrayTip();
    Render();
}

void ClockWindow::CreateTrayIcon() {
    trayIcon_ = appIcon_;
    if (!trayIcon_) {
        trayIcon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }

    ZeroMemory(&trayData_, sizeof(trayData_));
    trayData_.cbSize = sizeof(trayData_);
    trayData_.hWnd = hwnd_;
    trayData_.uID = 1;
    trayData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayData_.uCallbackMessage = kTrayMessage;
    trayData_.hIcon = trayIcon_;
    wcscpy_s(trayData_.szTip, kWindowTitle);

    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &trayData_) != FALSE;
    if (trayAdded_) {
        UpdateTrayTip();
    }
}

void ClockWindow::RemoveTrayIcon() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &trayData_);
        trayAdded_ = false;
    }
}

void ClockWindow::UpdateTrayTip() {
    if (!trayAdded_) {
        return;
    }
    if (editMode_) {
        wcscpy_s(trayData_.szTip, L"桌面时钟（编辑模式：拖动四角缩放，拖动文字移动）");
    } else {
        wcscpy_s(trayData_.szTip, L"桌面时钟（右键菜单）");
    }
    trayData_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &trayData_);
}

void ClockWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING | (editMode_ ? MF_CHECKED : MF_UNCHECKED),
                IDM_EDIT_MODE, L"编辑模式");
    AppendMenuW(menu, MF_STRING | (topmost_ ? MF_CHECKED : MF_UNCHECKED),
                IDM_TOPMOST, L"置顶");
    AppendMenuW(menu,
                MF_STRING | (hideOnHover_ ? MF_CHECKED : MF_UNCHECKED),
                IDM_HIDE_ON_HOVER, L"鼠标移上去消失");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,
                MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED),
                IDM_STARTUP, L"开机启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    POINT cursor = {};
    GetCursorPos(&cursor);

    SetForegroundWindow(hwnd_);
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y,
        0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (command != 0) {
        HandleTrayCommand(command);
    }
}

void ClockWindow::HandleTrayCommand(int command) {
    switch (command) {
        case IDM_EDIT_MODE:
            SetEditMode(!editMode_);
            break;

        case IDM_TOPMOST:
            SetTopmost(!topmost_);
            break;

        case IDM_HIDE_ON_HOVER:
            SetHideOnHover(!hideOnHover_);
            break;

        case IDM_STARTUP: {
            const bool enable = !IsStartupEnabled();
            if (!SetStartupEnabled(enable)) {
                MessageBoxW(hwnd_,
                            L"无法修改开机启动项，请检查注册表权限。",
                            kWindowTitle, MB_OK | MB_ICONWARNING);
            }
            break;
        }

        case IDM_EXIT:
            RemoveTrayIcon();
            DestroyWindow(hwnd_);
            break;

        default:
            break;
    }
}

void ClockWindow::UpdateTimeAndRender(bool force) {
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    if (!force && now.wSecond == lastSecond_) {
        return;
    }
    lastSecond_ = now.wSecond;
    Render();
}

void ClockWindow::Render() {
    if (!hwnd_) {
        return;
    }

    const int width = rect_.right - rect_.left;
    const int height = rect_.bottom - rect_.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!EnsureBackBuffer(width, height)) {
        return;
    }

    {
        Gdiplus::Bitmap canvas(width, height, width * 4,
                               PixelFormat32bppPARGB,
                               static_cast<BYTE*>(backBits_));
        Gdiplus::Graphics graphics(&canvas);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        if (graphics.GetLastStatus() == Gdiplus::Ok) {
            DrawScene(graphics, width, height);
            graphics.Flush(Gdiplus::FlushIntentionSync);
        }
    }

    Present();
}

void ClockWindow::Present() {
    if (!hwnd_ || !backDc_) {
        return;
    }

    const int width = rect_.right - rect_.left;
    const int height = rect_.bottom - rect_.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return;
    }

    POINT destination = {rect_.left, rect_.top};
    SIZE size = {width, height};
    POINT source = {0, 0};
    const BYTE alpha =
        static_cast<BYTE>(std::clamp(windowAlpha_, 0, 255));
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, &destination, &size, backDc_, &source, 0,
                        &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

bool ClockWindow::EnsureBackBuffer(int width, int height) {
    if (backDc_ && width == backWidth_ && height == backHeight_) {
        return true;
    }
    ReleaseBackBuffer();

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return false;
    }

    backDc_ = CreateCompatibleDC(screen);
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;  // top-down
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    backBitmap_ = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS,
                                   &backBits_, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!backDc_ || !backBitmap_ || !backBits_) {
        ReleaseBackBuffer();
        return false;
    }

    backOldBitmap_ = SelectObject(backDc_, backBitmap_);
    backWidth_ = width;
    backHeight_ = height;
    ZeroMemory(backBits_, static_cast<size_t>(width) * height * 4);
    return true;
}

void ClockWindow::ReleaseBackBuffer() {
    if (backDc_) {
        if (backOldBitmap_) {
            SelectObject(backDc_, backOldBitmap_);
        }
        DeleteDC(backDc_);
    }
    if (backBitmap_) {
        DeleteObject(backBitmap_);
    }
    backDc_ = nullptr;
    backBitmap_ = nullptr;
    backOldBitmap_ = nullptr;
    backBits_ = nullptr;
    backWidth_ = 0;
    backHeight_ = 0;
}

void ClockWindow::DrawScene(Gdiplus::Graphics& graphics, int width, int height) {
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    const std::wstring timeText = FormatTimeString(now);
    const std::wstring dateText = FormatDateString(now);

    auto pickFamilyName = []() -> const wchar_t* {
        for (const wchar_t* candidate :
             {L"Microsoft YaHei", L"Microsoft YaHei UI", L"Segoe UI", L"Arial"}) {
            Gdiplus::FontFamily probe(candidate);
            if (probe.IsAvailable()) {
                return candidate;
            }
        }
        return L"Arial";
    };
    Gdiplus::FontFamily family(pickFamilyName());

    Gdiplus::StringFormat measureFormat;
    measureFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                                 Gdiplus::StringFormatFlagsNoClip);
    measureFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    measureFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    auto fits = [&](const std::wstring& text, float size, float maxWidth,
                    float maxHeight) {
        Gdiplus::Font font(&family, size, Gdiplus::FontStyleRegular,
                           Gdiplus::UnitPixel);
        if (font.GetLastStatus() != Gdiplus::Ok) {
            return false;
        }
        Gdiplus::RectF layout(0, 0, 100000.0f, 100000.0f);
        Gdiplus::RectF bounds;
        graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font,
                               layout, &measureFormat, &bounds);
        return bounds.Width <= maxWidth &&
               font.GetHeight(&graphics) <= maxHeight;
    };

    auto fitFontSize = [&](const std::wstring& text, float maxWidth,
                           float maxHeight) {
        float low = 4.0f;
        float high = std::max(6.0f, maxHeight * 1.6f);
        for (int i = 0; i < 16; ++i) {
            const float middle = (low + high) * 0.5f;
            if (fits(text, middle, maxWidth, maxHeight)) {
                low = middle;
            } else {
                high = middle;
            }
        }
        return low;
    };

    const float padding = static_cast<float>(std::max(2, Scale(6)));
    const float availableWidth = static_cast<float>(width) - padding * 2.0f;
    const float timeHeight = static_cast<float>(height) * 0.56f;
    const float dateHeight = static_cast<float>(height) * 0.28f;

    const float timeSize =
        fitFontSize(timeText, availableWidth, timeHeight);
    const float dateSize =
        fitFontSize(dateText, availableWidth, dateHeight);

    const Gdiplus::RectF timeLayout(padding, static_cast<float>(height) * 0.045f,
                                    availableWidth, timeHeight);
    const Gdiplus::RectF dateLayout(padding, static_cast<float>(height) * 0.635f,
                                    availableWidth, dateHeight);

    DrawCenteredText(graphics, timeText, family, timeSize, timeLayout);
    DrawCenteredText(graphics, dateText, family, dateSize, dateLayout);

    if (editMode_) {
        Gdiplus::Pen border(Gdiplus::Color(150, 255, 255, 255), 1.0f);
        border.SetDashStyle(Gdiplus::DashStyleDash);
        graphics.DrawRectangle(&border, 0.5f, 0.5f,
                               static_cast<float>(width) - 1.0f,
                               static_cast<float>(height) - 1.0f);

        const float handleSize =
            static_cast<float>(std::max(6, Scale(10)));
        Gdiplus::SolidBrush fill(Gdiplus::Color(235, 255, 255, 255));
        Gdiplus::Pen outline(Gdiplus::Color(220, 0, 0, 0), 1.0f);

        auto drawHandle = [&](float x, float y) {
            graphics.FillRectangle(&fill, x, y, handleSize, handleSize);
            graphics.DrawRectangle(&outline, x, y, handleSize, handleSize);
        };
        drawHandle(0.0f, 0.0f);
        drawHandle(static_cast<float>(width) - handleSize, 0.0f);
        drawHandle(0.0f, static_cast<float>(height) - handleSize);
        drawHandle(static_cast<float>(width) - handleSize,
                   static_cast<float>(height) - handleSize);
    }
}

void ClockWindow::DrawCenteredText(Gdiplus::Graphics& graphics,
                                   const std::wstring& text,
                                   Gdiplus::FontFamily& family, float fontSize,
                                   const Gdiplus::RectF& layout) {
    Gdiplus::Font font(&family, fontSize, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) {
        return;
    }

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingNone);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                          Gdiplus::StringFormatFlagsNoClip);

    const float offset =
        std::max(1.0f, static_cast<float>(Scale(2)));

    Gdiplus::SolidBrush nearShadow(Gdiplus::Color(70, 0, 0, 0));
    Gdiplus::SolidBrush deepShadow(Gdiplus::Color(150, 0, 0, 0));
    Gdiplus::SolidBrush foreground(Gdiplus::Color(255, 255, 255, 255));

    Gdiplus::RectF nearRect(layout.X + offset * 0.5f, layout.Y + offset * 0.5f,
                            layout.Width, layout.Height);
    Gdiplus::RectF deepRect(layout.X + offset, layout.Y + offset, layout.Width,
                            layout.Height);

    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
                        nearRect, &format, &nearShadow);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
                        deepRect, &format, &deepShadow);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
                        layout, &format, &foreground);
}

RECT ClockWindow::DefaultRect() const {
    RECT workArea = {};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea = {0, 0, GetSystemMetrics(SM_CXSCREEN),
                    GetSystemMetrics(SM_CYSCREEN)};
    }

    const int margin = Scale(24);
    const int workWidth = static_cast<int>(workArea.right - workArea.left);
    const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
    const int width =
        std::min(Scale(360), std::max(Scale(180), workWidth - margin * 2));
    int height = std::min(Scale(100), std::max(Scale(50), workHeight - margin * 2));

    const int right = workArea.right - margin;
    const int top = workArea.top + margin;
    return {right - width, top, right, top + height};
}

void ClockWindow::RestoreRect(const AppConfig& config) {
    if (config.width >= Scale(120) && config.height >= Scale(40)) {
        RECT saved = {config.x, config.y, config.x + config.width,
                      config.y + config.height};
        if (MonitorFromRect(&saved, MONITOR_DEFAULTTONULL) != nullptr) {
            rect_ = saved;
            return;
        }
    }
    rect_ = DefaultRect();
}

void ClockWindow::ApplyRect(const RECT& rect, bool persist) {
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) {
        return;
    }
    rect_ = rect;
    Render();
    if (persist) {
        SaveSettings();
    }
}

void ClockWindow::SaveSettings() const {
    if (rect_.right <= rect_.left || rect_.bottom <= rect_.top) {
        return;
    }
    AppConfig config;
    config.x = rect_.left;
    config.y = rect_.top;
    config.width = rect_.right - rect_.left;
    config.height = rect_.bottom - rect_.top;
    config.topmost = topmost_;
    config.hideOnHover = hideOnHover_;
    SaveConfig(config);
}

void ClockWindow::SetTopmost(bool enabled) {
    if (!hwnd_ || topmost_ == enabled) {
        return;
    }
    topmost_ = enabled;
    SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SaveSettings();
}

void ClockWindow::SetHideOnHover(bool enabled) {
    if (hideOnHover_ == enabled) {
        return;
    }

    hideOnHover_ = enabled;
    SaveSettings();

    if (!enabled) {
        SetWindowAlpha(255);
    }
    UpdateHoverTimer();

    if (enabled && !editMode_) {
        // Apply immediately if the cursor already sits on the clock.
        OnHoverTimer();
    }
}

void ClockWindow::UpdateHoverTimer() {
    const bool needed = hwnd_ && hideOnHover_ && !editMode_;
    if (needed && hoverTimerId_ == 0) {
        hoverTimerId_ = SetTimer(hwnd_, kHoverTimerId, kHoverTimerIntervalMs,
                                 nullptr);
    } else if (!needed && hoverTimerId_ != 0) {
        KillTimer(hwnd_, hoverTimerId_);
        hoverTimerId_ = 0;
    }
}

void ClockWindow::OnHoverTimer() {
    if (!hideOnHover_ || editMode_) {
        SetWindowAlpha(255);
        return;
    }

    POINT cursor = {};
    GetCursorPos(&cursor);
    const bool inside = PtInRect(&rect_, cursor) != FALSE;
    const int target = inside ? 0 : 255;
    if (windowAlpha_ == target) {
        return;
    }

    const int next = windowAlpha_ < target
                         ? std::min(target, windowAlpha_ + kFadeStep)
                         : std::max(target, windowAlpha_ - kFadeStep);
    SetWindowAlpha(next);
}

void ClockWindow::SetWindowAlpha(int alpha) {
    const int clamped = std::clamp(alpha, 0, 255);
    if (windowAlpha_ == clamped) {
        return;
    }
    windowAlpha_ = clamped;
    Present();
}

int ClockWindow::Scale(int value) const {
    return MulDiv(value, dpi_, 96);
}

ClockWindow::DragMode ClockWindow::HitTestDrag(POINT clientPoint) const {
    const int width = rect_.right - rect_.left;
    const int height = rect_.bottom - rect_.top;
    const int zone = std::min(Scale(18), std::min(width, height) / 3);

    const bool left = clientPoint.x <= zone;
    const bool right = clientPoint.x >= width - zone;
    const bool top = clientPoint.y <= zone;
    const bool bottom = clientPoint.y >= height - zone;

    if (left && top) {
        return DragMode::ResizeTopLeft;
    }
    if (right && top) {
        return DragMode::ResizeTopRight;
    }
    if (left && bottom) {
        return DragMode::ResizeBottomLeft;
    }
    if (right && bottom) {
        return DragMode::ResizeBottomRight;
    }
    return DragMode::Move;
}

void ClockWindow::UpdateCursor(POINT clientPoint) {
    if (!editMode_) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }

    const DragMode mode =
        dragMode_ != DragMode::None ? dragMode_ : HitTestDrag(clientPoint);
    LPCTSTR cursorId = IDC_SIZEALL;
    switch (mode) {
        case DragMode::ResizeTopLeft:
        case DragMode::ResizeBottomRight:
            cursorId = IDC_SIZENWSE;
            break;
        case DragMode::ResizeTopRight:
        case DragMode::ResizeBottomLeft:
            cursorId = IDC_SIZENESW;
            break;
        case DragMode::Move:
        default:
            cursorId = IDC_SIZEALL;
            break;
    }
    SetCursor(LoadCursorW(nullptr, cursorId));
}

void ClockWindow::OnLButtonDown(POINT clientPoint) {
    if (!editMode_) {
        return;
    }
    dragMode_ = HitTestDrag(clientPoint);
    dragStartRect_ = rect_;
    GetCursorPos(&dragStart_);
    SetCapture(hwnd_);
    UpdateCursor(clientPoint);
}

void ClockWindow::OnMouseMove(POINT clientPoint) {
    if (!editMode_) {
        return;
    }
    if (dragMode_ == DragMode::None) {
        UpdateCursor(clientPoint);
        return;
    }

    POINT cursor = {};
    GetCursorPos(&cursor);
    const int deltaX = cursor.x - dragStart_.x;
    const int deltaY = cursor.y - dragStart_.y;

    RECT rect = dragStartRect_;
    const int minWidth = Scale(180);
    const int minHeight = Scale(48);

    switch (dragMode_) {
        case DragMode::Move:
            OffsetRect(&rect, deltaX, deltaY);
            break;

        case DragMode::ResizeTopLeft:
            rect.left += deltaX;
            rect.top += deltaY;
            if (rect.right - rect.left < minWidth) {
                rect.left = rect.right - minWidth;
            }
            if (rect.bottom - rect.top < minHeight) {
                rect.top = rect.bottom - minHeight;
            }
            break;

        case DragMode::ResizeTopRight:
            rect.right += deltaX;
            rect.top += deltaY;
            if (rect.right - rect.left < minWidth) {
                rect.right = rect.left + minWidth;
            }
            if (rect.bottom - rect.top < minHeight) {
                rect.top = rect.bottom - minHeight;
            }
            break;

        case DragMode::ResizeBottomLeft:
            rect.left += deltaX;
            rect.bottom += deltaY;
            if (rect.right - rect.left < minWidth) {
                rect.left = rect.right - minWidth;
            }
            if (rect.bottom - rect.top < minHeight) {
                rect.bottom = rect.top + minHeight;
            }
            break;

        case DragMode::ResizeBottomRight:
            rect.right += deltaX;
            rect.bottom += deltaY;
            if (rect.right - rect.left < minWidth) {
                rect.right = rect.left + minWidth;
            }
            if (rect.bottom - rect.top < minHeight) {
                rect.bottom = rect.top + minHeight;
            }
            break;

        case DragMode::None:
        default:
            return;
    }

    if (dragMode_ == DragMode::Move) {
        const RECT virtualScreen = {
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
        };
        const int minVisible = Scale(48);
        if (rect.right < virtualScreen.left + minVisible) {
            OffsetRect(&rect, virtualScreen.left + minVisible - rect.right, 0);
        }
        if (rect.left > virtualScreen.right - minVisible) {
            OffsetRect(&rect, virtualScreen.right - minVisible - rect.left, 0);
        }
        if (rect.bottom < virtualScreen.top + minVisible) {
            OffsetRect(&rect, 0, virtualScreen.top + minVisible - rect.bottom);
        }
        if (rect.top > virtualScreen.bottom - minVisible) {
            OffsetRect(&rect, 0, virtualScreen.bottom - minVisible - rect.top);
        }
    }

    ApplyRect(rect, false);
    UpdateCursor(clientPoint);
}

void ClockWindow::OnLButtonUp(bool persist) {
    if (dragMode_ == DragMode::None) {
        return;
    }
    dragMode_ = DragMode::None;
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    if (persist) {
        SaveSettings();
    }
    POINT cursor = {};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd_, &cursor);
    UpdateCursor(cursor);
}

std::wstring ClockWindow::FormatTimeString(const SYSTEMTIME& time) {
    wchar_t buffer[32] = {};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%02u:%02u:%02u",
                 time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

std::wstring ClockWindow::FormatDateString(const SYSTEMTIME& time) {
    wchar_t buffer[64] = {};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%04u年%u月%u日 %s",
                 time.wYear, time.wMonth, time.wDay,
                 kWeekdays[time.wDayOfWeek % 7]);
    return buffer;
}

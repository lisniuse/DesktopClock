#pragma once

#include <windows.h>
#include <shellapi.h>
#include <objidl.h>

#include <gdiplus.h>

#include <string>

#include "config.h"

class ClockWindow {
public:
    explicit ClockWindow(HINSTANCE instance);
    ~ClockWindow();

    ClockWindow(const ClockWindow&) = delete;
    ClockWindow& operator=(const ClockWindow&) = delete;

    bool Create();
    void Show();
    int RunMessageLoop();

    void SetEditMode(bool enabled);
    bool IsEditMode() const { return editMode_; }

private:
    enum class DragMode {
        None,
        Move,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomLeft,
        ResizeBottomRight,
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                    LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool RegisterWindowClass();
    void CreateTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTip();
    void ShowTrayMenu();
    void HandleTrayCommand(int command);
    void SetTopmost(bool enabled);
    void SetHideOnHover(bool enabled);
    void UpdateHoverTimer();
    void OnHoverTimer();
    void SetWindowAlpha(int alpha);

    void UpdateTimeAndRender(bool force);
    void Render();
    void Present();
    bool EnsureBackBuffer(int width, int height);
    void ReleaseBackBuffer();
    void DrawScene(Gdiplus::Graphics& graphics, int width, int height);
    void DrawCenteredText(Gdiplus::Graphics& graphics, const std::wstring& text,
                          Gdiplus::FontFamily& family, float fontSize,
                          const Gdiplus::RectF& layout);

    RECT DefaultRect() const;
    void RestoreRect(const AppConfig& config);
    void ApplyRect(const RECT& rect, bool persist);
    void SaveSettings() const;
    int Scale(int value) const;

    DragMode HitTestDrag(POINT clientPoint) const;
    void UpdateCursor(POINT clientPoint);
    void OnLButtonDown(POINT clientPoint);
    void OnMouseMove(POINT clientPoint);
    void OnLButtonUp(bool persist);

    static std::wstring FormatTimeString(const SYSTEMTIME& time);
    static std::wstring FormatDateString(const SYSTEMTIME& time);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON appIcon_ = nullptr;
    HICON trayIcon_ = nullptr;
    NOTIFYICONDATAW trayData_{};
    bool trayAdded_ = false;

    RECT rect_{};
    bool editMode_ = false;
    bool topmost_ = true;
    bool hideOnHover_ = false;
    int windowAlpha_ = 255;
    int dpi_ = 96;
    int lastSecond_ = -1;
    UINT_PTR timerId_ = 0;
    UINT_PTR hoverTimerId_ = 0;

    DragMode dragMode_ = DragMode::None;
    POINT dragStart_{};
    RECT dragStartRect_{};

    HDC backDc_ = nullptr;
    HBITMAP backBitmap_ = nullptr;
    HGDIOBJ backOldBitmap_ = nullptr;
    void* backBits_ = nullptr;
    int backWidth_ = 0;
    int backHeight_ = 0;
};

#include "maa-capture/Windows/ScreenDCScreencap.h"

#include "maa-capture/Windows/ForegroundUtils.h"
#include "maa-capture/Windows/HwndUtils.hpp"
#include "maa-capture/Logger.h"

MWS_NS_BEGIN

std::optional<Image> ScreenDCScreencap::screencap()
{
    if (!hwnd_) {
        LogError << "hwnd_ is nullptr";
        return std::nullopt;
    }

    if (!ensure_foreground_with_cooldown(hwnd_)) {
        LogWarn << "Failed to ensure foreground window before screencap";
    }

    // Ensure the window is fully visible on the monitor before screencap
    if (!ensure_window_on_screen(hwnd_)) {
        LogWarn << "Failed to ensure window on screen";
    }

    // 获取窗口客户区相对于屏幕（虚拟桌面）的坐标
    RECT window_rect = { 0 };
    if (!GetWindowRect(hwnd_, &window_rect)) {
        LogError << "GetWindowRect failed, error code: " << GetLastError();
        return std::nullopt;
    }

    // 获取客户区大小
    RECT client_rect = { 0 };
    if (!GetClientRect(hwnd_, &client_rect)) {
        LogError << "GetClientRect failed, error code: " << GetLastError();
        return std::nullopt;
    }

    // 计算客户区在屏幕上的位置
    POINT client_top_left = { client_rect.left, client_rect.top };
    if (!ClientToScreen(hwnd_, &client_top_left)) {
        LogError << "ClientToScreen failed, error code: " << GetLastError();
        return std::nullopt;
    }

    int width = client_rect.right - client_rect.left;
    int height = client_rect.bottom - client_rect.top;

    if (width <= 0 || height <= 0) {
        LogError << "Invalid window size" << VAR(width) << VAR(height);
        return std::nullopt;
    }

    HDC screen_dc = nullptr;
    HDC mem_dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old_obj = nullptr;

    OnScopeLeave([&]() {
        if (old_obj) {
            SelectObject(mem_dc, old_obj);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (mem_dc) {
            DeleteDC(mem_dc);
        }
        if (screen_dc) {
            ReleaseDC(nullptr, screen_dc);
        }
    });

    // 获取屏幕DC
    screen_dc = GetDC(nullptr);
    if (!screen_dc) {
        LogError << "GetDC failed, error code: " << GetLastError();
        return std::nullopt;
    }

    mem_dc = CreateCompatibleDC(screen_dc);
    if (!mem_dc) {
        LogError << "CreateCompatibleDC failed, error code: " << GetLastError();
        return std::nullopt;
    }

    bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    if (!bitmap) {
        LogError << "CreateCompatibleBitmap failed, error code: " << GetLastError();
        return std::nullopt;
    }

    old_obj = SelectObject(mem_dc, bitmap);
    if (!old_obj) {
        LogError << "SelectObject failed, error code: " << GetLastError();
        return std::nullopt;
    }

    // 从屏幕DC复制客户区内容
    if (!BitBlt(mem_dc, 0, 0, width, height, screen_dc, client_top_left.x, client_top_left.y, SRCCOPY)) {
        LogError << "BitBlt failed, error code: " << GetLastError();
        return std::nullopt;
    }

    Image mat(height, width, 4);
    if (!GetBitmapBits(bitmap, width * height * 4, mat.data)) {
        LogError << "GetBitmapBits failed, error code: " << GetLastError();
        return std::nullopt;
    }

    return bgra_to_bgr(mat);
}

MWS_NS_END
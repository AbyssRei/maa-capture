#include "maa-capture/GdiScreencap.h"

#include "maa-capture/HwndUtils.hpp"
#include "maa-capture/Logger.h"

MWS_NS_BEGIN

std::optional<Image> GdiScreencap::screencap()
{
    if (!hwnd_) {
        LogError << "hwnd_ is nullptr";
        return std::nullopt;
    }

    HDC hdc = nullptr;
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
        if (hdc) {
            ReleaseDC(hwnd_, hdc);
        }
    });

    hdc = GetDC(hwnd_);
    if (!hdc) {
        LogError << "GetDC failed, error code: " << GetLastError();
        return std::nullopt;
    }

    mem_dc = CreateCompatibleDC(hdc);
    if (!mem_dc) {
        LogError << "CreateCompatibleDC failed, error code: " << GetLastError();
        return std::nullopt;
    }

    auto [width, height] = window_size(hwnd_);
    if (width <= 0 || height <= 0) {
        LogError << "Invalid window size" << VAR(width) << VAR(height);
        return std::nullopt;
    }

    bitmap = CreateCompatibleBitmap(hdc, width, height);
    if (!bitmap) {
        LogError << "CreateCompatibleBitmap failed, error code: " << GetLastError();
        return std::nullopt;
    }

    old_obj = SelectObject(mem_dc, bitmap);
    if (!old_obj) {
        LogError << "SelectObject failed, error code: " << GetLastError();
        return std::nullopt;
    }

    if (!BitBlt(mem_dc, 0, 0, width, height, hdc, 0, 0, SRCCOPY)) {
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
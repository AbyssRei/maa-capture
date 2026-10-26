#include "maa-capture/Windows/GdiScreencap.h"

#include "maa-capture/Windows/HwndUtils.hpp"
#include "maa-capture/Logger.h"

MWS_NS_BEGIN

std::optional<Image> GdiScreencap::capture_image()
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

    // Read a top-down 32-bit DIB. DDB storage order/format is not an image contract.
    SelectObject(mem_dc, old_obj);
    old_obj = nullptr;
    BITMAPINFO bmi {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    Image mat(height, width, 4);
    if (GetDIBits(mem_dc, bitmap, 0, height, mat.data, &bmi, DIB_RGB_COLORS) != height) {
        error_ = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_CAPTURE, "GetDIBits failed", method_, GetLastError(), "Win32");
        return std::nullopt;
    }

    return bgra_to_bgr(mat);
}

MWS_NS_END
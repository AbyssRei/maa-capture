#include "CaptureInternal.hpp"
#include "maa-capture/Windows/SafeWindows.hpp"
#include <shellscalingapi.h>
#include <algorithm>
#include <cwctype>
#include <string>
namespace mws::detail {
class DpiContext {
    DPI_AWARENESS_CONTEXT old_ = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
public: ~DpiContext() { if (old_) SetThreadDpiAwarenessContext(old_); }
};
static std::string utf8(const wchar_t* value) {
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid display UTF-16 name");
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, s.data(), n, nullptr, nullptr); s.pop_back(); return s;
}
struct NamedMonitor { HMONITOR handle; MONITORINFOEXW info; };
std::optional<std::vector<MaaWSMonitorInfo>> platform_monitors(MaaWSError& e) {
    DpiContext dpi;
    UINT32 paths_count = 0, modes_count = 0;
    auto status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths_count, &modes_count);
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(paths_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modes_count);
    if (status == ERROR_SUCCESS) status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths_count, paths.data(), &modes_count, modes.data(), nullptr);
    if (status != ERROR_SUCCESS) {
        e = capture_error(status == ERROR_INSUFFICIENT_BUFFER ? MAA_WS_LAYOUT_CHANGED : MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_ENUMERATE, "QueryDisplayConfig failed", 0, status, "Win32"); return {};
    }
    paths.resize(paths_count);
    std::vector<NamedMonitor> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM data) -> BOOL {
        NamedMonitor m {}; m.handle = h; m.info.cbSize = sizeof(m.info);
        if (!GetMonitorInfoW(h, &m.info)) return FALSE;
        reinterpret_cast<std::vector<NamedMonitor>*>(data)->push_back(m); return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors))) {
        e = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_ENUMERATE, "EnumDisplayMonitors failed", 0, GetLastError(), "Win32"); return {};
    }
    std::vector<MaaWSMonitorInfo> result;
    for (const auto& p : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src {}; src.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(src), p.sourceInfo.adapterId, p.sourceInfo.id };
        DISPLAYCONFIG_TARGET_DEVICE_NAME dst {}; dst.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(dst), p.targetInfo.adapterId, p.targetInfo.id };
        status = DisplayConfigGetDeviceInfo(&src.header);
        if (!status) status = DisplayConfigGetDeviceInfo(&dst.header);
        if (status) { e = capture_error(MAA_WS_CAPTURE_FAILED, MAA_WS_STAGE_ENUMERATE, "DisplayConfigGetDeviceInfo failed", 0, status, "Win32"); return {}; }
        auto found = std::find_if(monitors.begin(), monitors.end(), [&](auto& m) { return _wcsicmp(m.info.szDevice, src.viewGdiDeviceName) == 0; });
        if (found == monitors.end()) { e = capture_error(MAA_WS_LAYOUT_CHANGED, MAA_WS_STAGE_ENUMERATE, "Display path has no matching HMONITOR"); return {}; }
        MaaWSMonitorInfo i {}; i.struct_size = sizeof(i); i.native_id = reinterpret_cast<uintptr_t>(found->handle);
        i.primary = (found->info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        auto r = found->info.rcMonitor; i.desktop_rect = { double(r.left), double(r.top), double(r.right - r.left), double(r.bottom - r.top) };
        i.pixel_width = r.right - r.left; i.pixel_height = r.bottom - r.top;
        i.coordinate_unit = MAA_WS_UNIT_PHYSICAL_PIXEL; i.coordinate_space = MAA_WS_SPACE_DESKTOP; i.axis = MAA_WS_AXIS_X_RIGHT_Y_DOWN;
        i.valid_fields = MAA_WS_VALID_IDENTITY | MAA_WS_VALID_GEOMETRY | MAA_WS_VALID_ROTATION;
        i.rotation = (static_cast<uint32_t>(p.targetInfo.rotation) - 1) * 90;
        DEVICE_SCALE_FACTOR scale;
        if (SUCCEEDED(GetScaleFactorForMonitor(found->handle, &scale))) { i.scale_x = i.scale_y = double(scale) / 100; i.valid_fields |= MAA_WS_VALID_SCALE; }
        std::wstring path(dst.monitorDevicePath); std::transform(path.begin(), path.end(), path.begin(), towlower);
        if (!path.empty()) {
            copy_text(i.monitor_key, "win32:" + utf8(path.c_str())); i.identity_scope = MAA_WS_IDENTITY_PERSISTENT;
            if (GetSystemMetrics(SM_REMOTESESSION) || p.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL)
                i.identity_scope = MAA_WS_IDENTITY_SESSION;
        } else {
            copy_text(i.monitor_key, "win32:session:" + std::to_string(p.targetInfo.adapterId.HighPart) + ":" + std::to_string(p.targetInfo.adapterId.LowPart) + ":" + std::to_string(p.targetInfo.id)); i.identity_scope = MAA_WS_IDENTITY_SESSION;
        }
        copy_text(i.name, utf8(dst.monitorFriendlyDeviceName[0] ? dst.monitorFriendlyDeviceName : src.viewGdiDeviceName));
        result.push_back(i);
    }
    return result;
}
}

#pragma once

// ============================================================================
// Wayland 客户端（截图所需的最小封装）
//
// 提取自 MaaFramework source/MaaLinuxControlUnit/Wayland/WaylandClient.h/.cpp，
// 仅保留截图相关的协议绑定（wl_output / wl_shm / wlr_screencopy_unstable_v1），
// 去掉输入相关协议（virtual-keyboard / wlr-virtual-pointer / wl_seat）。
// ============================================================================

#if !defined(__linux__)
#error "WaylandClient is Linux-only"
#endif

#include "maa-capture/Common.h"
#include "maa-capture/Linux/WaylandHelper.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

MWS_NS_BEGIN

class WaylandClient
{
public:
    WaylandClient();
    ~WaylandClient();

    WaylandClient(const WaylandClient&) = delete;
    WaylandClient& operator=(const WaylandClient&) = delete;

    // socket_name 为空时使用 WAYLAND_DISPLAY 环境变量指定的默认 socket
    bool connect(const std::string& socket_name);
    bool connected();
    bool process_requests() const;
    void close();

    std::shared_ptr<zwlr_screencopy_manager_v1> get_screencopy_manager();
    std::shared_ptr<wl_shm> get_shm();

    // 输出（显示器）列表：索引为合成器广播 wl_output global 的顺序。
    // 越界索引返回 nullptr。
    std::shared_ptr<wl_output> get_output(std::size_t index = 0);
    std::size_t output_count() const { return outputs_.size(); }

private:
    bool open();
    bool bind_protocol();

private:
    std::string socket_name_;
    bool connected_ = false;

    wl_registry_listener registry_listener_ = { };

    // Registry objects
    std::unique_ptr<wl_display> display_;
    std::unique_ptr<wl_registry> registry_;
    std::vector<std::shared_ptr<wl_output>> outputs_; // 多显示器：按广播顺序
    std::shared_ptr<wl_shm> shm_;
    std::shared_ptr<zwlr_screencopy_manager_v1> screencopy_manager_;
};

MWS_NS_END

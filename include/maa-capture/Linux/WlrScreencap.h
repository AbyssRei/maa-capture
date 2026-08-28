#pragma once

// ============================================================================
// wlr-screencopy（Wayland）截图单元
//
// 提取自 MaaFramework source/MaaLinuxControlUnit/Screencap/WlrScreencap.h/.cpp
// （cv::Mat → mws::Image，接口适配独立库 ScreencapBase）。
//
// 通过 wlr_screencopy_unstable_v1 协议让合成器把输出内容拷贝到客户端
// 提供的 wl_shm 缓冲（memfd），适用于 wlroots 系合成器
// （Sway / Hyprland / labwc 等）。
// ============================================================================

// 注意：编译进库需要在 CMake 配置期找到 wayland-client
// （MWS_ENABLE_WLR，默认 ON）；本头文件本身只要求 Linux 平台。

#if !defined(__linux__)
#error "WlrScreencap is Linux-only"
#endif

#include <memory>
#include <utility>

#include "maa-capture/Common.h"
#include "maa-capture/Linux/MemfdBuffer.h"
#include "maa-capture/Linux/WaylandClient.h"
#include "maa-capture/ScreencapBase.h"

MWS_NS_BEGIN

class WlrScreencap : public ScreencapBase
{
public:
    explicit WlrScreencap(std::shared_ptr<WaylandClient> client)
        : client_(std::move(client))
        , screencopy_manager_(client_->get_screencopy_manager())
        , shm_(client_->get_shm())
        , output_(client_->get_output())
    {
    }

    virtual ~WlrScreencap() override = default;

public:
    virtual std::optional<Image> screencap() override;

private:
    bool check_buffer(int format, int width, int height, int stride) const;
    bool create_buffer(int format, int width, int height, int stride);
    bool close_buffer();

private:
    std::shared_ptr<WaylandClient> client_;
    std::shared_ptr<zwlr_screencopy_manager_v1> screencopy_manager_;
    std::shared_ptr<wl_shm> shm_;
    std::shared_ptr<wl_output> output_;
    bool capture_waiting_ = false;
    bool capture_successful_ = false;
    std::unique_ptr<MemfdBuffer> buffer_;
    std::unique_ptr<wl_shm_pool> shm_pool_;
    std::unique_ptr<wl_buffer> buffer_obj_;
    int buffer_width_ = 0;
    int buffer_height_ = 0;
    int buffer_format_ = 0;
    int buffer_stride_ = 0;
};

MWS_NS_END

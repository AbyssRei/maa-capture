#pragma once

// ============================================================================
// PipeWire 截图单元（Linux）
//
// 提取自 MaaFramework source/MaaLinuxControlUnit/Screencap/PipeWireScreencap.h/.cpp
// （cv::Mat → mws::Image，接口适配独立库 ScreencapBase：
//   init() 折叠为 screencap() 首次调用时的惰性连接）。
//
// 两种连接模式：
//   1. Portal FD：外部（如 xdg-desktop-portal 的 ScreenCast 会话 / KWin 路径）
//      交出 PipeWire fd 与流节点 id
//   2. 会话 daemon：不给 fd，连接当前会话的 PipeWire daemon 并附加到指定节点
//      （如 gamescope 的 Video/Source 节点）
//
// 帧尺寸永远取协商出的流格式，而不采用调用方传入的尺寸。
//
// 线程安全：init()/close()/screencap() 不可并发调用，但 screencap() 可以在
// 与 init()/close() 不同的线程上执行——前提是调用方自行串行化。
// 跨线程状态（latest_frame_、帧尺寸、frame_wanted_）遵循 pw_thread_loop 的
// 纪律：消费侧代码持 loop 锁访问，回调运行在 loop 线程，持锁期间 loop 停摆。
// 握手使用 pw_thread_loop_timed_wait()/pw_thread_loop_signal()。
// ============================================================================

// 注意：编译进库需要在 CMake 配置期找到 libpipewire-0.3 与 libdrm
// （MWS_ENABLE_PIPEWIRE，默认 ON）；本头文件本身只要求 Linux 平台。

#if !defined(__linux__)
#error "PipeWireScreencap is Linux-only"
#endif

#include <atomic>
#include <cstdint>
#include <string>

#include <memory>

#include <spa/utils/hook.h>

#include <pipewire/stream.h>
#include <spa/param/video/raw.h>

#include "maa-capture/Common.h"
#include "maa-capture/Image.hpp"
#include "maa-capture/ScreencapBase.h"

/* Forward-declare PipeWire types (they are C types, not in any C++ namespace) */
struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_buffer;
struct spa_pod;

MWS_NS_BEGIN

class PipeWireScreencap : public ScreencapBase
{
public:
    explicit PipeWireScreencap(int pipewire_fd, uint32_t pipewire_node_id);
    ~PipeWireScreencap() override;

    PipeWireScreencap(const PipeWireScreencap&) = delete;
    PipeWireScreencap& operator=(const PipeWireScreencap&) = delete;

    // 建立连接并等待格式协商完成（超时失败）。重复调用安全。
    bool init();

    void close();
    bool connected() const;

    std::optional<Image> screencap() override;

    void inactive() override;

private:
    /* ---- Internal cleanup ---- */
    void close_internal();

    /* ---- PipeWire ---- */
    bool pw_init();
    bool pw_create_stream();
    bool pw_connect_stream(uint32_t node_id);

    /* ---- PipeWire callbacks (static) ---- */
    static void pw_on_core_error(void* data, uint32_t id, int seq, int res, const char* message);
    static void pw_on_stream_state_changed(void* data, enum pw_stream_state old_state, enum pw_stream_state new_state, const char* error);
    static void pw_on_stream_param_changed(void* data, uint32_t id, const struct spa_pod* param);
    static void pw_on_stream_process(void* data);

    /* ---- Frame processing ---- */
    /* 需持 loop 锁调用: 置位 frame_wanted_ 等新帧, 超时取消请求 */
    bool wait_for_frame_locked(int timeout_sec);
    bool copy_raw_frame(const struct spa_buffer* spa_buf);

    /* PipeWire state */
    int pipewire_fd_ = -1;
    uint32_t pipewire_node_id_ = 0;

    /* ---- Internal state ---- */
    std::atomic<bool> connected_ { false };

    /* PipeWire objects */
    pw_thread_loop* pw_thread_loop_ = nullptr;
    pw_context* pw_context_ = nullptr;
    pw_core* pw_core_ = nullptr;
    pw_stream* pw_stream_ = nullptr;

    /* PipeWire core hook (must outlive pw_core_add_listener) */
    spa_hook core_hook_ = { };

    /* PipeWire stream hook (embedded, must outlive pw_stream_add_listener) */
    spa_hook stream_hook_ = { };

    /* Negotiated frame dimensions (set by param_changed callback) */
    int frame_width_ = 0;
    int frame_height_ = 0;
    /* Negotiated SPA video format */
    enum spa_video_format frame_format_ = SPA_VIDEO_FORMAT_UNKNOWN;

    /* 最新一帧 (协商格式): 仅回调写入; 超时回退时交付它的转换结果 */
    Image latest_frame_;
    /* 帧请求握手: screencap 置位等帧, 回调拷贝成功后复位并 signal */
    bool frame_wanted_ = false;
    /* 连续帧超时计数: 静默死亡的 producer 无事件可依, 只能靠它兜底判定 */
    int frame_timeout_count_ = 0;
};

MWS_NS_END

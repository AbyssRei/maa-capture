#pragma once

// ============================================================================
// Wayland 辅助：协议对象的 std::default_delete 特化
//
// 提取自 MaaFramework source/MaaLinuxControlUnit/Wayland/WaylandHelper.h/.cpp，
// 仅保留截图单元用到的协议对象（去掉输入相关协议与键位修饰符工具）。
//
// 有了这些特化，wl_* / zwlr_* 裸指针即可直接放进 std::unique_ptr /
// std::shared_ptr，析构时自动调用对应的 *_destroy。
// ============================================================================

#if !defined(__linux__)
#error "WaylandHelper is Linux-only"
#endif

#include "maa-capture/Common.h"

#include "Protocol/wlr-screencopy.h"

#include <memory>

#include <wayland-client-protocol.h>

#define DEFAULT_DELETER_FDECL(TypeName) \
    namespace std                       \
    {                                   \
        template <>                     \
        struct default_delete<TypeName> \
        {                               \
            void operator()(TypeName* ptr); \
        };                              \
    }

DEFAULT_DELETER_FDECL(wl_display)
DEFAULT_DELETER_FDECL(wl_registry)
DEFAULT_DELETER_FDECL(wl_output)
DEFAULT_DELETER_FDECL(wl_shm)
DEFAULT_DELETER_FDECL(wl_shm_pool)
DEFAULT_DELETER_FDECL(wl_buffer)
DEFAULT_DELETER_FDECL(zwlr_screencopy_manager_v1)
DEFAULT_DELETER_FDECL(zwlr_screencopy_frame_v1)

MWS_NS_BEGIN

MWS_NS_END

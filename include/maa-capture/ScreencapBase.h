#pragma once

// ============================================================================
// ★ 外部继承接口（截图单元抽象基类）
//
// 提取自 MaaFramework source/MaaWin32ControlUnit/Base/UnitBase.h 的 ScreencapBase。
//
// 这是本库对外提供的「截图功能外部继承接口」之一：
// 外部使用者可以实现自己的截图单元（例如包装私有 API、远程服务、特定设备），
// 然后通过 ScreencapMgr::register_unit 注册进统一的测速选择器。
//
// 使用示例见 sample/external_cpp_interface.cpp
// ============================================================================

#include <optional>

#include "Common.h"
#include "Image.hpp"

MWS_NS_BEGIN

class ScreencapBase
{
public:
    virtual ~ScreencapBase() = default;

public:
    // 截图：返回 BGR 三通道图像（与 MaaFramework 内部约定一致）。
    // 失败时返回 std::nullopt。
    virtual std::optional<Image> screencap() = 0;

    // 释放资源 / 恢复窗口状态（伪最小化等）。可选覆写。
    virtual void inactive() { }
};

MWS_NS_END
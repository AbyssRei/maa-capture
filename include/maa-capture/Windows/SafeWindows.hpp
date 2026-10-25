#pragma once

// ============================================================================
// Safe Windows 头文件包装（替代原 MaaUtils/SafeWindows.hpp）
// 统一控制 windows.h 的编译选项，避免宏污染（min/max）与过旧的 SDK 目标。
// ============================================================================

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10，保证 GetDpiForWindow 等声明可见
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <dwmapi.h>

// GetLastError 等常用别名可继续直接使用；
// 若需要 std::min/std::max，可在此补充 using 声明
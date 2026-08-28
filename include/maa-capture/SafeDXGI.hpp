#pragma once

// ============================================================================
// Safe DXGI 包装头（替代原 MaaUtils/SafeDXGI.hpp）
// ============================================================================

#include "SafeWindows.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201) // nameless struct/union（DXGI 头文件内使用）
#pragma warning(disable : 4458)
#endif

#include <d3d11.h>
#include <dxgi1_2.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
# maa-capture

从 [MaaFramework](https://github.com/MaaXYZ/MaaFramework) 分离出来的**跨平台截图实现**独立库
（Win32 六种方式 + Linux PipeWire / wlr-screencopy + macOS ScreenCaptureKit）。

- 依赖精简：**零第三方依赖**（仅各平台系统 API + C++ 标准库）——图像与颜色转换由内置
  `Image.hpp` 提供（从 OpenCV 源码提取的最小实现），不再依赖 MaaFramework / MaaUtils / OpenCV
- Windows 保留原实现的全部 6 种截图方式与伪最小化机制；
  Linux 移植 PipeWire 与 Wlr(Wayland) 单元，macOS 移植 ScreenCaptureKit 单元
- 对外提供两套「截图功能外部扩展接口」：
  1. **`ScreencapBase` C++ 继承接口** —— 外部实现自己的截图单元并注册进统一测速选择器
  2. **`MaaWSCustomScreencapCallbacks` C 回调接口**（纯 C 头 `MaaWSCustomScreencap.h`）——
     对应 MaaFramework 集成文档中 `MaaCustomControllerCallbacks::screencap` 的"外部宿主实现截图"契约

---

## 目录结构

```text
maa-capture/
├── CMakeLists.txt
├── README.md
├── include/maa-capture/        # 公共头文件（含两个外部接口）
│   ├── ScreencapBase.h               # ★ 外部继承接口：截图单元抽象基类
│   ├── MaaWSCustomScreencap.h        # ★ 外部回调接口：纯 C 契约（对应自定义控制器 screencap）
│   ├── CustomScreencap.h             # 回调接口的 C++ 封装 + 桥接类
│   ├── ScreencapMgr.h                # 截图管理器：注册 + 测速选择 + 截图
│   ├── GdiScreencap.h
│   ├── ScreenDCScreencap.h
│   ├── PrintWindowScreencap.h
│   ├── DesktopDupScreencap.h
│   ├── DesktopDupWindowScreencap.h
│   ├── FramePoolScreencap.h          # Windows.Graphics.Capture（编译期门控）
│   ├── PseudoMinimizeHelper.h        # 伪最小化
│   ├── PrintWindowWithPseudoMinimizeScreencap.h
│   ├── FramePoolWithPseudoMinimizeScreencap.h
│   ├── HwndUtils.hpp / ForegroundUtils.h
│   ├── Common.h / Logger.h / SafeWindows.hpp / SafeDXGI.hpp / NoWarningCV.hpp
│   ├── Linux/                          # Linux 平台单元（PipeWire / Wlr / Wayland 基础设施）
│   │   ├── PipeWireScreencap.h
│   │   ├── WlrScreencap.h / WaylandClient.h / WaylandHelper.h / MemfdBuffer.h
│   │   └── Protocol/wlr-screencopy.*   # wayland-scanner 生成代码（随仓库提供）
│   └── MacOS/ScreenCaptureKitScreencap.h
├── source/                           # Win32 各实现 .cpp
├── source/Linux/                     # Linux 各实现 .cpp（Wayland/ 子目录为基础实施）
├── source/MacOS/                     # macOS 各实现 .mm
└── sample/                           # 两个外部接口的使用示例
    ├── external_cpp_interface.cpp    # 继承 ScreencapBase
    ├── external_c_callback.cpp       # C 回调（对应自定义控制器 screencap），C++ 侧桥接
    ├── external_c_only.c             # 纯 C 消费 C 回调接口
    └── external_c_manager.c          # 纯 C 消费截图管理器 C API
```

## 截图方式

| 方式 | 类 | 说明 | 前台/后台 |
|---|---|---|---|
| `Method_GDI` | `GdiScreencap` | 窗口 DC + BitBlt（Windows） | 前台 |
| `Method_ScreenDC` | `ScreenDCScreencap` | 屏幕 DC + BitBlt（Windows） | 前台 |
| `Method_PrintWindow` | `PrintWindowScreencap`（含活动弹窗合成） | PrintWindow 消息捕获（Windows） | **后台** |
| `Method_DXGI_DesktopDup` | `DesktopDupScreencap` | DXGI 桌面复制（整屏，Windows） | 前台 |
| `Method_DXGI_DesktopDup_Window` | `DesktopDupWindowScreencap` | DXGI 整屏裁剪窗口客户区（Windows） | 前台 |
| `Method_FramePool` | `FramePoolScreencap` | Windows.Graphics.Capture（Win10 1903+） | **后台** |
| `Method_PipeWire` | `PipeWireScreencap` | PipeWire 流截屏：portal fd（KWin/KDE）或会话 daemon 节点（gamescope 等）（Linux） | 后台 |
| `Method_Wlr` | `WlrScreencap` | wlr-screencopy（Wayland shm），适用 wlroots 系合成器（Sway/Hyprland 等）（Linux） | 后台 |
| `Method_ScreenCaptureKit` | `ScreenCaptureKitScreencap` | ScreenCaptureKit 按窗口（CGWindowID）或整屏（macOS 14+，需屏幕录制权限） | 后台 |

> FramePool / PrintWindow 内置 **伪最小化**：窗口最小化时置为透明+点击穿透并以不激活方式还原，
> 截图继续工作；用户激活窗口时自动恢复，退出时还原后再最小化。详见 `PseudoMinimizeHelper`。

预定义组合（与 MaaFramework 的 `Win32 截图方式枚举` 取值一致）：

- `Method_Foreground` = `DXGI_DesktopDup_Window | ScreenDC`
- `Method_Background` = `FramePool | PrintWindow`
- `Method_All`（默认）

## 构建

同时兼容 **Ninja** 与 **Visual Studio（MSBuild）** 两种生成器：CMakeLists 本身与生成器无关，
构建类型默认 Release（单配置生成器走 `CMAKE_BUILD_TYPE`，多配置生成器在构建期用 `--config` 选定）。
零第三方依赖：仅需 CMake + C++20 编译器 / Windows SDK。

### 方式一：Ninja（单配置生成器，构建快）

> Windows + MSVC 需先进入 “x64 Native Tools Command Prompt for VS 2022 / VS 2026”（或先运行
> 对应版本的 `vcvars64.bat`），使 `cl.exe` 与 VS 内置 Ninja 进入环境。
> 预设不绑定具体编译版本——在 VS2026 的 vcvars64 环境中运行即使用 VS2026 工具链（MSVC 14.51）。

```bash
cmake --preset ninja-release          # Ninja + Release，输出 build/
cmake --build --preset ninja-release

cmake --preset ninja-debug            # Ninja + Debug，输出 build-debug/
cmake --build --preset ninja-debug

# 等价命令行：构建类型在配置期决定，无需 --config
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 方式二：Visual Studio（MSBuild 多配置生成器，产出 .sln/.vcxproj）

> 无需 vcvars 环境（CMake 自动定位 MSVC）；架构在 preset 中固定为 x64。
> 同一套构建目录，Release/Debug 在**构建期**切换。VS2022 / VS2026 均可，按本机安装版本选择对应预设。

```bash
# VS2022 -> build-msvc/（含 Debug;Release;RelWithDebInfo;MinSizeRel）
cmake --preset vs2022
cmake --build --preset vs2022-release   # 或 --preset vs2022-debug

# VS2026 -> build-msvc2026/
cmake --preset vs2026
cmake --build --preset vs2026-release   # 或 --preset vs2026-debug

# 等价命令行（以 VS2026 为例）：
cmake -S . -B build-msvc2026 -G "Visual Studio 18 2026" -A x64
cmake --build build-msvc2026 --config Release
```

- **无第三方依赖**：图像与颜色转换由内置 `include/maa-capture/Image.hpp` 提供
  （从 OpenCV 源码提取的 `Mat` 子集语义 + `cvtColor` 子集 + `Rect` 相交），
  链接产物不依赖任何外部 DLL/SO
- `MWS_ENABLE_FRAMEPOOL`（默认 ON）：WGC 截图需要 Windows 10 SDK 10.0.22000+；SDK 不足时头文件自动降级为占位实现
- `MWS_BUILD_SAMPLES`（默认 ON）：构建示例程序
- `MWS_WIN32_COMPATIBLE`（默认 OFF）：旧版 DPI 缩放兼容分支（对应 MaaFramework 的 `MAA_WIN32_COMPATIBLE`）

### Linux / macOS

```bash
# Linux：安装截图单元的系统依赖（缺失时对应单元自动降级剔除，库仍可构建）
sudo apt-get install -y libpipewire-0.3-dev libdrm-dev libwayland-dev
cmake -S . -B build && cmake --build build --parallel    # 产出 build/libmaa-capture.so

# macOS：需 macOS 14+ SDK（ScreenCaptureKit），自动链接系统框架
cmake -S . -B build && cmake --build build --parallel    # 产出 build/libmaa-capture.dylib
```

- `MWS_ENABLE_PIPEWIRE`（默认 ON）：PipeWire 单元，需要 `libpipewire-0.3` 与 `libdrm` 头文件
- `MWS_ENABLE_WLR`（默认 ON）：wlr-screencopy 单元，需要 `wayland-client`
  （wlr-screencopy 协议生成代码已随仓库提供，无需 wayland-scanner）
- 两个单元均未启用时库仍可构建，此时只能通过外部自定义单元截图

各平台 `ScreencapMgr` 的目标语义：

| 平台 | 构造参数 `hwnd` | 内置方式 |
|---|---|---|
| Windows | 目标窗口 `HWND` | GDI / ScreenDC / PrintWindow / DXGI×2 / FramePool |
| Linux | 忽略；经 `set_pipewire_target(fd, node_id)` 或 `set_wlr_socket(name)` 配置 | PipeWire / Wlr |
| macOS | 目标窗口 `CGWindowID`（0 = 整屏） | ScreenCaptureKit |

**整屏截图的显示器选择**（`set_monitor_index(i)`，connect() 前调用）：

- Windows：`Method_DXGI_DesktopDup` 复制的显示器；**0 = 主显示器**，其余按
  `EnumDisplayMonitors` 顺序；设置后优先于 hwnd 定位，越界回退主显示器
- Linux：`Method_Wlr` 截取的 `wl_output`（合成器广播顺序，0 = 第一个）；
  PipeWire 的显示器选择发生在 portal 授权时，不受此设置影响
- macOS：hwnd 为空时 `Method_ScreenCaptureKit` 截取的 display（0 = 第一个）

**查询辅助**（无需 connect）：

- `available_builtin_methods()` / `MaaWSManagerGetAvailableMethods`：本构建在本平台
  可注册的内置方式位掩码（编译期视角；运行时连通性仍由测速决定）
- `ScreencapMgr::monitor_count()` / `monitor_info(i)`（C：`MaaWSManagerGetMonitorCount` /
  `MaaWSManagerGetMonitorInfo`）：显示器枚举，索引语义与 `set_monitor_index` 一致；
  返回主屏标记与设备名（Windows `\\.\DISPLAY2` / macOS 本地化名称 / Linux 空）。
  Linux 需可连接的 Wayland，macOS 需 TCC 屏幕录制权限。

C API 侧对应 `MaaWSManagerCreate(hwnd, methods)` 与 `MaaWSManagerSetPipeWireTarget` /
`MaaWSManagerSetWlrSocket` / `MaaWSManagerSetMonitorIndex`（非对应平台为 no-op）。

## 用法

### 1. 继承接口（C++）

```cpp
#include "maa-capture/ScreencapBase.h"
#include "maa-capture/ScreencapMgr.h"

class MyScreencap : public mws::ScreencapBase
{
public:
    std::optional<mws::Image> screencap() override
    {
        // ... 实现你的截图逻辑，返回 BGR 三通道 mws::Image
    }
    void inactive() override { /* 可选 */ }
};

int main()
{
    mws::ScreencapMgr mgr;
    mgr.register_unit("my-method", std::make_shared<MyScreencap>()); // 与内置方式一起测速
    mgr.connect();            // 预热 + 实测，自动选择最快的截图方式

    mws::Image image;
    mgr.screencap(image);
    mgr.inactive();
}
```

### 2. 回调接口（C，对应 MaaCustomControllerCallbacks::screencap）

```c
#include "maa-capture/MaaWSCustomScreencap.h"

static MaaWSBool my_screencap(void* trans_arg, MaaWSImageBuffer* out)
{
    /* 把截图结果写入 out（BGRA/BGR），成功返回 1 */
}

int main(void)
{
    MaaWSCustomScreencapCallbacks cb = { 0 };
    cb.screencap = my_screencap;
    MaaWSCustomScreencapHandle h = MaaWSCustomScreencapCreate(&cb, my_ctx);
    MaaWSImageBuffer img = { 0 };
    MaaWSCustomScreencapScreencap(h, &img);
    MaaWSCustomScreencapDestroy(h);
}
```

C++ 侧还可通过 `mws::CustomScreencapBridge` 把同一组回调包装成 `ScreencapBase` 单元，
注册进 `ScreencapMgr` 参与测速（见 `sample/external_c_callback.cpp`）。

## 与 MaaFramework 原始实现的对应关系

| 本库 | MaaFramework 原始位置 |
|---|---|
| `ScreencapBase` | `source/MaaWin32ControlUnit/Base/UnitBase.h` |
| 各截图单元 | `source/MaaWin32ControlUnit/Screencap/*` |
| `PseudoMinimizeHelper` | `source/MaaWin32ControlUnit/Screencap/PseudoMinimizeHelper.*` |
| `ScreencapMgr`（测速部分） | `source/MaaWin32ControlUnit/Manager/Win32ControlUnitMgr.cpp` |
| `PipeWireScreencap` | `source/MaaLinuxControlUnit/Screencap/PipeWireScreencap.*` |
| `WlrScreencap` + Wayland 基础设施 | `source/MaaLinuxControlUnit/Screencap/WlrScreencap.*` 与 `Wayland/*`、`Base/MemfdBuffer.*` |
| `ScreenCaptureKitScreencap` | `source/MaaMacOSControlUnit/Screencap/ScreenCaptureKitScreencap.*` |
| `MaaWSCustomScreencapCallbacks` | `include/MaaFramework/Instance/MaaCustomController.h` 的 `MaaCustomControllerCallbacks`（截图部分） |
| `HwndUtils` / `ForegroundUtils` | `source/MaaWin32ControlUnit/Screencap/HwndUtils.hpp` / `Base/ForegroundUtils.h` |
| 依赖工具（Logger/OnScopeLeave/SafeWindows） | `MaaUtils/*`（本库已内置精简版本，便于集成时替换回原版） |

所有提取代码保持与原实现一致的行为，仅替换了命名空间（`MWS_NS_*`）、日志宏与工具头文件。

## 许可

源代码提取自 [MaaFramework](https://github.com/MaaXYZ/MaaFramework)，其开源许可条款适用于对应代码。
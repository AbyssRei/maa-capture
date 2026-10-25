# maa-capture

从 [MaaFramework](https://github.com/MaaXYZ/MaaFramework) 提取的跨平台窗口与屏幕截图库，提供 C ABI、C++ 扩展接口及六种语言绑定。

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture) · [Python 包](https://pypi.org/project/maa-capture/) · [更新日志](CHANGELOG.md)

本库负责截图、图像格式归一化和截图方式测速选择。输入控制、OCR、任务流水线及 Portal 授权界面由调用方实现。

## 平台与分发

| 平台 | 内置方式 | 官方二进制架构 | 环境要求 |
|---|---|---|---|
| Windows | GDI、FramePool、DXGI DesktopDup、DXGI 窗口裁剪、PrintWindow、ScreenDC | x64 | 对应的图形 API、可访问的桌面会话 |
| Linux | PipeWire、wlr-screencopy | x64 | PipeWire/Wayland 运行库及对应视频源或合成器协议 |
| macOS | ScreenCaptureKit | ARM64 | macOS 14+、宿主应用的屏幕录制权限 |

源码可按目标平台编译。上述架构以外的机器需要自行构建并配置对应原生库。Python wheel 内置表中三种原生库，按运行平台选择；Linux 的系统运行库需要另外安装。

图像统一输出 **BGR 三通道**。Windows / macOS 使用系统图形 API；Linux 按启用的后端链接 PipeWire、libdrm、Wayland。内置 `Image.hpp` 替代了原项目中的 OpenCV 图像依赖。

## 快速开始

```bash
pip install --index-url https://pypi.org/simple maa-capture
# 可选的 NumPy 转换支持
pip install "maa-capture[numpy]"
```

Python 需要 3.12+。下面以 Windows 整屏截图为例：

```python
from maa_capture import Manager, available_methods

print(hex(available_methods()))
with Manager(methods=Manager.METHOD_DXGI_DESKTOP_DUP) as capture:
    capture.set_monitor_index(0)
    if not capture.connect():
        raise RuntimeError("没有可用的截图方式，请检查桌面会话和目标")
    image = capture.screencap()
    if image is None:
        raise RuntimeError("本次截图失败")
    print(capture.selected_unit_name(), image.width, image.height, image.channels)
    pixels = image.data  # 已复制的 bytes，可在管理器关闭后保留
```

macOS 使用 `METHOD_SCREENCAPTUREKIT`，`hwnd` 参数承载 CGWindowID，`None` 表示显示器截图。Linux 先配置 PipeWire 目标或 Wayland socket。完整示例见 [Python 文档](bindings/python/README.md)及[截图方式说明](docs/SCREEN_CAPTURE.md)。

## 截图方式怎么选

将需要的 `METHOD_*` / `Method_*` 位掩码按位或组合，`connect()` 会预热、实测并选取最快的可用方式。速度和兼容性随目标应用、驱动及桌面环境变化，不能保证某种方式对所有程序都有效。

- Windows 窗口可优先尝试 `METHOD_BACKGROUND`；需要桌面可见区域时使用 `METHOD_FOREGROUND`。整屏使用 `METHOD_DXGI_DESKTOP_DUP`。
- FramePool 和 PrintWindow 提供伪最小化辅助；它会改变窗口透明度、点击穿透及显示状态，并在停用或关闭时恢复。应用停止绘制时仍可能没有新画面。
- Linux 普通桌面的窗口选择通常通过 ScreenCast Portal 完成；本库消费调用方提供的 PipeWire FD 与节点，不自动创建授权会话。
- macOS 需要宿主应用获得屏幕录制权限。截图本身不需要输入控制使用的辅助功能权限。
- `available_methods()` 表示编译进库的方式，实际可用性由 `connect()` 检查。

详见[完整方式表、权限与范围](docs/SCREEN_CAPTURE.md)。

## C 与 C++ 接入

二进制 SDK 和 `cmake --install` 的输出都包含头文件、原生库与 CMake 包配置：

```cmake
find_package(maa-capture 1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE maa-capture::maa-capture)
```

公共头文件位于 `include/maa-capture/`。C ABI 使用 `MaaWSCustomScreencap.h`，C++ 管理器使用 `ScreencapMgr.h`。C++ 公共接口需要 C++20。

- [集成、图像所有权及生命周期](docs/INTEGRATION.md)
- [多语言绑定和验证范围](bindings/README.md)
- [C / C++ 扩展接口示例](sample/)

项目名、库文件和 CMake 目标使用 `maa-capture`；Python / Rust / Go 模块使用 `maa_capture`；Java 包为 `maa.capture`，Java 类及 C# 命名空间使用合法标识符 `MaaCapture`。C ABI 保留 `MaaWS` 前缀。

## 构建与验证

需要 CMake 3.21+、支持 C++20 的编译器及目标平台 SDK。Windows 建议使用当前 Visual Studio 的 x64 工具链；macOS 需要 macOS 14+ SDK；Linux 可选依赖见[构建文档](docs/BUILDING.md)。

```bash
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /path/to/sdk
```

CMake 目标的编译选项不会修改父项目的全局选项，支持 `add_subdirectory` 接入。作为子项目时默认不构建示例和测试。

```text
maa-capture/
├── include/maa-capture/     # 公共 C / C++ 接口
│   ├── Windows/            # Windows 专用接口和工具
│   ├── Linux/              # PipeWire / Wlr / Wayland
│   └── MacOS/              # ScreenCaptureKit
├── source/                 # 公共管理器和 C ABI 实现
│   ├── Windows/
│   ├── Linux/
│   └── MacOS/
├── bindings/               # Python / Node.js / C# / Go / Java / Rust
├── cmake/                  # 安装后 find_package 配置
├── tests/                  # 原生回归与安装后消费测试
├── scripts/                # 命名、版本和分发包检查
├── sample/                 # C / C++ 扩展接口示例
└── docs/
```

CI 在 GitHub 的 Windows、Linux、macOS runner 上构建，执行原生回归、安装后消费、绑定检查、Python 打包与安装验证，并检查全部发布资产。Windows 额外验证禁用 FramePool 的构建。

## 更多文档

- [截图方式与平台限制](docs/SCREEN_CAPTURE.md)
- [构建、选项和 SDK 安装](docs/BUILDING.md)
- [集成、回调及生命周期](docs/INTEGRATION.md)
- [故障排查与问题反馈](docs/TROUBLESHOOTING.md)
- [版本、令牌配置与发布流程](docs/RELEASING.md)

本项目的设计与平台说明参考 [MaaFramework 文档](https://github.com/AbyssRei/MaaFramework/tree/main/docs)，具体 API、方式位和功能范围以本仓库为准。

## 许可

采用 LGPL-3.0-or-later，详见 [LICENSE](LICENSE)。提取代码来源为 MaaFramework，图像子集实现保留了相关来源说明。

# 故障排查与问题反馈

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture)。

## 无法加载动态库

- 确认 Python 3.12+、宿主与库的 CPU 架构一致。官方原生库为 Windows x64、Linux x64、macOS ARM64。
- Python 查找顺序：`set_lib_path()`、`MAA_WS_LIB` / `MAA_WS_DLL_DIR`、wheel 内置目录、系统搜索路径。
- Linux 即使库文件存在，缺少 PipeWire、libdrm、Wayland 运行库也可能加载失败。
- Windows 检查 DLL 搜索路径及 Visual C++ 运行库；macOS 检查动态库依赖及宿主架构。
- 源码安装 / editable 安装不会自动编译本项目的 C++ 库，应先构建并调用 `set_lib_path()`。

Python 的加载异常会列出尝试路径，反馈时保留这些信息。

## connect() 失败

先输出 `available_methods()`，确认对应后端已编入，再检查目标、会话和权限。它不是运行时成功保证。

- Windows：HWND 是否有效、目标是否仍存在、窗口是否可见或被最小化，尝试另一类截图方式。
- Linux PipeWire：FD 和节点是否来自同一连接、授权会话是否仍存活；不要复用已被 PipeWire 消费的 FD。
- Linux Wlr：Wayland socket 是否正确，合成器是否支持 wlr-screencopy。
- macOS：实际宿主是否获得屏幕录制权限、CGWindowID 是否仍有效。
- 无桌面或远程会话环境下，自定义回调仍可工作，但系统截图可能不可用。

## 黑屏、遮挡、画面不更新或尺寸异常

比较多种方式，不要只比较返回值。桌面复制和屏幕 DC 会反映画面遮挡；后台捕获仍受目标是否继续绘制影响。受保护内容可能返回黑色。

PipeWire 复制协商尺寸的完整流，不按窗口矩形二次裁剪。逻辑窗口尺寸、显示器缩放和流像素尺寸可能不同；流本身的黑边会保留。

`set_monitor_index()` 只影响支持索引的整屏后端，不改变 PipeWire 来源。索引语义见[平台说明](SCREEN_CAPTURE.md)。

## 窗口状态发生变化

FramePool / PrintWindow 的伪最小化会临时修改窗口状态；使用 `with` / `close()` 或显式停用并销毁管理器确保恢复。DXGI 窗口裁剪也可能调整超出显示器的窗口。

## 日志和反馈材料

当前内置日志输出到标准错误流；本库没有 MaaFramework 的工作目录、`debug/maafw.log` 或自动截图存档。需要保存时重定向宿主输出。

在 [Issues](https://github.com/AbyssRei/maa-capture/issues) 反馈时提供：版本、操作系统与架构、宿主语言版本、截图方式位、`available_methods()`、连接及失败日志、最小复现代码，以及可公开的画面说明。截图和日志中应移除与问题无关的个人信息。

回调相关崩溃同时检查结构体、函数对象和像素缓冲的生命周期；详见[集成说明](INTEGRATION.md)。

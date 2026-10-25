# maa-capture

[maa-capture](https://github.com/AbyssRei/maa-capture) 的 Python 绑定
（ctypes，零编译依赖）。wheel 内置三平台原生库，安装即用。

- **Windows**：GDI / ScreenDC / PrintWindow / DXGI DesktopDup（×2）/ FramePool（Windows.Graphics.Capture），带伪最小化后台截图
- **Linux**：PipeWire（portal fd / 会话 daemon 节点，如 gamescope）+ wlr-screencopy（Wayland，Sway/Hyprland 等 wlroots 系合成器）
- **macOS**：ScreenCaptureKit（按窗口 CGWindowID 或整屏，需 macOS 14+ 与屏幕录制权限）

所有内置方式经统一「预热 + 实测」测速自动选择最快可用方案；亦可用回调接口
（对应 MaaFramework `MaaCustomControllerCallbacks::screencap` 契约）注册完全自定义的截图实现。

## 安装

```bash
pip install maa-capture
# 可选：numpy 支持（ImageBuffer.to_numpy）
pip install "maa-capture[numpy]"
```

需要 Python >= 3.9，无其他依赖。

## 快速上手

### 截取整个显示器（三平台）

```python
from maa_capture import Manager

mgr = Manager(hwnd=None)          # Windows: DXGI 整屏；macOS: 整个显示器
mgr.set_monitor_index(0)          # 可选：选择显示器（0 = Windows 主显示器 /
                                  #  Linux 第一个 wl_output /  macOS 第一个 display）
mgr.connect()                     # 预热 + 测速，自动选最快方式
info = mgr.screencap()            # ImageBuffer.Info（BGR 三通道）
print(info.width, info.height, info.channels, len(info.data))
print(mgr.selected_unit_name())   # 例如 "DXGI_DesktopDup" / "ScreenCaptureKit"
mgr.inactive()
```

Windows 上 `set_monitor_index(i)` 设置后优先于 hwnd 定位（截第 i 块屏无需任何窗口）；
Linux/macOS 的索引顺序分别为合成器广播 wl_output 的顺序与 `displays` 顺序。

Linux 上 PipeWire / Wlr 需要在 `connect()` 前配置目标：

```python
mgr = Manager(methods=Manager.METHOD_PIPEWIRE)
mgr.set_pipewire_target(fd=portal_fd, node_id=node)   # portal / KWin 路径
# 或连接会话 daemon（如 gamescope 节点）：
# mgr.set_pipewire_target(fd=-1, node_id=42)
mgr.connect()

# Wayland (wlroots) 整输出：
mgr = Manager(methods=Manager.METHOD_WLR)
mgr.set_wlr_socket(None)          # None = WAYLAND_DISPLAY 默认 socket
mgr.connect()
```

### 截取指定窗口（Windows）

```python
from maa_capture import Manager, find_window_by_title

hwnd = find_window_by_title("记事本")
mgr = Manager(hwnd=hwnd, methods=Manager.METHOD_ALL)
mgr.connect()
info = mgr.screencap()
```

### 自定义截图回调（全平台）

```python
from maa_capture import Manager, ScreencapCallbacks

def my_screencap(arg, out) -> int:
    # 写入 BGRA/BGR 像素到 out.data，设置宽高通道步长；成功返回 1
    ...

mgr = Manager(methods=Manager.METHOD_NONE)
mgr.register_custom_unit("my-unit", ScreencapCallbacks(screencap=my_screencap))
mgr.connect()
info = mgr.screencap()
```

### numpy 转换

```python
buf = ...  # ImageBuffer 实例（回调参数）
arr = buf.to_numpy()   # np.ndarray, shape=(H, W, 3), dtype=uint8, BGR
```

## 原生库加载顺序

1. `set_lib_path(path)` 显式指定（文件或目录）
2. 环境变量 `MAA_WS_LIB`（旧名 `MAA_WS_DLL_DIR` 等价）
3. wheel 内置的当前平台原生库
4. 系统库搜索路径

## 许可

LGPL-3.0-or-later（随库提取自 [MaaFramework](https://github.com/MaaXYZ/MaaFramework)），见 `LICENSE`。

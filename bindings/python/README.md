# maa-capture Python

[maa-capture](https://github.com/AbyssRei/maa-capture) 的 ABI 2 绑定，Python 3.12+，仅依赖标准库。

```python
from maa_capture import Manager, MonitorTarget, monitors, runtime_info

print(runtime_info())
items = monitors().items
if not items:
    raise RuntimeError("没有显示器")
with Manager(target=MonitorTarget(items[0].monitor_key)) as capture:
    if not capture.connect():
        raise RuntimeError(capture.last_error())
    frame = capture.screencap()
    if frame is None:
        raise RuntimeError(capture.last_error())
    print(frame.image.width, frame.source.desktop_rect, frame.source.layout_after)
```

`MonitorTarget` 必须来自设备 key；`WindowTarget(window_id, platform, process_id=0)`
显式区分 Windows(1) 与 macOS(2)。Linux 使用
`PipeWireTarget(fd, node_id, scope=4)` 或 `WlrTarget(socket_name="", output_index=0)`。
创建后更换来源需要关闭并新建管理器。methods=0 表示按目标自动选择，
显式方法中任何不可用或跨范围位都会失败。

自定义来源：

```python
from maa_capture import Manager, CustomTarget, ScreencapCallbacks, provider_frame

def capture():
    return provider_frame(source_key="example", data=bytes([10,20,30,255]),
                          width=1, height=1)

with Manager(target=CustomTarget("example")) as manager:
    callbacks = ScreencapCallbacks(capture)
    if not manager.register_custom_unit("example", callbacks) or not manager.connect():
        raise RuntimeError(manager.last_error())
    frame = manager.screencap()
```

回调异常转换为失败并记录日志。像素在回调释放前复制；返回帧与来源元数据可独立保留。
`close()` 可重复，关闭后操作抛 RuntimeError；`inactive()` 释放当前资源，再次使用须 connect。

`set_lib_path(path)` / `MAA_WS_LIB` 指定文件或目录；`lib_path()` 报告已知实际路径，
系统搜索无法定位时为 None。加载首先验证 ABI 2，再配置所有必需符号；混装直接拒绝。
不再提供 DLL 旧别名、hwnd 推断目标或分步来源 setter。

聚合 wheel 内置 Windows x64、Linux x64、macOS ARM64 原生库及对应构建清单。
其他架构必须明确提供匹配构建；安装成功不等于该平台有可运行的原生库。
`monitors()` 在 Linux 抛不支持错误；macOS 枚举错误不会冒充成功的空列表。

诊断：`python sample/diagnose_capture.py --list-monitors`；
`--target monitor --key <key> --samples 5` 输出 JSON。
原生捕获在子进程运行，`--timeout` 控制硬超时；
仅显式指定 `--output`、`--image-output`、`--log-output` 时写文件。
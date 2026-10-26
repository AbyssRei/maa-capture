# maa-capture

跨平台原生截图 SDK，提供 C/C++ 和六语言绑定。当前源码版本为 **2.0.0，ABI 2**；
所有使用 ABI 1 的消费者必须升级绑定并重新编译，不能直接替换历史 DLL。

- Windows：严格按显示器设备 key 捕获；支持窗口 GDI、FramePool、DXGI 裁剪、PrintWindow、ScreenDC。
- macOS 14+：ScreenCaptureKit 显示器/窗口捕获，按过滤器的 point/pixel 比例请求原生网格。
- Linux：延续外部授权的 PipeWire 来源和 Wlr 会话输出；稳定显示器身份查询返回不支持。
- C++、Python 提供完整快照、能力、错误及帧接口；其余绑定当前声明 Windows x64 的基本目标、回调、取帧和关闭接口。

```python
from maa_capture import Manager, MonitorTarget, monitors

snapshot = monitors()  # 枚举失败抛 CaptureException
primary = next((item for item in snapshot.items if item.primary), None)
if primary is None:
    raise RuntimeError("没有主显示器")
with Manager(target=MonitorTarget(primary.monitor_key)) as capture:
    if not capture.connect():
        raise RuntimeError(capture.last_error())
    frame = capture.screencap()
    if frame is None:
        raise RuntimeError(capture.last_error())
    print(frame.image.width, frame.image.height, frame.source.source_key)
```

目标必填且创建后不可变。省略 methods 时只选择目标范围内已编入的方法；
显示器目标不接受窗口方法，不提供来源回退或索引选屏。
帧独立拥有 BGR 像素和来源信息；Python 返回可保留的 bytes 与冻结元数据。
布局或来源改变时丢弃帧并返回结构化错误。

[构建](docs/BUILDING.md) · [集成契约](docs/INTEGRATION.md) ·
[ABI 2 迁移](docs/API_MIGRATION.md) · [平台几何](docs/SCREEN_CAPTURE.md) ·
[验证状态](docs/CAPTURE_SOURCE_VALIDATION.md) · [发布](docs/RELEASING.md)

完整源码分发物为 `maa-capture-v<version>-source.tar.gz`；
Python sdist 只代表 Python 分发范围。构建清单记录提交、工具链、选项和原生哈希；
Release 的 provenance 与 SHA256SUMS 关联全部资产。

当前实现与实机验收是独立状态。macOS、跨 GPU、旋转及插拔验收尚未在本次 Windows
工作环境完成，详见验证记录。许可证：[LGPL-3.0](LICENSE)。
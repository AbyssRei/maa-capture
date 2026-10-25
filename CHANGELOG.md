# 更新日志

版本遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)，
提交遵循 [Conventional Commits 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/)。
以下版本属于 maa-capture 的重新编号，旧项目已发布的发行版不覆盖、不重新发布。

## 1.0.0

- 确立跨平台 C ABI、C++ 扩展接口和多语言绑定的首个稳定版。
- 库、头文件路径、包名、命名空间和文档统一使用 maa-capture。
- 最新稳定依赖按兼容范围或上游浮动引用解析；Java 构建时获取最新 JNA。
- Windows、Linux、macOS CI 验证原生库、六语言绑定和 Python 原生回调。
- 分支推送与版本标签分开推送；开发版本标签只作为历史标记导入。

## 0.4.0

- 查询本构建可用的内置截图方式。
- 枚举显示器，查询主屏标记与设备名称。
- macOS 显示器名称按 displayID 匹配 NSScreen.localizedName。

## 0.3.0

- Windows、Linux Wlr 与 macOS 的整屏截图支持显示器索引选择。

## 0.2.0

- Python 绑定改为标准 src 包，提供跨平台库加载与内置原生库分发。
- 增加 wheel / sdist 构建、安装验证和可选 PyPI 发布。

## 0.1.2

- 无边框全屏跳过条件收紧为窗口与显示器矩形在容差内重合。
- 过大的窗口继续执行尺寸限制与归位，避免 Desktop Duplication 裁剪越界。
- 增加三平台原生构建、多语言编译检查及标签发布工作流。

## 0.1.1

- 使用完整显示器矩形处理窗口尺寸，保留无边框全屏窗口的几何信息。

## 0.1.0

- 合并初始实现与 Linux / macOS 截图实现，建立跨平台库。
- 提供 Win32 六种截图方式、Linux PipeWire / Wlr 与 macOS ScreenCaptureKit。
- 提供 C++ 继承、C 回调、统一测速管理器和六种语言绑定。

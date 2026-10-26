# 截图来源实施验证记录

日期：2026-10-26（Asia/Shanghai）。状态：Windows 本机部分验收；未发布。

本轮已实现 ABI 2 统一目标/帧/回调、显示器快照、Windows 严格来源、macOS 原生像素路径、
六语言迁移、子进程诊断和发布资产流程。原计划保留为设计基线，不能将本记录视为
P0—P6 全矩阵完成证明。

## 本机环境与构建

- Windows 11 专业工作站版，10.0.26300，x64。
- MSVC 19.51.36257，Windows SDK 10.0.26100.0；FramePool 开启/关闭均测试。
- GPU：NVIDIA RTX 4070 Laptop（32.0.16.1742）、AMD Radeon 610M（32.0.21045.16010）；
  存在 GameViewer 虚拟显示适配器。本记录未据此推断两台屏幕必定分属不同 GPU。
- 原生版本：2.0.0，ABI 2；源码基线 099434c870e9db021245863bcd7c50fa85bd5843，工作树实现带 -dirty。
- 2026-10-26 本机验收候选 SHA-256：
`663f771a527dfc1950218d150ebf60a37188abac030ae49130f3b247250386e4`。
  后续重新链接时以 build/maa-capture-build-Release.json 为准。

## 已执行

- 默认和关闭 FramePool：各 2 项原生 CTest 回归通过。
- 拓扑 provider：同名屏、重排、负坐标、旋转/比例字段、歧义、空列表、变化重试、失败区分。
- 严格来源/帧：错误 provider 不发布帧，丢失 monitor key 不回退，独立帧跨取图及销毁管理器保留，
  填充 BGRA 转 BGR，声明长度/步长错误、结构大小及无效范围诊断。
- Python：8 项回归，含冻结数据、异常、close/with、ABI/缺失符号拒绝和枚举。
- Node.js/C#/Java/Rust/Go cgo：新 ABI 回调、取帧、关闭运行验证；Node TypeScript 类型检查。
  Go 无 cgo 编译检查通过。其余五语言只声明 Windows x64。
- Windows 双屏：主屏 100%，左侧副屏 150%；按各自设备 key 取帧，实际身份与请求匹配。
- 每屏分别验证 GDI、FramePool、DXGI 窗口裁剪、PrintWindow、ScreenDC 的 320×240
  原生像素四象限色块，检查 BGR 颜色、方向和内容边界；整屏画面上的相同区域也通过。
- 每屏 20 次 PrintWindow 最小化/恢复循环，重复 inactive 后样式与最小化意图恢复。
- 安装后的 find_package(maa-capture 2) 消费者构建/运行通过。
- 从独立本地提交候选创建的完整源码包，已在独立临时目录重建并通过原生/Python 回归及安装后消费；
  从该候选打包的 Windows SDK 解压后，C/C++ 样例构建/运行通过。该隔离提交仅用于本地验证，
  不等同于原仓库已提交或 Release 已生成。
- 3 项来源清单回归通过，验证哈希图、资产篡改、提交错配与缺失资产拒绝。
- Windows Python 3.12.14 从实际构建的 wheel 安装后 8 项回归通过；本地该 wheel 仅装入 Windows 库，
  不等同于三平台聚合 wheel 验收。

五帧独立诊断采样（含布局核验，不代表吞吐基准）：

| 屏幕 | 像素网格 | 桌面原点 | 帧数 | p50 / p95 |
|---|---|---|---|---|
| 0 | 1920×1080 | (0, 0) | 5 | 32.18 / 36.53 ms |
| 1 | 2560×1440 | (-2560, 0) | 5 | 66.91 / 67.54 ms |

色块输出仅包含测试程序自己的窗口：build/validation-colors/monitor-0.ppm、monitor-1.ppm。
原始诊断 JSON 位于 build/monitor-0.json、monitor-1.json；日志位于 build/desktop-validation.log。
这些本地产物不随源码分发。复现：

```powershell
cmake -S . -B build -A x64 -DMWS_BUILD_DESKTOP_TESTS=ON
cmake --build build --config Release --parallel
./build/Release/maa_capture_desktop.exe --output build/validation-colors
```

循环暴露并修复了预恢复几何拒绝、跨线程窗口样式恢复死锁及 WinRT 工厂缓存失效崩溃。
伪最小化恢复改为调用线程执行；用户重新激活后的恢复发生在下一次截图/停用，
不再由后台线程同步改动宿主窗口。FramePool 使用专属 MTA 工作线程；没有新帧时返回超时，
不使用缓存像素冒充新捕获。

## 后续实机验收与调用方交接项目

- macOS 原生编译与 Retina/普通屏像素、边框/阴影、TCC 拒绝/撤销/恢复、迟到回调实机检查。
- Linux 带依赖的 PipeWire/Wlr 原生构建与授权来源截图运行；Wlr 方向标志及
  Portal FD 消费后禁止切换到 daemon 的修复尚无实机结果。
- 插拔、主屏切换、镜像、旋转屏、明确跨 GPU 输出及 Windows 125%+200% 组合。
- 全平台连续资源/CPU/内存对比；当前色块和恢复循环不替代资源泄漏长期测量。
- 三平台远端 CI、最终聚合 wheel、完整 Release 资产及真实提交的来源哈希交叉核验。

本机无法替代以上验收。来源/布局核验是前后采样，不声称原子桌面事务；
暂未实现的旋转或不可表达的跨屏几何明确返回不支持。

## 本次提交与发版顺序

按用户授权，本次以 Windows 本机回归和 GitHub Actions 全平台构建/资产验证作为发版门槛；
不使用 WSL 验证 Linux 构建。先分组提交实现并推送 main，全部 CI 通过后再独立提交版本提升，
推送并验证版本提交，最后单独推送 v2.0.0 标签。实机未验证的项目继续保留，不视为已通过。

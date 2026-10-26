# 截图来源、显示器身份与原生像素实施计划

编写日期：2026-10-26。状态：实施中；当前验证状态见 [CAPTURE_SOURCE_VALIDATION.md](CAPTURE_SOURCE_VALIDATION.md)。

基线：本仓库 `099434c`，CMake 与 Python 包版本均为 `1.0.0`。需求来源为
`nightreign-overlay-helper` 的 `docs/development/maa-capture-migration-plan.md`，重点对应其
第 2.2、3、6.2、7、8、10 节。本文件把调用方需求转化为截图库工作，不代表接口已提供、
桌面验收已完成或新版本已发布。设计约束：本轮允许破坏现有 API/ABI，直接统一管理器、
目标和帧模型，不保留兼容分支。下文接口签名和新类型均为拟实施设计。

## 1. 交付目标与边界

本轮交付以下能力：

1. Windows/macOS 的一次性显示器枚举快照，包含可匹配的设备身份和明确的图像几何。
2. 按设备身份严格指定显示器；来源丢失或改变时失败，不发布其他屏幕的图像。
3. Windows/macOS 默认输出原生像素，并描述内容边界、坐标单位及像素映射。
4. 每帧绑定实际来源和布局信息，调用方无需读取绑定私有字段。
5. 通用方法范围查询、结构化错误和原生运行时信息。
6. 统一接口回归、六语言绑定迁移、完整源码资产及二进制构建来源记录。

先实现 C ABI、C++ 和 Python，再把 Node.js、C#、Go、Java、Rust 的管理器、回调和示例
迁移到同一代 C ABI。发布时六语言绑定都必须能使用声明范围内的基本截图和生命周期
接口；显示器完整查询等辅助封装可分批补齐，并在绑定文档列明。不得留下仍调用旧签名的
绑定，也不得把现有 Windows 绑定顺带宣称为跨平台支持。

Linux 的来源配置和帧输出同步迁移到统一接口，保留 PipeWire/Wlr 已有捕获能力及测试，
补充通用能力报告和明确的不支持结果。本轮不扩展 Portal 授权、Wlr 热插拔或 Linux 稳定
设备身份。Windows/macOS 专用功能在 Linux 返回不支持，不能伪造成功或把缺少权限报告
为“成功枚举到零台设备”。

游戏进程识别、程序列表、窗口随程序选屏策略、Qt/QImage、配置迁移、标定、HDR 业务处理、
前台检测门控、应用发布版本及应用 PyInstaller 配置均由调用方负责。

## 2. 已确认的实现缺口

| 位置 | 当前事实 | 实施含义 |
|---|---|---|
| `include/maa-capture/ScreencapMgr.h` | `MonitorInfo` 只有 `primary/name` | 直接重构为完整快照与统一目标模型 |
| `source/Windows/Win32MonitorUtils.hpp` | 每次查询重新枚举，仅返回显示设备名 | 统一生成身份和几何快照，名称不能充当永久身份 |
| `source/Windows/DesktopDupScreencap.cpp` | 索引解析失败、输出查找失败均可能尝试回退 | 直接删除两处隐式回退，并绑定实际 DXGI 输出 |
| `source/MacOS/ScreenCaptureKitScreencap.mm` | count/info/capture 分别取共享内容；窗口输出尺寸直接取窗口 frame | 快照避免逐项枚举错配；默认使用原生像素并明确内容边界 |
| `include/maa-capture/MaaWSCustomScreencap.h` | 图像结构不含来源，失败主要返回 bool | 直接调整截图与回调契约，统一为帧和结构化错误 |
| `bindings/python/src/maa_capture/_core.py` | 图像已复制；close/with 已实现 | 复用清理机制，screencap 直接返回完整帧 |
| `source/ScreencapMgr.cpp` | 已按掩码惰性构造并停用未选中者 | 在现有管理器内重构，保留优化并扩充回归 |
| `.github/workflows/build.yml` | SDK 资产没有完整 C++ 源码；聚合 wheel 只在 Linux 安装冒烟 | 新增源码资产，并在三平台安装最终 wheel 验证 |

## 3. 统一接口与重构策略

### 3.1 重构原则

- 允许调整 C 函数签名、结构体、C++ 类布局/虚函数及语言绑定返回类型；所有消费者重新
  编译或升级。不编写旧头文件/旧二进制兼容测试，不保留旧原生库运行分支。
- 直接改造现有 `ScreencapMgr`、`ScreencapBase`、各平台后端和 C 门面。删除为双轨运行
  设置的 `CaptureSession`、Ex 接口及兼容路由设计，单一管理器完成全部捕获。
- 每次创建都显式指定目标；按目标导出截图范围，默认按范围筛选并测速。方法位值可以
  沿用以减少无关改动，但不作为兼容承诺；默认不再使用跨范围的 `METHOD_ALL`。
- Windows/macOS 显示器捕获必须绑定设备身份，目标消失即失败；删除隐式主屏回退和不严格模式开关。
- Windows/macOS 默认输出原生像素，不提供旧低分辨率模式或 legacy 分辨率选项；
  调用方需要缩放时自行处理，库返回实际像素映射。
- C/Python/C++ 统一使用包含像素和来源信息的帧对象，不保留仅返回旧图像结果的重载。
- 同一管理器仍由调用方串行访问；本库不引入 Qt、GUI 调度或全局串行锁。

### 3.2 单一目标与帧模型

创建参数采用带标签的 `CaptureTarget`，区分 `monitor/window/pipewire/wlr/custom`。
Windows/macOS 不再通过同一个 `hwnd` 参数推断整屏或窗口，也不通过连接前多个 setter
拼装来源。所有平台来源配置统一进入创建选项，连接后保持不可变；更换目标须关闭后重建。

直接重构 `ScreencapBase::screencap()` 为返回完整帧/结构化失败的契约，使平台后端能在
实际取图时填写来源元数据；C++ 类布局无需冻结，也不强制为历史布局引入 PIMPL。
C 门面的 `ManagerHandleImpl` 只持有一个管理器实现，负责句柄边界和所有权转交。

自定义来源同步采用新回调协议：注册时声明来源 key、范围和已知能力，每帧返回像素及
实际来源/几何。来源未知时只允许 `custom` 目标并设置元数据有效位；不能混入严格
screen/window 候选。自定义来源的身份由 provider 报告，标记验证级别，不能声称已由
操作系统核验。回调结果在有效期内复制，不要求调用方分配库内部帧句柄。

### 3.3 拟实施接口清单

下列现有函数名将采用新签名；名称相同不表示能够加载旧 SDK 或旧动态库。

| 层 | 接口/类型 | 契约 |
|---|---|---|
| 通用 | `MaaWSGetAbiVersion` | 无参数返回整数 ABI 代际，作为绑定加载后的首个校验入口 |
| 通用 | `MaaWSGetRuntimeInfo`、`MaaWSGetApiFeatures` | 返回原生版本、构建提交、ABI 代际和功能位 |
| 通用 | `MaaWSGetMethodCapabilities` | 查询单个方法位的范围、平台和条件，不代表运行时可截图 |
| C | `MaaWSMonitorSnapshotCreate/Destroy` | 独立不可变快照；创建失败有明确错误 |
| C | `MaaWSMonitorSnapshotGetCount/GetInfo` | 从同一快照读取条目和布局 token，不重新枚举 |
| C | `MaaWSManagerCreate` | 接收显式目标和方法选项；创建失败通过输出错误报告 |
| C | `MaaWSManagerScreencap` | 一次返回持有像素和来源信息的帧句柄 |
| C | `MaaWSFrameGetImage/GetInfo/Destroy` | 像素和元数据属于同一帧，直到帧销毁前有效 |
| C | `MaaWSManagerGetLastError` | 读取最近一次失败的结构化错误；查询本身不清除错误 |
| C++ | `MonitorSnapshot`、`CaptureTarget`、`CaptureOptions`、`CaptureFrame`、`ScreencapMgr` | 统一 RAII 接口，直接替换原图像/目标契约 |
| Python | `monitors()`、`method_capabilities()`、`runtime_info()` | 返回已复制数据，不暴露 ctypes 私有句柄 |
| Python | `MonitorTarget/WindowTarget/...`、`Manager(target=..., methods=...)`、`screencap()`、`last_error()` | 必填目标；截图统一返回完整帧或失败 |

新 C 结构使用固定宽度整数、明确的枚举值及 `struct_size`，运行时报告 ABI 代际以检测
包和原生库混装，不据此支持历史结构布局。字符串统一 UTF-8，
长度单位为字节，规定是否含 NUL；返回视图不得超过所属快照/帧/管理器的生命周期。
未知选项、ABI 代际不符、尺寸不足、无效指针参数必须可诊断，C 边界不得泄漏 C++ 异常。

## 4. 数据模型与关键不变量

### 4.1 显示器快照

| 字段 | 语义 |
|---|---|
| `layout_token` | 当前进程内相关布局状态的标识，只用于一致性判断，不持久化为硬件身份 |
| `index` | 当前快照的展示编号；顺序写入平台文档，不能跨快照当作身份 |
| `monitor_key` | 带平台前缀的设备身份，供目标匹配 |
| `identity_scope` | `persistent/session/ambiguous`，明确重启和重连后能否复用 |
| `native_id` | 本次会话的平台句柄或显示器编号，用于关联原生 API，不保证持久 |
| `name/primary` | 展示名称与主屏标记；不承担身份判断 |
| `desktop_rect` | 有符号桌面矩形，允许负坐标；同时给出单位、轴方向和坐标空间 |
| `pixel_width/pixel_height` | 当前显示配置对应的捕获像素网格尺寸；不是面板标称最高分辨率 |
| `scale_x/scale_y/rotation` | 可证明的像素比例和旋转；不可得时以有效位声明缺失 |

Windows 使用系统显示配置关联设备路径、HMONITOR 和 DXGI 输出；`DISPLAYn` 只作展示。
macOS 关联原生 display ID 与可用的设备 UUID。镜像输出、远程显示器及身份缺失时明确
标记歧义或会话级身份，不按名称/分辨率猜测唯一性。

快照在一次系统枚举结果上组装。系统调用期间检测到布局变化时有限重试，仍不一致返回
`LAYOUT_CHANGED`。token 覆盖身份、位置、尺寸、缩放、旋转和主屏等相关变化，不使用
墙钟时间冒充布局版本，也不承诺跨进程相同。

Windows 的物理坐标查询使用明确的 DPI 上下文，并恢复调用线程原状态，不修改宿主进程
的 DPI awareness。macOS 保留系统桌面 point 与实际像素的区别；不构造虚假的跨屏统一
物理像素平面，不引用 Qt 的最大 DPR 映射。

### 4.2 来源选项

`CaptureOptions` 包含必填 `target` 和方法选择。目标使用带标签的结构/联合类型，
不能同时设置窗口、显示器和视频源；其区域由目标及 provider 能力导出，不重复保存
一个可能冲突的可写 `region` 字段。

| 目标类型 | 必需数据 | 捕获范围/约束 |
|---|---|---|
| `monitor` | Windows/macOS `monitor_key` | screen；必须唯一匹配当前设备 |
| `window` | 平台及 HWND/CGWindowID | window；核对本次会话的窗口与进程身份 |
| `pipewire` | FD/node ID 和来源声明 | source-defined；延续外部授权及 FD 所有权要求 |
| `wlr` | socket 和本次会话的 output 选择 | screen；本轮只验证会话选择，不承诺持久设备身份 |
| `custom` | 注册的 provider/source key | provider 声明的范围及元数据有效位 |

- monitor/window 目标始终严格匹配，无来源回退开关；monitor 不接受索引作为独立身份。
- 必填字段缺失、目标类型与平台不符或字段冲突在创建/连接前失败。
- 自动方法选择使用目标范围与构建能力的交集；交集为空时报告 `METHOD_UNAVAILABLE`。
  显式方法选择先逐项验证，不支持或跨范围的项直接报告错误，不静默删除后再换方法。
- Windows/macOS 始终请求原生像素；无法满足时返回错误，不放大低分辨率帧替代。
  PipeWire/custom 的实际网格由来源提供，报告 provider-defined，不能凭此承诺面板原生分辨率。
- 用窗口定位屏幕由调用方先解析显示器身份，再创建 monitor 目标。
- 删除 `set_monitor_index` 等分步来源 setter。配置随管理器创建固定，更换来源重建。

### 4.3 帧信息

新帧至少绑定：`frame_id`、捕获单调时钟时间、实际方法、实际区域、实际设备/窗口身份、
捕获前后核验的 `layout_token`、内容矩形、坐标空间、像素尺寸、像素映射和已知限制。

元数据必须来自实际使用的输出/过滤器及像素缓冲，不得截图后独立重新枚举再猜测来源。
C 帧句柄独立持有像素；后续取图不能使尚未销毁的帧失效。Python 复制像素
和元数据后在 `finally` 中销毁原生帧，继续提供可独立保留的 bytes。

捕获前解析预期身份，取图后重新验证相关身份和几何。检测到变化即丢弃结果并返回
`LAYOUT_CHANGED/TARGET_CHANGED`，由调用方重试。该检查不能保证发现两次采样之间所有
瞬态事件；报告必须如实区分“完成一致性核验”与“系统提供原子桌面事务”。

### 4.4 能力与错误

| 方法 | 支持区域 | 说明 |
|---|---|---|
| GDI / FramePool / DXGI 窗口裁剪 / PrintWindow / ScreenDC | window | ScreenDC 不提供任意显示器整屏能力 |
| DXGI DesktopDup | screen | 按设备身份严格选择输出 |
| ScreenCaptureKit | screen / window | 由目标类型决定过滤器 |
| Wlr | screen | 会话级 output 选择；本轮不承诺持久设备身份 |
| PipeWire | source-defined | 范围由授权的视频源决定 |
| 自定义来源 | provider-defined / unknown | 新回调携带来源声明；缺失数据须标明有效位 |

方法集合不单独作为原生方法查询。自动选择由目标派生，不依赖跨范围的 All。
运行时能力判断仍需 connect 和真实取帧，不能把“编入后端”解释为“当前权限和目标可用”。

错误类别至少覆盖 `INVALID_ARGUMENT`、`INVALID_STATE`、`UNSUPPORTED_OPTION`、
`METHOD_UNAVAILABLE`、`TARGET_NOT_FOUND`、`TARGET_AMBIGUOUS`、`SOURCE_MISMATCH`、
`LAYOUT_CHANGED`、`TARGET_CHANGED`、`PERMISSION_DENIED`、`TIMEOUT`、
`UNSUPPORTED_GEOMETRY`、`CAPTURE_FAILED`、`INTERNAL_ERROR`。
同时携带操作阶段、方法、原生错误域/错误码和简短描述。系统未提供明确权限证据时保留
原生错误，不能把所有枚举失败归类为权限拒绝。

## 5. 分阶段实施

### P0：确定统一契约与迁移清单

改动/新增：

- `docs/INTEGRATION.md`：记录新的目标、帧、回调、错误与生命周期契约。
- 拟新增 `docs/API_MIGRATION.md`：按旧入口列出替代调用，供消费者重编译/升级。
- `include/maa-capture/ScreencapBase.h`、`ScreencapMgr.h`、`MaaWSCustomScreencap.h`：确定统一签名。
- `sample/`、`tests/`、六语言绑定：列出所有受影响调用，禁止遗漏仍使用旧 ABI 的示例。

工作：

1. 固定第 3、4 节的数据所有权、枚举值、错误、回调协议和配置验证规则。
2. 建立 C/Python/C++ 的最小消费样例，验证新声明的布局与导出一致；这项检查面向
   本次发布的 SDK 和原生库，不执行历史 ABI 消费。
3. 制定迁移表：hwnd/索引/setter → 显式目标，ImageBuffer.Info → CaptureFrame，
   默认 ALL → 按目标自动选择，低分辨率窗口输出 → 原生像素，回调 → 带来源的 FrameInput。
4. 确定 ABI 代际为 2；绑定及 C/C++ 示例在调用有变化的签名前验证代际。缺少运行时
   查询符号或代际不符立即报告版本不匹配，不尝试回退运行旧接口。

完成条件：统一契约、迁移清单和最小样例齐全；阶段代码骨架能够构建，未完成能力不报为可用。

### P1：快照、公共类型和 Windows 显示器身份

改动/新增：

- 拟新增 `include/maa-capture/CaptureTypes.h`、`source/MonitorSnapshot.cpp`。
- `include/maa-capture/ScreencapBase.h`、`ScreencapMgr.h`、`source/ScreencapMgr.cpp`。
- `include/maa-capture/MaaWSCustomScreencap.h`、`source/CustomScreencap.cpp`。
- `source/Windows/Win32MonitorUtils.hpp`；拟新增内部 Windows 显示配置实现。
- 拟新增 `tests/monitor_snapshot.cpp`、`tests/capture_contracts.cpp`；`CMakeLists.txt`。

工作：直接替换目标/帧类型并适配各后端签名，实现快照生命周期、方法范围表和错误载体；
同步调整 C 门面、C/C++ 示例及原生测试，确保核心实现与消费者使用同一套签名。提供 Windows 同源枚举，
建立设备路径/HMONITOR/DXGI 输出关联。快照可独立创建，不触发截图测速或窗口状态修改。
系统查询封装成内部可替换 provider，供无桌面的拓扑测试使用，不暴露为生产 API。

完成条件：同名同分辨率显示器可区分；重排不改变持久设备匹配；失败与空列表可区分；
快照读取不重新枚举；负坐标、旋转、缩放、镜像歧义和枚举期间变化测试通过。

### P2：Windows 严格捕获与同帧元数据

改动/新增：

- `source/ScreencapMgr.cpp`、`source/CustomScreencap.cpp` 和统一 C 接口。
- `source/Windows/DesktopDupScreencap.cpp`、`DesktopDupWindowScreencap.cpp` 及对应头文件。
- 其他 Windows 后端和 `ScreencapBase`：直接增加来源状态与帧元数据，不复制一套严格后端。
- 拟新增 `tests/strict_source.cpp`、`tests/capture_frame.cpp`。

工作：

1. 整屏捕获按 `monitor_key` 找当前输出，记录预期身份；删除索引选屏和两处主屏回退，
   目标消失、歧义或输出解析失败即结束。
2. D3D 设备与选定输出适配器匹配；验证跨 GPU 副屏，不能固定在默认 GPU 后假定成功。
3. 每帧核对实际输出及相关几何；处理访问丢失、分辨率变化和设备重建。即使索引仍合法，
   也不得接受错误身份。
4. 实现新帧所有权、带符号原点、方向和步长检查。旋转屏必须输出已声明方向的画面；
   暂不支持的方向返回不支持，不配错误矩形。
5. 窗口目标只使用同区域的方法候选，捕获前后核对窗口有效性、进程身份和内容几何。
   可见桌面裁剪方式记录实际输出；无法满足内容映射的跨屏窗口返回明确错误。
6. 纹理映射有效期间完成像素复制；部分连接失败、测速全失败和析构均恢复临时窗口状态。

完成条件：错误来源测试必须验证没有发布帧，不能只检查返回尺寸；管理器只存在一套目标
与帧处理流程，未修改的正确捕获/清理行为仍有回归覆盖。

### P3：macOS 快照与原生像素

依赖 P1 的数据模型和 P2 的帧契约；Windows 实机结果不替代 macOS 验收。

改动/新增：

- `source/MacOS/ScreenCaptureKitScreencap.mm` 及对应头文件：直接改造过滤器、原生尺寸和帧输出。
- 拟新增 `source/MacOS/MonitorSnapshot.mm`。
- 管理器的类型化目标路由、CMake 平台源列表；拟新增 macOS 几何与超时回归。

工作：

1. 单次共享内容枚举生成快照，按原生 display ID 关联身份、桌面 point 和像素网格；
   不假设索引 0 为主屏，不依靠屏幕名称匹配。
2. monitor 目标按身份选择显示器；window 目标使用明确的窗口 ID，不再用 hwnd=0 推断目标。
3. 默认根据经过 SDK 文档及实测确认的过滤器内容范围、像素比例设置原生输出，
   以实际像素缓冲核验结果；不得把低分辨率帧放大后标记为原生像素。
4. 固定内容边界/阴影策略并写入元数据；以色块核验原点、方向及边框覆盖。
5. 混合 DPR 跨屏窗口若不能用当前元数据无损描述，返回 `UNSUPPORTED_GEOMETRY`；
   不用单一 DPR 掩盖误差，屏幕路径继续可用。
6. 异步回调使用独立共享状态；超时后迟到回调不得访问栈对象或已销毁管理器。
   权限拒绝、授权恢复、目标退出和部分初始化失败均有清理路径。

完成条件：Retina 与普通屏的屏幕、窗口均有像素内容证据；权限恢复不需重启宿主；
超时与迟到回调回归通过；不保留旧 point 尺寸输出分支。

### P4：六语言绑定迁移、运行时诊断与示例

可在 P1/P2 后先完成 Windows 部分，macOS 完整验收依赖 P3。

改动/新增：

- `bindings/python/src/maa_capture/_core.py`、`_loader.py`、`__init__.py`。
- 拟新增 `_types.py`；`bindings/python/tests/test_binding.py` 及新增独立测试模块。
- `bindings/nodejs/index.js`、`index.d.ts`、`test.js`；`bindings/csharp/MaaCapture.cs` 及测试。
- `bindings/golang/maa_capture.go`、`custom_cgo.go` 及示例；`bindings/rust/src/lib.rs` 及示例。
- `bindings/java/MaaCapture.java`、`Sample.java`；各绑定文档。
- 拟新增 `sample/diagnose_capture.py`；`bindings/python/README.md`、`bindings/README.md`。

工作：

1. Python 使用冻结的数据类型和必填类型化目标；`screencap()` 直接返回包含 image/source 的
   `CaptureFrame`。删除 `ImageBuffer.Info` 返回形式、旧来源 setter 及 `set_dll_path/dll_path`
   别名，统一用 `set_lib_path/lib_path`。
2. 六语言绑定同步更新创建参数、帧所有权和回调协议；枚举、帧及错误数据明确复制或 RAII
   持有。补齐跨 FFI 的整数宽度、结构对齐和字符串长度检查，所有失败均释放快照/帧。
3. 先验证必需的 ABI 代际查询符号及返回值，再配置/调用其他原型。缺少查询符号、代际
   不符或必需符号缺失立即拒绝加载，报告包版本、原生版本、实际路径和失败原因。
   系统路径加载只报告已知定位结果，无法确定路径时标记未知，不把请求库名当作绝对路径。
4. Python 侧验证过大整数、非法枚举和冲突目标，避免 ctypes 无声截断；其他绑定对等校验。
5. 诊断工具支持列屏、目标类型、设备 key、窗口 ID、方法和采样次数；以 JSON
   输出运行时信息、请求/实际来源、布局、帧几何、错误及 connect/取帧耗时分位数。
6. 实际截图放在独立子进程中，父进程设硬超时，超时后清理子进程；不在库内声称可安全
   取消任意阻塞原生调用。日志/截图输出按命令行显式指定。
7. 六语言示例使用同一代接口；Windows 增加各绑定基本创建/回调/取帧/关闭的运行验证。
   Go 的新回调在 cgo 配置中单独验证；Node.js 类型声明增加实际类型检查。其他平台
   只对声明支持的绑定执行运行验证，不把交叉编译或语法检查当作原生运行验收。

示例调用草案（实现前不可作为现有 API 运行）：

```python
snapshot = monitors()
target = next(item for item in snapshot.items if item.primary)
with Manager(target=MonitorTarget(monitor_key=target.monitor_key)) as capture:
    if not capture.connect():
        raise RuntimeError(capture.last_error())
    frame = capture.screencap()
    if frame is None:
        raise RuntimeError(capture.last_error())
    # frame.image 包含已复制的 BGR bytes；frame.source 描述实际来源。
```

调用方必须处理枚举失败、没有主屏、目标歧义和取帧失败；正式示例补齐这些分支。
省略 methods 时，管理器按目标的范围能力与当前构建能力筛选，不使用跨范围的 ALL 测速。

完成条件：六语言消费者全部迁移，公开接口能完成各自声明范围内的基本截图/回调/关闭；
Python 可完成完整枚举、screen/window 取图及诊断。旧库混装在调用新签名前被拒绝，
close/with 等清理机制继续有回归覆盖。

### P5：CI、完整源码与资产来源

改动/新增：

- `.github/workflows/build.yml`、`scripts/check-python-dist.py`、`scripts/check-project.py`。
- 拟新增 `scripts/check-release-assets.py`、`scripts/create-source-archive.py`。
- `docs/BUILDING.md`、`docs/RELEASING.md`、`docs/INTEGRATION.md`、`docs/SCREEN_CAPTURE.md`、
  `docs/TROUBLESHOOTING.md`、`README.md`、`CHANGELOG.md`。

工作：

1. 三平台构建、原生测试、安装后消费和六语言绑定检查全部改用统一接口；Windows 保留
   FramePool 关闭构建。新增测试必须进入完整测试目标，避免仅构建 smoke 却注册未构建测试。
2. 聚合 wheel 生成后增加三平台安装 job；使用最终 wheel 的内置原生库执行查询、新符号、
   回调和错误路径冒烟。Python 增加 3.12 下限与现有当前版本组合。ABI 代际不符及必需符号
   缺失使用可控测试库或 fake loader 验证拒绝路径，不建立旧库兼容运行矩阵。
3. 新增 `maa-capture-v<version>-source.tar.gz`，由已跟踪提交快照生成，包含 `source/`、
   `include/`、`cmake/`、构建文件、测试、绑定、示例、脚本、文档及许可证，不混入工作树产物。
4. 每平台生成构建清单：版本、完整提交、编译器/SDK、构建选项、目标 OS/架构、原生库
   SHA-256。SDK 和 wheel 内嵌对应清单；源码包与清单指向同一提交。
   原生二进制不内嵌自身最终哈希，避免自引用；哈希在链接后计算。
5. 增加 `maa-capture-v<version>-provenance.json`，记录源码包、三平台原生库和分发物哈希；
   清单自身哈希写入独立 `SHA256SUMS`，不创建循环引用。
6. 资产检查从固定五个文件扩展为三平台 SDK、wheel、Python sdist、完整源码包、来源清单
   和校验和。分支检查、Release 上传 glob、README 和发布文档同步。
7. 在独立目录从源码包构建并运行安装后消费者；从 SDK 解压包验证 find_package；
   不使用原工作树补齐遗漏文件。Python sdist 的范围如实说明，不能等同于完整原生源码。
8. 保持当前聚合 wheel 布局；验证目标平台/架构与所选库相符。未提供的架构给出明确
   不支持结果，不能仅凭 wheel 安装成功宣称原生运行支持。
9. CMake 包按 MAJOR 版本隔离消费；支持 SONAME/install-name 的平台设置库 ABI 代际，
   同步实际打包的文件、符号链接和加载路径。Windows SDK/应用必须成套替换并重编译，
   不宣称同名 DLL 可直接替换历史应用使用的库。安装后示例改用 `find_package(maa-capture 2 ...)`。
10. 更新 `docs/RELEASING.md`、`docs/API_MIGRATION.md` 和发布说明中的 ABI 承诺，明确目标、
    返回值、默认输出和回调变化；更新对外文档时清除与本计划冲突的“旧符号保持兼容”说明。

完成条件：所有预期资产齐全、哈希可核验、源码可独立构建；三平台最终 wheel 验证通过。

### P6：桌面验收与调用方交接

提交带日期的验证记录，记录系统、缩放、GPU/驱动、库提交、原生哈希、请求/实际来源、
诊断 JSON 和色块截图。需要真实桌面的用例不得因 CI 无桌面被跳过后标记为已通过。

交付给调用方：已验证依赖版本/资产哈希、公开 API 示例、几何映射约定、错误及恢复表、
设备身份稳定性边界和平台实机限制。调用方据此实现 Qt 映射、游戏目标解析和配置迁移。

完成条件：第 6 节矩阵中 Windows/macOS 必测项目完成，来源错配为零；未支持能力明确
返回错误。完整 macOS 原生像素未验收前，不把三项迁移阻塞全部标为已解除。

## 6. 验证矩阵

| 类别 | 自动测试 | 桌面/产物验收 |
|---|---|---|
| 身份与快照 | 同名屏、重排、重复/歧义身份、空列表、变化重试、快照生命周期 | 双屏、主屏切换、插拔、镜像与扩展模式 |
| 严格来源 | 设备变化、输出查找失败、缺少目标、错误帧不发布、无隐式回退 | 非主屏断开不切主屏；跨 GPU 输出 |
| 几何 | 负原点、旋转、非整数比例、窗口重建、捕获期间变化 | Windows 100%+150%、125%+200%；副屏左/上/右及旋转屏 |
| macOS 像素 | point/pixel 映射、拒绝不可表达几何、默认原生像素 | macOS 14+ ARM64，Retina+普通屏、换主屏、边框/阴影与固定色块 |
| 图像与内存 | BGR 颜色、行填充、尺寸/步长溢出、帧跨多次取图仍有效、失败清理 | 图像内容、方向、锐度和冻结检查，不仅看尺寸 |
| 生命周期 | 连接部分失败、全候选失败、inactive 后恢复、重复 close、回调异常/迟到 | 连续切换至少 20 次；资源回落及伪最小化状态恢复 |
| 权限与错误 | 原生错误映射、超时、未知字段、ABI 代际/必需符号不匹配 | macOS 授权前/后/撤销/恢复；目标退出重开 |
| 能力 | 平台/范围交集、ScreenDC/window、PipeWire/source-defined、禁用后端 | 固定方法与请求区域一致，无隐式来源替换 |
| 绑定与分发 | 同代 C/C++ SDK 消费、Python 3.12、六语言签名/类型/运行验证 | 三平台最终 wheel、解压 SDK、完整源码包构建和来源清单 |

新版外部回调声明像素长度、来源及元数据有效位；测试验证声明长度、最小步长和溢出，
不声称能证明任意外部指针后的真实内存长度。默认原生测试使用可控内存与 provider，
不依赖实际桌面、游戏进程或用户屏幕录制权限。

性能记录使用同场景对比：connect、单帧 p50/p95、CPU、常驻内存及资源数量，说明布局
核验的额外开销。优先缓存未变化的系统信息，但不得因此省略实际来源校验。性能不能
代替来源和像素正确性验收。

## 7. 本仓库验证入口

以下是后续代码阶段的门禁；本次文档变更不要求下载依赖或运行桌面截图。

Windows 原生默认配置：

```powershell
python scripts/check-project.py
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
$env:PYTHONPATH = (Resolve-Path 'bindings/python/src').Path
python bindings/python/tests/test_binding.py (Resolve-Path 'build/Release/maa-capture.dll').Path
Remove-Item Env:PYTHONPATH
git diff --check
```

Python 新测试统一由现有 `test_binding.py` 入口调用，或在 CI 显式增加独立 unittest 入口，
不能只添加文件却不运行。临时环境变量在自动脚本中通过 `try/finally` 恢复。

其他门禁：FramePool 禁用构建、三平台同代 SDK 消费、六语言接口/运行检查、
最终 wheel 安装、完整源码包重建、发布资产及哈希核验。Linux/macOS 使用各自平台的
CMake 生成器和库路径，沿用现有 CI，不能照搬调用方项目的 Qt/pytest/Ruff 命令。

## 8. 依赖与建议提交顺序

依赖关系：`P0 → P1 → P2 → P3 → P6`；P4 可在 P2 后启动；P5 的资产工作可在 P1 后推进，
最终门禁依赖 P3/P4；正式交接需要 P0—P6 全部达到声明范围内的完成条件。

建议每项形成独立可审查提交，测试随功能提交进入，不集中到最后：

1. `docs(capture): define source identity and native pixel implementation plan`
2. `docs(api): define explicit capture targets and consumer migration`
3. `feat(api)!: unify capture targets frames and callback contracts`
4. `feat(monitors): add immutable Windows display snapshots`
5. `feat(win32): capture strict display targets with frame provenance`
6. `feat(macos): enumerate display identities and capture native pixels`
7. `feat(python)!: require explicit targets and return capture frames`
8. `refactor(bindings)!: migrate native consumers to the unified capture ABI`
9. `feat(diagnostics): report capture sources and structured failures`
10. `build(release): archive full sources and binary provenance`
11. `ci(capture): verify final wheels and source archives across platforms`
12. `docs(capture): publish validated geometry and platform limitations`

macOS 提交若过大，按“快照/原生输出/权限和异步清理”拆分。原生签名变更与直接消费它的
平台后端、C/C++ 示例和原生测试在同一提交迁移，保持核心构建可用。六语言绑定可逐个
提交迁移，过渡期间在任务清单明确尚未迁移的绑定，不能靠跳过失败验证通过最终 CI；
发布前全部恢复并通过。破坏性提交使用 `!`，正文说明 `BREAKING CHANGE:` 和迁移入口。
尚未完成的能力不提前报告为可用功能位。

## 9. 版本与完成判据

本次修订仅更新计划，不调整当前源码版本、依赖或标签。代码实施按破坏性 MAJOR 版本
规划，候选为本库 `2.0.0`，正式采用前核对版本占用；ABI 代际为 2。该版本决定来自本库
的签名、目标、返回值和默认行为变化，与调用方应用的版本独立。

版本准备时同步 CMake、Python 元数据与 `__version__`、Node.js、Rust、C# 版本和更新日志。
当前仓库校验只接受严格的三段稳定版；本计划不要求预发布标签。如果后续需要预发布，
先单独设计 CMake 数字版本、对外 SemVer 和 Python 元数据的映射，再修改门禁，不能只
放宽正则。发布沿用先分支 CI、后单独附注标签的流程，已发布资产不覆盖。

验收清单：

- [ ] 管理器、目标、帧和回调统一，无 Ex、兼容路由和旧默认行为分支。
- [ ] Windows/macOS 公共显示器快照能用于唯一来源匹配，单位和身份限制明确。
- [ ] 严格捕获在缺失、歧义和来源变化时失败，不能发布其他来源的帧。
- [ ] 新帧的像素、实际来源和布局信息具有一致的生命周期。
- [ ] Windows/macOS 默认原生像素通过内容与边界核验；不可表达几何有明确拒绝路径。
- [ ] 六语言绑定及示例迁移到同代 ABI，Python 完整公开接口和资源释放回归通过。
- [ ] ABI 混装被明确拒绝，SDK 和消费者的重编译要求及迁移步骤写入文档。
- [ ] 方法范围、错误和实际加载信息可用于独立诊断。
- [ ] 三平台 CI、最终 wheel、SDK、完整源码及来源清单核验通过。
- [ ] Windows/macOS 桌面验收及限制记录齐全，调用方可据此开始迁移。

任何来源错配、像素映射错误、超时后访问失效对象或关闭后无法恢复临时窗口状态的问题，
都阻止相关能力验收。无法在当前机器完成的实机项保留为未验证，不以文档或模拟测试代替。

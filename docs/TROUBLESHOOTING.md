# 故障与恢复

| 错误 | 处理 |
|---|---|
| INVALID_ARGUMENT / UNSUPPORTED_OPTION | 检查标签、必填字段、结构大小、保留位、整数范围和平台 |
| METHOD_UNAVAILABLE | 查询单方法能力；删去跨范围/未编入的方法或使用自动选择 |
| TARGET_NOT_FOUND / TARGET_AMBIGUOUS | 重新获取快照并让调用方确定唯一来源；不要改为主屏回退 |
| SOURCE_MISMATCH | provider 声明或实际输出不符，停止发布并修复来源配置 |
| LAYOUT_CHANGED / TARGET_CHANGED | 丢弃当前结果，重新解析目标；必要时重建管理器 |
| PERMISSION_DENIED | 根据 native_domain/native_code 检查系统权限，再重新连接 |
| TIMEOUT | 调用方可重试；诊断子进程超时后由父进程终止 |
| UNSUPPORTED_GEOMETRY | 检查旋转、跨屏窗口、过滤器边界；当前实现无法表达时须换目标 |
| CAPTURE_FAILED / INTERNAL_ERROR | 保留原生错误、运行时版本和诊断 JSON，检查后端/系统状态 |

MaaWSGetMethodCapabilities 的 available 只代表已编入，不证明权限或当前来源可用。
枚举错误与成功的空列表分开；系统未明确报告权限拒绝时不猜测权限错误。
Linux 稳定显示器枚举返回不支持。

绑定首先检查 MaaWSGetAbiVersion。缺少符号或 ABI != 2 时升级整套 SDK/绑定，
不能通过改函数原型或退回旧 setter 绕过。Python 的 lib_path 无法确定系统加载位置时
返回 None；建议诊断时显式指定 --lib。

诊断工具 sample/diagnose_capture.py 支持 --list-monitors、--target、--key、
--window-id、--method、--samples，输出请求和实际来源、几何、错误及耗时分位数。
--timeout 控制子进程硬超时；库本身不承诺取消任意阻塞原生调用。
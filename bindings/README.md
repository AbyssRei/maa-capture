# 语言绑定支持范围（ABI 2）

所有绑定均已改为显式目标和独立帧；没有 ABI 1 回退或旧签名路径。

| 绑定 | 声明的运行平台 | 已封装接口 |
|---|---|---|
| Python | Windows x64 / Linux x64 / macOS ARM64 | 全部公共查询、类型化目标、帧、错误、回调和生命周期 |
| C++ | 跟随原生 SDK | 快照、能力、目标、帧、继承接口、错误和 RAII |
| Node.js | Windows x64 | 目标、回调、帧复制、错误、close；koffi |
| C# | Windows x64 | 目标、回调、帧复制、错误、IDisposable |
| Java | Windows x64 | 目标、回调、帧复制、错误、AutoCloseable；JNA |
| Go | Windows x64 | 目标、帧复制、错误、Close；自定义回调要求 cgo |
| Rust | Windows x64 | 目标、帧复制、错误、自定义回调、Drop |

其余五语言的完整显示器快照/运行时辅助封装尚未补齐；不能把语法检查当作跨平台原生验收。
可从 C/Python 获取 monitor key 后传给这些绑定的显式 monitor 目标。
结构声明由 `scripts/generate-ffi.py` 从 C 头生成，修改契约后重新生成并检查。

最小运行样例：Node.js `test.js`、C# `test/`、Java `Sample.java`、
Go `test/`、Rust `examples/demo.rs`，全部测试自定义来源的创建/取帧/关闭。
先构建原生库并把 DLL 目录加入 PATH。Go/Node 支持 `MAA_WS_LIB`；
Rust 链接时把 import library 目录加入 LIB，Java 可通过 jna.library.path 指定目录。

ABI 查询必须先于任何新签名调用。C++ 和 C 示例先检查 `MaaWSGetAbiVersion()==2`。
重新编译是强制要求；本轮没有历史结构布局兼容承诺。
# maa-capture 多语言绑定

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture)。所有绑定围绕同一套 C ABI，提供截图管理器和外部回调；不等于 MaaFramework 的完整控制器或任务接口。

## 能力与验证范围

| 语言 | 机制 | 当前范围 | CI 验证 |
|---|---|---|---|
| C / C++ | 原生 ABI / 扩展类 | 三平台管理器及后端 | 三平台编译、原生回归、安装后消费 |
| Python | ctypes | 三平台、来源配置、显示器查询、上下文管理 | 三平台语法、运行时回调/查询、wheel 安装 |
| Node.js | Koffi | Windows 管理器、回调、窗口辅助 | 三平台依赖安装和语法；窗口辅助依赖 user32 |
| C# | P/Invoke | 基础管理器、回调、Windows 窗口辅助 | 三平台编译，示例以 Windows 为主 |
| Go | syscall / 可选 cgo | Windows；cgo 用于自定义回调 | Windows 目标编译，非 Windows runner 做交叉编译 |
| Java | JNA | 基础管理器、回调、Windows 窗口辅助 | 三平台编译，示例以 Windows 为主 |
| Rust | extern C | 当前绑定及示例依赖 user32，面向 Windows | 三平台类型检查、Windows 原生链接 |

Python 目前提供完整的平台来源和查询辅助 API。其他绑定未全部封装新增方法；不要把“语法/编译通过”视为全部平台 GUI 功能已运行通过。CI 不要求 Linux/macOS runner 有可授权的桌面会话。

## 各语言快速开始

Python 的安装和平台示例见 [Python README](python/README.md)。Windows Node.js：

```powershell
cd bindings/nodejs
npm install --no-package-lock
$env:MAA_WS_DLL_DIR = 'C:/path/to/sdk/bin'
node test.js
```

C#：先把 SDK 的 bin 加入 PATH，然后执行：

```powershell
dotnet run --project bindings/csharp/test --configuration Release
```

Go（Windows）：

```powershell
cd bindings/golang
go mod init maa_capture  # 仅首次执行；当前示例使用本地模块名
$env:CGO_ENABLED = '0'
go run ./test
```

自定义 Go 回调需 `CGO_ENABLED=1` 和匹配架构的 C 编译器，见 `custom_cgo.go`。CI 当前只验证纯 Go 路径。

Java：在 `bindings/java` 中执行：

```powershell
./fetch-jna.ps1
javac -encoding UTF-8 -d build-classes -cp .deps/jna.jar MaaCapture.java Sample.java
java --enable-native-access=ALL-UNNAMED -Djna.library.path=../../build/Release -cp "build-classes;.deps/jna.jar" maa.capture.Sample
```

PowerShell 脚本从 Maven Central 解析最新稳定 JNA。Java classpath 在 Linux/macOS 使用 `:` 分隔；示例中的窗口辅助仅用于 Windows。

Rust（Windows，导入库和 DLL 路径均需配置）：

```powershell
$env:LIB = 'C:/path/to/sdk/lib;' + $env:LIB
$env:PATH = 'C:/path/to/sdk/bin;' + $env:PATH
cargo run --manifest-path bindings/rust/Cargo.toml --example demo
```

C / C++ 示例位于 [sample](../sample/)，随 `MWS_BUILD_SAMPLES` 构建。安装后的标准 CMake 接入见[构建文档](../docs/BUILDING.md)。

## 生命周期与输出

C ABI 返回 BGR 图像指针，由原生句柄持有；需要保留时复制。回调结构体、函数及像素缓冲必须保持有效，不能在原生调用结束前回收。

Python 使用 `with` / `close()`；C# 使用 `Dispose()` / `using`；Java 使用 `close()` / try-with-resources；Rust 通过 Drop 释放。Go / Node.js 使用各绑定的显式销毁方法，结束前按需调用停用接口。

同一管理器操作需串行化，窗口句柄的含义随平台变化。详见[集成及所有权](../docs/INTEGRATION.md)、[平台限制](../docs/SCREEN_CAPTURE.md)和[故障排查](../docs/TROUBLESHOOTING.md)。

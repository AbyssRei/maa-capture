# maa-capture —— 多语言绑定

对应 MaaFramework（`docs/zh_cn/2.1-集成文档.md`）所列语言的 **Win32 截图功能** 绑定。
所有绑定都封装同一套 C ABI（`include/maa-capture/MaaWSCustomScreencap.h`），
提供两套「截图功能外部扩展接口」：

1. **外部回调接口** —— 对应 `MaaCustomControllerCallbacks::screencap`：
   你用自己的语言实现截图回调，注册进统一测速选择器
2. **截图管理器** —— 内置 6 种 Win32 截图方式（GDI / FramePool / DXGI_DesktopDup /
   DXGI_DesktopDup_Window / PrintWindow / ScreenDC）自动预热+实测测速，选最快方式截图

## 状态总览

| 语言 | 目录 | 机制 | 验证状态 |
|---|---|---|---|
| C | 项目根 `sample/external_c_*.c` | 纯 C | ✅ 编译+运行通过 |
| C++ | 项目根（头文件+示例） | 原生头文件 | ✅ 编译+运行通过 |
| Python | `bindings/python` | ctypes（零编译） | ✅ 运行通过（内置+回调） |
| NodeJS | `bindings/nodejs` | koffi FFI（零 node-gyp） | ✅ 编译+运行通过（内置+回调） |
| C# | `bindings/csharp` | P/Invoke | ✅ dotnet 编译+运行通过（内置+回调） |
| Golang | `bindings/golang` | syscall（零 cgo）+ cgo 可选 | ✅ 两条路径均运行通过 |
| Java | `bindings/java` | JNA（零 JNI 编译） | ✅ javac 编译+运行通过（内置+回调） |
| Rust | `bindings/rust` | extern "C" + 安全包装 | ✅ cargo 编译+运行通过（内置+回调） |

> C 与 C++ 的完整示例在项目根 `sample/`（`external_c_only.c` / `external_c_manager.c` /
> `external_c_callback.cpp` / `external_cpp_interface.cpp`），随 CMake `MWS_BUILD_SAMPLES` 构建。

## 各语言快速开始

### Python（ctypes）

```bash
# 安装随 wheel 分发三平台原生库的绑定包
pip install maa-capture        # 或本地：pip install -e bindings/python

# 运行自测（真实截图，需桌面会话）
python bindings/python/tests/test_binding.py [原生库目录]
```

### NodeJS（koffi）

```bash
cd bindings/nodejs && npm i koffi
node test.js [dll目录]
```

### C#（P/Invoke）

```bash
cd bindings/csharp && dotnet run --project test
（把 build/Release 加入 PATH 或复制 DLL）
```

### Go（双路径）

```bash
# 纯 Go（内置方式）
cd bindings/golang && go run ./test
# cgo（自定义回调，需要 MinGW gcc）
CGO_ENABLED=1 CC=gcc go run ./test
```

### Java（JNA）

```bash
# jna-5.19.1.jar 已随绑定目录提供（也可自行从 Maven Central 下载）
javac -encoding UTF-8 -cp jna-5.19.1.jar MaaCapture.java Sample.java
java --enable-native-access=ALL-UNNAMED -Djna.library.path=build/Release -cp .;jna-5.19.1.jar maa.capture.Sample
```

### Rust

```bash
cd bindings/rust
LIB=../../build/Release cargo build --example demo
PATH=../../build/Release:$PATH cargo run --example demo
```

## C ABI 稳定性

所有语言绑定只依赖 3 组稳定导出：

- `MaaWSCustomScreencapCreate/Destroy/Screencap` —— 外部回调句柄
- `MaaWSManagerCreate/Destroy/Connect/Screencap/SelectedUnitName/Inactive` —— 截图管理器
- `MaaWSManagerRegisterCustomUnit` —— 把「外部回调实现」注册进测速选择器

加上 `MaaWSImageBuffer`（BGR 归一化输出）与 `MaaWSCustomScreencapCallbacks` 两个布局稳定的结构体，
以及 `MaaWSMethod` 位掩码常数。语言侧改动不涉及 C++ 层。

> 注意：截图输出统一归一化为 **BGR 三通道**（与 MaaFramework 内部约定一致），
> 即使回调写入 4 通道 BGRA 也会在桥接层完成 BGRA→BGR。
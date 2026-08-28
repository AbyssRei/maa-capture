# Java 绑定（JNA）

对应 MaaFramework 的 `maa-framework-java`。基于 [JNA](https://github.com/java-native-access/jna)，无需 JNI 编译。

## 文件

- `MaaCapture.java` — 绑定主体（Library 接口 + ImageBuffer/Callbacks 结构 + ScreencapManager）
- `Sample.java` — 使用示例（外部回调单元 + 内置方式）

## 依赖

- `jna`（如 `io.github.java-native-access:jna:5.19.1`，或从 Maven Central 下载 `jna-5.19.1.jar`；本目录已随附该 jar）

## 运行

1. 构建 `maa-capture.dll`（无第三方依赖，见项目根 README）
2. 指定 DLL 搜索路径：

```
java -Djna.library.path=D:\path\to\build\Release -cp .;jna-5.19.1.jar maa.capture.Sample
```

> JNA 默认以 C declaration 调用（x64 无 stdcall 差异），无需 W32APIOptions。

## 两个外部接口

1. **外部回调**：实现 `Callbacks`（必须实现 `screencap`），
   经 `ScreencapManager.registerCustomUnit` 注册进测速选择器
2. **内置方式**：`new ScreencapManager(hwnd, Methods.ALL)` → `connect()` → `screencap()`

注意：`ImageBuffer.copyBytes()` 返回的像素是归一化 **BGR 三通道**（与 MaaFramework 内部约定一致）。

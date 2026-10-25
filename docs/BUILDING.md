# 构建与 SDK 接入

项目：[maa-capture](https://github.com/AbyssRei/maa-capture)。需要 CMake 3.21+ 和 C++20 编译器。

## Windows

安装 Visual Studio 的桌面 C++ 组件及 Windows SDK。FramePool 需要 SDK 10.0.22000+ 与 C++/WinRT；配置阶段会检测能力。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix C:/sdk/maa-capture
```

也可用 `vs2022` / `vs2026` 预设，或在编译器开发环境中使用 Ninja。预设与具体目录见根目录 `CMakePresets.json`。

## Linux / macOS

Linux 可选依赖见[平台说明](SCREEN_CAPTURE.md)。macOS 需要 macOS 14+ SDK。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local/maa-capture"
```

本项目 CI 的 Linux 和 macOS 验证直接运行在对应 GitHub Actions runner，包含原生测试和安装后消费测试。

## CMake 选项

| 选项 | 默认值 | 用途 |
|---|---|---|
| MWS_ENABLE_FRAMEPOOL | ON | Windows SDK 支持时构建 WGC |
| MWS_ENABLE_PIPEWIRE | ON | Linux 依赖可用时构建 PipeWire |
| MWS_ENABLE_WLR | ON | Linux 依赖可用时构建 Wlr |
| MWS_WIN32_COMPATIBLE | OFF | Windows 旧版 DPI 缩放兼容逻辑 |
| MWS_BUILD_SAMPLES | 顶层项目 ON | 编译 C / C++ 示例 |
| MWS_BUILD_TESTS | 顶层项目 ON | 编译原生回归测试 |

禁用全部系统后端仍可使用外部自定义单元。`MWS_*_AVAILABLE` 编译定义通过 CMake 目标传递给消费者，手工接入头文件和动态库时应保持配置一致。

## 作为子项目

```cmake
add_subdirectory(path/to/maa-capture)
target_link_libraries(my_app PRIVATE maa-capture::maa-capture)
```

编译字符集、链接优化等选项限定于本库目标，避免污染父项目。作为子项目时默认不构建示例和测试。

## 使用安装后的 SDK

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_app LANGUAGES C CXX)
find_package(maa-capture 1 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE maa-capture::maa-capture)
```

配置时传入 `-DCMAKE_PREFIX_PATH=/path/to/sdk`。Windows 运行时需要把 SDK 的 `bin` 加入 PATH，或把 DLL 放到应用程序目录。Linux / macOS 需要动态加载器能找到 SDK 库及系统依赖。

SDK 的 CMake 配置位于 `lib/cmake/maa-capture`（具体 lib 目录遵循 GNUInstallDirs）。Linux 安装包会查找已启用后端的 pkg-config 开发依赖。CI 用 `tests/install-consumer` 验证 SDK 不依赖源码树即可使用。

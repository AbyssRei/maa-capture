# 构建与验证

需要 CMake 3.21+、C++20；Windows SDK 10.0.22000+ 可启用 FramePool，
macOS 需要 14+ SDK 和 ScreenCaptureKit；Linux 的 PipeWire/libdrm/wayland-client 按探测启用。

```powershell
python scripts/check-project.py
python scripts/generate-ffi.py --check
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Linux/macOS 省略 -A x64。MWS_ENABLE_FRAMEPOOL=OFF 可检查关闭后端的能力报告；
仍须构建完整测试目标后运行 ctest。MWS_BUILD_SAMPLES/MWS_BUILD_TESTS 控制消费者与回归。

安装及独立消费：

```powershell
cmake --install build --config Release --prefix build-install
cmake -S tests/install-consumer -B build-consumer -DCMAKE_PREFIX_PATH="$PWD/build-install"
cmake --build build-consumer --config Release --parallel
```

Windows 运行消费者前把安装目录的 bin 加入 PATH，并在 finally 恢复环境。
Python 回归入口为 bindings/python/tests/test_binding.py，可传原生库绝对路径；
源树运行时临时设置 PYTHONPATH=bindings/python/src，结束后恢复。
默认回归使用可控 provider，不自动修改真实桌面拓扑。

结构生成以 CaptureTypes.h 为唯一来源。修改后运行 scripts/generate-ffi.py，
所有生成文件须随契约提交。CI 的 --check 防止 FFI 声明漂移。
每次链接后生成 build/maa-capture-build-Release.json；
本地未提交变更的提交标识带 -dirty，不能冒充可发布来源。

六语言运行门禁、最终 wheel 安装、源码包重建和 SDK 解压消费由 CI 执行。
跨平台编译/桌面验收须查看各自结果，不能以 Windows 成功替代 macOS。
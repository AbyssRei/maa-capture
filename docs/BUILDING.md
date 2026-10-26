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

窗口状态、选择事务和 FramePool 静态/动态测试只操作自有 Win32 窗口，显式启用：

```powershell
cmake -S . -B build-desktop -A x64 -DMWS_BUILD_DESKTOP_TESTS=ON
cmake --build build-desktop --config Release --parallel
ctest --test-dir build-desktop -C Release --output-on-failure
```

这会将 windows_desktop 加入 CTest；无交互桌面的 CI 保持默认 OFF。
桌面测试父进程的 180 秒超时是测试保护，不代表库支持硬取消。
新 Python 绑定与旧 ABI 2 库的兼容性可使用
`python bindings/python/tests/test_old_library.py <旧库绝对路径>` 验证；新缓存策略必须明确拒绝。
安装后可用 `python scripts/check-legacy-c-consumer.py --prefix <安装目录>` 编译原始 ABI 2 C 头与调用方，
与当前动态库链接运行。原始引用需要在本地 Git 对象库中（默认固定到 2.0.0 基线提交）。
`maa_capture_desktop --soak-seconds 120 --output <新目录>` 另外执行持续流及句柄/GDI 对象稳定性检查；
这不是 Qt 应用 QThread 或冻结包的验收。
当前持续流组合启用缓存（可用时）、严格后台、限量诊断、5 秒采样年龄/连接/首帧预算和 500ms 取图预算。
桌面套件包含真实 DXGI 无更新观察及实际样本后的受控 WAIT_TIMEOUT/错误回归；
静态像素仍可能有鼠标/合成器更新，不把未发生原生超时的输出误判为默认语义失败。

结构生成以 CaptureTypes.h 为唯一来源。修改后运行 scripts/generate-ffi.py，
所有生成文件须随契约提交。CI 的 --check 防止 FFI 声明漂移。
每次链接后生成 build/maa-capture-build-Release.json；
本地未提交变更的提交标识带 -dirty，不能冒充可发布来源。

六语言运行门禁、最终 wheel 安装、源码包重建和 SDK 解压消费由 CI 执行。
跨平台编译/桌面验收须查看各自结果，不能以 Windows 成功替代 macOS。

焦点干扰补测可运行 `maa_capture_desktop --focus-cycles 60 --output <新目录>`。
每屏覆盖无切换、原最小化时序、切其他自有窗口、程序恢复目标与独立宿主线程；
CSV 记录每次获取/释放前后的 HWND、输入时间、样式和最小化状态。
补测期间勿操作桌面；焦点控制不可用或输入变化需查看原始记录，不应直接归因于库。
外部程序恢复并激活目标也可取消最小化意图，无须键鼠输入。

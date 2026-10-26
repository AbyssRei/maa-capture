# Java（Windows x64，ABI 2）

运行 JNA 示例：

```powershell
./fetch-jna.ps1
javac -encoding UTF-8 -d build-classes -cp .deps/jna.jar NativeTypes.java MaaCapture.java Sample.java
java --enable-native-access=ALL-UNNAMED "-Djna.library.path=../../build/Release" -cp "build-classes;.deps/jna.jar" maa.capture.Sample
```

目标通过 customTarget/monitorTarget/windowTarget 显式创建；
Manager 使用 AutoCloseable，screencap 返回复制后的 CaptureFrame。
回调内存持续至 release_frame，异常转换为失败。完整快照辅助封装未提供。
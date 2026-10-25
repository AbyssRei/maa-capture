# Java 绑定（JNA）

`MaaCapture.java` 提供 C ABI 的 JNA 接口，`Sample.java` 演示自定义回调和截图管理器。
Java 包名为 `maa.capture`，无需编译 JNI。

在本目录执行（需要 JDK 和 PowerShell）：

```powershell
./fetch-jna.ps1
javac -encoding UTF-8 -d build-classes -cp .deps/jna.jar MaaCapture.java Sample.java
java --enable-native-access=ALL-UNNAMED -Djna.library.path=../../build/Release -cp "build-classes;.deps/jna.jar" maa.capture.Sample
```

Linux / macOS 的 classpath 分隔符为 `:`，库目录通常为 `../../build`。
`fetch-jna.ps1` 每次从 Maven Central 的 `net.java.dev.jna:jna` 元数据解析最新稳定版，
验证 SHA-1 后写入 `.deps/jna.jar`，不固定精确版本或提交 jar。

先在项目根目录构建原生库。`ScreencapManager.registerCustomUnit` 接收自定义回调；
`new ScreencapManager(hwnd, Methods.ALL)` 选择内置方式。输出像素统一为 BGR 三通道。

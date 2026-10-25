# 内置原生库占位说明

此目录在**构建 wheel 时**由 CI（或本地打包脚本）填充各平台的
maa-capture 原生库，随包分发、由 `_loader` 按当前平台自动选取：

```
_native/
├── windows-x64/maa-capture.dll
├── linux-x64/libmaa-capture.so
└── macos-arm64/libmaa-capture.dylib
```

源码仓库中不提交二进制；安装后的包若当前平台无对应文件，
`_loader` 会回退到 `MAA_WS_LIB` 环境变量与系统库搜索路径。

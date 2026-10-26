# ABI 2 发布

2.0.0 是破坏性 MAJOR 版本，ABI 代际 2。正式发布前确认版本未占用，
同步 CMake、Python 元数据/__version__、Node、Rust/Cargo.lock、C# 工程和 CHANGELOG。
本仓库只接受三段稳定版本；不覆盖已发布资产。

先让分支 CI 完成，再单独创建附注标签 v<version>。当前任务不自动推送或发布。
标签必须与源码版本一致。发布依赖各平台构建/测试、六语言检查、
最终聚合 wheel 在三平台 Python 3.12 和当前版安装，以及独立源码/SDK 消费。
桌面验收记录独立于无桌面 CI，未执行的项目保持未验证。

固定资产：

- maa-capture-v<version>-windows-x64.7z
- maa-capture-v<version>-linux-x64.tar.gz
- maa-capture-v<version>-macos-arm64.tar.gz
- maa_capture-<version>-py3-none-any.whl
- maa_capture-<version>.tar.gz（Python sdist）
- maa-capture-v<version>-source.tar.gz（完整原生源码）
- maa-capture-v<version>-provenance.json
- SHA256SUMS

create-source-archive.py 从指定已跟踪提交生成完整源码包，包含 SOURCE_COMMIT；
不读取工作树产物。原生构建清单包含版本、完整提交、编译器/SDK、OS/架构、
选项和链接后 SHA-256；安装 SDK 和 wheel 内嵌对应清单。
未提交构建带 -dirty，check-release-assets.py 拒绝与源码提交不一致的来源。

provenance 关联源码包、SDK、Python 分发物及三平台原生库；
SHA256SUMS 包含 provenance 自身哈希，不包含自身，避免循环引用。
verify-source-archive.py 在独立目录仅从源码资产构建并验证安装消费者；
verify-sdk.py 仅使用解压后的 SDK 头文件、库和示例。
scripts/check-release-assets.py 是分支和 Release 共用的完整性入口。

SONAME/install-name 代际为 2，SDK 打包保留版本文件和符号链接。
Windows 不具备同名 DLL 的历史二进制兼容承诺，必须成套替换并重编译。
迁移详情见 [API_MIGRATION.md](API_MIGRATION.md)。
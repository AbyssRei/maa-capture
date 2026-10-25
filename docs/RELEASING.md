# 版本与发布

主仓库：[AbyssRei/maa-capture](https://github.com/AbyssRei/maa-capture)。

项目展示名、仓库名、CMake 目标、头文件目录及动态库名称统一使用 `maa-capture`。
Python / Rust / Go 模块采用 `maa_capture`；Java 包为 `maa.capture`，Java 类及 C# 命名空间
采用合法标识符 `MaaCapture`。C ABI 的 `MaaWS` 符号保持兼容。

## 版本约定

项目采用 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。公共 API 包括
`include/maa-capture` 的 C / C++ 接口、库文件名，以及各语言绑定对外导出的接口与包名。
`0.x` 表示开发阶段；`1.0.0` 是 maa-capture 重新编号后的首个稳定版。
稳定版新增兼容功能递增 MINOR，兼容修复递增 PATCH，不兼容 API 修改递增 MAJOR。
已经发布的版本不得覆盖。

CMake、Python 包元数据和 `__version__`、Node.js、Rust、C# 版本必须一致。
提交使用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)，
例如 `feat(capture): ...`、`fix(win32): ...`、`ci: ...`。

## 发布步骤

1. 修改全部版本字段和更新日志，运行 `python scripts/check-project.py`。
2. 提交更改，仅推送分支，等待该次分支 CI 全部通过：

   ```sh
   git -c push.followTags=false push origin main
   ```

3. 创建与 CMake 版本一致的附注标签，再单独推送该标签：

   ```sh
   git tag -a v1.0.0 -m "chore(release): publish maa-capture 1.0.0"
   git -c push.followTags=false push origin refs/tags/v1.0.0
   ```

不要同时推送分支和标签，也不要使用 `--follow-tags` 或 `--mirror` 发布。
分支推送执行验证；标签推送执行验证、GitHub Release 及可选 PyPI 发布。
未配置 `PYPI_API_TOKEN` 时，仅跳过 PyPI 上传，wheel / sdist 仍附在 GitHub Release。

## 依赖策略

- Marketplace Actions 使用最新稳定主版本的浮动标签；`setup-uv` 上游未提供主版本
  别名，使用其 `main` 分支并由 CI 验证。
- Python 的 `uv_build` / NumPy 和 Node.js 的 Koffi 使用包含当前最新稳定版的兼容范围。
  Python 最低版本为 3.12，以支持当前 NumPy。
- Java 的 JNA 由 `bindings/java/fetch-jna.ps1` 从 Maven Central 解析最新稳定版。
- 不提交外部依赖的精确锁定文件或 JNA jar；Rust 锁文件仅记录本项目自身版本。
- Go / Rust 使用 stable，Node.js 使用 current，Python 使用最新 3.x，Java 使用最新
  稳定特性版本（下限 27），.NET 使用最新稳定的 10.0 系列；Linux 系统库从 runner 的
  官方软件源安装最新可用版本。Dependabot 每周检查 Actions、npm 和 Python 声明。

## 历史整理

初始根提交合并 `df13393` 与 `1fe7b76`，随后依次保留 `f9e7fae`、`b0e45dd`
两次窗口修复；其余构建修复按所属功能合并。原作者信息保留，新历史从根提交起
统一采用新项目名称。新项目独立重新编号；重写前的历史和发行资产需先备份。
历史标签导入时暂时关闭工作流，避免为历史版本重复发布。

## PyPI 令牌与重建仓库

在 GitHub 的 Settings → Secrets and variables → Actions 新建仓库 Secret，名称为
`PYPI_API_TOKEN`，值为包含 `pypi-` 前缀的完整正式 PyPI 令牌。不要写入源码、提交消息或命令参数。
也可以执行 `gh secret set PYPI_API_TOKEN --repo AbyssRei/maa-capture`，在交互提示中输入。
工作流的发布 job 使用 `pypi` environment，可读取仓库 Secret。

删除并重建 GitHub 仓库会删除原有 Secrets。先创建新仓库，重新配置 Secret 并确认存在，
再单独推送正式版本标签。历史标签导入期间临时关闭构建工作流，恢复后只推正式发布标签。
原仓库中的代码改动应先通过完整 CI，再创建新仓库；代码优化后需要重新验证。

PyPI 不允许复用曾上传过的发行文件名，即使删除整个项目也一样。更新同名包应使用新版本，
只改变大小写或 `-` / `_` / `.` 不会形成不同的标准化包名。详见
[PyPI 文件名规则](https://docs.pypi.org/publishing/troubleshooting/)和
[名称规范化](https://packaging.python.org/en/latest/specifications/name-normalization/)。

## 发布验证范围

发布前必须通过三平台原生回归、安装后 `find_package` 消费测试、绑定检查、Python wheel/sdist
元数据及长描述链接检查、内置原生库检查和五个发布资产检查。Linux/macOS 测试直接在 GitHub
Actions 对应 runner 执行。正式发布后从官方 PyPI 源安装，核对包名、版本、仓库链接、文件摘要
及原生回调。发布包中的名称、说明、原生库加载名和主仓库链接必须一致。

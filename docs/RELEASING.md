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

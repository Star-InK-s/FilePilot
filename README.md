# FilePilot

使用 C++17、Qt 6 和 SQLite 构建的 Windows 11 本地文件整理与备份工具。

![Version](https://img.shields.io/badge/version-v1.0.0-2ea44f)
![Platform](https://img.shields.io/badge/platform-Windows%2011-0078D4)
[![CI](https://github.com/Star-InK-s/FilePilot/actions/workflows/ci.yml/badge.svg)](https://github.com/Star-InK-s/FilePilot/actions/workflows/ci.yml)
![Validation](https://img.shields.io/badge/build-CMake%20%2F%20CTest%20validated-2ea44f)
![License](https://img.shields.io/badge/license-MIT-4c4c4c)

![FilePilot 浅色主题](docs/images/filepilot-light.png)

## 项目简介

FilePilot 是一个本地优先的 Windows 桌面应用，用于扫描、整理、分析和备份本地文件。项目采用 C++17 核心逻辑、Qt 6 Widgets 界面、SQLite 数据持久化，并适配 Windows 11 系统主题。

## 主要功能

- 文件扫描，显示类型与分类统计、进度、错误和取消状态
- RuleEngine 规则分类与整理预览
- 目标路径冲突策略：Skip、Overwrite、AutoRename
- 通过文件大小和 SHA-256 内容校验检测重复文件
- 单文件和目录 Backup，并保留源文件
- Backup staging、重新校验、SHA-256 验证和最终发布
- SQLite 保存执行历史和备份历史
- Windows 11 浅色、深色、Accent、High Contrast 和 DPI 适配
- Qt Test 与 CTest 覆盖核心逻辑、UI smoke、整理、重复文件、主题和 Backup 路径

## 界面截图

### 整理预览

![整理预览](docs/images/filepilot-organize.png)

### 重复文件

![重复文件检测](docs/images/filepilot-duplicate.png)

### 备份

![备份预览](docs/images/filepilot-backup.png)

### 历史记录

![执行历史](docs/images/filepilot-history.png)

### 主题适配

| 深色主题 | Accent 主题 |
| --- | --- |
| ![FilePilot 深色主题](docs/images/filepilot-dark.png) | ![FilePilot Accent 主题](docs/images/filepilot-accent.png) |

## 系统架构

FilePilot 将扫描、规划、执行、持久化和界面展示划分为独立层次：

```text
扫描 -> 分类 -> 整理规划 -> 预览 -> 执行核心 -> SQLite 历史

重复文件检测 -> 大小/Hash 分析 -> 重复文件分组

Backup Plan -> 预校验 -> Staging -> SHA-256 验证 -> 发布 -> Backup History

WindowsThemeDetector -> ThemeSnapshot -> ThemePalette
-> ThemeStyleSheet/QtThemeApplier -> 各功能页面
```

重复文件检测和 Backup 是独立流程。主题模块读取当前 Windows 系统状态，不维护另一套脱离系统的设计体系。

## 安全设计

- Backup 保留源文件，并通过 staging tree 复制内容。
- 发布前重新验证源文件和目标路径身份。
- 最终发布前使用 SHA-256 验证 Backup 内容。
- 拒绝 Reparse Point、Junction 和不安全的路径嵌套关系。
- 取消操作会停止尚未完成的工作，并保留已经验证和发布的正确结果。
- 冲突策略支持 Skip、Overwrite 和 AutoRename。
- 历史结果写入 SQLite，便于后续检查。

这些机制用于降低文件系统风险，但不声称能够消除所有 Windows TOCTOU 竞态。

## 技术栈

- C++17
- Qt 6 Widgets、SQL 和 Test
- CMake 与 Ninja
- MinGW 64-bit
- SQLite
- CTest
- Windows API

## 构建

环境要求：

- Windows 11
- Qt 6.5 或更高版本，并安装 MinGW 64-bit kit
- MinGW 64-bit
- CMake 3.21 或更高版本
- Ninja 或 MinGW Makefiles

以下路径只是通用示例，请替换为本机 Qt 和 MinGW 安装位置。

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.1\mingw_64\bin;" + $env:Path
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.11.1\mingw_64"

cmake --preset windows-mingw-release
cmake --build --preset release
```

Debug 构建：

```powershell
cmake --preset windows-mingw-debug
cmake --build --preset debug
```

## 运行

从构建目录启动程序：

```powershell
.\build\windows-mingw-release\src\filepilot.exe
```

如果需要生成可交付目录，请使用 Qt `windeployqt`，并包含 Qt SQL SQLite plugin、MinGW runtime、`README.md`、`LICENSE` 和 `THIRD_PARTY_NOTICES.md`。不要将构建目录、CMake cache、日志或测试输出打包到发布目录。

## 测试

```powershell
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

v1.0.0 验证记录为 185 passed、0 failed、3 skipped。该数字是 v1.0.0 的记录结果，不代表所有未来环境都保证相同结果。当前本地复验中，Debug 和 Release 的 15 个 CTest targets 均已通过。

## 下载

从 GitHub Releases 下载 [`FilePilot-v1.0.0-Windows-x64.zip`](https://github.com/Star-InK-s/FilePilot/releases/tag/v1.0.0)。

SHA-256：

```text
5ADBAE3455EC8DDD3141DEEA7DD4E866AA2518CD9F5591DC7AB38BF79A58349E
```

该 ZIP 是 Windows x64 便携式应用包，包含当前版本所需的 Qt 和 MinGW runtime。

## 已知限制

- 尚未支持增量 Backup。
- 尚未支持云同步。
- 尚未支持计划任务 Backup。
- 尚未支持网络 Backup。
- 尚未支持高级 Backup 版本链。
- 不提供重复文件自动删除。
- 不提供 Undo。
- v1.0.0 整理页面提供扫描、规划、预览和确认，但端到端 Execute 按钮仍处于禁用状态。
- Backup 成功后，当错误信息为空时，Backup History 可能因 SQLite `NOT NULL` 约束而持久化失败。

## 路线图

未来可能的方向：

- 在 v1 UI 中开放端到端整理执行。
- 补充成功 Backup History 持久化回归测试。
- 增量和计划任务 Backup。
- 更安全的重复文件检查与清理流程。
- 已完成整理操作的 Undo 和恢复辅助。

## 许可证

FilePilot 使用 [MIT License](LICENSE) 发布。

第三方组件和代码来源说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

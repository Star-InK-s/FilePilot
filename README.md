# FilePilot

FilePilot 是一个面向 Windows 11 的本地文件整理与备份桌面工具，使用 C++17、Qt 6 Widgets、SQLite、CMake 和 MinGW 64-bit 构建。

当前正式版本：**1.0.0**。

## 主要功能

- 文件扫描、统计、错误收集和取消
- RuleEngine 文件分类与可配置分类规则
- OrganizePlanner 整理预览和安全整理执行
- 文件整理冲突处理：Skip、Overwrite、AutoRename
- Cancellation 和失败恢复语义
- SQLite Execution History
- Duplicate Finder
- Fluent UI
- Windows Light / Dark 主题适配
- Windows Accent 颜色适配
- High Contrast 支持
- DPI adaptation
- Backup MVP
  - 单文件 Backup
  - 目录 Backup
  - 空目录 Backup
  - 嵌套目录和目录结构保持
  - SHA-256 verification
  - source preservation
  - Skip、Overwrite、AutoRename
  - BackupTask 生命周期、进度和取消
  - Backup History
  - Backup UI

## 安全边界

- Backup 在 staging tree 中复制并验证内容，验证通过后才发布最终目标。
- 源文件不会被 Backup 删除或移动。
- 目录递归覆盖明确不支持，避免危险的递归覆盖。
- AutoRename 发布前重新检查最终名称，并使用不覆盖的发布操作。
- 取消只停止尚未完成的工作，已经发布且验证成功的结果不会因为取消而被删除。
- 极端 Windows TOCTOU 竞态仍属于 residual risk，不在 v1.0 声称完全消除。

## 环境要求

- Windows 11
- C++17
- Qt 6.5 或更高版本，验证版本为 Qt 6.11.1
- MinGW 64-bit
- CMake 3.21+
- Ninja 或 MinGW Makefiles

## 构建

请根据本机 Qt 和 MinGW 安装位置替换路径。以下只是通用示例：

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.1\mingw_64\bin;" + $env:Path

cmake --preset windows-mingw-release
cmake --build --preset release
```

Debug 构建：

```powershell
cmake --preset windows-mingw-debug
cmake --build --preset debug
```

CMake 文件不包含用户个人绝对路径。

## 测试

```powershell
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
```

测试覆盖 Core、Organize、Duplicate、Theme/DPI、Backup Foundation、BackupExecutor、Directory Backup、BackupTask、Backup History 和 Backup UI smoke。

## 运行程序

源码构建：

```powershell
.\build\windows-mingw-release\src\filepilot.exe
```

正式目录构建：

```powershell
.\release\FilePilot-v1.0.0\FilePilot.exe
```

## 发布目录

正式 ZIP/目录形式的发布包位于：

```text
release/FilePilot-v1.0.0/
```

发布目录包含 `FilePilot.exe`、Qt runtime、MinGW runtime、Qt plugins、SQLite plugin、README、LICENSE 和第三方声明。`build/`、`.git/`、测试输出、CMake cache、源码和临时文件不应放入发布目录。

使用 Qt 提供的 `windeployqt.exe` 部署 Qt runtime 和 plugins。SQLite 通过 Qt SQL SQLite plugin 提供。

## 已知限制

- 目录递归覆盖未实现。
- 增量备份未实现。
- 云同步未实现。
- 版本链未实现。
- 计划任务未实现。
- 目录 overwrite 明确不支持。
- 极端 Windows TOCTOU 仍属于 residual risk。
- 中文路径下 Qt `moc` 是开发构建环境限制，建议开发构建目录使用 ASCII 路径。
- 当前 v1.0 使用 Windows 本地文件系统。
- 尚未提供 MSI、NSIS 或其他安装器；当前交付形式是 ZIP/目录。

## License

MIT。第三方组件和调研来源见 `THIRD_PARTY_NOTICES.md`。

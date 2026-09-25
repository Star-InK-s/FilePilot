# FilePilot

FilePilot 是一个面向 Windows 11 的本地文件整理与备份桌面工具。项目使用
C++17、Qt 6 Widgets、SQLite、CMake 和 Git，目标是形成一个结构完整、
可读、可维护的本科实习个人项目。

当前代码处于 **Phase 4：整理预览**。本阶段实现了目录选择、后台扫描、
扫描进度、错误收集、文件统计、RuleEngine 分类、整理计划和 Qt Model/View 预览。移动、
复制、删除、冲突执行、重复文件检测、备份和撤销操作均未实现。

## 当前功能

- Qt 6 Widgets 主窗口和五项导航
- 文件整理页面目录选择
- `std::filesystem` 后台文件扫描
- 文件名、完整路径、扩展名、大小、创建时间和修改时间
- 文件总数、总大小、扩展名数量、分类数量和错误数量
- 数据驱动的 `ClassificationRule` 和 `RuleEngine`
- 默认分类、自定义规则、优先级、禁用规则和扩展名规范化
- OrganizePlanner 生成稳定的整理计划快照
- Windows category / 文件名安全校验
- Planned / Invalid 预览状态
- 确认和取消整理计划均不修改文件系统
- 扫描进度、当前目录和取消操作
- 单个文件或目录错误记录并继续扫描
- Windows Junction / reparse point 跳过和根路径保护
- `QAbstractTableModel` 文件表格
- 全局任务状态和进度区域
- 设置持久化和文件日志

工具栏中的“整理”“检测重复”“开始备份”仍然禁用，对应功能属于后续阶段。

## 技术栈

- C++17
- Qt 6 Core / Gui / Widgets / Sql / Test
- SQLite
- CMake 3.21+
- MinGW 64-bit
- Ninja 或 MinGW Makefiles
- Git

## 项目结构

```text
FilePilot/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── THIRD_PARTY_NOTICES.md
├── cmake/
│   └── CompilerWarnings.cmake
├── docs/
│   ├── research/
│   └── screenshots/
├── resources/
├── src/
│   ├── app/
│   │   ├── Application.*
│   │   ├── MainWindow.*
│   │   └── main.cpp
│   ├── core/
│   │   ├── classify/
│   │   ├── logging/
│   │   ├── model/
│   │   ├── organize/
│   │   ├── scan/
│   │   ├── settings/
│   │   └── tasks/
│   └── ui/
│       ├── models/
│       └── pages/
└── tests/
    ├── CMakeLists.txt
    ├── TestCases.h
    └── test_filepilot.cpp
```

## 核心实现

### ScanService

`ScanService` 只负责扫描逻辑和结果组织，不依赖 Qt Widgets。它使用
`std::filesystem::recursive_directory_iterator`、`directory_options` 和
`std::error_code` 遍历目录。Windows 下通过 `FindFirstFileW` 和 reparse tag
识别 Junction、符号链接和其他 reparse point，默认不进入任何链接。根路径是
链接或 reparse point 时明确拒绝。

单个条目发生元数据错误时会记录错误并继续处理其他条目。目录探测会记录
无法访问的子目录。扫描结果包含：

- `std::vector<FileInfo>`
- `ScanStatistics`
- `ScanError` 列表（只保留有限详情，错误总数始终准确）
- 完成、取消和致命失败状态
### ClassificationRule / RuleEngine

分类是独立的 Qt Core 业务模块，不依赖 Qt Widgets：

- `ClassificationRule` 保存名称、优先级、启用状态、分类和扩展名
- `RuleEngine` 对规则按 priority 升序稳定排序
- 数值越小优先级越高
- 第一个匹配规则生效
- Others 只在所有普通规则都不匹配时生效，不参与 wildcard 抢先匹配
- category 为空或纯空白的规则会被拒绝
- disabled 规则不会参与匹配
- 扩展名统一去掉开头的点并转小写
- 规则扩展名和 `FileInfo.extension` 使用同一套规范化
- 默认提供 Documents、Images、Videos、Audio、Archives、Programming 和 Others
- `RuleEngine::classify(ScanResult&)` 将结果写入 `FileInfo.category`。`FileOrganizePage` 只展示 RuleEngine 的结果。
### ScanTask

`ScanTask` 是扫描后台任务控制器：

- 使用 `QThread` 执行 `ScanService`
- 使用 `std::atomic_bool` 和 `ScanCancellationToken` 取消
- 通过 Qt queued signal 报告状态、进度、错误批次和结果
- 进度按时间间隔合并，不逐文件刷新 UI
- 错误按批次累计，最终错误总数不丢失
- 工作线程异常统一转换为 `TaskState::Failed`
- 同一运行内终态不会被取消操作回退
### OrganizePlan / OrganizePlanner

整理预览是独立纯业务模块：

- `OrganizePlanItem` 保存源路径、目标路径、分类、文件名、大小、修改时间和状态
- Plan provenance 保存规范化 targetRoot、targetRoot 类型（Empty / Relative / Absolute / UNC / DeviceNamespace）、plan generation、scan generation 和扫描来源
- `plannedItems()` / `executableCandidates()` 永远排除 Invalid 和 NoOp 项
- `OrganizePlan` 保存稳定计划快照并统计 Planned、Invalid 和分类数量
- `OrganizePlanner` 根据 `FileInfo.category` 和目标根目录生成目标路径
- `OrganizePathValidator` 校验 category 和文件名
- `NoOp` 明确表示 sourcePath 与 destinationPath 词法等价
- 不调用 exists、copy、move、rename 或 remove
- Phase 4 只进行 lexical normalization 和静态路径检查
- Phase 5 必须重新验证 canonical path、volume identity、reparse point、实际目标目录身份和文件系统状态
- 无效 category、Windows 保留名称、非法字符、路径逃逸和非法文件名会生成 Invalid 项

`OrganizePreviewModel` 使用 `QAbstractTableModel` 展示：
文件名、当前路径、分类、目标路径和状态。
### FileTableModel

`FileTableModel` 继承 `QAbstractTableModel`，显示：

- 文件名
- 类型
- 大小
- 修改时间
- 路径

模型只接收 `std::vector<FileInfo>`，不负责扫描或文件操作。

### 文件整理页面

页面负责：

- 选择目录
- 调用 `ScanTask`
- 展示扫描状态和错误
- 更新文件统计和扩展名统计
- 将扫描结果交给 `FileTableModel`
- 将任务信号转发给 MainWindow 的全局进度区域

UI 不直接调用 `std::filesystem`。

## 构建

需要 Qt 6.5 或更高版本、CMake、Ninja 和 MinGW 64-bit。

PowerShell 示例：

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.8.0\mingw_64\bin;" + $env:Path

cmake -S . -B build `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\mingw_64

cmake --build build --parallel
```

请根据本机 Qt 安装位置替换路径。CMake 文件不包含机器绝对路径。

## 运行测试

```powershell
ctest --test-dir build --output-on-failure
```

测试覆盖：

- 空目录
- 普通文件和多级子目录
- 中文路径、空格和特殊字符
- 文件数量、总大小和扩展名统计
- 不存在目录和单文件访问失败
- 扫描取消
- Preparing、Running、完成瞬间和重复取消状态竞争
- 后台任务不会阻塞事件循环
- 错误批次累计和最终 flush
- Windows 普通目录、根 Junction、嵌套 Junction 和 Junction 环
- 默认分类、自定义规则、优先级、禁用规则和扩展名规范化
- OrganizePlanner 生成稳定的整理计划快照
- Windows category / 文件名安全校验
- Planned / Invalid 预览状态
- 确认和取消整理计划均不修改文件系统
- `FileInfo.category` 写入和分类统计
- OrganizePlan / OrganizePlanner 纯路径规划和安全校验
- OrganizePreviewModel、预览生成、确认和取消
- FileTableModel 行列、表头和格式化
- 文件整理页面扫描结果和失败状态
- Phase 1 基础模块回归
## 运行程序

```powershell
.\build\src\filepilot.exe
```

## 截图

扫描功能的截图将在 UI 稳定后放入 `docs/screenshots/`。

## 已知问题

- Qt 6.11.1 的 `moc` 在输出目录包含中文字符时可能无法生成元对象文件。
  本机验证使用了 ASCII 路径下的独立构建目录，建议项目路径和构建路径
  尽量使用 ASCII 字符。
- 当前环境的 Qt License Service 未正确注册。构建 Qt 元对象时可按 Qt 提示
  设置 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`，或修复本机 Qt License Service。
- Windows ACL 造成的真实权限拒绝需要在后续测试中补充自动化夹具；当前测试
  使用了可重复的文件元数据访问失败场景。
- `ScanTask` 完成后再次调用 `start()` 视为一次新的任务运行；同一运行内的终态
  不会被 `cancel()` 回退。
- 当前扫描结果只保存在内存中，没有 SQLite 业务数据持久化。
- 自定义分类规则目前只能通过 `RuleEngine` 接口传入，尚无规则编辑 UI 或持久化。
- 整理计划只进行词法路径规划，不检查目标文件是否存在；冲突检测属于 Phase 5。
- `executableCandidates()` 只是候选集合，Phase 5 执行器入口仍必须调用 `OrganizeExecutionPrevalidator` 重新验证。
## 下一阶段

Phase 5 将实现整理执行和冲突处理：

- 让执行器消费现有 `OrganizePlan`
- 执行前重新验证计划项
- 检查目标文件冲突
- 实现覆盖、跳过和自动重命名策略
- 执行移动操作并记录错误
- 执行过程提供进度和取消
- 不包含重复文件检测、备份或 SQLite 历史持久化
## License

MIT。第三方组件和调研来源见 `THIRD_PARTY_NOTICES.md`。

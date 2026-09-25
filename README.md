# FilePilot

FilePilot 是一个面向 Windows 11 的本地文件整理与备份桌面工具。项目使用
C++17、Qt 6 Widgets、SQLite、CMake 和 Git，目标是形成一个结构完整、
可读、可维护的本科实习个人项目。

当前代码处于 **Phase 2：文件扫描**。本阶段实现了目录选择、后台扫描、
扫描进度、错误收集、文件统计和 Qt Model/View 文件列表。文件分类、移动、
复制、删除、重复文件检测、备份和撤销操作均未实现。

## 当前功能

- Qt 6 Widgets 主窗口和五项导航
- 文件整理页面目录选择
- `std::filesystem` 后台文件扫描
- 文件名、完整路径、扩展名、大小、创建时间和修改时间
- 文件总数、总大小、扩展名数量和错误数量
- 扫描进度、当前目录和取消操作
- 单个文件或目录错误记录并继续扫描
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
│   │   ├── logging/
│   │   ├── model/
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
`std::error_code` 遍历目录。默认不跟随目录软链接和 Junction。

单个条目发生元数据错误时会记录错误并继续处理其他条目。目录探测会记录
无法访问的子目录。扫描结果包含：

- `std::vector<FileInfo>`
- `ScanStatistics`
- `ScanError` 列表
- 完成、取消和致命失败状态

### ScanTask

`ScanTask` 是扫描后台任务控制器：

- 使用 `QThread` 执行 `ScanService`
- 使用 `std::atomic_bool` 和 `ScanCancellationToken` 取消
- 通过 Qt queued signal 报告状态、进度、错误和结果
- 进度按时间间隔合并，不逐文件刷新 UI
- 支持 Preparing、Running、Cancelling、Completed、
  CompletedWithErrors、Cancelled 和 Failed 状态

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
- 后台任务不会阻塞事件循环
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
- 当前扫描结果只保存在内存中，没有 SQLite 业务数据持久化。

## 下一阶段

Phase 3 将只实现文件分类：

- 数据驱动的扩展名分类规则
- 默认分类和用户自定义规则
- 分类规则优先级
- 将分类结果用于整理预览的准备数据

Phase 3 不实现文件移动、复制、删除、重复检测和备份。

## License

MIT。第三方组件和调研来源见 `THIRD_PARTY_NOTICES.md`。

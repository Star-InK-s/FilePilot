# FilePilot

FilePilot 是一个面向 Windows 11 的本地文件整理与备份桌面工具。项目使用
C++17、Qt 6 Widgets、SQLite、CMake 和 Git，目标是形成一个结构完整、
可读、可维护的本科实习个人项目。

当前代码处于 **Phase 1：项目骨架**。本轮只完成工程结构、主窗口外壳、
基础服务和测试骨架，不包含文件扫描、移动、删除、重复文件检测和备份。

## 当前功能

- Qt 6 Widgets 主窗口
- 左侧五项导航和 `QStackedWidget`
- 顶部占位工具栏
- 全局任务进度区域
- 底部状态栏
- `FileInfo`、`TaskState`、`AppError` 基础类型
- INI 设置服务
- 文件日志服务
- Qt Test / CTest 测试骨架
- 窗口布局状态保存

当前工具栏命令和任务取消按钮处于禁用状态，因为对应业务功能尚未实现。

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
│   │   └── settings/
│   └── ui/
│       └── pages/
└── tests/
    ├── CMakeLists.txt
    ├── TestCases.h
    └── test_filepilot.cpp
```

## CMake 目标

- `filepilot_core`：基础模型、设置和日志模块
- `filepilot_gui`：Application、MainWindow 和页面
- `filepilot`：桌面程序入口
- `filepilot_tests`：Qt Test 自动测试

## 构建

需要 Qt 6.5 或更高版本、CMake、Ninja 和 MinGW 64-bit。

PowerShell 示例：

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.8.0\mingw_64\bin;" + $env:Path

cmake -S . -B build `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\mingw_64

cmake --build build --parallel
```

请根据本机 Qt 安装位置替换上述路径。CMake 文件本身不包含机器绝对路径。

也可以使用 `CMakePresets.json`，但在配置前需要把 MinGW、CMake、Ninja
加入 `PATH`，并设置 Qt 的 `CMAKE_PREFIX_PATH`。

## 运行测试

```powershell
ctest --test-dir build --output-on-failure
```

测试覆盖：

- 基础模型
- 任务状态
- 错误对象
- 设置持久化
- 日志级别和写入
- 主窗口、导航、页面和进度区域

## 运行程序

```powershell
.\build\src\filepilot.exe
```

## 截图

Phase 1 主窗口截图将在后续 UI 稳定后放入 `docs/screenshots/`。
当前截图不应被误认为最终界面。

## 已知问题

- Qt 6.11.1 的 `moc` 在输出目录包含中文字符时可能无法生成元对象文件。
  本机验证时使用了 ASCII 路径下的独立构建目录。建议项目路径和构建路径
  尽量使用 ASCII 字符。
- 当前环境的 Qt License Service 未正确注册。构建 Qt 元对象时可按 Qt 提示
  设置 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`，或修复本机 Qt License Service。
- Phase 1 没有业务文件操作，工具栏按钮为占位状态。

## 下一阶段

Phase 2 将只实现文件扫描：

- 选择本地目录
- 扫描文件元数据
- 处理无权限和异常路径
- 显示文件数量、总大小和文件列表
- 保持现有功能不被破坏

## License

MIT。第三方组件和调研来源见 `THIRD_PARTY_NOTICES.md`。

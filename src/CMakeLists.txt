add_library(filepilot_core STATIC
    core/logging/LogManager.cpp
    core/logging/LogManager.h
    core/model/AppError.cpp
    core/model/AppError.h
    core/model/FileInfo.h
    core/model/TaskState.cpp
    core/model/TaskState.h
    core/scan/ScanService.cpp
    core/scan/ScanService.h
    core/tasks/ScanTask.cpp
    core/tasks/ScanTask.h
    core/settings/SettingsService.cpp
    core/settings/SettingsService.h
)
target_include_directories(filepilot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(filepilot_core
    PUBLIC
        Qt6::Core
)

filepilot_enable_warnings(filepilot_core)

add_library(filepilot_gui STATIC
    app/Application.cpp
    app/Application.h
    app/MainWindow.cpp
    app/MainWindow.h
    ui/models/FileTableModel.cpp
    ui/models/FileTableModel.h
    ui/pages/FileOrganizePage.cpp
    ui/pages/FileOrganizePage.h
    ui/pages/Pages.cpp
    ui/pages/Pages.h
)

target_include_directories(filepilot_gui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(filepilot_gui
    PUBLIC
        filepilot_core
        Qt6::Gui
        Qt6::Widgets
)

filepilot_enable_warnings(filepilot_gui)

add_executable(filepilot WIN32
    app/main.cpp
)

target_link_libraries(filepilot
    PRIVATE
        filepilot_gui
)

filepilot_enable_warnings(filepilot)

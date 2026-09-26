add_library(filepilot_core STATIC
    core/classify/ClassificationRule.cpp
    core/classify/ClassificationRule.h
    core/classify/RuleEngine.cpp
    core/classify/RuleEngine.h
    core/database/ExecutionHistoryRepository.cpp
    core/database/ExecutionHistoryRepository.h
    core/database/HistoryTypes.h
    core/filesystem/FileIdentity.cpp
    core/filesystem/FileIdentity.h
    core/logging/LogManager.cpp
    core/logging/LogManager.h
    core/model/AppError.cpp
    core/model/AppError.h
    core/model/FileInfo.h
    core/model/TaskState.cpp
    core/model/TaskState.h
    core/duplicates/DuplicateFinder.cpp
    core/duplicates/DuplicateFinder.h
    core/duplicates/DuplicateTask.cpp
    core/duplicates/DuplicateTask.h
    core/duplicates/DuplicateTypes.cpp
    core/duplicates/DuplicateTypes.h
    core/execution/ConflictResolver.cpp
    core/execution/ConflictResolver.h
    core/execution/OrganizeExecutionTask.cpp
    core/execution/OrganizeExecutionTask.h
    core/execution/FileOperator.cpp
    core/execution/FileOperator.h
    core/execution/ExecutionTypes.cpp
    core/execution/ExecutionTypes.h
    core/organize/OrganizeExecutionPrevalidator.cpp
    core/organize/OrganizeExecutionPrevalidator.h
    core/organize/OrganizePathValidator.cpp
    core/organize/OrganizePathValidator.h
    core/organize/OrganizePlan.cpp
    core/organize/OrganizePlan.h
    core/organize/OrganizePlanProvenance.cpp
    core/organize/OrganizePlanProvenance.h
    core/organize/OrganizePlanItem.cpp
    core/organize/OrganizePlanItem.h
    core/organize/OrganizePlanner.cpp
    core/organize/OrganizePlanner.h
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
    Qt6::Sql
)

filepilot_enable_warnings(filepilot_core)

add_library(filepilot_gui STATIC
    app/Application.cpp
    app/Application.h
    app/MainWindow.cpp
    app/MainWindow.h
    ui/models/DuplicateModels.cpp
    ui/models/DuplicateModels.h
    ui/models/ExecutionHistoryModel.cpp
    ui/models/ExecutionHistoryModel.h
    ui/models/ExecutionResultModel.cpp
    ui/models/ExecutionResultModel.h
    ui/models/FileTableModel.cpp
    ui/models/OrganizePreviewModel.cpp
    ui/models/OrganizePreviewModel.h
    ui/models/FileTableModel.h
    ui/pages/DuplicateFilesPage.cpp
    ui/pages/DuplicateFilesPage.h
    ui/pages/FileOrganizePage.cpp
    ui/pages/FileOrganizePage.h
    ui/pages/Pages.cpp
    ui/pages/Pages.h
    ui/presenters/DuplicateTheme.cpp
    ui/presenters/DuplicateTheme.h
    ui/theme/ThemeSnapshot.h
    ui/theme/ThemePalette.cpp
    ui/theme/ThemePalette.h
    ui/theme/QtThemeApplier.cpp
    ui/theme/QtThemeApplier.h
    platform/windows/WindowsThemeDetector.cpp
    platform/windows/WindowsThemeDetector.h
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

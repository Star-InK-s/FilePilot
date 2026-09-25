add_library(filepilot_core STATIC
    core/model/AppError.cpp
    core/model/AppError.h
    core/model/FileInfo.h
    core/model/TaskState.cpp
    core/model/TaskState.h
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

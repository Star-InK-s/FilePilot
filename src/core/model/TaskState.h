#pragma once

#include <QString>

namespace FilePilot {

enum class TaskState {
    Idle,
    Preparing,
    Running,
    Cancelling,
    Completed,
    CompletedWithErrors,
    Cancelled,
    Failed
};

QString taskStateName(TaskState state);
bool isTerminalTaskState(TaskState state);

} // namespace FilePilot

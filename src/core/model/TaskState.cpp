#include "core/model/TaskState.h"

namespace FilePilot {

QString taskStateName(const TaskState state)
{
    switch (state) {
    case TaskState::Idle:
        return QStringLiteral("Idle");
    case TaskState::Preparing:
        return QStringLiteral("Preparing");
    case TaskState::Running:
        return QStringLiteral("Running");
    case TaskState::Cancelling:
        return QStringLiteral("Cancelling");
    case TaskState::Completed:
        return QStringLiteral("Completed");
    case TaskState::CompletedWithErrors:
        return QStringLiteral("Completed with errors");
    case TaskState::Cancelled:
        return QStringLiteral("Cancelled");
    case TaskState::Failed:
        return QStringLiteral("Failed");
    }

    return QStringLiteral("Unknown");
}

bool isTerminalTaskState(const TaskState state)
{
    switch (state) {
    case TaskState::Completed:
    case TaskState::CompletedWithErrors:
    case TaskState::Cancelled:
    case TaskState::Failed:
        return true;
    case TaskState::Idle:
    case TaskState::Preparing:
    case TaskState::Running:
    case TaskState::Cancelling:
        return false;
    }

    return false;
}

} // namespace FilePilot

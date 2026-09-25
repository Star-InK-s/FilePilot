#include "core/execution/ExecutionTypes.h"

namespace FilePilot {

QString conflictPolicyName(const ConflictPolicy policy)
{
    switch (policy) {
    case ConflictPolicy::Skip:
        return QStringLiteral("Skip");
    case ConflictPolicy::Overwrite:
        return QStringLiteral("Overwrite");
    case ConflictPolicy::AutoRename:
        return QStringLiteral("AutoRename");
    }

    return QStringLiteral("AutoRename");
}

QString conflictDecisionName(const ConflictDecisionAction action)
{
    switch (action) {
    case ConflictDecisionAction::Proceed:
        return QStringLiteral("Proceed");
    case ConflictDecisionAction::Skip:
        return QStringLiteral("Skip");
    case ConflictDecisionAction::Overwrite:
        return QStringLiteral("Overwrite");
    case ConflictDecisionAction::AutoRename:
        return QStringLiteral("AutoRename");
    case ConflictDecisionAction::Reject:
        return QStringLiteral("Reject");
    }

    return QStringLiteral("Reject");
}

QString executionItemStatusName(const ExecutionItemStatus status)
{
    switch (status) {
    case ExecutionItemStatus::Succeeded:
        return QStringLiteral("Succeeded");
    case ExecutionItemStatus::Skipped:
        return QStringLiteral("Skipped");
    case ExecutionItemStatus::Failed:
        return QStringLiteral("Failed");
    case ExecutionItemStatus::Rejected:
        return QStringLiteral("Rejected");
    case ExecutionItemStatus::Cancelled:
        return QStringLiteral("Cancelled");
    case ExecutionItemStatus::SourceCleanupFailed:
        return QStringLiteral("SourceCleanupFailed");
    }

    return QStringLiteral("Failed");
}

} // namespace FilePilot

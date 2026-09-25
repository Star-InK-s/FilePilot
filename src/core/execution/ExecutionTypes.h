#pragma once

#include "core/organize/OrganizePlanItem.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <vector>

namespace FilePilot {

enum class ConflictPolicy {
    Skip,
    Overwrite,
    AutoRename
};

enum class ConflictDecisionAction {
    Proceed,
    Skip,
    Overwrite,
    AutoRename,
    Reject
};

QString conflictPolicyName(ConflictPolicy policy);
QString conflictDecisionName(ConflictDecisionAction action);

struct ConflictDecision {
    OrganizePlanItem item;
    ConflictDecisionAction action = ConflictDecisionAction::Proceed;
    QString destinationPath;
    bool planInternalConflict = false;
    QString errorMessage;
};

enum class ExecutionItemStatus {
    Succeeded,
    Skipped,
    Failed,
    Rejected,
    Cancelled,
    SourceCleanupFailed
};

QString executionItemStatusName(ExecutionItemStatus status);

struct ExecutionItemResult {
    OrganizePlanItem item;
    QString actualDestination;
    ExecutionItemStatus status = ExecutionItemStatus::Rejected;
    QString errorMessage;
    QDateTime timestamp;
};

struct ExecutionSummary {
    qint64 planned = 0;
    qint64 succeeded = 0;
    qint64 skipped = 0;
    qint64 failed = 0;
    qint64 rejected = 0;
    qint64 cancelled = 0;
    qint64 sourceCleanupFailed = 0;
};

struct ExecutionResult {
    std::vector<ExecutionItemResult> items;
    ExecutionSummary summary;
    bool completed = false;
    bool cancelled = false;
    QString fatalError;
};

} // namespace FilePilot

Q_DECLARE_METATYPE(FilePilot::ConflictDecision)
Q_DECLARE_METATYPE(FilePilot::ExecutionItemResult)
Q_DECLARE_METATYPE(FilePilot::ExecutionResult)

#pragma once

#include "core/filesystem/FileIdentity.h"
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

struct ExecutionContext {
    QString executionId;
    QString targetRoot;
    QString scanSourceRoot;
    quint64 planGeneration = 0;
    quint64 scanGeneration = 0;

    ExecutionContext() = default;

    ExecutionContext(
        QString id,
        QString target,
        QString scanRoot,
        const quint64 plan,
        const quint64 scan)
        : executionId(std::move(id))
        , targetRoot(std::move(target))
        , scanSourceRoot(std::move(scanRoot))
        , planGeneration(plan)
        , scanGeneration(scan)
    {
    }

    ExecutionContext(
        QString target,
        QString scanRoot,
        const quint64 plan,
        const quint64 scan)
        : ExecutionContext({}, std::move(target), std::move(scanRoot), plan, scan)
    {
    }
};
struct ConflictDecision {
    OrganizePlanItem item;
    ConflictDecisionAction action = ConflictDecisionAction::Proceed;
    QString destinationPath;
    FileIdentity expectedDestinationIdentity;
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

struct PublishedMoveRecoveryState {
    QString executionId;
    quint64 planGeneration = 0;
    quint64 scanGeneration = 0;
    ConflictPolicy policy = ConflictPolicy::AutoRename;
    QString sourcePath;
    QString destinationPath;
    FileIdentity sourceIdentity;
    FileIdentity destinationIdentity;
    qint64 sourceSize = -1;
    qint64 destinationSize = -1;
    QByteArray sourceDigest;
    QByteArray destinationDigest;

    bool isValid() const
    {
        return !executionId.isEmpty()
            && planGeneration != 0
            && scanGeneration != 0
            && !sourcePath.isEmpty()
            && !destinationPath.isEmpty()
            && sourceIdentity.valid
            && destinationIdentity.valid
            && sourceSize >= 0
            && destinationSize >= 0
            && !sourceDigest.isEmpty()
            && sourceDigest == destinationDigest;
    }
};
struct ExecutionItemResult {
    OrganizePlanItem item;
    QString actualDestination;
    ExecutionItemStatus status = ExecutionItemStatus::Rejected;
    QString errorMessage;
    QDateTime timestamp;
    bool resumedPublishedResult = false;
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

struct ExecutionProgressUpdate {
    qint64 completed = 0;
    qint64 total = 0;
    QString sourcePath;
    QString destinationPath;
    QString action;
    QString phase;
    ExecutionSummary summary;
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
Q_DECLARE_METATYPE(FilePilot::ExecutionProgressUpdate)
Q_DECLARE_METATYPE(FilePilot::ExecutionItemResult)
Q_DECLARE_METATYPE(FilePilot::ExecutionResult)

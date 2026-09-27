#pragma once

#include "core/backup/BackupExecutionTypes.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/model/AppError.h"

#include <QDateTime>
#include <QString>

namespace FilePilot {

struct BackupHistoryRecord {
    qint64 id = 0;
    QString operationId;
    QDateTime createdAt;
    QString sourcePath;
    BackupEntryKind sourceKind = BackupEntryKind::File;
    QString destinationRoot;
    QString actualDestination;
    BackupExecutionStatus status = BackupExecutionStatus::Failed;
    bool published = false;
    bool verified = false;
    bool sourcePreserved = false;
    bool cleanupComplete = false;
    QString errorMessage;
    QString temporaryPath;
};

struct BackupHistoryPersistenceResult {
    bool persisted = false;
    qint64 historyId = 0;
    QString errorMessage;
    AppError error;
};

} // namespace FilePilot

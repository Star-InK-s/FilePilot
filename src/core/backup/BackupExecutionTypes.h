#pragma once

#include "core/model/AppError.h"

#include <QString>

#include <functional>

namespace FilePilot {

enum class BackupExecutionStatus {
    Succeeded,
    Skipped,
    Failed,
    Cancelled,
    VerificationFailed,
    SourceChanged,
    DestinationConflict,
    PublishFailed,
    CleanupFailed
};

struct BackupExecutionResult {
    BackupExecutionStatus status = BackupExecutionStatus::Failed;
    bool published = false;
    bool verified = false;
    bool sourcePreserved = false;
    bool cleanupComplete = false;
    QString errorMessage;
    QString temporaryPath;
    QString actualDestination;
    AppError error;
};

struct BackupExecutorHooks {
    std::function<bool(const QString &, const QString &, QString &)> beforeCopy;
    std::function<bool(qint64, const QString &, const QString &, QString &)> duringCopy;
    std::function<bool(const QString &, const QString &, QString &)> afterCopy;
    std::function<bool(const QString &, const QString &, QString &)> beforeVerify;
    std::function<bool(const QString &, const QString &, QString &)> beforePublish;
    std::function<bool(const QString &, const QString &, QString &)> afterPublish;
    std::function<bool(const QString &, const QString &, QString &)> temporaryVerifier;
    std::function<bool(const QString &, QString &)> beforeDirectoryEnumeration;
    std::function<bool(const QString &, QString &)> cleanupTemporary;
};

} // namespace FilePilot

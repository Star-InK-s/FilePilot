#pragma once

#include "core/database/BackupHistoryTypes.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <vector>

namespace FilePilot {

class BackupHistoryRepository
{
public:
    explicit BackupHistoryRepository(
        QString databasePath,
        QString connectionName = {});
    ~BackupHistoryRepository();

    BackupHistoryRepository(const BackupHistoryRepository &) = delete;
    BackupHistoryRepository &operator=(const BackupHistoryRepository &) = delete;

    bool initialize(QString *error = nullptr);
    BackupHistoryPersistenceResult saveBackupResult(
        const BackupPlan &plan,
        const BackupExecutionResult &result);
    std::vector<BackupHistoryRecord> listBackups(QString *error = nullptr);
    std::optional<BackupHistoryRecord> getBackup(
        qint64 historyId,
        QString *error = nullptr);
    bool deleteBackup(qint64 historyId, QString *error = nullptr);

    QString databasePath() const;
    bool isOpen() const;

private:
    bool ensureOpen(QString *error);
    bool createSchema(QString *error);
    static BackupHistoryRecord readRecord(const QSqlQuery &query);

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace FilePilot

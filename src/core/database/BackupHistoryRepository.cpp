#include "core/database/BackupHistoryRepository.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace FilePilot {
namespace {

QString connectionNameFor(const QString &requested)
{
    return requested.isEmpty()
        ? QStringLiteral("FilePilotBackupHistory_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
        : requested;
}

QString errorText(const QSqlQuery &query)
{
    const QSqlError error = query.lastError();
    return error.isValid() ? error.text() : QStringLiteral("SQLite operation failed");
}

QString statusName(const BackupExecutionStatus status)
{
    switch (status) {
    case BackupExecutionStatus::Succeeded:
        return QStringLiteral("Succeeded");
    case BackupExecutionStatus::Skipped:
        return QStringLiteral("Skipped");
    case BackupExecutionStatus::Failed:
        return QStringLiteral("Failed");
    case BackupExecutionStatus::Cancelled:
        return QStringLiteral("Cancelled");
    case BackupExecutionStatus::VerificationFailed:
        return QStringLiteral("VerificationFailed");
    case BackupExecutionStatus::SourceChanged:
        return QStringLiteral("SourceChanged");
    case BackupExecutionStatus::DestinationConflict:
        return QStringLiteral("DestinationConflict");
    case BackupExecutionStatus::PublishFailed:
        return QStringLiteral("PublishFailed");
    case BackupExecutionStatus::CleanupFailed:
        return QStringLiteral("CleanupFailed");
    }
    return QStringLiteral("Failed");
}

BackupExecutionStatus statusFromName(const QString &name)
{
    if (name == QStringLiteral("Succeeded")) {
        return BackupExecutionStatus::Succeeded;
    }
    if (name == QStringLiteral("Skipped")) {
        return BackupExecutionStatus::Skipped;
    }
    if (name == QStringLiteral("Cancelled")) {
        return BackupExecutionStatus::Cancelled;
    }
    if (name == QStringLiteral("VerificationFailed")) {
        return BackupExecutionStatus::VerificationFailed;
    }
    if (name == QStringLiteral("SourceChanged")) {
        return BackupExecutionStatus::SourceChanged;
    }
    if (name == QStringLiteral("DestinationConflict")) {
        return BackupExecutionStatus::DestinationConflict;
    }
    if (name == QStringLiteral("PublishFailed")) {
        return BackupExecutionStatus::PublishFailed;
    }
    if (name == QStringLiteral("CleanupFailed")) {
        return BackupExecutionStatus::CleanupFailed;
    }
    return BackupExecutionStatus::Failed;
}

BackupHistoryPersistenceResult persistenceFailure(
    const QString &message,
    const QString &context)
{
    BackupHistoryPersistenceResult result;
    result.persisted = false;
    result.errorMessage = message;
    result.error = AppError{ErrorCode::Database, message, context};
    return result;
}

} // namespace

BackupHistoryRepository::BackupHistoryRepository(
    QString databasePath,
    QString connectionName)
    : databasePath_(std::move(databasePath))
    , connectionName_(connectionNameFor(connectionName))
{
    database_ = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
}

BackupHistoryRepository::~BackupHistoryRepository()
{
    if (database_.isOpen()) {
        database_.close();
    }
    const QString connectionName = connectionName_;
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool BackupHistoryRepository::initialize(QString *error)
{
    if (!ensureOpen(error)) {
        return false;
    }
    return createSchema(error);
}

bool BackupHistoryRepository::ensureOpen(QString *error)
{
    if (database_.isOpen()) {
        return true;
    }

    const QFileInfo databaseInfo(databasePath_);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("无法创建 Backup History 数据库目录");
        }
        return false;
    }
    if (!database_.open()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

bool BackupHistoryRepository::createSchema(QString *error)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS backup_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "operation_id TEXT NOT NULL,"
            "created_at TEXT NOT NULL,"
            "source_path TEXT NOT NULL,"
            "source_kind TEXT NOT NULL,"
            "destination_root TEXT NOT NULL,"
            "actual_destination TEXT NOT NULL,"
            "status TEXT NOT NULL,"
            "published INTEGER NOT NULL,"
            "verified INTEGER NOT NULL,"
            "source_preserved INTEGER NOT NULL,"
            "cleanup_complete INTEGER NOT NULL,"
            "error_message TEXT NOT NULL DEFAULT '',"
            "temporary_path TEXT NOT NULL DEFAULT ''"
            ")"))) {
        if (error != nullptr) {
            *error = errorText(query);
        }
        return false;
    }

    QSqlQuery index(database_);
    if (!index.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_backup_history_created_at "
            "ON backup_history(created_at DESC)"))) {
        if (error != nullptr) {
            *error = errorText(index);
        }
        return false;
    }
    return true;
}

BackupHistoryPersistenceResult BackupHistoryRepository::saveBackupResult(
    const BackupPlan &plan,
    const BackupExecutionResult &result)
{
    QString error;
    if (!ensureOpen(&error) || !createSchema(&error)) {
        return persistenceFailure(error, databasePath_);
    }

    const QString operationId = plan.operationId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : plan.operationId;
    const QDateTime createdAt = QDateTime::currentDateTimeUtc();

    if (!database_.transaction()) {
        return persistenceFailure(
            database_.lastError().text(), databasePath_);
    }

    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO backup_history ("
        "operation_id, created_at, source_path, source_kind, destination_root, "
        "actual_destination, status, published, verified, source_preserved, "
        "cleanup_complete, error_message, temporary_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(operationId);
    insert.addBindValue(createdAt.toString(Qt::ISODateWithMs));
    insert.addBindValue(plan.sourcePath);
    insert.addBindValue(plan.sourceKind == BackupEntryKind::Directory
        ? QStringLiteral("Directory")
        : QStringLiteral("File"));
    insert.addBindValue(plan.destinationRoot);
    insert.addBindValue(result.actualDestination.isEmpty()
        ? plan.finalDestinationPath
        : result.actualDestination);
    insert.addBindValue(statusName(result.status));
    insert.addBindValue(result.published ? 1 : 0);
    insert.addBindValue(result.verified ? 1 : 0);
    insert.addBindValue(result.sourcePreserved ? 1 : 0);
    insert.addBindValue(result.cleanupComplete ? 1 : 0);
    insert.addBindValue(result.errorMessage);
    insert.addBindValue(result.temporaryPath);

    if (!insert.exec()) {
        const QString message = errorText(insert);
        database_.rollback();
        return persistenceFailure(message, databasePath_);
    }
    if (!database_.commit()) {
        const QString message = database_.lastError().text();
        database_.rollback();
        return persistenceFailure(message, databasePath_);
    }

    BackupHistoryPersistenceResult persistence;
    persistence.persisted = true;
    persistence.historyId = insert.lastInsertId().toLongLong();
    return persistence;
}

BackupHistoryRecord BackupHistoryRepository::readRecord(const QSqlQuery &query)
{
    BackupHistoryRecord record;
    record.id = query.value(0).toLongLong();
    record.operationId = query.value(1).toString();
    record.createdAt = QDateTime::fromString(
        query.value(2).toString(), Qt::ISODateWithMs);
    record.sourcePath = query.value(3).toString();
    record.sourceKind = query.value(4).toString() == QStringLiteral("Directory")
        ? BackupEntryKind::Directory
        : BackupEntryKind::File;
    record.destinationRoot = query.value(5).toString();
    record.actualDestination = query.value(6).toString();
    record.status = statusFromName(query.value(7).toString());
    record.published = query.value(8).toInt() != 0;
    record.verified = query.value(9).toInt() != 0;
    record.sourcePreserved = query.value(10).toInt() != 0;
    record.cleanupComplete = query.value(11).toInt() != 0;
    record.errorMessage = query.value(12).toString();
    record.temporaryPath = query.value(13).toString();
    return record;
}

std::vector<BackupHistoryRecord> BackupHistoryRepository::listBackups(
    QString *error)
{
    std::vector<BackupHistoryRecord> records;
    if (!ensureOpen(error) || !createSchema(error)) {
        return records;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, operation_id, created_at, source_path, source_kind, "
            "destination_root, actual_destination, status, published, verified, "
            "source_preserved, cleanup_complete, error_message, temporary_path "
            "FROM backup_history ORDER BY created_at DESC, id DESC"))) {
        if (error != nullptr) {
            *error = errorText(query);
        }
        return records;
    }

    while (query.next()) {
        records.push_back(readRecord(query));
    }
    return records;
}

std::optional<BackupHistoryRecord> BackupHistoryRepository::getBackup(
    const qint64 historyId,
    QString *error)
{
    if (!ensureOpen(error) || !createSchema(error)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, operation_id, created_at, source_path, source_kind, "
        "destination_root, actual_destination, status, published, verified, "
        "source_preserved, cleanup_complete, error_message, temporary_path "
        "FROM backup_history WHERE id = ?"));
    query.addBindValue(historyId);
    if (!query.exec() || !query.next()) {
        if (error != nullptr && query.lastError().isValid()) {
            *error = errorText(query);
        }
        return std::nullopt;
    }
    return readRecord(query);
}

bool BackupHistoryRepository::deleteBackup(
    const qint64 historyId,
    QString *error)
{
    if (!ensureOpen(error) || !createSchema(error)) {
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM backup_history WHERE id = ?"));
    query.addBindValue(historyId);
    if (!query.exec()) {
        if (error != nullptr) {
            *error = errorText(query);
        }
        return false;
    }
    return query.numRowsAffected() == 1;
}

QString BackupHistoryRepository::databasePath() const
{
    return databasePath_;
}

bool BackupHistoryRepository::isOpen() const
{
    return database_.isOpen();
}

} // namespace FilePilot

#include "core/database/ExecutionHistoryRepository.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace FilePilot {

namespace {

QString connectionNameFor(const QString &requested)
{
    return requested.isEmpty()
        ? QStringLiteral("FilePilotHistory_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
        : requested;
}

QString nonNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}
QString errorText(const QSqlQuery &query)
{
    const QSqlError error = query.lastError();
    return error.isValid() ? error.text() : QStringLiteral("SQLite operation failed");
}

} // namespace

ExecutionHistoryRepository::ExecutionHistoryRepository(
    QString databasePath,
    QString connectionName)
    : databasePath_(std::move(databasePath))
    , connectionName_(connectionNameFor(connectionName))
{
    database_ = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
}

ExecutionHistoryRepository::~ExecutionHistoryRepository()
{
    if (database_.isOpen()) {
        database_.close();
    }
    const QString connectionName = connectionName_;
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool ExecutionHistoryRepository::initialize(QString *error)
{
    if (!ensureOpen(error)) {
        return false;
    }
    return createSchema(error);
}

bool ExecutionHistoryRepository::ensureOpen(QString *error)
{
    if (database_.isOpen()) {
        return true;
    }
    if (!database_.open()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

bool ExecutionHistoryRepository::createSchema(QString *error)
{
    QSqlQuery pragma(database_);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        if (error != nullptr) {
            *error = errorText(pragma);
        }
        return false;
    }

    QSqlQuery versionQuery(database_);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version"))
        || !versionQuery.next()) {
        if (error != nullptr) {
            *error = errorText(versionQuery);
        }
        return false;
    }

    const int version = versionQuery.value(0).toInt();
    if (version > 1) {
        if (error != nullptr) {
            *error = QStringLiteral("SQLite schema version is newer than FilePilot supports");
        }
        return false;
    }
    if (version == 1) {
        return true;
    }

    const QStringList statements{
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS execution_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "execution_id TEXT NOT NULL UNIQUE,"
            "created_at TEXT NOT NULL,"
            "source_root TEXT NOT NULL,"
            "target_root TEXT NOT NULL,"
            "final_state TEXT NOT NULL,"
            "total_count INTEGER NOT NULL,"
            "succeeded_count INTEGER NOT NULL,"
            "skipped_count INTEGER NOT NULL,"
            "rejected_count INTEGER NOT NULL,"
            "failed_count INTEGER NOT NULL,"
            "cleanup_failed_count INTEGER NOT NULL,"
            "cancelled_count INTEGER NOT NULL,"
            "completed_flag INTEGER NOT NULL,"
            "cancelled_flag INTEGER NOT NULL,"
            "fatal_error TEXT NOT NULL DEFAULT ''"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS execution_history_item ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "execution_id TEXT NOT NULL,"
            "source_path TEXT NOT NULL,"
            "destination_path TEXT NOT NULL,"
            "status TEXT NOT NULL,"
            "error_message TEXT NOT NULL DEFAULT '',"
            "timestamp TEXT NOT NULL,"
            "resumed INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(execution_id) REFERENCES execution_history(execution_id)"
            " ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_execution_history_created_at "
            "ON execution_history(created_at DESC)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_execution_history_item_execution "
            "ON execution_history_item(execution_id)"),
        QStringLiteral("PRAGMA user_version = 1"),
    };

    for (const QString &statement : statements) {
        QSqlQuery query(database_);
        if (!query.exec(statement)) {
            if (error != nullptr) {
                *error = errorText(query);
            }
            return false;
        }
    }
    return true;
}

bool ExecutionHistoryRepository::saveExecutionResult(
    const ExecutionContext &context,
    const TaskState finalState,
    const ExecutionResult &result,
    qint64 *historyId,
    QString *error)
{
    if (!ensureOpen(error)) {
        return false;
    }

    const QString executionId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime createdAt = QDateTime::currentDateTimeUtc();
    if (!database_.transaction()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }

    QSqlQuery insertHistory(database_);
    insertHistory.prepare(QStringLiteral(
        "INSERT INTO execution_history ("
        "execution_id, created_at, source_root, target_root, final_state, "
        "total_count, succeeded_count, skipped_count, rejected_count, "
        "failed_count, cleanup_failed_count, cancelled_count, "
        "completed_flag, cancelled_flag, fatal_error) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insertHistory.addBindValue(executionId);
    insertHistory.addBindValue(createdAt.toString(Qt::ISODateWithMs));
    insertHistory.addBindValue(nonNull(context.scanSourceRoot));
    insertHistory.addBindValue(nonNull(context.targetRoot));
    insertHistory.addBindValue(taskStateName(finalState));
    insertHistory.addBindValue(result.summary.planned);
    insertHistory.addBindValue(result.summary.succeeded);
    insertHistory.addBindValue(result.summary.skipped);
    insertHistory.addBindValue(result.summary.rejected);
    insertHistory.addBindValue(result.summary.failed);
    insertHistory.addBindValue(result.summary.sourceCleanupFailed);
    insertHistory.addBindValue(result.summary.cancelled);
    insertHistory.addBindValue(result.completed ? 1 : 0);
    insertHistory.addBindValue(result.cancelled ? 1 : 0);
    insertHistory.addBindValue(nonNull(result.fatalError));
    if (!insertHistory.exec()) {
        database_.rollback();
        if (error != nullptr) {
            *error = errorText(insertHistory);
        }
        return false;
    }

    const qint64 id = insertHistory.lastInsertId().toLongLong();
    QSqlQuery insertItem(database_);
    insertItem.prepare(QStringLiteral(
        "INSERT INTO execution_history_item ("
        "execution_id, source_path, destination_path, status, error_message, "
        "timestamp, resumed) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    for (const ExecutionItemResult &item : result.items) {
        insertItem.bindValue(0, executionId);
        insertItem.bindValue(1, item.item.sourcePath);
        insertItem.bindValue(2, item.actualDestination.isEmpty()
            ? item.item.destinationPath
            : item.actualDestination);
        insertItem.bindValue(3, executionItemStatusName(item.status));
        insertItem.bindValue(4, nonNull(item.errorMessage));
        insertItem.bindValue(
            5,
            item.timestamp.isValid()
                ? item.timestamp.toUTC().toString(Qt::ISODateWithMs)
                : createdAt.toString(Qt::ISODateWithMs));
        insertItem.bindValue(6, item.resumedPublishedResult ? 1 : 0);
        if (!insertItem.exec()) {
            database_.rollback();
            if (error != nullptr) {
                *error = errorText(insertItem);
            }
            return false;
        }
    }

    if (!database_.commit()) {
        database_.rollback();
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }

    if (historyId != nullptr) {
        *historyId = id;
    }
    return true;
}

ExecutionHistoryRecord ExecutionHistoryRepository::readRecord(const QSqlQuery &query)
{
    ExecutionHistoryRecord record;
    record.id = query.value(0).toLongLong();
    record.executionId = query.value(1).toString();
    record.createdAt = QDateTime::fromString(
        query.value(2).toString(), Qt::ISODateWithMs);
    record.sourceRoot = query.value(3).toString();
    record.targetRoot = query.value(4).toString();
    record.finalState = query.value(5).toString();
    record.summary.planned = query.value(6).toLongLong();
    record.summary.succeeded = query.value(7).toLongLong();
    record.summary.skipped = query.value(8).toLongLong();
    record.summary.rejected = query.value(9).toLongLong();
    record.summary.failed = query.value(10).toLongLong();
    record.summary.sourceCleanupFailed = query.value(11).toLongLong();
    record.summary.cancelled = query.value(12).toLongLong();
    record.completed = query.value(13).toInt() != 0;
    record.cancelled = query.value(14).toInt() != 0;
    record.fatalError = query.value(15).toString();
    return record;
}

std::vector<ExecutionHistoryRecord> ExecutionHistoryRepository::listExecutions(
    QString *error)
{
    std::vector<ExecutionHistoryRecord> records;
    if (!ensureOpen(error)) {
        return records;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, execution_id, created_at, source_root, target_root, "
            "final_state, total_count, succeeded_count, skipped_count, "
            "rejected_count, failed_count, cleanup_failed_count, cancelled_count, "
            "completed_flag, cancelled_flag, fatal_error "
            "FROM execution_history ORDER BY created_at DESC, id DESC"))) {
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

std::optional<ExecutionHistoryDetail> ExecutionHistoryRepository::getExecution(
    const qint64 historyId,
    QString *error)
{
    if (!ensureOpen(error)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, execution_id, created_at, source_root, target_root, "
        "final_state, total_count, succeeded_count, skipped_count, "
        "rejected_count, failed_count, cleanup_failed_count, cancelled_count, "
        "completed_flag, cancelled_flag, fatal_error "
        "FROM execution_history WHERE id = ?"));
    query.addBindValue(historyId);
    if (!query.exec() || !query.next()) {
        if (error != nullptr) {
            *error = query.lastError().isValid()
                ? query.lastError().text()
                : QStringLiteral("History record was not found");
        }
        return std::nullopt;
    }

    ExecutionHistoryDetail detail;
    detail.record = readRecord(query);
    detail.result.summary = detail.record.summary;
    detail.result.completed = detail.record.completed;
    detail.result.cancelled = detail.record.cancelled;
    detail.result.fatalError = detail.record.fatalError;

    QSqlQuery itemQuery(database_);
    itemQuery.prepare(QStringLiteral(
        "SELECT source_path, destination_path, status, error_message, "
        "timestamp, resumed FROM execution_history_item "
        "WHERE execution_id = ? ORDER BY id"));
    itemQuery.addBindValue(detail.record.executionId);
    if (!itemQuery.exec()) {
        if (error != nullptr) {
            *error = errorText(itemQuery);
        }
        return std::nullopt;
    }

    while (itemQuery.next()) {
        OrganizePlanItem item;
        item.sourcePath = itemQuery.value(0).toString();
        item.destinationPath = itemQuery.value(1).toString();
        ExecutionItemResult resultItem;
        resultItem.item = item;
        resultItem.actualDestination = item.destinationPath;
        resultItem.status = executionItemStatusFromName(itemQuery.value(2).toString());
        resultItem.errorMessage = itemQuery.value(3).toString();
        resultItem.timestamp = QDateTime::fromString(
            itemQuery.value(4).toString(), Qt::ISODateWithMs);
        resultItem.resumedPublishedResult = itemQuery.value(5).toInt() != 0;
        detail.result.items.push_back(std::move(resultItem));
    }

    return detail;
}

bool ExecutionHistoryRepository::deleteExecution(
    const qint64 historyId,
    QString *error)
{
    if (!ensureOpen(error)) {
        return false;
    }
    if (!database_.transaction()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }

    QSqlQuery deleteItems(database_);
    deleteItems.prepare(QStringLiteral(
        "DELETE FROM execution_history_item WHERE execution_id = "
        "(SELECT execution_id FROM execution_history WHERE id = ?)"));
    deleteItems.addBindValue(historyId);
    if (!deleteItems.exec()) {
        database_.rollback();
        if (error != nullptr) {
            *error = errorText(deleteItems);
        }
        return false;
    }

    QSqlQuery deleteHistory(database_);
    deleteHistory.prepare(QStringLiteral(
        "DELETE FROM execution_history WHERE id = ?"));
    deleteHistory.addBindValue(historyId);
    if (!deleteHistory.exec()) {
        database_.rollback();
        if (error != nullptr) {
            *error = errorText(deleteHistory);
        }
        return false;
    }

    if (!database_.commit()) {
        database_.rollback();
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }
    return true;
}

QString ExecutionHistoryRepository::databasePath() const
{
    return databasePath_;
}

bool ExecutionHistoryRepository::isOpen() const
{
    return database_.isOpen();
}

} // namespace FilePilot

#pragma once

#include "core/database/HistoryTypes.h"
#include "core/execution/ExecutionTypes.h"
#include "core/model/TaskState.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <vector>

namespace FilePilot {

class ExecutionHistoryRepository
{
public:
    explicit ExecutionHistoryRepository(
        QString databasePath,
        QString connectionName = {});
    ~ExecutionHistoryRepository();

    ExecutionHistoryRepository(const ExecutionHistoryRepository &) = delete;
    ExecutionHistoryRepository &operator=(const ExecutionHistoryRepository &) = delete;

    bool initialize(QString *error = nullptr);
    bool saveExecutionResult(
        const ExecutionContext &context,
        TaskState finalState,
        const ExecutionResult &result,
        qint64 *historyId = nullptr,
        QString *error = nullptr);
    std::vector<ExecutionHistoryRecord> listExecutions(QString *error = nullptr);
    std::optional<ExecutionHistoryDetail> getExecution(
        qint64 historyId,
        QString *error = nullptr);
    bool deleteExecution(qint64 historyId, QString *error = nullptr);

    QString databasePath() const;
    bool isOpen() const;

private:
    bool ensureOpen(QString *error);
    bool createSchema(QString *error);
    static ExecutionHistoryRecord readRecord(const QSqlQuery &query);

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace FilePilot

#pragma once

#include "core/execution/ExecutionTypes.h"
#include "core/model/TaskState.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace FilePilot {

struct ExecutionHistoryRecord {
    qint64 id = 0;
    QString executionId;
    QDateTime createdAt;
    QString sourceRoot;
    QString targetRoot;
    QString finalState;
    ExecutionSummary summary;
    bool completed = false;
    bool cancelled = false;
    QString fatalError;
};

struct ExecutionHistoryDetail {
    ExecutionHistoryRecord record;
    ExecutionResult result;
};

} // namespace FilePilot

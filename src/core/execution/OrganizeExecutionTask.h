#pragma once

#include "core/execution/ExecutionTypes.h"
#include "core/model/TaskState.h"
#include "core/organize/OrganizePlan.h"

#include <QObject>

#include <atomic>
#include <memory>

class QString;
class QThread;

namespace FilePilot {

class OrganizeExecutionTask : public QObject
{
    Q_OBJECT

public:
    explicit OrganizeExecutionTask(QObject *parent = nullptr);
    ~OrganizeExecutionTask() override;

    OrganizeExecutionTask(const OrganizeExecutionTask &) = delete;
    OrganizeExecutionTask &operator=(const OrganizeExecutionTask &) = delete;

    bool start(const OrganizePlan &plan,
               const ExecutionContext &context,
               ConflictPolicy policy = ConflictPolicy::AutoRename);
    void cancel();

    bool isActive() const;
    TaskState state() const;

signals:
    void stateChanged(FilePilot::TaskState state);
    void progressChanged(qint64 completed,
                         qint64 total,
                         QString currentFile);
    void itemProgressChanged(FilePilot::ExecutionProgressUpdate progress);
    void completed(FilePilot::ExecutionResult result);
    void failed(QString message);
    void cancelled();

private:
    bool setState(TaskState state);
    void detachWorker(QThread *thread);

    QThread *workerThread_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellationToken_;
    std::atomic_bool active_{false};
    std::atomic<TaskState> state_{TaskState::Idle};
    QHash<QString, PublishedMoveRecoveryState> recoveryStates_;
};

} // namespace FilePilot

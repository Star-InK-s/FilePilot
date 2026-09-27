#pragma once

#include "core/backup/BackupExecutionTypes.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/model/TaskState.h"

#include <QMutex>
#include <QObject>

#include <atomic>
#include <memory>

class QThread;

namespace FilePilot {

class BackupTask : public QObject
{
    Q_OBJECT

public:
    explicit BackupTask(QObject *parent = nullptr);
    ~BackupTask() override;

    BackupTask(const BackupTask &) = delete;
    BackupTask &operator=(const BackupTask &) = delete;

    bool start(const BackupPlan &plan);
    void cancel();

    bool isActive() const;
    TaskState state() const;
    BackupExecutionResult result() const;
    bool hasResult() const;

signals:
    void stateChanged(FilePilot::TaskState state);
    void progressChanged(qint64 completed,
                         qint64 total,
                         QString currentFile,
                         QString phase);
    void currentFileChanged(QString currentFile);
    void completed(FilePilot::BackupExecutionResult result);
    void failed(QString message);
    void cancelled();

private:
    bool setState(TaskState state);
    void detachWorker(QThread *thread);

    QThread *workerThread_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellationToken_;
    std::atomic_bool active_{false};
    std::atomic<TaskState> state_{TaskState::Idle};
    mutable QMutex resultMutex_;
    BackupExecutionResult result_;
    bool hasResult_ = false;
};

} // namespace FilePilot

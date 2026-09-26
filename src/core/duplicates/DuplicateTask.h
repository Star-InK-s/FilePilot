#pragma once

#include "core/duplicates/DuplicateTypes.h"
#include "core/model/TaskState.h"
#include "core/scan/ScanService.h"

#include <QObject>

class QString;
class QThread;

namespace FilePilot {

class DuplicateTask : public QObject
{
    Q_OBJECT

public:
    explicit DuplicateTask(QObject *parent = nullptr);
    ~DuplicateTask() override;

    DuplicateTask(const DuplicateTask &) = delete;
    DuplicateTask &operator=(const DuplicateTask &) = delete;

    bool start(const ScanResult &scanResult);
    void cancel();

    bool isActive() const;
    TaskState state() const;

signals:
    void stateChanged(FilePilot::TaskState state);
    void progressChanged(FilePilot::DuplicateProgress progress);
    void errorReported(FilePilot::DuplicateError error);
    void completed(FilePilot::DuplicateResult result);
    void failed(QString message);
    void cancelled();

private:
    bool setState(TaskState state);
    void detachWorker(QThread *thread);

    QThread *workerThread_ = nullptr;
    ScanCancellationToken cancellationToken_;
    std::atomic_bool active_{false};
    std::atomic<TaskState> state_{TaskState::Idle};
};

} // namespace FilePilot

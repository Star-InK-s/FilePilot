#pragma once

#include "core/model/TaskState.h"
#include "core/scan/ScanService.h"

#include <QObject>

#include <atomic>

class QString;
class QThread;

namespace FilePilot {

class ScanTask : public QObject
{
    Q_OBJECT

public:
    explicit ScanTask(QObject *parent = nullptr);
    ~ScanTask() override;

    ScanTask(const ScanTask &) = delete;
    ScanTask &operator=(const ScanTask &) = delete;

    bool start(const QString &rootPath);
    void cancel();

    bool isActive() const;
    TaskState state() const;

signals:
    void stateChanged(FilePilot::TaskState state);
    void progressChanged(qint64 scannedFileCount,
                         QString currentDirectory,
                         QString currentFile);
    void errorReported(FilePilot::ScanError error);
    void completed(FilePilot::ScanResult result);
    void failed(QString message);
    void cancelled();

private:
    void setState(TaskState state);
    void detachWorker(QThread *thread);

    QThread *workerThread_ = nullptr;
    ScanCancellationToken cancellationToken_;
    std::atomic_bool active_{false};
    std::atomic<TaskState> state_{TaskState::Idle};
};

} // namespace FilePilot

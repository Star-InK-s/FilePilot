#include "core/tasks/ScanTask.h"

#include <QThread>

#include <chrono>

namespace FilePilot {

namespace {

constexpr auto progressInterval = std::chrono::milliseconds(50);
constexpr auto errorInterval = std::chrono::milliseconds(100);

} // namespace

ScanTask::ScanTask(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ScanError>();
    qRegisterMetaType<ScanProgress>();
    qRegisterMetaType<ScanResult>();
}

ScanTask::~ScanTask()
{
    cancel();

    QThread *thread = workerThread_;
    if (thread != nullptr) {
        workerThread_ = nullptr;
        disconnect(thread, nullptr, this, nullptr);
        thread->requestInterruption();
        thread->wait();
        delete thread;
    }
}

bool ScanTask::start(const QString &rootPath)
{
    if (active_.exchange(true)) {
        return false;
    }

    cancellationToken_ = ScanCancellationToken();
    setState(TaskState::Preparing);

    auto *thread = QThread::create([this, rootPath] {
        setState(TaskState::Running);

        ScanService service;
        auto lastProgress = std::chrono::steady_clock::now() - progressInterval;
        auto lastError = std::chrono::steady_clock::now() - errorInterval;

        const ScanProgressCallback progressCallback =
            [this, &lastProgress](const ScanProgress &progress) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastProgress < progressInterval) {
                    return;
                }

                lastProgress = now;
                emit progressChanged(
                    progress.scannedFileCount,
                    progress.currentDirectory,
                    progress.currentFile);
            };

        const ScanErrorCallback errorCallback =
            [this, &lastError](const ScanError &error) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastError < errorInterval) {
                    return;
                }

                lastError = now;
                emit errorReported(error);
            };

        ScanResult result = service.scan(
            rootPath,
            cancellationToken_,
            progressCallback,
            errorCallback);

        emit progressChanged(
            result.statistics.fileCount,
            result.rootPath,
            QString());

        if (result.cancelled) {
            setState(TaskState::Cancelled);
            emit cancelled();
            return;
        }

        if (!result.completed) {
            setState(TaskState::Failed);
            emit failed(result.fatalError);
            return;
        }

        setState(result.statistics.errorCount > 0
                     ? TaskState::CompletedWithErrors
                     : TaskState::Completed);
        emit completed(result);
    });

    connect(thread,
            &QThread::finished,
            this,
            [this, thread] { detachWorker(thread); });

    workerThread_ = thread;
    thread->setObjectName(QStringLiteral("FilePilotScanWorker"));
    thread->start();
    return true;
}

void ScanTask::cancel()
{
    if (!active_.load() || isTerminalTaskState(state_.load())) {
        return;
    }

    cancellationToken_.cancel();
    setState(TaskState::Cancelling);
}

bool ScanTask::isActive() const
{
    return active_.load();
}

TaskState ScanTask::state() const
{
    return state_.load();
}

void ScanTask::setState(const TaskState state)
{
    const TaskState previous = state_.exchange(state);
    if (previous != state) {
        emit stateChanged(state);
    }
}

void ScanTask::detachWorker(QThread *thread)
{
    active_.store(false);
    if (workerThread_ == thread) {
        workerThread_ = nullptr;
    }

    thread->deleteLater();
}

} // namespace FilePilot


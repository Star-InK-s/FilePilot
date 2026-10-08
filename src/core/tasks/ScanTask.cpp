#include "core/tasks/ScanTask.h"

#include "core/classify/RuleEngine.h"

#include <QThread>

#include <chrono>
#include <exception>

namespace FilePilot {

namespace {

constexpr auto progressInterval = std::chrono::milliseconds(50);
constexpr auto errorInterval = std::chrono::milliseconds(100);
constexpr qsizetype maxErrorDetailsPerBatch = 64;

bool canTransition(const TaskState from, const TaskState to)
{
    if (from == to) {
        return true;
    }

    if (isTerminalTaskState(from)) {
        return false;
    }

    switch (from) {
    case TaskState::Idle:
        return to == TaskState::Preparing || to == TaskState::Cancelling;
    case TaskState::Preparing:
        return to == TaskState::Running
            || to == TaskState::Cancelling
            || to == TaskState::Failed;
    case TaskState::Running:
        return to == TaskState::Cancelling
            || to == TaskState::Completed
            || to == TaskState::CompletedWithErrors
            || to == TaskState::Failed;
    case TaskState::Cancelling:
        return to == TaskState::Cancelled
            || to == TaskState::Completed
            || to == TaskState::CompletedWithErrors
            || to == TaskState::Failed;
    case TaskState::Completed:
    case TaskState::CompletedWithErrors:
    case TaskState::Cancelled:
    case TaskState::Failed:
        return false;
    }

    return false;
}

} // namespace

ScanTask::ScanTask(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ScanError>();
    qRegisterMetaType<ScanProgress>();
    qRegisterMetaType<ScanErrorBatch>();
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

    if (workerThread_ != nullptr) {
        active_.store(false);
        return false;
    }

    // A new explicit run gets a fresh state. Terminal states remain immutable
    // for the duration of the run they describe.
    if (isTerminalTaskState(state_.load())) {
        state_.store(TaskState::Idle);
    }

    cancellationToken_ = ScanCancellationToken();
    setState(TaskState::Preparing);

    // The worker calls ScanService and reports through signals; it never
    // updates QWidget state directly.
    auto *thread = QThread::create([this, rootPath] {
        qint64 totalErrorCount = 0;
        qint64 flushedErrorCount = 0;
        QList<ScanError> pendingErrors;
        auto lastErrorFlush = std::chrono::steady_clock::now() - errorInterval;

        const auto flushErrors = [this,
                                  &totalErrorCount,
                                  &flushedErrorCount,
                                  &pendingErrors,
                                  &lastErrorFlush] {
            if (totalErrorCount == flushedErrorCount && pendingErrors.isEmpty()) {
                return;
            }

            emit errorBatchReported(ScanErrorBatch{
                totalErrorCount,
                pendingErrors,
            });
            flushedErrorCount = totalErrorCount;
            pendingErrors.clear();
            lastErrorFlush = std::chrono::steady_clock::now();
        };

        try {
            if (cancellationToken_.isCancelled()) {
                flushErrors();
                if (setState(TaskState::Cancelled)) {
                    emit cancelled();
                }
                return;
            }

            if (!setState(TaskState::Running)) {
                flushErrors();
                if (state_.load() == TaskState::Cancelling
                    && setState(TaskState::Cancelled)) {
                    emit cancelled();
                }
                return;
            }

            ScanService service;
            auto lastProgress =
                std::chrono::steady_clock::now() - progressInterval;

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
                [this,
                 &totalErrorCount,
                 &pendingErrors,
                 &lastErrorFlush,
                 &flushErrors](const ScanError &error) {
                    ++totalErrorCount;
                    if (pendingErrors.size() < maxErrorDetailsPerBatch) {
                        pendingErrors.append(error);
                    }

                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastErrorFlush >= errorInterval) {
                        flushErrors();
                    }
                };
            ScanResult result = service.scan(
                rootPath,
                cancellationToken_,
                progressCallback,
                errorCallback);

            if (result.completed) {
                RuleEngine().classify(result);
            }

            flushErrors();
            emit progressChanged(
                result.statistics.fileCount,
                result.rootPath,
                QString());

            if (result.cancelled) {
                if (setState(TaskState::Cancelled)) {
                    emit cancelled();
                }
                return;
            }

            if (!result.completed) {
                if (setState(TaskState::Failed)) {
                    emit failed(result.fatalError);
                }
                return;
            }

            const TaskState finalState =
                result.statistics.errorCount > 0
                    ? TaskState::CompletedWithErrors
                    : TaskState::Completed;
            if (setState(finalState)) {
                emit completed(result);
            }
        } catch (const std::exception &exception) {
            flushErrors();
            const QString message =
                QStringLiteral("扫描后台异常：%1")
                    .arg(QString::fromLocal8Bit(exception.what()));
            if (setState(TaskState::Failed)) {
                emit failed(message);
            }
        } catch (...) {
            flushErrors();
            if (setState(TaskState::Failed)) {
                emit failed(QStringLiteral("扫描后台发生未知异常"));
            }
        }
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

bool ScanTask::setState(const TaskState state)
{
    TaskState previous = state_.load();
    while (true) {
        if (previous == state || !canTransition(previous, state)) {
            return false;
        }

        if (state_.compare_exchange_weak(previous, state)) {
            emit stateChanged(state);
            return true;
        }
    }
}

void ScanTask::detachWorker(QThread *thread)
{
    active_.store(false);
    if (workerThread_ == thread) {
        workerThread_ = nullptr;
    }

    if (!isTerminalTaskState(state_.load()) && setState(TaskState::Failed)) {
        emit failed(QStringLiteral("扫描线程意外结束"));
    }

    thread->deleteLater();
}

} // namespace FilePilot

#include "core/duplicates/DuplicateTask.h"

#include "core/duplicates/DuplicateFinder.h"

#include <QThread>

#include <exception>

namespace FilePilot {

namespace {

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

DuplicateTask::DuplicateTask(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DuplicateItem>();
    qRegisterMetaType<DuplicateGroup>();
    qRegisterMetaType<DuplicateError>();
    qRegisterMetaType<DuplicateProgress>();
    qRegisterMetaType<DuplicateResult>();
}

DuplicateTask::~DuplicateTask()
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

bool DuplicateTask::start(const ScanResult &scanResult)
{
    if (active_.exchange(true)) {
        return false;
    }
    if (workerThread_ != nullptr) {
        active_.store(false);
        return false;
    }
    if (isTerminalTaskState(state_.load())) {
        state_.store(TaskState::Idle);
    }

    cancellationToken_ = ScanCancellationToken();
    setState(TaskState::Preparing);

    const ScanResult scanCopy = scanResult;
    auto *thread = QThread::create([this, scanCopy] {
        try {
            if (cancellationToken_.isCancelled()) {
                DuplicateResult result;
                result.state = TaskState::Cancelled;
                result.completed = true;
                result.cancelled = true;
                if (setState(TaskState::Cancelled)) {
                    emit cancelled();
                    emit completed(result);
                }
                return;
            }

            if (!setState(TaskState::Running)) {
                if (state_.load() == TaskState::Cancelling
                    && setState(TaskState::Cancelled)) {
                    DuplicateResult cancelledResult;
                    cancelledResult.state = TaskState::Cancelled;
                    cancelledResult.completed = true;
                    cancelledResult.cancelled = true;
                    emit cancelled();
                    emit completed(cancelledResult);
                }
                return;
            }

            DuplicateFinder finder;
            const DuplicateResult result = finder.findDuplicates(
                scanCopy,
                cancellationToken_,
                [this](const DuplicateProgress &progress) {
                    emit progressChanged(progress);
                },
                [this](const DuplicateError &error) {
                    emit errorReported(error);
                });

            if (result.cancelled) {
                if (setState(TaskState::Cancelled)) {
                    emit cancelled();
                    emit completed(result);
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
                result.summary.errorCount > 0
                    ? TaskState::CompletedWithErrors
                    : TaskState::Completed;
            if (setState(finalState)) {
                emit completed(result);
            }
        } catch (const std::exception &exception) {
            const QString message =
                QStringLiteral("重复文件检测后台异常：%1")
                    .arg(QString::fromLocal8Bit(exception.what()));
            if (setState(TaskState::Failed)) {
                emit failed(message);
            }
        } catch (...) {
            if (setState(TaskState::Failed)) {
                emit failed(QStringLiteral("重复文件检测后台发生未知异常"));
            }
        }
    });

    connect(thread, &QThread::finished, this, [this, thread] {
        detachWorker(thread);
    });
    workerThread_ = thread;
    thread->setObjectName(QStringLiteral("FilePilotDuplicateWorker"));
    thread->start();
    return true;
}

void DuplicateTask::cancel()
{
    if (!active_.load() || isTerminalTaskState(state_.load())) {
        return;
    }

    cancellationToken_.cancel();
    setState(TaskState::Cancelling);
}

bool DuplicateTask::isActive() const
{
    return active_.load();
}

TaskState DuplicateTask::state() const
{
    return state_.load();
}

bool DuplicateTask::setState(const TaskState state)
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

void DuplicateTask::detachWorker(QThread *thread)
{
    active_.store(false);
    if (workerThread_ == thread) {
        workerThread_ = nullptr;
    }
    if (!isTerminalTaskState(state_.load()) && setState(TaskState::Failed)) {
        emit failed(QStringLiteral("重复文件检测线程意外结束"));
    }
    thread->deleteLater();
}

} // namespace FilePilot

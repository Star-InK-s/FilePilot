#include "core/backup/BackupTask.h"

#include "core/backup/BackupExecutor.h"

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

BackupTask::BackupTask(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BackupExecutionResult>();
    qRegisterMetaType<TaskState>();
}

BackupTask::~BackupTask()
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

bool BackupTask::start(const BackupPlan &plan)
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

    {
        QMutexLocker locker(&resultMutex_);
        result_ = BackupExecutionResult{};
        hasResult_ = false;
    }

    cancellationToken_ = std::make_shared<std::atomic_bool>(false);
    setState(TaskState::Preparing);

    const BackupPlan planCopy = plan;
    auto *thread = QThread::create([this, planCopy, cancellationToken = cancellationToken_] {
        BackupExecutorHooks hooks;
        qint64 completedItems = 0;
        const qint64 totalItems =
            planCopy.items.empty() ? 1 : static_cast<qint64>(planCopy.items.size());

        hooks.beforeCopy = [this, &completedItems, totalItems](
                               const QString &source,
                               const QString &,
                               QString &) {
            emit currentFileChanged(source);
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("copy"));
            return true;
        };
        hooks.duringCopy = [this, &completedItems, totalItems](
                               qint64,
                               const QString &source,
                               const QString &,
                               QString &) {
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("copy"));
            return true;
        };
        hooks.afterCopy = [this, &completedItems, totalItems](
                               const QString &source,
                               const QString &,
                               QString &) {
            ++completedItems;
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("verify"));
            return true;
        };
        hooks.beforeVerify = [this, &completedItems, totalItems](
                                  const QString &source,
                                  const QString &,
                                  QString &) {
            emit currentFileChanged(source);
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("verify"));
            return true;
        };
        hooks.beforePublish = [this, &completedItems, totalItems](
                                   const QString &source,
                                   const QString &,
                                   QString &) {
            emit currentFileChanged(source);
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("publish"));
            return true;
        };
        hooks.afterPublish = [this, &completedItems, totalItems](
                                  const QString &source,
                                  const QString &,
                                  QString &) {
            emit progressChanged(completedItems, totalItems, source, QStringLiteral("finalize"));
            return true;
        };

        try {
            if (!setState(TaskState::Running)) {
                if (cancellationToken->load(std::memory_order_relaxed)
                    || state_.load() == TaskState::Cancelling) {
                    BackupExecutionResult cancelledResult;
                    cancelledResult.status = BackupExecutionStatus::Cancelled;
                    cancelledResult.errorMessage = QStringLiteral("备份已取消");
                    cancelledResult.error = AppError{
                        ErrorCode::Cancelled,
                        cancelledResult.errorMessage,
                        planCopy.finalDestinationPath,
                    };
                    {
                        QMutexLocker locker(&resultMutex_);
                        result_ = cancelledResult;
                        hasResult_ = true;
                    }
                    setState(TaskState::Cancelled);
                    emit cancelled();
                    emit completed(cancelledResult);
                    return;
                }
                BackupExecutionResult failedResult;
                failedResult.errorMessage = QStringLiteral("备份任务无法进入运行状态");
                failedResult.error = AppError{
                    ErrorCode::Unknown,
                    failedResult.errorMessage,
                    planCopy.finalDestinationPath,
                };
                {
                    QMutexLocker locker(&resultMutex_);
                    result_ = failedResult;
                    hasResult_ = true;
                }
                setState(TaskState::Failed);
                emit failed(failedResult.errorMessage);
                emit completed(failedResult);
                return;
            }

            const BackupExecutionResult executionResult =
                BackupExecutor().execute(planCopy, *cancellationToken, hooks);
            {
                QMutexLocker locker(&resultMutex_);
                result_ = executionResult;
                hasResult_ = true;
            }

            if (executionResult.status == BackupExecutionStatus::Cancelled) {
                setState(TaskState::Cancelled);
                emit cancelled();
                emit completed(executionResult);
                return;
            }

            const bool cleanSuccess =
                executionResult.status == BackupExecutionStatus::Succeeded
                || executionResult.status == BackupExecutionStatus::Skipped;
            const TaskState finalState = cleanSuccess
                ? TaskState::Completed
                : executionResult.status == BackupExecutionStatus::CleanupFailed
                    ? TaskState::CompletedWithErrors
                    : TaskState::Failed;
            setState(finalState);
            if (!cleanSuccess) {
                emit failed(executionResult.errorMessage);
            }
            emit completed(executionResult);
        } catch (const std::exception &exception) {
            const QString message = QStringLiteral("备份后台异常：%1")
                .arg(QString::fromLocal8Bit(exception.what()));
            BackupExecutionResult failedResult;
            failedResult.status = BackupExecutionStatus::Failed;
            failedResult.errorMessage = message;
            failedResult.error = AppError{
                ErrorCode::Unknown,
                message,
                planCopy.finalDestinationPath,
            };
            {
                QMutexLocker locker(&resultMutex_);
                result_ = failedResult;
                hasResult_ = true;
            }
            setState(TaskState::Failed);
            emit failed(message);
            emit completed(failedResult);
        } catch (...) {
            const QString message = QStringLiteral("备份后台发生未知异常");
            BackupExecutionResult failedResult;
            failedResult.status = BackupExecutionStatus::Failed;
            failedResult.errorMessage = message;
            failedResult.error = AppError{
                ErrorCode::Unknown,
                message,
                planCopy.finalDestinationPath,
            };
            {
                QMutexLocker locker(&resultMutex_);
                result_ = failedResult;
                hasResult_ = true;
            }
            setState(TaskState::Failed);
            emit failed(message);
            emit completed(failedResult);
        }
    });

    connect(thread, &QThread::finished, this, [this, thread] {
        detachWorker(thread);
    });

    workerThread_ = thread;
    thread->setObjectName(QStringLiteral("FilePilotBackupWorker"));
    thread->start();
    return true;
}

void BackupTask::cancel()
{
    if (!active_.load() || isTerminalTaskState(state_.load())) {
        return;
    }

    if (cancellationToken_) {
        cancellationToken_->store(true, std::memory_order_relaxed);
    }
    setState(TaskState::Cancelling);
}

bool BackupTask::isActive() const
{
    return active_.load();
}

TaskState BackupTask::state() const
{
    return state_.load();
}

BackupExecutionResult BackupTask::result() const
{
    QMutexLocker locker(&resultMutex_);
    return result_;
}

bool BackupTask::hasResult() const
{
    QMutexLocker locker(&resultMutex_);
    return hasResult_;
}

bool BackupTask::setState(const TaskState state)
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

void BackupTask::detachWorker(QThread *thread)
{
    active_.store(false);
    if (workerThread_ == thread) {
        workerThread_ = nullptr;
    }

    if (!isTerminalTaskState(state_.load()) && setState(TaskState::Failed)) {
        emit failed(QStringLiteral("备份线程意外结束"));
    }

    thread->deleteLater();
}

} // namespace FilePilot

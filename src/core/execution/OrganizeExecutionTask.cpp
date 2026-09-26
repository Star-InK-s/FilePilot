#include "core/execution/OrganizeExecutionTask.h"

#include "core/execution/ConflictResolver.h"
#include "core/execution/FileOperator.h"
#include "core/organize/OrganizeExecutionPrevalidator.h"
#include "core/organize/OrganizePathValidator.h"

#include <QDateTime>
#include <QSet>
#include <QThread>

#include <chrono>
#include <exception>
#include <utility>

namespace FilePilot {

namespace {

constexpr auto progressInterval = std::chrono::milliseconds(50);

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

bool matchesExecutionContext(
    const OrganizePlan &plan,
    const ExecutionContext &context)
{
    const OrganizePlanProvenance &provenance = plan.provenance();
    if (context.targetRoot.isEmpty()
        || context.scanSourceRoot.isEmpty()
        || context.planGeneration == 0
        || context.scanGeneration == 0
        || provenance.planGeneration != context.planGeneration
        || provenance.scanGeneration != context.scanGeneration
        || !OrganizePathValidator::pathsEqual(
              provenance.normalizedTargetRoot, context.targetRoot)
        || !OrganizePathValidator::pathsEqual(
              provenance.scanSourceRoot, context.scanSourceRoot)) {
        return false;
    }

    for (const OrganizePlanItem &item : plan.executableCandidates()) {
        if (item.sourcePath.isEmpty()
            || !OrganizePathValidator::isPathInsideRoot(
                   context.scanSourceRoot, item.sourcePath)) {
            return false;
        }
    }

    return true;
}
void addResult(ExecutionResult &result, ExecutionItemResult item)
{
    switch (item.status) {
    case ExecutionItemStatus::Succeeded:
        ++result.summary.succeeded;
        break;
    case ExecutionItemStatus::Skipped:
        ++result.summary.skipped;
        break;
    case ExecutionItemStatus::Failed:
        ++result.summary.failed;
        break;
    case ExecutionItemStatus::Rejected:
        ++result.summary.rejected;
        break;
    case ExecutionItemStatus::Cancelled:
        ++result.summary.cancelled;
        break;
    case ExecutionItemStatus::SourceCleanupFailed:
        ++result.summary.sourceCleanupFailed;
        break;
    }

    result.items.push_back(std::move(item));
}

QString effectiveExecutionId(
    const ExecutionContext &context,
    const ConflictPolicy policy)
{
    if (!context.executionId.isEmpty()) {
        return context.executionId
            + QLatin1Char(':')
            + QString::number(static_cast<int>(policy));
    }

    return OrganizePathValidator::normalizePath(context.targetRoot).toLower()
        + QLatin1Char('\n')
        + OrganizePathValidator::normalizePath(context.scanSourceRoot).toLower()
        + QLatin1Char('\n')
        + QString::number(context.planGeneration)
        + QLatin1Char(':')
        + QString::number(context.scanGeneration)
        + QLatin1Char(':')
        + QString::number(static_cast<int>(policy));
}

QString recoveryKey(
    const OrganizePlanItem &item,
    const QString &executionId)
{
    return executionId
        + QLatin1Char('\n')
        + OrganizePathValidator::normalizePath(item.sourcePath).toLower()
        + QLatin1Char('\n')
        + OrganizePathValidator::normalizePath(item.destinationPath).toLower();
}

ExecutionItemResult makeResult(
    const OrganizePlanItem &item,
    const ExecutionItemStatus status,
    const QString &destination,
    const QString &message)
{
    return ExecutionItemResult{
        item,
        destination,
        status,
        message,
        QDateTime::currentDateTimeUtc(),
        false,
    };
}

} // namespace

OrganizeExecutionTask::OrganizeExecutionTask(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ExecutionProgressUpdate>();
    qRegisterMetaType<ExecutionItemResult>();
    qRegisterMetaType<ExecutionResult>();
}

OrganizeExecutionTask::~OrganizeExecutionTask()
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

bool OrganizeExecutionTask::start(
    const OrganizePlan &plan,
    const ExecutionContext &context,
    const ConflictPolicy policy)
{
    if (!matchesExecutionContext(plan, context)) {
        return false;
    }
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

    cancellationToken_ = std::make_shared<std::atomic_bool>(false);
    setState(TaskState::Preparing);

    const QString executionId = effectiveExecutionId(context, policy);
    for (auto recovery = recoveryStates_.begin();
         recovery != recoveryStates_.end();) {
        if (recovery.value().executionId != executionId) {
            recovery = recoveryStates_.erase(recovery);
        } else {
            ++recovery;
        }
    }

    const OrganizePlan planCopy = plan;
    auto *thread = QThread::create(
        [this, planCopy, policy, executionId,
         planGeneration = context.planGeneration,
         scanGeneration = context.scanGeneration] {        ExecutionResult result;
        bool wasCancelled = false;
        OrganizeExecutionPrevalidator prevalidator;
        ConflictResolver conflictResolver;
        FileOperator fileOperator;
        auto lastProgress = std::chrono::steady_clock::now() - progressInterval;

        try {
            if (!setState(TaskState::Running)) {
                if (cancellationToken_->load(std::memory_order_relaxed)
                    || state_.load() == TaskState::Cancelling) {
                    const auto cancelledCandidates =
                        planCopy.executableCandidates();
                    result.summary.planned =
                        static_cast<qint64>(cancelledCandidates.size());
                    for (const OrganizePlanItem &item : cancelledCandidates) {
                        addResult(
                            result,
                            makeResult(
                                item,
                                ExecutionItemStatus::Cancelled,
                                item.destinationPath,
                                QStringLiteral("执行已取消")));
                    }
                    result.completed = true;
                    result.cancelled = true;
                    setState(TaskState::Cancelled);
                    emit cancelled();
                    emit completed(std::move(result));
                    return;
                }

                result.fatalError = QStringLiteral("执行任务无法进入运行状态");
                setState(TaskState::Failed);
                emit failed(result.fatalError);
                return;
            }
            const ExecutionValidationResult targetValidation =
                prevalidator.validateTargetRoot(planCopy.provenance());
            if (!targetValidation.valid) {
                result.fatalError = QStringLiteral("目标根目录执行前验证失败：%1")
                    .arg(targetValidation.message);
                setState(TaskState::Failed);
                emit failed(result.fatalError);
                return;
            }

            const std::vector<OrganizePlanItem> candidates =
                planCopy.executableCandidates();
            result.summary.planned = static_cast<qint64>(candidates.size());
            QSet<QString> reservedDestinations;
            const auto reportItemProgress =
                [this, &result](
                    const OrganizePlanItem &item,
                    const QString &destination,
                    const QString &action,
                    const QString &phase) {
                    emit itemProgressChanged(ExecutionProgressUpdate{
                        static_cast<qint64>(result.items.size()),
                        result.summary.planned,
                        item.sourcePath,
                        destination,
                        action,
                        phase,
                        result.summary,
                    });
                };
            const auto reportProgress =
                [this, &lastProgress, &result](const QString &currentFile) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastProgress < progressInterval) {
                        return;
                    }
                    lastProgress = now;
                    emit progressChanged(
                        static_cast<qint64>(result.items.size()),
                        result.summary.planned,
                        currentFile);
                };

            for (const OrganizePlanItem &item : candidates) {
                if (cancellationToken_->load(std::memory_order_relaxed)) {
                    wasCancelled = true;
                    addResult(
                        result,
                        makeResult(
                            item,
                            ExecutionItemStatus::Cancelled,
                            item.destinationPath,
                            QStringLiteral("执行已取消")));
                    reportProgress(item.sourcePath);
                    continue;
                }

                const ExecutionValidationResult validation =
                    prevalidator.validateCandidate(planCopy.provenance(), item);
                if (!validation.valid) {
                    addResult(
                        result,
                        makeResult(
                            item,
                            ExecutionItemStatus::Rejected,
                            item.destinationPath,
                            validation.message));
                    reportProgress(item.sourcePath);
                    continue;
                }

                const QString recoveryKeyForItem = recoveryKey(item, executionId);
                const auto recovery = recoveryStates_.find(recoveryKeyForItem);
                if (recovery != recoveryStates_.end()) {
                    reservedDestinations.insert(
                        OrganizePathValidator::normalizePath(item.destinationPath)
                            .toLower());
                    reportItemProgress(
                        item,
                        item.destinationPath,
                        QStringLiteral("恢复"),
                        QStringLiteral("恢复清理"));
                    const FileMoveResult recoveryResult =
                        fileOperator.resumePublishedCleanup(
                            FileMoveRequest{
                                item.sourcePath,
                                item.destinationPath,
                                ConflictDecisionAction::Proceed,
                            },
                            recovery.value(),
                            *cancellationToken_);
                    if (recoveryResult.status != ExecutionItemStatus::SourceCleanupFailed) {
                        recoveryStates_.erase(recovery);
                    }
                    if (recoveryResult.status == ExecutionItemStatus::Cancelled) {
                        wasCancelled = true;
                    }

                    ExecutionItemResult recoveryItemResult = makeResult(
                        item,
                        recoveryResult.status,
                        recoveryResult.actualDestination,
                        recoveryResult.errorMessage);
                    recoveryItemResult.resumedPublishedResult =
                        recoveryResult.resumedPublishedResult;
                    addResult(result, std::move(recoveryItemResult));
                    reportProgress(item.sourcePath);
                    continue;
                }

                const ConflictDecision decision = conflictResolver.resolveSingle(
                    item, policy, reservedDestinations);
                if (decision.action == ConflictDecisionAction::Reject) {
                    reportItemProgress(
                        item,
                        decision.destinationPath,
                        QStringLiteral("拒绝"),
                        QStringLiteral("已拒绝"));
                    addResult(
                        result,
                        makeResult(
                            item,
                            ExecutionItemStatus::Rejected,
                            decision.destinationPath,
                            decision.errorMessage));
                    reportProgress(item.sourcePath);
                    continue;
                }

                reservedDestinations.insert(
                    OrganizePathValidator::normalizePath(decision.destinationPath)
                        .toLower());
                if (decision.action == ConflictDecisionAction::Skip) {
                    reportItemProgress(
                        item,
                        decision.destinationPath,
                        QStringLiteral("跳过"),
                        QStringLiteral("已跳过"));
                    addResult(
                        result,
                        makeResult(
                            item,
                            ExecutionItemStatus::Skipped,
                            decision.destinationPath,
                            decision.errorMessage));
                    reportProgress(item.sourcePath);
                    continue;
                }

                reportItemProgress(
                    item,
                    decision.destinationPath,
                    conflictDecisionName(decision.action),
                    QStringLiteral("处理中"));
                const FileMoveResult moveResult = fileOperator.move(
                    FileMoveRequest{
                        item.sourcePath,
                        decision.destinationPath,
                        decision.action,
                        decision.expectedDestinationIdentity,
                        validation.sourceIdentity,
                    },
                    *cancellationToken_);

                if (moveResult.status == ExecutionItemStatus::Cancelled) {
                    wasCancelled = true;
                }
                if (moveResult.status == ExecutionItemStatus::SourceCleanupFailed
                    && moveResult.publishedState) {
                    PublishedMoveRecoveryState recoveryState =
                        *moveResult.publishedState;
                    recoveryState.executionId = executionId;
                    recoveryState.planGeneration = planGeneration;
                    recoveryState.scanGeneration = scanGeneration;
                    recoveryState.policy = policy;
                    recoveryStates_.insert(
                        recoveryKeyForItem, std::move(recoveryState));
                } else {
                    recoveryStates_.remove(recoveryKeyForItem);
                }

                ExecutionItemResult itemResult = makeResult(
                    item,
                    moveResult.status,
                    moveResult.actualDestination,
                    moveResult.errorMessage);
                itemResult.resumedPublishedResult =
                    moveResult.resumedPublishedResult;
                addResult(result, std::move(itemResult));
                reportProgress(item.sourcePath);
            }
            result.completed = true;
            result.cancelled = wasCancelled;
            const bool hasErrors =
                result.summary.failed > 0
                || result.summary.rejected > 0
                || result.summary.sourceCleanupFailed > 0;
            const TaskState finalState = wasCancelled
                ? TaskState::Cancelled
                : hasErrors
                    ? TaskState::CompletedWithErrors
                    : TaskState::Completed;
            setState(finalState);
            emit progressChanged(
                static_cast<qint64>(result.items.size()),
                result.summary.planned,
                QString());
            if (wasCancelled) {
                emit cancelled();
            }
            emit completed(std::move(result));
        } catch (const std::exception &exception) {
            result.fatalError =
                QStringLiteral("整理执行异常：%1")
                    .arg(QString::fromLocal8Bit(exception.what()));
            setState(TaskState::Failed);
            emit failed(result.fatalError);
        } catch (...) {
            result.fatalError = QStringLiteral("整理执行发生未知异常");
            setState(TaskState::Failed);
            emit failed(result.fatalError);
        }
    });

    connect(thread,
            &QThread::finished,
            this,
            [this, thread] { detachWorker(thread); });
    workerThread_ = thread;
    thread->setObjectName(QStringLiteral("FilePilotOrganizeExecutionWorker"));
    thread->start();
    return true;
}

void OrganizeExecutionTask::cancel()
{
    if (!active_.load() || isTerminalTaskState(state_.load())) {
        return;
    }

    if (cancellationToken_) {
        cancellationToken_->store(true, std::memory_order_relaxed);
    }
    setState(TaskState::Cancelling);
}

bool OrganizeExecutionTask::isActive() const
{
    return active_.load();
}

TaskState OrganizeExecutionTask::state() const
{
    return state_.load();
}

bool OrganizeExecutionTask::setState(const TaskState state)
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

void OrganizeExecutionTask::detachWorker(QThread *thread)
{
    active_.store(false);
    if (workerThread_ == thread) {
        workerThread_ = nullptr;
    }
    if (!isTerminalTaskState(state_.load()) && setState(TaskState::Failed)) {
        emit failed(QStringLiteral("整理执行线程意外结束"));
    }
    thread->deleteLater();
}

} // namespace FilePilot

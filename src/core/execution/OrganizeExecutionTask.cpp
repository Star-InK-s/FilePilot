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
        ++result.summary.failed;
        ++result.summary.sourceCleanupFailed;
        break;
    }

    result.items.push_back(std::move(item));
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
    };
}

} // namespace

OrganizeExecutionTask::OrganizeExecutionTask(QObject *parent)
    : QObject(parent)
{
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
    const ConflictPolicy policy)
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

    cancellationToken_ = std::make_shared<std::atomic_bool>(false);
    setState(TaskState::Preparing);

    const OrganizePlan planCopy = plan;
    auto *thread = QThread::create([this, planCopy, policy] {
        ExecutionResult result;
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
            const std::vector<ConflictDecision> decisions =
                conflictResolver.resolve(candidates, policy);

            QSet<QString> reservedDestinations;
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

            for (const ConflictDecision &decision : decisions) {
                if (cancellationToken_->load(std::memory_order_relaxed)) {
                    wasCancelled = true;
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Cancelled,
                            decision.destinationPath,
                            QStringLiteral("执行已取消")));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }

                if (decision.action == ConflictDecisionAction::Reject) {
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Rejected,
                            decision.destinationPath,
                            decision.errorMessage));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }

                if (decision.action == ConflictDecisionAction::Skip) {
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Skipped,
                            decision.destinationPath,
                            decision.errorMessage));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }

                const ExecutionValidationResult validation =
                    prevalidator.validateCandidate(
                        planCopy.provenance(), decision.item);
                if (!validation.valid) {
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Rejected,
                            decision.destinationPath,
                            validation.message));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }

                const ConflictDecision currentDecision =
                    conflictResolver.resolveSingle(
                        decision.item, policy, reservedDestinations);
                if (currentDecision.action == ConflictDecisionAction::Reject) {
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Rejected,
                            currentDecision.destinationPath,
                            currentDecision.errorMessage));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }
                if (currentDecision.action == ConflictDecisionAction::Skip) {
                    addResult(
                        result,
                        makeResult(
                            decision.item,
                            ExecutionItemStatus::Skipped,
                            currentDecision.destinationPath,
                            currentDecision.errorMessage));
                    reportProgress(decision.item.sourcePath);
                    continue;
                }

                reservedDestinations.insert(
                    OrganizePathValidator::normalizePath(
                        currentDecision.destinationPath)
                        .toLower());

                const FileMoveResult moveResult = fileOperator.move(
                    FileMoveRequest{
                        decision.item.sourcePath,
                        currentDecision.destinationPath,
                        currentDecision.action,
                    },
                    *cancellationToken_);

                if (moveResult.status == ExecutionItemStatus::Cancelled) {
                    wasCancelled = true;
                }
                addResult(
                    result,
                    makeResult(
                        decision.item,
                        moveResult.status,
                        moveResult.actualDestination,
                        moveResult.errorMessage));
                reportProgress(decision.item.sourcePath);
            }

            result.completed = true;
            result.cancelled = wasCancelled;
            const bool hasErrors =
                result.summary.failed > 0 || result.summary.rejected > 0;
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

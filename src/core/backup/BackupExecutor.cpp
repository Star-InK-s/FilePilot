#include "core/backup/BackupExecutor.h"

#include "core/backup/BackupPrevalidator.h"
#include "core/filesystem/FileHasher.h"
#include "core/filesystem/FileSnapshot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace {

BackupExecutionResult makeResult(
    const BackupExecutionStatus status,
    const QString &message,
    const bool published,
    const bool verified,
    const bool sourcePreserved,
    const bool cleanupComplete,
    const QString &temporaryPath,
    const QString &destination)
{
    BackupExecutionResult result;
    result.status = status;
    result.published = published;
    result.verified = verified;
    result.sourcePreserved = sourcePreserved;
    result.cleanupComplete = cleanupComplete;
    result.errorMessage = message;
    result.temporaryPath = temporaryPath;
    result.actualDestination = destination;
    if (!message.isEmpty()) {
        result.error = AppError{
            status == BackupExecutionStatus::Cancelled ? ErrorCode::Cancelled
                                                       : ErrorCode::Io,
            message,
            destination,
        };
    }
    return result;
}

QString normalizedAbsolutePath(const QString &path)
{
    QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool pathIsWithinRoot(const QString &rootPath, const QString &childPath)
{
    const QString root = normalizedAbsolutePath(rootPath);
    const QString child = normalizedAbsolutePath(childPath);
    if (root.isEmpty() || child.isEmpty() || root == child) {
        return false;
    }

    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }
    return child.startsWith(prefix);
}

bool sameSnapshotContent(const FileSnapshot &left, const FileSnapshot &right)
{
    return left.size == right.size && left.sha256 == right.sha256;
}

bool captureRegularSnapshot(const QString &path,
                            FileSnapshot &snapshot,
                            QString &error)
{
    AppError snapshotError;
    if (!captureFileSnapshot(path, snapshot, snapshotError)
        || snapshot.kind != FilesystemPathKind::RegularFile) {
        error = snapshotError.message().isEmpty()
            ? QStringLiteral("无法捕获普通文件快照")
            : snapshotError.message();
        return false;
    }
    return true;
}

bool verifySourceAgainstSnapshot(const QString &path,
                                 const FileSnapshot &expected,
                                 bool &sourceMissing,
                                 QString &error)
{
    sourceMissing = !QFileInfo::exists(path);
    if (sourceMissing) {
        error = QStringLiteral("源文件在发布前被删除");
        return false;
    }

    FileSnapshot current;
    if (!captureRegularSnapshot(path, current, error)) {
        return false;
    }
    if (current.identity != expected.identity
        || !sameSnapshotContent(current, expected)) {
        error = QStringLiteral("源文件在发布前发生变化");
        return false;
    }
    return true;
}

bool destinationMatchesState(const QString &path,
                             const bool existed,
                             const FileSnapshot &expected,
                             QString &error)
{
    const bool currentlyExists = QFileInfo::exists(path);
    if (existed != currentlyExists) {
        error = QStringLiteral("目标文件在发布前发生变化");
        return false;
    }
    if (!existed) {
        return true;
    }

    FileSnapshot current;
    if (!captureRegularSnapshot(path, current, error)) {
        return false;
    }
    if (current.identity != expected.identity
        || !sameSnapshotContent(current, expected)) {
        error = QStringLiteral("目标文件状态在发布前发生变化");
        return false;
    }
    return true;
}

bool publishTemporary(const QString &temporaryPath,
                      const QString &destination,
                      const bool overwrite,
                      QString &error)
{
#ifdef Q_OS_WIN
    const QString nativeTemporary = QDir::toNativeSeparators(temporaryPath);
    const QString nativeDestination = QDir::toNativeSeparators(destination);
    const DWORD flags = MOVEFILE_WRITE_THROUGH
        | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (MoveFileExW(
            reinterpret_cast<LPCWSTR>(nativeTemporary.utf16()),
            reinterpret_cast<LPCWSTR>(nativeDestination.utf16()),
            flags)
        == FALSE) {
        error = QStringLiteral("备份目标发布失败");
        return false;
    }
    return true;
#else
    std::error_code code;
    std::filesystem::rename(
        std::filesystem::u8path(temporaryPath.toStdString()),
        std::filesystem::u8path(destination.toStdString()),
        code);
    if (code) {
        error = QStringLiteral("备份目标发布失败");
        return false;
    }
    return true;
#endif
}

bool captureTemporaryIdentity(const QString &temporaryPath,
                              FileIdentity &identity,
                              QString &error)
{
    FilesystemPathInfo info;
    QString inspectionError;
    if (!inspectFilesystemPath(temporaryPath, info, inspectionError)
        || !info.inspected
        || !info.isRegularFile()
        || !info.identity.valid) {
        error = inspectionError.isEmpty()
            ? QStringLiteral("无法验证临时备份文件身份")
            : inspectionError;
        return false;
    }
    identity = info.identity;
    return true;
}

bool cleanupTemporary(const QString &temporaryPath,
                      const FileIdentity &expectedIdentity,
                      const BackupExecutorHooks &hooks,
                      QString &error)
{
    if (hooks.cleanupTemporary) {
        if (!hooks.cleanupTemporary(temporaryPath, error)) {
            return false;
        }
        if (QFileInfo::exists(temporaryPath)) {
            error = QStringLiteral("临时文件清理后仍存在");
            return false;
        }
        return true;
    }

    FilesystemPathInfo info;
    QString inspectionError;
    if (!inspectFilesystemPath(temporaryPath, info, inspectionError)
        || !info.inspected) {
        error = inspectionError.isEmpty()
            ? QStringLiteral("无法验证临时备份文件清理状态")
            : inspectionError;
        return false;
    }
    if (info.kind == FilesystemPathKind::Missing) {
        return true;
    }
    if (!info.isRegularFile() || info.identity != expectedIdentity) {
        error = QStringLiteral("临时备份路径已被其他文件替换，拒绝删除");
        return false;
    }
    if (!QFile::remove(temporaryPath)) {
        error = QStringLiteral("临时文件清理失败");
        return false;
    }

    FilesystemPathInfo remainingInfo;
    if (!inspectFilesystemPath(temporaryPath, remainingInfo, inspectionError)
        || !remainingInfo.inspected
        || remainingInfo.kind != FilesystemPathKind::Missing) {
        error = QStringLiteral("临时文件清理后仍存在");
        return false;
    }
    return true;
}

bool runHook(
    const std::function<bool(const QString &, const QString &, QString &)> &hook,
    const QString &source,
    const QString &destination,
    QString &error)
{
    return !hook || hook(source, destination, error);
}

} // namespace

BackupExecutionResult BackupExecutor::execute(
    const BackupPlan &plan,
    const std::atomic_bool &cancelled,
    const BackupExecutorHooks &hooks) const
{
    const QString source = plan.sourcePath;
    const QString destination = plan.finalDestinationPath;
    const QString temporaryPath =
        destination + QStringLiteral(".filepilot-backup-staging");

    if (cancelled.load(std::memory_order_relaxed)) {
        return makeResult(
            BackupExecutionStatus::Cancelled,
            QStringLiteral("备份已取消"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    if (plan.sourceKind != BackupEntryKind::File
        || plan.items.size() != 1
        || plan.items.front().kind != BackupEntryKind::File
        || source.isEmpty()
        || destination.isEmpty()) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("Phase 1 仅支持单个普通文件备份"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupValidationResult validation =
        BackupPrevalidator().validateRoots(source, plan.destinationRoot);
    if (!validation.valid) {
        return makeResult(
            BackupExecutionStatus::Failed,
            validation.error.message().isEmpty()
                ? QStringLiteral("备份预验证失败")
                : validation.error.message(),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupValidationResult finalPathValidation =
        BackupPrevalidator().validateRoots(source, destination);
    if (!finalPathValidation.valid) {
        return makeResult(
            BackupExecutionStatus::Failed,
            finalPathValidation.error.message().isEmpty()
                ? QStringLiteral("最终备份目标路径验证失败")
                : finalPathValidation.error.message(),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupPlanItem &plannedItem = plan.items.front();
    if (plannedItem.plannedDestinationPath != destination) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("计划目标路径与执行目标路径不一致"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }
    if (plannedItem.sourcePath != source
        || plannedItem.relativePath != QFileInfo(source).fileName()
        || plannedItem.conflictPolicy != plan.conflictPolicy) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("计划条目与单文件备份计划不一致"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const QFileInfo destinationRootInfo(plan.destinationRoot);
    const QFileInfo destinationInfo(destination);
    if (!destinationRootInfo.exists()
        || !destinationRootInfo.isDir()
        || !QDir::isAbsolutePath(destination)
        || !pathIsWithinRoot(plan.destinationRoot, destination)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("备份目标目录不可用"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    FileSnapshot sourceSnapshot;
    QString error;
    if (!captureRegularSnapshot(source, sourceSnapshot, error)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const bool sourceExistedAtStart = QFileInfo::exists(source);
    const bool destinationExisted = QFileInfo::exists(destination);
    FileSnapshot destinationSnapshot;
    if (destinationExisted
        && !captureRegularSnapshot(destination, destinationSnapshot, error)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            sourceExistedAtStart,
            true,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.beforeCopy, source, temporaryPath, error)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            error.isEmpty() ? QStringLiteral("复制前检查失败") : error,
            false,
            false,
            sourceExistedAtStart,
            true,
            temporaryPath,
            destination);
    }

    QFile sourceFile(source);
    QFile temporaryFile(temporaryPath);
    if (!sourceFile.open(QIODevice::ReadOnly)
        || !temporaryFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::NewOnly)) {
        error = QStringLiteral("无法创建或打开临时备份文件");
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            sourceExistedAtStart,
            !QFileInfo::exists(temporaryPath),
            temporaryPath,
            destination);
    }

    FileIdentity temporaryIdentity;
    if (!captureTemporaryIdentity(temporaryPath, temporaryIdentity, error)) {
        temporaryFile.close();
        sourceFile.close();
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            sourceExistedAtStart,
            false,
            temporaryPath,
            destination);
    }

    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    qint64 totalWritten = 0;
    while (!sourceFile.atEnd()) {
        if (cancelled.load(std::memory_order_relaxed)) {
            temporaryFile.close();
            sourceFile.close();
            QString cleanupError;
            const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Cancelled,
                QStringLiteral("备份已取消"),
                false,
                false,
                sourceExistedAtStart && QFileInfo::exists(source),
                cleaned,
                temporaryPath,
                destination);
        }

        const qint64 bytesRead = sourceFile.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            temporaryFile.close();
            sourceFile.close();
            QString cleanupError;
            const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                QStringLiteral("读取源文件失败"),
                false,
                false,
                sourceExistedAtStart && QFileInfo::exists(source),
                cleaned,
                temporaryPath,
                destination);
        }
        if (bytesRead == 0) {
            break;
        }
        if (temporaryFile.write(buffer.constData(), bytesRead) != bytesRead) {
            temporaryFile.close();
            sourceFile.close();
            QString cleanupError;
            const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                QStringLiteral("写入临时备份文件失败"),
                false,
                false,
                sourceExistedAtStart && QFileInfo::exists(source),
                cleaned,
                temporaryPath,
                destination);
        }
        totalWritten += bytesRead;
    }

    if (!temporaryFile.flush()) {
        temporaryFile.close();
        sourceFile.close();
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("临时备份文件 flush 失败"),
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }
    temporaryFile.close();
    sourceFile.close();

    if (totalWritten != sourceSnapshot.size) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("复制长度与源文件不一致"),
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.afterCopy, source, temporaryPath, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Failed,
            error.isEmpty() ? QStringLiteral("复制后检查失败") : error,
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.beforeVerify, source, temporaryPath, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Failed,
            error.isEmpty() ? QStringLiteral("验证前检查失败") : error,
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    FileSnapshot temporarySnapshot;
    if (!captureRegularSnapshot(temporaryPath, temporarySnapshot, error)
        || !sameSnapshotContent(temporarySnapshot, sourceSnapshot)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::VerificationFailed,
            QStringLiteral("临时备份内容与源快照不一致"),
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (hooks.temporaryVerifier
        && !hooks.temporaryVerifier(source, temporaryPath, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::VerificationFailed,
            error.isEmpty() ? QStringLiteral("临时备份验证失败") : error,
            false,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    bool sourceMissing = false;
    if (!verifySourceAgainstSnapshot(
            source, sourceSnapshot, sourceMissing, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::SourceChanged,
            error,
            false,
            false,
            sourceExistedAtStart && !sourceMissing,
            cleaned,
            temporaryPath,
            destination);
    }

    if (!destinationMatchesState(
            destination, destinationExisted, destinationSnapshot, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::DestinationConflict,
            error,
            false,
            true,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.beforePublish, source, destination, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::PublishFailed,
            error.isEmpty() ? QStringLiteral("发布前检查失败") : error,
            false,
            true,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (cancelled.load(std::memory_order_relaxed)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Cancelled,
            QStringLiteral("备份已取消"),
            false,
            true,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (!publishTemporary(
            temporaryPath,
            destination,
            plan.conflictPolicy == ConflictPolicy::Overwrite,
            error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::PublishFailed,
            error,
            false,
            true,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.afterPublish, source, destination, error)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::PublishFailed,
            error.isEmpty() ? QStringLiteral("发布后检查失败") : error,
            true,
            true,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    FileSnapshot publishedSnapshot;
    if (!captureRegularSnapshot(destination, publishedSnapshot, error)
        || !sameSnapshotContent(publishedSnapshot, sourceSnapshot)) {
        QString cleanupError;
        const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::VerificationFailed,
            QStringLiteral("最终备份内容与源快照不一致"),
            true,
            false,
            sourceExistedAtStart && QFileInfo::exists(source),
            cleaned,
            temporaryPath,
            destination);
    }

    const bool sourcePreserved =
        !sourceExistedAtStart || QFileInfo::exists(source);
    QString cleanupError;
    const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
    if (!cleaned) {
        return makeResult(
            BackupExecutionStatus::CleanupFailed,
            cleanupError.isEmpty() ? QStringLiteral("临时备份清理失败")
                                    : cleanupError,
            true,
            true,
            sourcePreserved,
            false,
            temporaryPath,
            destination);
    }

    return makeResult(
        BackupExecutionStatus::Succeeded,
        QString(),
        true,
        true,
        sourcePreserved,
        true,
        temporaryPath,
        destination);
}

} // namespace FilePilot

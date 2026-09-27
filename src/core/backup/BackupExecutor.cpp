#include "core/backup/BackupExecutor.h"

#include "core/backup/BackupInventoryBuilder.h"
#include "core/backup/BackupPrevalidator.h"
#include "core/filesystem/FileHasher.h"
#include "core/filesystem/FileSnapshot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
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

QString autoRenameCandidate(const QString &destination, const int index)
{
    const QFileInfo info(destination);
    const QString suffix = info.completeSuffix();
    QString baseName = info.completeBaseName();
    static const QRegularExpression existingNumbering(
        QStringLiteral(" \\([0-9]+\\)$"));
    baseName.remove(existingNumbering);
    const QString candidateName =
        baseName
        + QStringLiteral(" (")
        + QString::number(index)
        + QLatin1Char(')')
        + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
    return QDir(info.absolutePath()).filePath(candidateName);
}

bool chooseAutoRenameDestination(const QString &destinationRoot,
                                 const QString &destination,
                                 QString &renamed,
                                 QString &error)
{
    for (int index = 1; index <= 10000; ++index) {
        const QString candidate = autoRenameCandidate(destination, index);
        if (!pathIsWithinRoot(destinationRoot, candidate)) {
            error = QStringLiteral("自动重命名目标超出备份目标根目录");
            return false;
        }

        FilesystemPathInfo info;
        QString inspectionError;
        if (!inspectFilesystemPath(candidate, info, inspectionError)
            || !info.inspected) {
            error = inspectionError.isEmpty()
                ? QStringLiteral("无法重新检查自动重命名目标")
                : inspectionError;
            return false;
        }
        if (info.kind == FilesystemPathKind::Missing) {
            renamed = candidate;
            return true;
        }
    }

    error = QStringLiteral("无法找到安全的自动重命名目标");
    return false;
}

bool publishAutoRenameTemporary(const QString &temporaryPath,
                                const QString &destinationRoot,
                                QString &destination,
                                QString &error)
{
    for (int attempt = 0; attempt < 10000; ++attempt) {
        FilesystemPathInfo info;
        QString inspectionError;
        if (!inspectFilesystemPath(destination, info, inspectionError)
            || !info.inspected) {
            error = inspectionError.isEmpty()
                ? QStringLiteral("无法重新检查自动重命名目标")
                : inspectionError;
            return false;
        }

        if (info.kind != FilesystemPathKind::Missing) {
            if (!chooseAutoRenameDestination(
                    destinationRoot, destination, destination, error)) {
                return false;
            }
            continue;
        }

        if (publishTemporary(temporaryPath, destination, false, error)) {
            return true;
        }

        if (!inspectFilesystemPath(destination, info, inspectionError)
            || !info.inspected) {
            error = QStringLiteral("无法确认自动重命名发布结果");
            return false;
        }
        if (info.kind == FilesystemPathKind::Missing) {
            return false;
        }
        if (!chooseAutoRenameDestination(
                destinationRoot, destination, destination, error)) {
            return false;
        }
    }

    error = QStringLiteral("无法安全发布自动重命名目标");
    return false;
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

QString relativeStorageKey(const QString &relativePath)
{
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool samePath(const QString &left, const QString &right)
{
    return normalizedAbsolutePath(left) == normalizedAbsolutePath(right);
}

bool inspectDirectoryIdentity(const QString &path,
                              FileIdentity &identity,
                              QString &error)
{
    FilesystemPathInfo info;
    QString inspectionError;
    if (!inspectFilesystemPath(path, info, inspectionError)
        || !info.inspected
        || info.isReparsePoint()
        || !info.isDirectory()
        || !info.identity.valid) {
        error = inspectionError.isEmpty()
            ? QStringLiteral("无法验证备份目录身份")
            : inspectionError;
        return false;
    }
    identity = info.identity;
    return true;
}

bool createStagingDirectory(const QString &path, QString &error)
{
#ifdef Q_OS_WIN
    if (CreateDirectoryW(
            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
            nullptr)
        == FALSE) {
        error = QStringLiteral("无法创建临时备份目录");
        return false;
    }
    return true;
#else
    std::error_code code;
    const bool created = std::filesystem::create_directory(
        std::filesystem::u8path(path.toStdString()), code);
    if (!created || code) {
        error = QStringLiteral("无法创建临时备份目录");
        return false;
    }
    return true;
#endif
}

bool runDirectoryEnumeration(
    const QString &source,
    const BackupExecutorHooks &hooks,
    QString &error)
{
    if (hooks.beforeDirectoryEnumeration
        && !hooks.beforeDirectoryEnumeration(source, error)) {
        if (error.isEmpty()) {
            error = QStringLiteral("目录枚举失败");
        }
        return false;
    }
    return true;
}

bool captureDirectoryInventory(
    const QString &source,
    const BackupExecutorHooks &hooks,
    BackupInventory &inventory,
    QString &error)
{
    if (!runDirectoryEnumeration(source, hooks, error)) {
        return false;
    }
    AppError inventoryError;
    if (!BackupInventoryBuilder().build(source, inventory, inventoryError)) {
        error = inventoryError.message().isEmpty()
            ? QStringLiteral("无法枚举备份目录")
            : inventoryError.message();
        return false;
    }
    return true;
}

bool snapshotMatchesPlanItem(const BackupPlanItem &item,
                             const FileSnapshot &snapshot)
{
    if (item.kind == BackupEntryKind::File) {
        if (item.sourceSnapshot.valid) {
            return item.sourceSnapshot.identity == snapshot.identity
                && sameSnapshotContent(item.sourceSnapshot, snapshot);
        }
        return (!item.expectedSourceIdentity.valid
                || item.expectedSourceIdentity == snapshot.identity)
            && (item.expectedSize < 0 || item.expectedSize == snapshot.size);
    }

    if (item.sourceSnapshot.valid) {
        return item.sourceSnapshot.identity == snapshot.identity;
    }
    return !item.expectedSourceIdentity.valid
        || item.expectedSourceIdentity == snapshot.identity;
}

bool directoryInventoryMatchesPlan(const BackupPlan &plan,
                                   const BackupInventory &inventory,
                                   QString &error)
{
    if (!plan.unsupportedEntries.empty() || !inventory.unsupportedEntries.empty()) {
        error = QStringLiteral("备份目录包含不支持的文件系统条目");
        return false;
    }
    if (plan.items.size() != inventory.items.size()) {
        error = QStringLiteral("备份目录内容在执行期间发生变化");
        return false;
    }

    std::map<QString, const BackupPlanItem *> plannedItems;
    for (const BackupPlanItem &item : plan.items) {
        const QString key = relativeStorageKey(item.relativePath);
        if (key.isEmpty()
            || key == QStringLiteral(".")
            || key.startsWith(QStringLiteral("../"))
            || key == QStringLiteral("..")
            || plannedItems.count(key) != 0) {
            error = QStringLiteral("备份计划包含无效或重复的相对路径");
            return false;
        }
        plannedItems.emplace(key, &item);
    }

    for (const BackupPlanItem &item : inventory.items) {
        const auto found = plannedItems.find(relativeStorageKey(item.relativePath));
        if (found == plannedItems.end()) {
            error = QStringLiteral("备份目录包含计划外条目");
            return false;
        }

        const BackupPlanItem &planned = *found->second;
        if ((planned.kind == BackupEntryKind::File
             && !planned.sourceSnapshot.valid
             && !planned.expectedSourceIdentity.valid)
            || (planned.kind == BackupEntryKind::Directory
                && !planned.sourceSnapshot.valid
                && !planned.expectedSourceIdentity.valid)) {
            error = QStringLiteral("备份计划缺少可验证的源身份快照");
            return false;
        }
        if (planned.kind != item.kind
            || !samePath(planned.sourcePath, item.sourcePath)
            || !samePath(
                planned.plannedDestinationPath,
                QDir(plan.finalDestinationPath).filePath(item.relativePath))
            || !snapshotMatchesPlanItem(planned, item.sourceSnapshot)) {
            error = QStringLiteral("备份计划与当前目录内容不一致");
            return false;
        }
    }
    return true;
}

enum class DirectoryCopyStatus {
    Copied,
    Cancelled,
    Failed
};

DirectoryCopyStatus copyDirectoryFile(
    const QString &sourcePath,
    const QString &temporaryPath,
    const FileSnapshot &expectedSnapshot,
    const std::atomic_bool &cancelled,
    const BackupExecutorHooks &hooks,
    FileIdentity &createdIdentity,
    QString &error)
{
    QFile sourceFile(sourcePath);
    QFile temporaryFile(temporaryPath);
    if (!sourceFile.open(QIODevice::ReadOnly)
        || !temporaryFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::NewOnly)) {
        error = QStringLiteral("无法创建或打开临时备份文件");
        return DirectoryCopyStatus::Failed;
    }

    if (!captureTemporaryIdentity(temporaryPath, createdIdentity, error)) {
        temporaryFile.close();
        sourceFile.close();
        return DirectoryCopyStatus::Failed;
    }

    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    qint64 totalWritten = 0;
    while (!sourceFile.atEnd()) {
        if (cancelled.load(std::memory_order_relaxed)) {
            temporaryFile.close();
            sourceFile.close();
            return DirectoryCopyStatus::Cancelled;
        }

        const qint64 bytesRead = sourceFile.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            temporaryFile.close();
            sourceFile.close();
            error = QStringLiteral("读取源文件失败");
            return DirectoryCopyStatus::Failed;
        }
        if (bytesRead == 0) {
            break;
        }
        if (temporaryFile.write(buffer.constData(), bytesRead) != bytesRead) {
            temporaryFile.close();
            sourceFile.close();
            error = QStringLiteral("写入临时备份文件失败");
            return DirectoryCopyStatus::Failed;
        }
        totalWritten += bytesRead;
        if (hooks.duringCopy
            && !hooks.duringCopy(
                totalWritten, sourcePath, temporaryPath, error)) {
            temporaryFile.close();
            sourceFile.close();
            return DirectoryCopyStatus::Failed;
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            temporaryFile.close();
            sourceFile.close();
            return DirectoryCopyStatus::Cancelled;
        }
    }

    if (!temporaryFile.flush()) {
        temporaryFile.close();
        sourceFile.close();
        error = QStringLiteral("临时备份文件 flush 失败");
        return DirectoryCopyStatus::Failed;
    }
    temporaryFile.close();
    sourceFile.close();

    if (totalWritten != expectedSnapshot.size) {
        error = QStringLiteral("复制长度与源文件不一致");
        return DirectoryCopyStatus::Failed;
    }
    return DirectoryCopyStatus::Copied;
}

struct DirectoryStagingState {
    FileIdentity rootIdentity;
    std::map<QString, FileIdentity> ownedIdentities;
    std::map<QString, FileSnapshot> copiedFiles;
    std::set<QString> copiedDirectories;
};

bool removeOwnedDirectoryTree(
    const QString &path,
    const QString &relativePath,
    const std::map<QString, FileIdentity> &ownedIdentities,
    QString &error)
{
    FilesystemPathInfo info;
    QString inspectionError;
    if (!inspectFilesystemPath(path, info, inspectionError) || !info.inspected) {
        error = inspectionError.isEmpty()
            ? QStringLiteral("无法验证临时备份目录清理状态")
            : inspectionError;
        return false;
    }
    if (info.kind == FilesystemPathKind::Missing) {
        return true;
    }

    const auto expected = ownedIdentities.find(relativeStorageKey(relativePath));
    if (expected == ownedIdentities.end()
        || info.isReparsePoint()
        || !info.isDirectory()
        || info.identity != expected->second) {
        error = QStringLiteral("临时备份目录已被替换，拒绝删除");
        return false;
    }

    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        const QString childRelative = relativePath == QStringLiteral(".")
            ? entry.fileName()
            : relativePath + QLatin1Char('/') + entry.fileName();

        FilesystemPathInfo childInfo;
        if (!inspectFilesystemPath(entry.filePath(), childInfo, inspectionError)
            || !childInfo.inspected) {
            error = QStringLiteral("无法验证临时备份目录子项");
            return false;
        }
        if (childInfo.kind == FilesystemPathKind::Missing) {
            continue;
        }

        const auto childExpected =
            ownedIdentities.find(relativeStorageKey(childRelative));
        if (childExpected == ownedIdentities.end()
            || childInfo.isReparsePoint()
            || childInfo.identity != childExpected->second) {
            error = QStringLiteral("临时备份目录包含未知或被替换的条目");
            return false;
        }

        if (childInfo.isDirectory()) {
            if (!removeOwnedDirectoryTree(
                    entry.filePath(), childRelative, ownedIdentities, error)) {
                return false;
            }
        } else if (childInfo.isRegularFile()) {
            if (!QFile::remove(entry.filePath())) {
                error = QStringLiteral("临时文件清理失败");
                return false;
            }
        } else {
            error = QStringLiteral("临时备份目录包含不支持的文件系统条目");
            return false;
        }
    }

#ifdef Q_OS_WIN
    if (RemoveDirectoryW(
            reinterpret_cast<LPCWSTR>(
                QDir::toNativeSeparators(path).utf16()))
        == FALSE) {
        error = QStringLiteral("临时目录清理失败");
        return false;
    }
#else
    std::error_code code;
    std::filesystem::remove(std::filesystem::u8path(path.toStdString()), code);
    if (code) {
        error = QStringLiteral("临时目录清理失败");
        return false;
    }
#endif
    return true;
}

bool cleanupDirectoryTemporary(
    const QString &temporaryPath,
    const DirectoryStagingState &staging,
    const BackupExecutorHooks &hooks,
    QString &error)
{
    if (hooks.cleanupTemporary) {
        if (!hooks.cleanupTemporary(temporaryPath, error)) {
            return false;
        }
        if (QFileInfo::exists(temporaryPath)) {
            error = QStringLiteral("临时目录清理后仍存在");
            return false;
        }
        return true;
    }
    return removeOwnedDirectoryTree(
        temporaryPath, QStringLiteral("."), staging.ownedIdentities, error);
}

bool verifyPublishedDirectory(
    const QString &destination,
    const DirectoryStagingState &staging,
    QString &error)
{
    FilesystemPathInfo destinationInfo;
    QString inspectionError;
    if (!inspectFilesystemPath(destination, destinationInfo, inspectionError)
        || !destinationInfo.inspected
        || destinationInfo.isReparsePoint()
        || !destinationInfo.isDirectory()) {
        error = QStringLiteral("最终备份目录不可验证");
        return false;
    }

    BackupInventory inventory;
    AppError inventoryError;
    if (!BackupInventoryBuilder().build(destination, inventory, inventoryError)) {
        error = inventoryError.message().isEmpty()
            ? QStringLiteral("无法验证最终备份目录")
            : inventoryError.message();
        return false;
    }
    if (!inventory.unsupportedEntries.empty()
        || inventory.items.size()
            != staging.copiedFiles.size() + staging.copiedDirectories.size()) {
        error = QStringLiteral("最终备份目录结构与临时备份不一致");
        return false;
    }

    for (const BackupPlanItem &item : inventory.items) {
        const QString key = relativeStorageKey(item.relativePath);
        if (item.kind == BackupEntryKind::Directory) {
            if (staging.copiedDirectories.count(key) == 0) {
                error = QStringLiteral("最终备份目录包含计划外目录");
                return false;
            }
            continue;
        }

        const auto expected = staging.copiedFiles.find(key);
        if (expected == staging.copiedFiles.end()
            || !sameSnapshotContent(expected->second, item.sourceSnapshot)) {
            error = QStringLiteral("最终备份文件内容与临时备份不一致");
            return false;
        }
    }
    return true;
}

BackupExecutionResult executeDirectoryBackup(
    const BackupPlan &plan,
    const std::atomic_bool &cancelled,
    const BackupExecutorHooks &hooks)
{
    const QString source = plan.sourcePath;
    QString destination = plan.finalDestinationPath;
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

    if (plan.sourceKind != BackupEntryKind::Directory
        || source.isEmpty()
        || destination.isEmpty()
        || !QDir::isAbsolutePath(source)
        || !QDir::isAbsolutePath(destination)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("目录备份计划无效"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupValidationResult rootValidation =
        BackupPrevalidator().validateRoots(source, plan.destinationRoot);
    const BackupValidationResult destinationValidation =
        BackupPrevalidator().validateRoots(source, destination);
    if (!rootValidation.valid || !destinationValidation.valid) {
        const BackupValidationResult &failure =
            rootValidation.valid ? destinationValidation : rootValidation;
        return makeResult(
            BackupExecutionStatus::Failed,
            failure.error.message().isEmpty()
                ? QStringLiteral("目录备份路径验证失败")
                : failure.error.message(),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const QFileInfo destinationRootInfo(plan.destinationRoot);
    if (!destinationRootInfo.exists()
        || !destinationRootInfo.isDir()
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

    if (QFileInfo::exists(destination)) {
        return makeResult(
            BackupExecutionStatus::DestinationConflict,
            QStringLiteral("目录备份目标已存在；当前阶段不支持目录覆盖"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    QString error;
    BackupInventory currentInventory;
    if (!captureDirectoryInventory(source, hooks, currentInventory, error)
        || !directoryInventoryMatchesPlan(plan, currentInventory, error)) {
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

    FileIdentity destinationRootIdentity;
    if (!inspectDirectoryIdentity(
            plan.destinationRoot, destinationRootIdentity, error)) {
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

    if (!createStagingDirectory(temporaryPath, error)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            true,
            false,
            temporaryPath,
            destination);
    }

    DirectoryStagingState staging;
    if (!inspectDirectoryIdentity(
            temporaryPath, staging.rootIdentity, error)) {
        return makeResult(
            BackupExecutionStatus::Failed,
            error,
            false,
            false,
            true,
            false,
            temporaryPath,
            destination);
    }
    staging.ownedIdentities.emplace(
        relativeStorageKey(QStringLiteral(".")), staging.rootIdentity);

    std::vector<BackupPlanItem> orderedItems = plan.items;
    std::sort(
        orderedItems.begin(),
        orderedItems.end(),
        [](const BackupPlanItem &left, const BackupPlanItem &right) {
            return relativeStorageKey(left.relativePath)
                < relativeStorageKey(right.relativePath);
        });

    for (const BackupPlanItem &item : orderedItems) {
        if (cancelled.load(std::memory_order_relaxed)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Cancelled,
                QStringLiteral("备份已取消"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        const QString itemDestination =
            QDir(temporaryPath).filePath(item.relativePath);
        if (!pathIsWithinRoot(temporaryPath, itemDestination)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                QStringLiteral("临时备份条目路径超出 staging tree"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (item.kind == BackupEntryKind::Directory) {
            if (!createStagingDirectory(itemDestination, error)) {
                QString cleanupError;
                const bool cleaned = cleanupDirectoryTemporary(
                    temporaryPath, staging, hooks, cleanupError);
                return makeResult(
                    BackupExecutionStatus::Failed,
                    error,
                    false,
                    false,
                    true,
                    cleaned,
                    temporaryPath,
                    destination);
            }

            FileIdentity directoryIdentity;
            if (!inspectDirectoryIdentity(
                    itemDestination, directoryIdentity, error)) {
                QString cleanupError;
                const bool cleaned = cleanupDirectoryTemporary(
                    temporaryPath, staging, hooks, cleanupError);
                return makeResult(
                    BackupExecutionStatus::Failed,
                    error,
                    false,
                    false,
                    true,
                    cleaned,
                    temporaryPath,
                    destination);
            }
            staging.ownedIdentities.emplace(
                relativeStorageKey(item.relativePath), directoryIdentity);
            staging.copiedDirectories.insert(relativeStorageKey(item.relativePath));
            continue;
        }

        FileSnapshot sourceSnapshot;
        if (!captureRegularSnapshot(item.sourcePath, sourceSnapshot, error)
            || !snapshotMatchesPlanItem(item, sourceSnapshot)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::SourceChanged,
                error.isEmpty()
                    ? QStringLiteral("源文件在复制前发生变化")
                    : error,
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (!runHook(hooks.beforeCopy, item.sourcePath, itemDestination, error)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                error.isEmpty() ? QStringLiteral("复制前检查失败") : error,
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        FileIdentity createdIdentity;
        const DirectoryCopyStatus copyStatus = copyDirectoryFile(
            item.sourcePath,
            itemDestination,
            sourceSnapshot,
            cancelled,
            hooks,
            createdIdentity,
            error);
        if (createdIdentity.valid) {
            staging.ownedIdentities.emplace(
                relativeStorageKey(item.relativePath), createdIdentity);
        }
        if (copyStatus == DirectoryCopyStatus::Cancelled) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Cancelled,
                QStringLiteral("备份已取消"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }
        if (copyStatus == DirectoryCopyStatus::Failed) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                error,
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (cancelled.load(std::memory_order_relaxed)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Cancelled,
                QStringLiteral("备份已取消"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (!runHook(hooks.afterCopy, item.sourcePath, itemDestination, error)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                error.isEmpty() ? QStringLiteral("复制后检查失败") : error,
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (!runHook(hooks.beforeVerify, item.sourcePath, itemDestination, error)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                error.isEmpty() ? QStringLiteral("验证前检查失败") : error,
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        if (cancelled.load(std::memory_order_relaxed)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Cancelled,
                QStringLiteral("备份已取消"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        FileSnapshot temporarySnapshot;
        if (!captureRegularSnapshot(itemDestination, temporarySnapshot, error)
            || !sameSnapshotContent(temporarySnapshot, sourceSnapshot)) {
            QString cleanupError;
            const bool cleaned = cleanupDirectoryTemporary(
                temporaryPath, staging, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::VerificationFailed,
                QStringLiteral("临时备份内容与源快照不一致"),
                false,
                false,
                true,
                cleaned,
                temporaryPath,
                destination);
        }

        staging.ownedIdentities.emplace(
            relativeStorageKey(item.relativePath), temporarySnapshot.identity);
        staging.copiedFiles.emplace(
            relativeStorageKey(item.relativePath), temporarySnapshot);
    }

    BackupInventory revalidatedInventory;
    if (!captureDirectoryInventory(source, hooks, revalidatedInventory, error)
        || !directoryInventoryMatchesPlan(plan, revalidatedInventory, error)) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::SourceChanged,
            error,
            false,
            false,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    FileIdentity currentDestinationRootIdentity;
    if (!inspectDirectoryIdentity(
            plan.destinationRoot, currentDestinationRootIdentity, error)
        || currentDestinationRootIdentity != destinationRootIdentity) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::DestinationConflict,
            error.isEmpty()
                ? QStringLiteral("目标根目录在发布前发生变化")
                : error,
            false,
            false,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    if (plan.conflictPolicy != ConflictPolicy::AutoRename
        && QFileInfo::exists(destination)) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::DestinationConflict,
            QStringLiteral("目标目录在发布前出现"),
            false,
            false,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    if (!runHook(hooks.beforePublish, source, destination, error)) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::PublishFailed,
            error.isEmpty() ? QStringLiteral("发布前检查失败") : error,
            false,
            true,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    if (cancelled.load(std::memory_order_relaxed)) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::Cancelled,
            QStringLiteral("备份已取消"),
            false,
            true,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    const bool publishSucceeded =
        plan.conflictPolicy == ConflictPolicy::AutoRename
        ? publishAutoRenameTemporary(
            temporaryPath, plan.destinationRoot, destination, error)
        : publishTemporary(temporaryPath, destination, false, error);
    if (!publishSucceeded) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::PublishFailed,
            error,
            false,
            true,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    if (!verifyPublishedDirectory(destination, staging, error)) {
        QString cleanupError;
        const bool cleaned = cleanupDirectoryTemporary(
            temporaryPath, staging, hooks, cleanupError);
        return makeResult(
            BackupExecutionStatus::VerificationFailed,
            error,
            true,
            false,
            true,
            cleaned,
            temporaryPath,
            destination);
    }

    QString cleanupError;
    const bool cleaned = cleanupDirectoryTemporary(
        temporaryPath, staging, hooks, cleanupError);
    if (!cleaned) {
        return makeResult(
            BackupExecutionStatus::CleanupFailed,
            cleanupError.isEmpty()
                ? QStringLiteral("临时备份清理失败")
                : cleanupError,
            true,
            true,
            true,
            false,
            temporaryPath,
            destination);
    }

    return makeResult(
        BackupExecutionStatus::Succeeded,
        QString(),
        true,
        true,
        true,
        true,
        temporaryPath,
        destination);
}

} // namespace

BackupExecutionResult BackupExecutor::execute(
    const BackupPlan &plan,
    const std::atomic_bool &cancelled,
    const BackupExecutorHooks &hooks) const
{
    BackupPlan activePlan = plan;
    const QString source = activePlan.sourcePath;
    QString destination = activePlan.finalDestinationPath;
    QString temporaryPath =
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

    if (source.isEmpty() || destination.isEmpty()) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("备份计划缺少源路径或目标路径"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupValidationResult conflictRootValidation =
        BackupPrevalidator().validateRoots(source, activePlan.destinationRoot);
    const BackupValidationResult conflictDestinationValidation =
        BackupPrevalidator().validateRoots(source, destination);
    if (!conflictRootValidation.valid || !conflictDestinationValidation.valid) {
        const BackupValidationResult &failure = conflictRootValidation.valid
            ? conflictDestinationValidation
            : conflictRootValidation;
        return makeResult(
            BackupExecutionStatus::Failed,
            failure.error.message().isEmpty()
                ? QStringLiteral("备份路径验证失败")
                : failure.error.message(),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    BackupPlan effectivePlan = activePlan;
    QString effectiveDestination = destination;
    if (QFileInfo::exists(destination)) {
        if (activePlan.conflictPolicy == ConflictPolicy::Skip) {
            return makeResult(
                BackupExecutionStatus::Skipped,
                QString(),
                false,
                false,
                true,
                true,
                temporaryPath,
                destination);
        }

        if (activePlan.sourceKind == BackupEntryKind::Directory
            && activePlan.conflictPolicy == ConflictPolicy::Overwrite) {
            return makeResult(
                BackupExecutionStatus::Failed,
                QStringLiteral("不支持目录覆盖；拒绝危险的递归覆盖"),
                false,
                false,
                true,
                true,
                temporaryPath,
                destination);
        }

        if (activePlan.conflictPolicy == ConflictPolicy::AutoRename) {
            QString renameError;
            if (!chooseAutoRenameDestination(
                    activePlan.destinationRoot,
                    destination,
                    effectiveDestination,
                    renameError)) {
                return makeResult(
                    BackupExecutionStatus::Failed,
                    renameError,
                    false,
                    false,
                    true,
                    true,
                    temporaryPath,
                    destination);
            }

            effectivePlan.finalDestinationPath = effectiveDestination;
            for (BackupPlanItem &item : effectivePlan.items) {
                item.plannedDestinationPath =
                    activePlan.sourceKind == BackupEntryKind::Directory
                    ? QDir(effectiveDestination).filePath(item.relativePath)
                    : effectiveDestination;
            }
        }
    }

    activePlan = effectivePlan;
    destination = activePlan.finalDestinationPath;
    temporaryPath = destination + QStringLiteral(".filepilot-backup-staging");

    if (activePlan.sourceKind == BackupEntryKind::Directory) {
        return executeDirectoryBackup(activePlan, cancelled, hooks);
    }

    if (activePlan.sourceKind != BackupEntryKind::File
        || activePlan.items.size() != 1
        || activePlan.items.front().kind != BackupEntryKind::File) {
        return makeResult(
            BackupExecutionStatus::Failed,
            QStringLiteral("仅支持单个普通文件或目录备份"),
            false,
            false,
            true,
            true,
            temporaryPath,
            destination);
    }

    const BackupValidationResult validation =
        BackupPrevalidator().validateRoots(source, activePlan.destinationRoot);
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

    const BackupPlanItem &plannedItem = activePlan.items.front();
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
        || plannedItem.conflictPolicy != activePlan.conflictPolicy) {
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

    const QFileInfo destinationRootInfo(activePlan.destinationRoot);
    const QFileInfo destinationInfo(destination);
    if (!destinationRootInfo.exists()
        || !destinationRootInfo.isDir()
        || !QDir::isAbsolutePath(destination)
        || !pathIsWithinRoot(activePlan.destinationRoot, destination)) {
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
        if (hooks.duringCopy
            && !hooks.duringCopy(totalWritten, source, temporaryPath, error)) {
            temporaryFile.close();
            sourceFile.close();
            QString cleanupError;
            const bool cleaned = cleanupTemporary(temporaryPath, temporaryIdentity, hooks, cleanupError);
            return makeResult(
                BackupExecutionStatus::Failed,
                error.isEmpty() ? QStringLiteral("复制中检查失败") : error,
                false,
                false,
                sourceExistedAtStart && QFileInfo::exists(source),
                cleaned,
                temporaryPath,
                destination);
        }
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

    if (cancelled.load(std::memory_order_relaxed)) {
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

    if (cancelled.load(std::memory_order_relaxed)) {
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

    if (activePlan.conflictPolicy != ConflictPolicy::AutoRename
        && !destinationMatchesState(
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

    const bool publishSucceeded =
        activePlan.conflictPolicy == ConflictPolicy::AutoRename
        ? publishAutoRenameTemporary(
            temporaryPath, activePlan.destinationRoot, destination, error)
        : publishTemporary(
            temporaryPath,
            destination,
            activePlan.conflictPolicy == ConflictPolicy::Overwrite,
            error);
    if (!publishSucceeded) {
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
